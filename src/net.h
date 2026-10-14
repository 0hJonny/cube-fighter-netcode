// Minimal non-blocking UDP socket. Platform code (winsock / BSD sockets) lives
// only in net.cpp, so <windows.h> never meets raylib.h (their names clash).
//
// Includes a small network conditioner for testing: outgoing packets can be
// delayed (lag + random jitter, which also reorders them) and dropped.
#pragma once
#include <chrono>
#include <cstdint>
#include <deque>
#include <random>
#include <vector>

class UdpSocket {
 public:
  ~UdpSocket();
  bool open(uint16_t local_port);                     // bind 0.0.0.0:port, non-blocking
  bool set_peer(const char* host, uint16_t port);     // where send() goes
  void set_conditions(int lag_ms, int jitter_ms, int loss_percent);
  void send(const uint8_t* data, int len);
  int recv(uint8_t* buf, int cap);                    // bytes read, or -1 if nothing waiting
  void close();

 private:
  using Clock = std::chrono::steady_clock;
  struct Pending {
    Clock::time_point release;
    std::vector<uint8_t> data;
  };
  void send_now(const uint8_t* data, int len);
  void flush_pending();

  intptr_t fd_ = -1;
  uint8_t peer_addr_[128] = {};  // sockaddr_storage, kept opaque here
  int peer_len_ = 0;

  int lag_ms_ = 0, jitter_ms_ = 0, loss_percent_ = 0;
  std::deque<Pending> pending_;
  std::mt19937 rng_{12345};  // test-only randomness, never touches the sim
};
