#include "sim.h"

namespace {

// ---- Tuning (fixed-point, FP = 1000 per world unit; times in frames) ----
constexpr int32_t ARENA_HALF = 7 * FP;  // fighters stay within [-7, 7]
constexpr int32_t HALF_W     = FP / 2;  // cube is 1x1
constexpr int32_t HEIGHT     = FP;
constexpr int32_t WALK_SPEED = 90;      // 0.09 units/tick = 5.4 units/s
constexpr int32_t JUMP_VEL   = 230;
constexpr int32_t GRAVITY    = 12;
constexpr int32_t FRICTION   = 8;       // knockback slows down by this per tick
constexpr uint8_t BUFFER_FRAMES = 6;    // early presses are remembered this long
constexpr uint16_t ROUND_END_PAUSE = 3 * TICK_RATE;

struct MoveData {
  uint16_t startup, active, recovery;  // frame data, like a fighting-game wiki
  int32_t reach;                       // hitbox length in front of the cube
  int16_t damage;
  uint16_t hitstun, blockstun;
  int32_t knockback, block_push;       // initial push speed on hit / on block
};

constexpr MoveData LIGHT = {4, 3, 8, 800, 50, 16, 10, 40, 30};
constexpr MoveData HEAVY = {9, 4, 18, 1100, 120, 24, 16, 200, 60};

const MoveData& data(Move m) { return m == Move::Heavy ? HEAVY : LIGHT; }

int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }
int32_t abs32(int32_t v) { return v < 0 ? -v : v; }
int32_t sign(int32_t v) { return (v > 0) - (v < 0); }

bool in_active_frames(const Fighter& p) {
  if (p.state != FState::Attack) return false;
  const MoveData& d = data(p.move);
  return p.state_frame >= d.startup && p.state_frame < d.startup + d.active;
}

void start_attack(Fighter& p, Move m, uint8_t chain) {
  p.state = FState::Attack;
  p.move = m;
  p.chain = chain;
  p.state_frame = 0;
  p.hit_landed = false;
  p.buffered = Move::None;
  p.buffer_timer = 0;
  p.vx = 0;
}

void process_input(Fighter& p, Input in, const Fighter& opp) {
  // Edge detection: an attack starts on the frame the button goes down,
  // holding it does nothing. Presses are buffered for a few frames, so a
  // press slightly before the fighter can act still comes out.
  const Input pressed = static_cast<Input>(in & ~p.prev_in);
  p.prev_in = in;
  if (pressed & BTN_HEAVY) {
    p.buffered = Move::Heavy;
    p.buffer_timer = BUFFER_FRAMES;
  } else if (pressed & BTN_LIGHT) {
    p.buffered = Move::Light;
    p.buffer_timer = BUFFER_FRAMES;
  } else if (p.buffer_timer > 0 && --p.buffer_timer == 0) {
    p.buffered = Move::None;
  }

  const bool grounded = p.y == 0;
  const int32_t toward_opp = opp.x >= p.x ? 1 : -1;
  const Input back_btn = toward_opp > 0 ? BTN_LEFT : BTN_RIGHT;
  p.holding_back = false;

  switch (p.state) {
    case FState::Idle:
      if (!grounded) break;  // committed jump: no air control, no air attacks
      if (p.buffered != Move::None) {
        start_attack(p, p.buffered, p.buffered == Move::Heavy ? 2 : 0);
        break;
      }
      p.holding_back = (in & back_btn) != 0;
      p.vx = 0;
      if (in & BTN_LEFT) p.vx -= WALK_SPEED;
      if (in & BTN_RIGHT) p.vx += WALK_SPEED;
      if (in & BTN_JUMP) p.vy = JUMP_VEL;
      break;

    case FState::Attack: {
      // Chain cancel: once an attack has hit, the next attack in the chain can
      // interrupt its recovery. light > light > heavy (or light > heavy).
      const bool can_cancel = p.hit_landed && p.state_frame >= data(p.move).startup;
      if (can_cancel && p.move == Move::Light) {
        if (p.buffered == Move::Light && p.chain == 0) {
          start_attack(p, Move::Light, 1);
        } else if (p.buffered == Move::Heavy) {
          start_attack(p, Move::Heavy, 2);
        }
      }
      break;
    }

    case FState::Hitstun:
    case FState::Blockstun:
    case FState::KO:
      break;  // no control; the buffer still records presses for a reversal
  }
}

void integrate(Fighter& p) {
  const bool stunned = p.state == FState::Hitstun || p.state == FState::Blockstun ||
                       p.state == FState::KO;
  if (stunned && p.y == 0) {  // knockback slides to a stop
    const int32_t s = sign(p.vx);
    p.vx -= s * FRICTION;
    if (sign(p.vx) != s) p.vx = 0;
  }
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
  if (abs32(dx) >= min_dist || abs32(b.y - a.y) >= HEIGHT) return;

  const int32_t overlap = min_dist - abs32(dx);
  // dir: which way b is from a. Exact tie (dx == 0) puts fighter 0 on the left.
  const int32_t dir = dx >= 0 ? 1 : -1;
  const int32_t half = overlap / 2;
  a.x -= dir * half;
  b.x += dir * (overlap - half);  // odd remainder goes to b, never lost
  a.x = clamp(a.x, -ARENA_HALF + HALF_W, ARENA_HALF - HALF_W);
  b.x = clamp(b.x, -ARENA_HALF + HALF_W, ARENA_HALF - HALF_W);
}

void face_each_other(Fighter& a, Fighter& b) {
  if (a.x == b.x) return;
  if (a.y == 0 && a.state == FState::Idle) a.facing = b.x > a.x ? 1 : -1;
  if (b.y == 0 && b.state == FState::Idle) b.facing = a.x > b.x ? 1 : -1;
}

