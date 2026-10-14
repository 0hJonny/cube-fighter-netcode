// Lockstep session with input delay.
//
// Each peer simulates the whole game. Only inputs travel over the network.
// A frame is simulated only when BOTH players' inputs for it are known.
// Local input is scheduled `input_delay` frames into the future, which gives
// the packet that long to reach the other peer before it is needed.
#pragma once
#include <cstdint>

#include "net.h"
#include "sim.h"

struct SessionConfig {
  int player = 0;              // 0 or 1
  uint16_t local_port = 7000;
  const char* peer_host = "127.0.0.1";
  uint16_t peer_port = 7001;
  int input_delay = 3;         // frames (3 = 50 ms at 60 Hz)
};

class LockstepSession {
 public:
  bool start(const SessionConfig& cfg);

  // Call once per 60 Hz tick. Returns true if the simulation advanced one frame,
  // false if it had to wait for the remote input (a "stall").
  bool tick(GameState& state, Input local_now);

  int player() const { return cfg_.player; }
  int input_delay() const { return cfg_.input_delay; }
  uint32_t stalls() const { return stalls_; }
  uint32_t ticks_since_packet() const { return ticks_since_packet_; }
  bool peer_seen() const { return peer_seen_; }
  uint32_t remote_latest() const { return remote_latest_; }
  uint32_t restarts() const { return restarts_; }

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

  bool receive(uint32_t current_frame);  // true if the peer restarted
  void send_inputs(uint32_t newest_frame);
  void reset_inputs();

  SessionConfig cfg_;
  UdpSocket sock_;
  InputRing local_, remote_;
  // Random per process start. A different nonce from the peer means it was
  // restarted, so its game is back at frame 0 and ours must restart too.
  uint32_t my_nonce_ = 0;
  uint32_t peer_nonce_ = 0;
  uint32_t old_peer_nonce_ = 0;
  uint32_t restarts_ = 0;
  uint32_t stalls_ = 0;
  uint32_t ticks_since_packet_ = 0;
  uint32_t remote_latest_ = 0;
  bool peer_seen_ = false;
};