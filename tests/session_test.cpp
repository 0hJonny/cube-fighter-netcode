// Network determinism test: two lockstep sessions talk over real UDP sockets on
// localhost, each with its own random local input. After N frames both must
// hold the exact same game state (same hash).
#include <cstdio>

#include "session.h"

int main() {
  SessionConfig ca;
  ca.player = 0;
  ca.local_port = 7100;
  ca.peer_port = 7101;
  SessionConfig cb = ca;
  cb.player = 1;
  cb.local_port = 7101;
  cb.peer_port = 7100;

  LockstepSession a, b;
  if (!a.start(ca) || !b.start(cb)) {
    std::printf("FAIL: could not open sockets\n");
    return 1;
  }

  GameState sa = sim_init();
  GameState sb = sim_init();
  uint32_t seed_a = 1, seed_b = 2;
  auto rnd = [](uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return static_cast<Input>(s & 0x1F);
  };

  const uint32_t frames = 3000;
  // b ticks only every other round, so it lags behind and a must stall.
  for (int round = 0; round < 100000 && (sa.frame < frames || sb.frame < frames); ++round) {
    if (sa.frame < frames) a.tick(sa, rnd(seed_a));
    if (sb.frame < frames && round % 2 == 0) b.tick(sb, rnd(seed_b));
  }

  if (sa.frame != frames || sb.frame != frames) {
    std::printf("FAIL: stuck at frames %u / %u\n", sa.frame, sb.frame);
    return 1;
  }
  const bool ok = sim_hash(sa) == sim_hash(sb);
  std::printf("%s: %u frames, hash A %016llx, hash B %016llx, stalls A %u, B %u\n",
              ok ? "OK" : "DESYNC", frames, static_cast<unsigned long long>(sim_hash(sa)),
              static_cast<unsigned long long>(sim_hash(sb)), a.stalls(), b.stalls());
  return ok ? 0 : 1;
}