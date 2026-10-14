// Network tests over real UDP sockets on localhost.
//  1. Two sessions with different random inputs (one deliberately slower)
//     must end on the same frame with the same state hash.
//  2. Peer B is killed mid-match and relaunched: both must restart the round
//     from frame 0 and stay in sync (regression test for the reconnect bug).
#include <cstdio>
#include <memory>

#include "session.h"

namespace {

Input rnd(uint32_t& s) {  // xorshift32
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return static_cast<Input>(s & 0x1F);
}

SessionConfig make_cfg(int player) {
  SessionConfig c;
  c.player = player;
  c.local_port = player == 0 ? 7100 : 7101;
  c.peer_port = player == 0 ? 7101 : 7100;
  return c;
}

// Ticks both sides until each has simulated `frames` frames (B at half speed).
bool run_until(LockstepSession& a, GameState& sa, LockstepSession& b, GameState& sb,
               uint32_t frames, uint32_t& seed_a, uint32_t& seed_b) {
  for (int round = 0; round < 200000; ++round) {
    if (sa.frame >= frames && sb.frame >= frames) return true;
    if (sa.frame < frames) a.tick(sa, rnd(seed_a));
    if (sb.frame < frames && round % 2 == 0) b.tick(sb, rnd(seed_b));
  }
  return false;
}

bool check(const char* name, const GameState& sa, const GameState& sb) {
  const bool ok = sa.frame == sb.frame && sim_hash(sa) == sim_hash(sb);
  std::printf("%s %s: frames %u/%u, hash %016llx / %016llx\n", ok ? "OK  " : "FAIL", name,
              sa.frame, sb.frame, static_cast<unsigned long long>(sim_hash(sa)),
              static_cast<unsigned long long>(sim_hash(sb)));
  return ok;
}

}  // namespace

int main() {
  uint32_t seed_a = 1, seed_b = 2;
  LockstepSession a;
  auto b = std::make_unique<LockstepSession>();
  if (!a.start(make_cfg(0)) || !b->start(make_cfg(1))) {
    std::printf("FAIL: could not open sockets\n");
    return 1;
  }

  GameState sa = sim_init();
  GameState sb = sim_init();
  if (!run_until(a, sa, *b, sb, 3000, seed_a, seed_b) || !check("sync", sa, sb)) return 1;

  // Kill B mid-match: A keeps ticking alone for a while and must stall.
  b.reset();  // closes B's socket
  const uint32_t frame_before = sa.frame;
  for (int i = 0; i < 100; ++i) a.tick(sa, rnd(seed_a));
  // A may use the inputs B already sent (up to input_delay frames ahead), no more.
  if (sa.frame > frame_before + static_cast<uint32_t>(a.input_delay())) {
    std::printf("FAIL: A advanced %u frames without peer\n", sa.frame - frame_before);
    return 1;
  }
  std::printf("OK   A stalled after %u buffered frames while B was down\n",
              sa.frame - frame_before);

  // Relaunch B: fresh process, frame 0, new nonce.
  b = std::make_unique<LockstepSession>();
  if (!b->start(make_cfg(1))) {
    std::printf("FAIL: could not reopen B\n");
    return 1;
  }
  sb = sim_init();
  // Tick both until A notices the new nonce and restarts its round.
  for (int i = 0; i < 1000 && a.restarts() == 0; ++i) {
    a.tick(sa, rnd(seed_a));
    b->tick(sb, rnd(seed_b));
  }
  if (!run_until(a, sa, *b, sb, 2000, seed_a, seed_b) || !check("reconnect", sa, sb)) {
    return 1;
  }
  if (a.restarts() != 1) {
    std::printf("FAIL: expected A to restart the round once, got %u\n", a.restarts());
    return 1;
  }
  std::printf("OK   A restarted the round %u time(s) after B relaunched\n", a.restarts());
  return 0;
}