// Determinism test: two independent simulations fed the same pseudo-random
// input stream must produce the same hash on every frame.
// (The PRNG lives here in the test, never inside the sim.)
#include <cstdio>

#include "sim.h"

int main() {
  GameState a = sim_init();
  GameState b = sim_init();

  uint32_t seed = 12345;
  auto next = [&seed]() {  // xorshift32: same sequence on every platform
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
  };

  const int frames = 60 * 60 * 10;  // 10 minutes of play
  for (int i = 0; i < frames; ++i) {
    const uint32_t r = next();
    const Input in0 = static_cast<Input>(r & 0x1F);
    const Input in1 = static_cast<Input>((r >> 8) & 0x1F);
    sim_step(a, in0, in1);
    sim_step(b, in0, in1);
    if (sim_hash(a) != sim_hash(b)) {
      std::printf("DESYNC at frame %d\n", i);
      return 1;
    }
  }
  std::printf("OK: %d frames, final hash %016llx\n", frames,
              static_cast<unsigned long long>(sim_hash(a)));
  return 0;
}
