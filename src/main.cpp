// netfight: two cubes, deterministic lockstep over UDP.
//
//   local (one keyboard): ./netfight
//   online, window A:     ./netfight --player 0 --port 7000 --peer 127.0.0.1:7001
//   online, window B:     ./netfight --player 1 --port 7001 --peer 127.0.0.1:7000
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "raylib.h"
#include "session.h"
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
  DrawCube({pos.x + 0.55f * p.facing, pos.y + 0.2f, 0.0f}, 0.2f, 0.2f, 0.2f, DARKGRAY);
}

struct Args {
  bool online = false;
  SessionConfig cfg;
  std::string peer_host = "127.0.0.1";
};

bool parse_args(int argc, char** argv, Args& a) {
  for (int i = 1; i < argc; ++i) {
    const bool has_value = i + 1 < argc;
    if (!std::strcmp(argv[i], "--player") && has_value) {
      a.cfg.player = std::atoi(argv[++i]);
    } else if (!std::strcmp(argv[i], "--port") && has_value) {
      a.cfg.local_port = static_cast<uint16_t>(std::atoi(argv[++i]));
    } else if (!std::strcmp(argv[i], "--delay") && has_value) {
      a.cfg.input_delay = std::atoi(argv[++i]);
    } else if (!std::strcmp(argv[i], "--peer") && has_value) {
      const std::string peer = argv[++i];  // host:port
      const auto colon = peer.rfind(':');
      if (colon == std::string::npos) return false;
      a.peer_host = peer.substr(0, colon);
      a.cfg.peer_port = static_cast<uint16_t>(std::atoi(peer.c_str() + colon + 1));
      a.online = true;
    } else {
      return false;
    }
  }
  if (a.cfg.player != 0 && a.cfg.player != 1) return false;
  if (a.cfg.input_delay < 0 || a.cfg.input_delay > 30) return false;
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  Args args;
  if (!parse_args(argc, argv, args)) {
    std::fprintf(stderr,
                 "usage: netfight [--player 0|1 --port N --peer host:port [--delay frames]]\n");
    return 1;
  }

  LockstepSession session;
  if (args.online) {
    args.cfg.peer_host = args.peer_host.c_str();
    if (!session.start(args.cfg)) {
      std::fprintf(stderr, "failed to start network session\n");
      return 1;
    }
  }

  SetConfigFlags(FLAG_MSAA_4X_HINT);
  const char* title = !args.online ? "netfight - local"
                      : args.cfg.player == 0 ? "netfight - player 0 (blue)"
                                             : "netfight - player 1 (orange)";
  InitWindow(1280, 720, title);

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
    if (!args.online && IsKeyPressed(KEY_F5)) state = sim_init();

    accumulator += GetFrameTime();
    if (accumulator > 0.25) accumulator = 0.25;
    while (accumulator >= TICK_DT) {
      if (args.online) {
        session.tick(state, read_p1());  // in online mode you always use WASD/JK
      } else {
        sim_step(state, read_p1(), read_p2());
      }
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
    if (args.online) {
      DrawText(TextFormat("player %d   input delay: %d   stalls: %u   peer frame: %u   "
                          "rounds restarted: %u",
                          session.player(), session.input_delay(), session.stalls(),
                          session.remote_latest(), session.restarts()),
               10, 62, 20, DARKGRAY);
      if (!session.peer_seen()) {
        DrawText("waiting for peer...", 520, 300, 30, MAROON);
      } else if (session.ticks_since_packet() > TICK_RATE) {
        DrawText("connection lost - waiting...", 450, 300, 30, MAROON);
      }
      DrawText("you: A/D move, W jump, J/K attack", 10, 690, 20, GRAY);
    } else {
      DrawText("P1: A/D W J/K    P2: arrows , .    F5: reset", 10, 690, 20, GRAY);
    }
    EndDrawing();
  }

  CloseWindow();
  return 0;
}