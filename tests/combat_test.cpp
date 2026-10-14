// Gameplay rules, checked frame by frame on the deterministic sim.
#include <cstdio>

#include "sim.h"

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "OK  " : "FAIL", what);
  if (!ok) ++failures;
}

// Fighters standing next to each other, 0 on the left facing right.
GameState close_range() {
  GameState s = sim_init();
  s.f[0].x = -600;
  s.f[1].x = 600;
  return s;
}

void run(GameState& s, int frames, Input in0 = 0, Input in1 = 0) {
  for (int i = 0; i < frames; ++i) sim_step(s, in0, in1);
}

// Press a button for one frame, then release.
void tap(GameState& s, Input btn0, Input hold1 = 0) {
  sim_step(s, btn0, hold1);
  sim_step(s, 0, hold1);
}

}  // namespace

int main() {
  {
    GameState s = close_range();
    tap(s, BTN_LIGHT);
    run(s, 10);
    expect(s.f[1].hp == MAX_HP - 50, "light attack deals 50");
  }
  {
    // light > light > heavy, each pressed as soon as the previous one hits.
    GameState s = close_range();
    tap(s, BTN_LIGHT);
    run(s, 4);
    tap(s, BTN_LIGHT);
    run(s, 4);
    tap(s, BTN_HEAVY);
    uint16_t max_combo = 0;
    for (int i = 0; i < 30; ++i) {
      sim_step(s, 0, 0);
      if (s.f[1].combo_hits > max_combo) max_combo = s.f[1].combo_hits;
    }
    expect(s.f[1].hp == MAX_HP - 220, "light > light > heavy deals 220");
    expect(max_combo == 3, "the chain is a true 3-hit combo (no escape between hits)");
  }
  {
    // Fighter 1 holds back (right, away from fighter 0): the attack is blocked.
    GameState s = close_range();
    tap(s, BTN_HEAVY, BTN_RIGHT);
    run(s, 20, 0, BTN_RIGHT);
    expect(s.f[1].hp == MAX_HP, "holding back blocks the heavy attack");
  }
  {
    // Out of range: nothing happens.
    GameState s = sim_init();
    tap(s, BTN_HEAVY);
    run(s, 30);
    expect(s.f[1].hp == MAX_HP, "attack whiffs at long range");
  }
  {
    // Holding the button is not mashing: only one attack comes out.
    GameState s = close_range();
    run(s, 60, BTN_LIGHT);
    expect(s.f[1].hp == MAX_HP - 50, "holding light attacks only once");
  }
  {
    GameState s = sim_init();
    run(s, ROUND_FRAMES);
    expect(s.round == Round::Over && s.winner == 2, "time over with equal HP is a draw");
    run(s, 3 * TICK_RATE);
    expect(s.round == Round::Fighting && s.timer == ROUND_FRAMES - 1, "next round starts by itself");
  }
  {
    GameState s = close_range();
    s.f[1].hp = 40;
    tap(s, BTN_LIGHT);
    run(s, 10);
    expect(s.f[1].state == FState::KO && s.round == Round::Over && s.winner == 0 &&
               s.wins[0] == 1,
           "KO ends the round and counts a win");
  }
  return failures == 0 ? 0 : 1;
}
