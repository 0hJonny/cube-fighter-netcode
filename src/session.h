// Lockstep session with input delay.
//
// Each peer simulates the whole game. Only inputs travel over the network.
// A frame is simulated only when BOTH players' inputs for it are known.
// Local input is scheduled `input_delay` frames into the future, which gives
// the packet that long to reach the other peer before it is needed.
//
// On top of that: RTT/jitter measurement, desync detection by exchanging state
// hashes every CHECK_INTERVAL frames, and restart detection via a per-launch nonce.
#pragma once
#include <chrono>
#include <cstdint>

#include "net.h"
#include "sim.h"

struct SessionConfig {
  int player = 0;              // 0 or 1
  uint16_t local_port = 7000;
  const char* peer_host = "127.0.0.1";
  uint16_t peer_port = 7001;
  int input_delay = 3;         // frames (3 = 50 ms at 60 Hz)
  int lag_ms = 0;              // network conditioner (outgoing packets only)
  int jitter_ms = 0;
  int loss_percent = 0;
};

class LockstepSession {
 public:
  static constexpr uint32_t CHECK_INTERVAL = 30;  // hash compare twice per second

  bool start(const SessionConfig& cfg);

  // Call once per 60 Hz tick. Returns true if the simulation advanced one frame,
  // false if it had to wait for the remote input (a "stall").
  bool tick(GameState& state, Input local_now);

  int player() const { return cfg_.player; }
  int input_delay() const { return cfg_.input_delay; }
  const SessionConfig& config() const { return cfg_; }
  uint32_t stalls() const { return stalls_; }
  uint32_t ticks_since_packet() const { return ticks_since_packet_; }
  bool peer_seen() const { return peer_seen_; }
  uint32_t remote_latest() const { return remote_latest_; }
  uint32_t restarts() const { return restarts_; }
  int rtt_ms() const { return rtt_ms_; }
  int jitter_ms() const { return jitter_ms_; }
  bool desynced() const { return desync_; }
  uint32_t desync_frame() const { return desync_frame_; }
  uint32_t last_verified_frame() const { return last_verified_; }

 private:
  // Ring buffer of inputs indexed by frame. Each slot remembers which frame it
  // holds, so a stale slot from 64 frames ago is never mistaken for a new one.
  struct InputRing {
    static constexpr uint32_t SIZE = 64;
    struct Slot {
      uint32_t frame = UINT32_MAX;
      Input in = 0;
    } slots[SIZE];
    bool has(uint32_t f) const { return slots[f % SIZE].frame == f; }
    Input get(uint32_t f) const { return slots[f % SIZE].in; }
    void set(uint32_t f, Input in) { slots[f % SIZE] = {f, in}; }
  };

  // Recent state hashes at checkpoint frames (multiples of CHECK_INTERVAL).
  struct HashRing {
    static constexpr uint32_t SIZE = 16;
    struct Slot {
      uint32_t frame = UINT32_MAX;
      uint64_t hash = 0;
    } slots[SIZE];
    bool get(uint32_t f, uint64_t& out) const {
      const Slot& s = slots[(f / CHECK_INTERVAL) % SIZE];
      if (s.frame != f) return false;
      out = s.hash;
      return true;
    }
    void set(uint32_t f, uint64_t h) { slots[(f / CHECK_INTERVAL) % SIZE] = {f, h}; }
  };

  bool receive(uint32_t current_frame);  // true if the peer restarted
  void send_inputs(uint32_t newest_frame, uint32_t current_frame);
  void reset_round_state();
  void compare_hashes(uint32_t frame);
  uint32_t now_ms() const;

  SessionConfig cfg_;
  UdpSocket sock_;
  InputRing local_, remote_;
  HashRing local_hashes_, remote_hashes_;
  uint32_t latest_check_frame_ = UINT32_MAX;  // newest local checkpoint to advertise
  uint32_t peer_ack_ = 0;  // peer's current frame: it needs our inputs from here on

  // Random per process start. A different nonce from the peer means it was
  // restarted, so its game is back at frame 0 and ours must restart too.
  uint32_t my_nonce_ = 0;
  uint32_t peer_nonce_ = 0;
  uint32_t old_peer_nonce_ = 0;

  // RTT: we stamp each packet with our clock; the peer echoes the latest stamp
  // back together with how long it held it, so RTT = now - stamp - hold time.
  std::chrono::steady_clock::time_point epoch_;
  uint32_t peer_stamp_ = 0;     // newest timestamp received from the peer
  uint32_t peer_stamp_rx_ = 0;  // our clock when it arrived
  int rtt_ms_ = 0;
  int jitter_ms_ = 0;

  uint32_t restarts_ = 0;
  uint32_t stalls_ = 0;
  uint32_t ticks_since_packet_ = 0;
  uint32_t remote_latest_ = 0;
  bool peer_seen_ = false;
  bool desync_ = false;
  uint32_t desync_frame_ = 0;
  uint32_t last_verified_ = 0;
};