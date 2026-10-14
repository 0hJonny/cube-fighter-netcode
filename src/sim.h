// Deterministic fighting-game simulation.
//
// Rules that keep both peers bit-identical:
//   - Only integers (fixed-point). No float/double anywhere in this file or sim.cpp.
//   - No randomness, no clocks, no OS calls. Output depends only on (state, inputs).
//   - Iteration order is always explicit (fighter 0, then fighter 1).
#pragma once
#include <cstdint>

// 1 world unit = 1000 fixed-point units ("milli-units"). Rendering divides by FP.
constexpr int32_t FP = 1000;

constexpr int TICK_RATE = 60;  // simulation steps per second, independent of render FPS

// Input is one byte per player per frame. This byte is all that goes over the network.
enum Button : uint8_t {
  BTN_LEFT  = 1 << 0,
  BTN_RIGHT = 1 << 1,
  BTN_JUMP  = 1 << 2,
  BTN_LIGHT = 1 << 3,
  BTN_HEAVY = 1 << 4,
};
using Input = uint8_t;

struct Fighter {
  int32_t x = 0, y = 0;    // position, fixed-point (y = 0 is the floor)
  int32_t vx = 0, vy = 0;  // velocity per tick, fixed-point
  int8_t facing = 1;       // +1 looks right, -1 looks left
  int16_t hp = 1000;
};

struct GameState {
  uint32_t frame = 0;
  Fighter f[2];
};

GameState sim_init();
void sim_step(GameState& s, Input in0, Input in1);

// FNV-1a over every field, in a fixed order. Used for desync detection:
// peers exchange this hash and compare. Hashing fields (not raw struct bytes)
// avoids padding garbage and makes the hash portable across compilers.
uint64_t sim_hash(const GameState& s);
