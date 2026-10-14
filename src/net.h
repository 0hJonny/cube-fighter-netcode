// Minimal non-blocking UDP socket. Platform code (winsock / BSD sockets) lives
// only in net.cpp, so <windows.h> never meets raylib.h (their names clash).
#pragma once
#include <cstdint>

class UdpSocket {
 public:
  ~UdpSocket();
  bool open(uint16_t local_port);                     // bind 0.0.0.0:port, non-blocking
  bool set_peer(const char* host, uint16_t port);     // where send() goes
  void send(const uint8_t* data, int len);
  int recv(uint8_t* buf, int cap);                    // bytes read, or -1 if nothing waiting
  void close();

 private:
  intptr_t fd_ = -1;
  uint8_t peer_addr_[128] = {};  // sockaddr_storage, kept opaque here
  int peer_len_ = 0;
};