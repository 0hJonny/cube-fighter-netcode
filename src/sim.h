// Deterministic fighting-game simulation.
//
// Rules that keep both peers bit-identical:
//   - Only integers (fixed-point). No float/double anywhere in this file or sim.cpp.
//   - No randomness, no clocks, no OS calls. Output depends only on (state, inputs).
//   - Iteration order is always explicit (fighter 0, then fighter 1), and hits are
//     detected for both fighters first and applied afterwards, so neither player
//     gets an advantage from being "first" in the code.
#pragma once
#include <cstdint>

// 1 world unit = 1000 fixed-point units ("milli-units"). Rendering divides by FP.
constexpr int32_t FP = 1000;

constexpr int TICK_RATE = 60;  // simulation steps per second, independent of render FPS
constexpr uint16_t ROUND_FRAMES = 60 * TICK_RATE;  // 60-second round
constexpr int16_t MAX_HP = 1000;

// Input is one byte per player per frame. This byte is all that goes over the network.
enum Button : uint8_t {
  BTN_LEFT  = 1 << 0,
  BTN_RIGHT = 1 << 1,
  BTN_JUMP  = 1 << 2,
  BTN_LIGHT = 1 << 3,
  BTN_HEAVY = 1 << 4,
};
using Input = uint8_t;

enum class FState : uint8_t { Idle, Attack, Hitstun, Blockstun, KO };
enum class Move : uint8_t { None, Light, Heavy };

struct Fighter {
  int32_t x = 0, y = 0;    // position, fixed-point (y = 0 is the floor)
  int32_t vx = 0, vy = 0;  // velocity per tick, fixed-point
  int8_t facing = 1;       // +1 looks right, -1 looks left
  int16_t hp = MAX_HP;

  FState state = FState::Idle;
  Move move = Move::None;     // current attack while state == Attack
  uint16_t state_frame = 0;   // frames spent in the current attack
  uint16_t stun = 0;          // frames of hit/block stun left
  uint8_t chain = 0;          // position in the light > light > heavy chain
  bool hit_landed = false;    // current attack already connected (hits once)
  bool holding_back = false;  // grounded, idle and holding away = blocking

  Input prev_in = 0;          // previous frame's input, to detect presses
  Move buffered = Move::None; // attack pressed too early, kept for a few frames
  uint8_t buffer_timer = 0;

  uint16_t combo_hits = 0;    // hits taken in the current combo (for the UI)
};

enum class Round : uint8_t { Fighting, Over };

struct GameState {
  uint32_t frame = 0;
  Fighter f[2];
  uint16_t timer = ROUND_FRAMES;  // frames left in the round
  Round round = Round::Fighting;
  uint8_t winner = 0;             // 0, 1, or 2 = draw (valid when round == Over)
  uint16_t over_frames = 0;       // frames since the round ended
  uint8_t wins[2] = {0, 0};
};

GameState sim_init();
void sim_step(GameState& s, Input in0, Input in1);

// Attack hitbox in fixed-point world coordinates, if the fighter's attack is in
// its active frames. Used by the sim and by the debug overlay (F2).
bool sim_hitbox(const Fighter& p, int32_t& x0, int32_t& x1, int32_t& y0, int32_t& y1);

// FNV-1a over every field, in a fixed order. Used for desync detection:
// peers exchange this hash and compare. Hashing fields (not raw struct bytes)
// avoids padding garbage and makes the hash portable across compilers.
uint64_t sim_hash(const GameState& s);
