// Network tests over real UDP sockets on localhost.
//  1. Two sessions with different random inputs (one deliberately slower)
//     must end on the same frame with the same state hash.
//  2. Peer B is killed mid-match and relaunched: both must restart from frame 0
//     and stay in sync (regression test for the reconnect bug).
//  3. A corrupted local state must be reported as a desync by hash exchange.
//  4. Bad network (lag, jitter = reordering, 20% packet loss): still in sync.
//  5. Different input delays per peer under heavy loss: no freeze, no desync.
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

#include "session.h"

namespace {

Input rnd(uint32_t& s) {  // xorshift32
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return static_cast<Input>(s & 0x1F);
}

SessionConfig make_cfg(int player, uint16_t base_port) {
  SessionConfig c;
  c.player = player;
  c.local_port = static_cast<uint16_t>(base_port + player);
  c.peer_port = static_cast<uint16_t>(base_port + 1 - player);
  return c;
}

// Ticks both sides until each has simulated `frames` frames (B at half speed).
// With `pace`, sleeps 1 ms per round so the lag simulation's clock moves.
bool run_until(LockstepSession& a, GameState& sa, LockstepSession& b, GameState& sb,
               uint32_t frames, uint32_t& seed_a, uint32_t& seed_b, bool pace = false) {
  for (int round = 0; round < 200000; ++round) {
    if (sa.frame >= frames && sb.frame >= frames) return true;
    if (sa.frame < frames) a.tick(sa, rnd(seed_a));
    if (sb.frame < frames && round % 2 == 0) b.tick(sb, rnd(seed_b));
    if (pace) std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
  if (!a.start(make_cfg(0, 7100)) || !b->start(make_cfg(1, 7100))) {
    std::printf("FAIL: could not open sockets\n");
    return 1;
  }

  // --- 1. plain sync ---
  GameState sa = sim_init();
  GameState sb = sim_init();
  if (!run_until(a, sa, *b, sb, 3000, seed_a, seed_b) || !check("sync", sa, sb)) return 1;
  if (a.desynced() || b->desynced() || a.last_verified_frame() == 0) {
    std::printf("FAIL: hash exchange did not verify any checkpoint\n");
    return 1;
  }
  std::printf("OK   hash checkpoints verified up to frame %u\n", a.last_verified_frame());

  // --- 2. kill B mid-match and relaunch it ---
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

  b = std::make_unique<LockstepSession>();
  if (!b->start(make_cfg(1, 7100))) {
    std::printf("FAIL: could not reopen B\n");
    return 1;
  }
  sb = sim_init();
  // Tick both until A notices the new nonce and restarts.
  for (int i = 0; i < 1000 && a.restarts() == 0; ++i) {
    a.tick(sa, rnd(seed_a));
    b->tick(sb, rnd(seed_b));
  }
  if (!run_until(a, sa, *b, sb, 2000, seed_a, seed_b) || !check("reconnect", sa, sb)) {
    return 1;
  }
  if (a.restarts() != 1) {
    std::printf("FAIL: expected A to restart once, got %u\n", a.restarts());
    return 1;
  }
  std::printf("OK   A restarted %u time(s) after B relaunched\n", a.restarts());

  // --- 3. corrupt A's state: the hash exchange must catch it ---
  sa.f[0].hp -= 1;
  run_until(a, sa, *b, sb, 2100, seed_a, seed_b);
  if (!a.desynced() || !b->desynced()) {
    std::printf("FAIL: corrupted state was not detected\n");
    return 1;
  }
  std::printf("OK   desync detected by both peers at frame %u\n", a.desync_frame());

  // --- 4. bad network: 40 ms lag, +0..30 ms jitter (reordering), 20%% loss ---
  SessionConfig c0 = make_cfg(0, 7110), c1 = make_cfg(1, 7110);
  for (SessionConfig* c : {&c0, &c1}) {
    c->lag_ms = 40;
    c->jitter_ms = 30;
    c->loss_percent = 20;
  }
  LockstepSession x, y;
  if (!x.start(c0) || !y.start(c1)) {
    std::printf("FAIL: could not open sockets\n");
    return 1;
  }
  // Both keep ticking the whole time, like real games: a peer that stops
  // ticking stops sending, and with 20% loss its last packets may never arrive.
  // Sync is proven by the in-protocol hash exchange instead of a final compare.
  GameState sx = sim_init(), sy = sim_init();
  for (int round = 0; round < 60000 && (sx.frame < 300 || sy.frame < 300); ++round) {
    x.tick(sx, rnd(seed_a));
    if (round % 2 == 0) y.tick(sy, rnd(seed_b));
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const uint32_t verified = x.last_verified_frame() < y.last_verified_frame()
                                ? x.last_verified_frame()
                                : y.last_verified_frame();
  if (sx.frame < 300 || sy.frame < 300 || x.desynced() || y.desynced() || verified < 240) {
    std::printf("FAIL bad network: frames %u/%u, verified to %u, desync %d/%d\n", sx.frame,
                sy.frame, verified, x.desynced(), y.desynced());
    return 1;
  }
  std::printf("OK   bad network: frames %u/%u, both verified in sync up to frame %u\n",
              sx.frame, sy.frame, verified);
  // --- 5. different input delays on each side (1 vs 10 frames) + 30% loss ---
  // Regression: a fixed 8-frame resend window froze when one peer fell more
  // than 8 frames behind, and pre-filling the PEER's first frames with empty
  // input desynced when the delays differed.
  {
    SessionConfig d0 = make_cfg(0, 7120), d1 = make_cfg(1, 7120);
    d0.input_delay = 1;
    d1.input_delay = 10;
    for (SessionConfig* c : {&d0, &d1}) {
      c->lag_ms = 30;
      c->jitter_ms = 10;
      c->loss_percent = 30;
    }
    LockstepSession p, q;
    if (!p.start(d0) || !q.start(d1)) {
      std::printf("FAIL: could not open sockets\n");
      return 1;
    }
    GameState sp = sim_init(), sq = sim_init();
    for (int round = 0; round < 60000 && (sp.frame < 300 || sq.frame < 300); ++round) {
      p.tick(sp, rnd(seed_a));
      if (round % 3 != 0) q.tick(sq, rnd(seed_b));
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const uint32_t v = p.last_verified_frame() < q.last_verified_frame() ? p.last_verified_frame()
                                                                         : q.last_verified_frame();
    if (sp.frame < 300 || sq.frame < 300 || p.desynced() || q.desynced() || v < 240) {
      std::printf("FAIL mixed delays: frames %u/%u, verified to %u, desync %d/%d\n", sp.frame,
                  sq.frame, v, p.desynced(), q.desynced());
      return 1;
    }
    std::printf("OK   delays 1 vs 10 frames + 30%% loss: in sync up to frame %u\n", v);
  }

  std::printf("OK   measured RTT %d ms, jitter %d ms (configured 2 x (40 + 0..30) ms)\n",
              x.rtt_ms(), x.jitter_ms());
  return 0;
}