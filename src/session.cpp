#include "session.h"

#include <random>

namespace {

// Packet layout, protocol v3. All little-endian, written byte by byte so the
// format does not depend on the CPU or on struct padding.
//   [0..1]   magic 'N','F'        [2] version       [3] sender player
//   [4..7]   sender nonce (random per launch, detects a restarted peer)
//   [8..11]  sender timestamp, ms
//   [12..15] echo: newest timestamp received from the receiver
//   [16..17] echo hold time, ms (how long the sender sat on that timestamp)
//   [18..21] checkpoint frame     [22..29] state hash at that frame
//   [30..33] newest input frame   [34] count
//   [35..]   inputs for frames newest-count+1 .. newest
constexpr uint8_t MAGIC0 = 'N', MAGIC1 = 'F', VERSION = 3;
constexpr int HEADER = 35;
// Every packet repeats the last 8 frames of input, so a lost packet is covered
// by the next one. Inputs are 1 byte, so redundancy costs almost nothing.
constexpr uint32_t REDUNDANCY = 8;

void put_u16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}
void put_u32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
void put_u64(uint8_t* p, uint64_t v) {
  for (int i = 0; i < 8; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
uint16_t get_u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t get_u32(const uint8_t* p) {
  uint32_t v = 0;
  for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(p[i]) << (8 * i);
  return v;
}
uint64_t get_u64(const uint8_t* p) {
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(p[i]) << (8 * i);
  return v;
}

int abs_int(int v) { return v < 0 ? -v : v; }

}  // namespace

bool LockstepSession::start(const SessionConfig& cfg) {
  cfg_ = cfg;
  if (!sock_.open(cfg.local_port)) return false;
  if (!sock_.set_peer(cfg.peer_host, cfg.peer_port)) return false;
  sock_.set_conditions(cfg.lag_ms, cfg.jitter_ms, cfg.loss_percent);
  // Randomness is fine here: the nonce never enters the simulation.
  std::random_device rd;
  do {
    my_nonce_ = rd();
  } while (my_nonce_ == 0);
  epoch_ = std::chrono::steady_clock::now();
  reset_round_state();
  return true;
}

uint32_t LockstepSession::now_ms() const {
  const auto d = std::chrono::steady_clock::now() - epoch_;
  // +1 so a valid timestamp is never 0 (0 means "nothing to echo yet").
  return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(d).count()) +
         1;
}

void LockstepSession::reset_round_state() {
  local_ = InputRing{};
  remote_ = InputRing{};
  local_hashes_ = HashRing{};
  remote_hashes_ = HashRing{};
  latest_check_frame_ = UINT32_MAX;
  remote_latest_ = 0;
  desync_ = false;
  last_verified_ = 0;
  // The first `input_delay` frames have no real input yet: both peers agree
  // they are empty, so nobody waits for them.
  for (uint32_t f = 0; f < static_cast<uint32_t>(cfg_.input_delay); ++f) {
    local_.set(f, 0);
    remote_.set(f, 0);
  }
}

bool LockstepSession::tick(GameState& state, Input local_now) {
  if (receive(state.frame)) {
    // Peer restarted: lockstep cannot resume mid-match without transferring
    // state, so both sides start a fresh match from frame 0.
    state = sim_init();
    reset_round_state();
    ++restarts_;
    receive(state.frame);  // re-read: packets that were too "old" may fit now
  }
  const uint32_t frame = state.frame;

  // Schedule this tick's input for (frame + delay). While stalled the slot is
  // already filled, so a held stall does not shift input timing.
  const uint32_t target = frame + cfg_.input_delay;
  if (!local_.has(target)) local_.set(target, local_now);

  // Send every tick, even while stalled: the peer may be waiting on us.
  send_inputs(target);

  if (!remote_.has(frame)) {
    ++stalls_;
    return false;
  }
  const Input mine = local_.get(frame);
  const Input theirs = remote_.get(frame);
  if (cfg_.player == 0) {
    sim_step(state, mine, theirs);
  } else {
    sim_step(state, theirs, mine);
  }

  if (state.frame % CHECK_INTERVAL == 0) {
    local_hashes_.set(state.frame, sim_hash(state));
    latest_check_frame_ = state.frame;
    compare_hashes(state.frame);
  }
  return true;
}

void LockstepSession::compare_hashes(uint32_t frame) {
  uint64_t mine, theirs;
  if (!local_hashes_.get(frame, mine) || !remote_hashes_.get(frame, theirs)) return;
  if (mine != theirs) {
    if (!desync_) desync_frame_ = frame;  // remember the FIRST divergence
    desync_ = true;
  } else if (frame > last_verified_) {
    last_verified_ = frame;
  }
}

