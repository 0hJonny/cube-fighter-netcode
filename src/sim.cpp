#include "sim.h"

namespace {

// Tuning, all fixed-point (FP = 1000 per world unit).
constexpr int32_t ARENA_HALF = 7 * FP;  // fighters stay within [-7, 7]
constexpr int32_t HALF_W     = FP / 2;  // cube is 1x1
constexpr int32_t WALK_SPEED = 90;      // 0.09 units/tick = 5.4 units/s
constexpr int32_t JUMP_VEL   = 230;
constexpr int32_t GRAVITY    = 12;

int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }
int32_t abs32(int32_t v) { return v < 0 ? -v : v; }

void apply_input(Fighter& p, Input in) {
  const bool grounded = p.y == 0;

  if (grounded) {
    p.vx = 0;
    if (in & BTN_LEFT) p.vx -= WALK_SPEED;
    if (in & BTN_RIGHT) p.vx += WALK_SPEED;
    if (in & BTN_JUMP) p.vy = JUMP_VEL;
  }
  // In the air: keep the take-off velocity (committed jump, like most fighters).
}

void integrate(Fighter& p) {
  p.x += p.vx;
  p.y += p.vy;
  if (p.y > 0) {
    p.vy -= GRAVITY;
  } else {
    p.y = 0;
    p.vy = 0;
  }
  p.x = clamp(p.x, -ARENA_HALF + HALF_W, ARENA_HALF - HALF_W);
}

// Fighters cannot overlap: push both apart equally (deterministic, symmetric).
void resolve_push(Fighter& a, Fighter& b) {
  const int32_t dx = b.x - a.x;
  const int32_t min_dist = 2 * HALF_W;
  if (abs32(dx) >= min_dist || abs32(b.y - a.y) >= min_dist) return;

  const int32_t overlap = min_dist - abs32(dx);
  // dir: which way b is from a. Exact tie (dx == 0) breaks toward fighter 0 on the left.
  const int32_t dir = dx >= 0 ? 1 : -1;
  const int32_t half = overlap / 2;
  a.x -= dir * half;
  b.x += dir * (overlap - half);  // odd remainder goes to b, never lost
  a.x = clamp(a.x, -ARENA_HALF + HALF_W, ARENA_HALF - HALF_W);
  b.x = clamp(b.x, -ARENA_HALF + HALF_W, ARENA_HALF - HALF_W);
}

void face_each_other(Fighter& a, Fighter& b) {
  if (a.y == 0 && b.x != a.x) a.facing = b.x > a.x ? 1 : -1;
  if (b.y == 0 && b.x != a.x) b.facing = a.x > b.x ? 1 : -1;
}

void hash_mix(uint64_t& h, int64_t v) {
  for (int i = 0; i < 8; ++i) {
    h ^= static_cast<uint8_t>(v >> (i * 8));
    h *= 1099511628211ull;
  }
}

}  // namespace

GameState sim_init() {
  GameState s;
  s.f[0].x = -3 * FP;
  s.f[0].facing = 1;
  s.f[1].x = 3 * FP;
  s.f[1].facing = -1;
  return s;
}

void sim_step(GameState& s, Input in0, Input in1) {
  apply_input(s.f[0], in0);
  apply_input(s.f[1], in1);
  integrate(s.f[0]);
  integrate(s.f[1]);
  resolve_push(s.f[0], s.f[1]);
  face_each_other(s.f[0], s.f[1]);
  s.frame++;
}

uint64_t sim_hash(const GameState& s) {
  uint64_t h = 1469598103934665603ull;
  hash_mix(h, s.frame);
  for (const Fighter& p : s.f) {
    hash_mix(h, p.x);
    hash_mix(h, p.y);
    hash_mix(h, p.vx);
    hash_mix(h, p.vy);
    hash_mix(h, p.facing);
    hash_mix(h, p.hp);
  }
  return h;
}
