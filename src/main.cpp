// Stage 1: local two-player build. Window + fixed 60 Hz simulation + rendering.
// Networking replaces read_local_inputs() in stage 3; the sim does not change.
#include <cstdio>

#include "raylib.h"
#include "sim.h"

namespace {

constexpr double TICK_DT = 1.0 / TICK_RATE;

Input read_p1() {  // A/D move, W jump, J light, K heavy
  Input in = 0;
  if (IsKeyDown(KEY_A)) in |= BTN_LEFT;
  if (IsKeyDown(KEY_D)) in |= BTN_RIGHT;
  if (IsKeyDown(KEY_W)) in |= BTN_JUMP;
  if (IsKeyDown(KEY_J)) in |= BTN_LIGHT;
  if (IsKeyDown(KEY_K)) in |= BTN_HEAVY;
  return in;
}

Input read_p2() {  // arrows move/jump, , light, . heavy
  Input in = 0;
  if (IsKeyDown(KEY_LEFT)) in |= BTN_LEFT;
  if (IsKeyDown(KEY_RIGHT)) in |= BTN_RIGHT;
  if (IsKeyDown(KEY_UP)) in |= BTN_JUMP;
  if (IsKeyDown(KEY_COMMA)) in |= BTN_LIGHT;
  if (IsKeyDown(KEY_PERIOD)) in |= BTN_HEAVY;
  return in;
}

// Fixed-point -> float happens ONLY here, for drawing. Never feeds back into the sim.
Vector3 to_world(const Fighter& p) {
  return {static_cast<float>(p.x) / FP, static_cast<float>(p.y) / FP + 0.5f, 0.0f};
}

void draw_fighter(const Fighter& p, Color body) {
  const Vector3 pos = to_world(p);
  DrawCube(pos, 1.0f, 1.0f, 1.0f, body);
  DrawCubeWires(pos, 1.0f, 1.0f, 1.0f, BLACK);
  // Small "nose" shows facing direction.
  DrawCube({pos.x + 0.55f * p.facing, pos.y + 0.2f, 0.0f}, 0.2f, 0.2f, 0.2f, DARKGRAY);
}

}  // namespace

int main() {
  SetConfigFlags(FLAG_MSAA_4X_HINT);
  InitWindow(1280, 720, "netfight - stage 1 (local)");

  // Render FPS is deliberately decoupled from the 60 Hz tick: F1 cycles it so the
  // video can show the simulation speed does not change with frame rate.
  const int fps_options[] = {30, 60, 144};
  int fps_idx = 1;
  SetTargetFPS(fps_options[fps_idx]);

  Camera3D cam{};
  cam.position = {0.0f, 3.0f, 14.0f};
  cam.target = {0.0f, 1.5f, 0.0f};
  cam.up = {0.0f, 1.0f, 0.0f};
  cam.fovy = 45.0f;
  cam.projection = CAMERA_PERSPECTIVE;

  GameState state = sim_init();
  double accumulator = 0.0;

  while (!WindowShouldClose()) {
    if (IsKeyPressed(KEY_F1)) {
      fps_idx = (fps_idx + 1) % 3;
      SetTargetFPS(fps_options[fps_idx]);
    }
    if (IsKeyPressed(KEY_F5)) state = sim_init();

    // Fixed timestep: run as many 1/60 s ticks as real time has accumulated.
    // Clamp prevents a "spiral of death" after a long stall (window drag, breakpoint).
    accumulator += GetFrameTime();
    if (accumulator > 0.25) accumulator = 0.25;
    while (accumulator >= TICK_DT) {
      sim_step(state, read_p1(), read_p2());
      accumulator -= TICK_DT;
    }

    BeginDrawing();
    ClearBackground(RAYWHITE);
    BeginMode3D(cam);
    DrawPlane({0.0f, 0.0f, 0.0f}, {14.0f, 4.0f}, LIGHTGRAY);
    DrawGrid(14, 1.0f);
    draw_fighter(state.f[0], SKYBLUE);
    draw_fighter(state.f[1], ORANGE);
    EndMode3D();

    DrawText(TextFormat("render FPS: %d (F1)   sim: %d Hz   frame: %u", GetFPS(), TICK_RATE,
                        state.frame),
             10, 10, 20, DARKGRAY);
    DrawText(TextFormat("hash: %016llx", static_cast<unsigned long long>(sim_hash(state))), 10,
             36, 20, DARKGRAY);
    DrawText("P1: A/D W J/K    P2: arrows , .    F5: reset", 10, 690, 20, GRAY);
    EndDrawing();
  }

  CloseWindow();
  return 0;
}