bool LockstepSession::receive(uint32_t current_frame) {
  ++ticks_since_packet_;
  uint8_t buf[256];
  int n;
  while ((n = sock_.recv(buf, sizeof(buf))) > 0) {  // drain everything queued
    // Validate before trusting anything: never trust bytes from the network.
    if (n < HEADER || buf[0] != MAGIC0 || buf[1] != MAGIC1 || buf[2] != VERSION) continue;
    if (buf[3] != 1 - cfg_.player) continue;
    const uint32_t nonce = get_u32(buf + 4);
    const uint32_t count = buf[34];
    if (nonce == 0 || count == 0 || count > REDUNDANCY ||
        n != HEADER + static_cast<int>(count)) {
      continue;
    }
    if (nonce == old_peer_nonce_) continue;  // late packet from the dead process
    if (peer_nonce_ != 0 && nonce != peer_nonce_) {
      // New process on the other side. Everything we buffered from the old one
      // is meaningless; stop here and let tick() reset, then read again.
      old_peer_nonce_ = peer_nonce_;
      peer_nonce_ = nonce;
      peer_stamp_ = 0;  // the new process's clock starts from zero again
      return true;
    }
    peer_nonce_ = nonce;
    peer_seen_ = true;
    ticks_since_packet_ = 0;

    // --- RTT / jitter ---
    const uint32_t now = now_ms();
    const uint32_t stamp = get_u32(buf + 8);
    if (stamp > peer_stamp_) {  // newest only: jitter can reorder packets
      peer_stamp_ = stamp;
      peer_stamp_rx_ = now;
    }
    const uint32_t echo = get_u32(buf + 12);
    const uint32_t hold = get_u16(buf + 16);
    if (echo != 0 && now >= echo + hold) {
      const int sample = static_cast<int>(now - echo - hold);
      if (rtt_ms_ == 0) {
        rtt_ms_ = sample;
      } else {
        // Exponential moving averages (like TCP's SRTT/RTTVAR, simplified).
        jitter_ms_ = (jitter_ms_ * 7 + abs_int(sample - rtt_ms_)) / 8;
        rtt_ms_ = (rtt_ms_ * 7 + sample) / 8;
      }
    }

    // --- desync detection ---
    const uint32_t check_frame = get_u32(buf + 18);
    if (check_frame != UINT32_MAX) {
      remote_hashes_.set(check_frame, get_u64(buf + 22));
      compare_hashes(check_frame);
    }

    // --- inputs ---
    const uint32_t newest = get_u32(buf + 30);
    if (newest > remote_latest_) remote_latest_ = newest;
    for (uint32_t i = 0; i < count; ++i) {
      const uint32_t f = newest - (count - 1) + i;
      // Ignore frames already simulated (old duplicates) and frames too far
      // ahead to fit the ring. Duplicates are harmless: same frame, same byte.
      if (f < current_frame || f >= current_frame + InputRing::SIZE) continue;
      remote_.set(f, buf[HEADER + i]);
    }
  }
  return false;
}

void LockstepSession::send_inputs(uint32_t newest) {
  uint8_t buf[HEADER + REDUNDANCY];
  const uint32_t count = newest + 1 < REDUNDANCY ? newest + 1 : REDUNDANCY;
  const uint32_t now = now_ms();
  buf[0] = MAGIC0;
  buf[1] = MAGIC1;
  buf[2] = VERSION;
  buf[3] = static_cast<uint8_t>(cfg_.player);
  put_u32(buf + 4, my_nonce_);
  put_u32(buf + 8, now);
  put_u32(buf + 12, peer_stamp_);
  const uint32_t hold = peer_stamp_ != 0 ? now - peer_stamp_rx_ : 0;
  put_u16(buf + 16, static_cast<uint16_t>(hold > 65535 ? 65535 : hold));
  uint64_t check_hash = 0;
  if (latest_check_frame_ != UINT32_MAX) local_hashes_.get(latest_check_frame_, check_hash);
  put_u32(buf + 18, latest_check_frame_);
  put_u64(buf + 22, check_hash);
  put_u32(buf + 30, newest);
  buf[34] = static_cast<uint8_t>(count);
  for (uint32_t i = 0; i < count; ++i) buf[HEADER + i] = local_.get(newest - (count - 1) + i);
  sock_.send(buf, HEADER + static_cast<int>(count));
}
