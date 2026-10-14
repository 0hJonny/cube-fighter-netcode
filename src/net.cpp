#include "net.h"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
using sock_t = SOCKET;
static const sock_t BAD_SOCK = INVALID_SOCKET;
static void close_sock(sock_t s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
using sock_t = int;
static const sock_t BAD_SOCK = -1;
static void close_sock(sock_t s) { ::close(s); }
#endif

UdpSocket::~UdpSocket() { close(); }

bool UdpSocket::open(uint16_t local_port) {
#ifdef _WIN32
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif
  sock_t s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == BAD_SOCK) return false;

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(local_port);  // network byte order (big-endian)
  if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    std::fprintf(stderr, "bind to port %u failed (already in use?)\n", local_port);
    close_sock(s);
    return false;
  }

  // Non-blocking: recv() must never stall the game loop waiting for a packet.
#ifdef _WIN32
  u_long nb = 1;
  ioctlsocket(s, FIONBIO, &nb);
#else
  fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
  fd_ = static_cast<intptr_t>(s);
  return true;
}

bool UdpSocket::set_peer(const char* host, uint16_t port) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo* res = nullptr;
  char port_str[8];
  std::snprintf(port_str, sizeof(port_str), "%u", port);
  if (getaddrinfo(host, port_str, &hints, &res) != 0 || res == nullptr) return false;
  std::memcpy(peer_addr_, res->ai_addr, res->ai_addrlen);
  peer_len_ = static_cast<int>(res->ai_addrlen);
  freeaddrinfo(res);
  return true;
}

void UdpSocket::send(const uint8_t* data, int len) {
  if (fd_ == -1 || peer_len_ == 0) return;
  // Fire and forget: UDP gives no delivery guarantee, the protocol handles loss.
  sendto(static_cast<sock_t>(fd_), reinterpret_cast<const char*>(data), len, 0,
         reinterpret_cast<const sockaddr*>(peer_addr_), peer_len_);
}

int UdpSocket::recv(uint8_t* buf, int cap) {
  if (fd_ == -1) return -1;
  const auto n = recvfrom(static_cast<sock_t>(fd_), reinterpret_cast<char*>(buf), cap, 0,
                          nullptr, nullptr);
  return n > 0 ? static_cast<int>(n) : -1;  // -1 also covers "would block"
}

void UdpSocket::close() {
  if (fd_ == -1) return;
  close_sock(static_cast<sock_t>(fd_));
  fd_ = -1;
#ifdef _WIN32
  WSACleanup();
#endif
}