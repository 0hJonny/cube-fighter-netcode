#include "session.h"

#include <random>

namespace {

// Packet layout (all little-endian, written byte by byte so the format does not
// depend on the CPU or on struct padding):
//   [0..1]  magic 'N','F'   [2] version   [3] sender player
//   [4..7]  sender nonce (random per launch, detects a restarted peer)
//   [8..11] newest frame    [12] count    [13..] inputs for frames
//                                                newest-count+1 .. newest
constexpr uint8_t MAGIC0 = 'N', MAGIC1 = 'F', VERSION = 2;
constexpr int HEADER = 13;
// Every packet repeats the last 8 frames of input, so a lost packet is covered
// by the next one. Inputs are 1 byte, so redundancy costs almost nothing.
constexpr uint32_t REDUNDANCY = 8;

void put_u32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
uint32_t get_u32(const uint8_t* p) {
  uint32_t v = 0;
  for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(p[i]) << (8 * i);
  return v;
}

}  // namespace

bool LockstepSession::start(const SessionConfig& cfg) {
  cfg_ = cfg;
  if (!sock_.open(cfg.local_port)) return false;
  if (!sock_.set_peer(cfg.peer_host, cfg.peer_port)) return false;
  // Randomness is fine here: the nonce never enters the simulation.
  std::random_device rd;
  do {
    my_nonce_ = rd();
  } while (my_nonce_ == 0);
  reset_inputs();
  return true;
}

void LockstepSession::reset_inputs() {
  local_ = InputRing{};
  remote_ = InputRing{};
  remote_latest_ = 0;
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
    // the whole state, so both sides start a fresh round from frame 0.
    state = sim_init();
    reset_inputs();
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
  return true;
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
    const uint32_t newest = get_u32(buf + 8);
    const uint32_t count = buf[12];
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
      return true;
    }
    peer_nonce_ = nonce;
    peer_seen_ = true;
    ticks_since_packet_ = 0;
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
  buf[0] = MAGIC0;
  buf[1] = MAGIC1;
  buf[2] = VERSION;
  buf[3] = static_cast<uint8_t>(cfg_.player);
  put_u32(buf + 4, my_nonce_);
  put_u32(buf + 8, newest);
  buf[12] = static_cast<uint8_t>(count);
  for (uint32_t i = 0; i < count; ++i) buf[HEADER + i] = local_.get(newest - (count - 1) + i);
  sock_.send(buf, HEADER + static_cast<int>(count));
}