bool hits(const Fighter& att, const Fighter& def) {
  if (att.hit_landed || def.state == FState::KO) return false;
  int32_t x0, x1, y0, y1;
  if (!sim_hitbox(att, x0, x1, y0, y1)) return false;
  return x0 < def.x + HALF_W && x1 > def.x - HALF_W && y0 < def.y + HEIGHT && y1 > def.y;
}

void apply_hit(Fighter& att, Fighter& def) {
  const MoveData& d = data(att.move);
  att.hit_landed = true;
  if (def.holding_back && def.state == FState::Idle) {
    def.state = FState::Blockstun;
    def.stun = d.blockstun;
    def.vx = att.facing * d.block_push;
    return;
  }
  def.combo_hits = def.state == FState::Hitstun ? def.combo_hits + 1 : 1;
  def.hp = static_cast<int16_t>(def.hp > d.damage ? def.hp - d.damage : 0);
  def.move = Move::None;
  def.vx = att.facing * d.knockback;
  if (def.hp == 0) {
    def.state = FState::KO;
    def.stun = 0;
  } else {
    def.state = FState::Hitstun;
    def.stun = d.hitstun;
  }
}

void advance_timers(Fighter& p) {
  switch (p.state) {
    case FState::Attack: {
      ++p.state_frame;
      const MoveData& d = data(p.move);
      if (p.state_frame >= d.startup + d.active + d.recovery) {
        p.state = FState::Idle;
        p.move = Move::None;
        p.chain = 0;
      }
      break;
    }
    case FState::Hitstun:
    case FState::Blockstun:
      if (p.stun > 0) --p.stun;
      if (p.stun == 0) p.state = FState::Idle;
      break;
    case FState::Idle:
    case FState::KO:
      break;
  }
}

void reset_round(GameState& s) {
  const GameState fresh = sim_init();
  s.f[0] = fresh.f[0];
  s.f[1] = fresh.f[1];
  s.timer = ROUND_FRAMES;
  s.round = Round::Fighting;
  s.over_frames = 0;
  // s.frame and s.wins carry over: the frame counter never goes backwards.
}

void end_round(GameState& s, uint8_t winner) {
  s.round = Round::Over;
  s.winner = winner;
  s.over_frames = 0;
  if (winner < 2) ++s.wins[winner];
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

bool sim_hitbox(const Fighter& p, int32_t& x0, int32_t& x1, int32_t& y0, int32_t& y1) {
  if (!in_active_frames(p)) return false;
  const int32_t front = p.x + p.facing * HALF_W;
  const int32_t tip = front + p.facing * data(p.move).reach;
  x0 = p.facing > 0 ? front : tip;
  x1 = p.facing > 0 ? tip : front;
  y0 = p.y + HEIGHT / 4;
  y1 = p.y + HEIGHT * 3 / 4;
  return true;
}

void sim_step(GameState& s, Input in0, Input in1) {
  if (s.round == Round::Over) {
    // Inputs are ignored between rounds; the next round starts by itself.
    in0 = in1 = 0;
    if (++s.over_frames >= ROUND_END_PAUSE) reset_round(s);
  }

  process_input(s.f[0], in0, s.f[1]);
  process_input(s.f[1], in1, s.f[0]);
  integrate(s.f[0]);
  integrate(s.f[1]);
  resolve_push(s.f[0], s.f[1]);

  // Detect both hits first, then apply: a trade (both hit on the same frame)
  // is symmetric and does not depend on which fighter the code checks first.
  const bool fighting = s.round == Round::Fighting;
  const bool hit01 = fighting && hits(s.f[0], s.f[1]);
  const bool hit10 = fighting && hits(s.f[1], s.f[0]);
  if (hit01) apply_hit(s.f[0], s.f[1]);
  if (hit10) apply_hit(s.f[1], s.f[0]);

  advance_timers(s.f[0]);
  advance_timers(s.f[1]);
  face_each_other(s.f[0], s.f[1]);

  if (s.round == Round::Fighting) {
    const bool ko0 = s.f[0].hp == 0, ko1 = s.f[1].hp == 0;
    if (ko0 || ko1) {
      end_round(s, ko0 && ko1 ? 2 : (ko1 ? 0 : 1));
    } else if (--s.timer == 0) {
      const int16_t h0 = s.f[0].hp, h1 = s.f[1].hp;
      end_round(s, h0 == h1 ? 2 : (h0 > h1 ? 0 : 1));
    }
  }
  s.frame++;
}

uint64_t sim_hash(const GameState& s) {
  uint64_t h = 1469598103934665603ull;
  hash_mix(h, s.frame);
  hash_mix(h, s.timer);
  hash_mix(h, static_cast<int64_t>(s.round));
  hash_mix(h, s.winner);
  hash_mix(h, s.over_frames);
  hash_mix(h, s.wins[0]);
  hash_mix(h, s.wins[1]);
  for (const Fighter& p : s.f) {
    hash_mix(h, p.x);
    hash_mix(h, p.y);
    hash_mix(h, p.vx);
    hash_mix(h, p.vy);
    hash_mix(h, p.facing);
    hash_mix(h, p.hp);
    hash_mix(h, static_cast<int64_t>(p.state));
    hash_mix(h, static_cast<int64_t>(p.move));
    hash_mix(h, p.state_frame);
    hash_mix(h, p.stun);
    hash_mix(h, p.chain);
    hash_mix(h, p.hit_landed);
    hash_mix(h, p.holding_back);
    hash_mix(h, p.prev_in);
    hash_mix(h, static_cast<int64_t>(p.buffered));
    hash_mix(h, p.buffer_timer);
    hash_mix(h, p.combo_hits);
  }
  return h;
}
