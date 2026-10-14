// netfight: a two-cube fighting game with deterministic lockstep netcode over UDP.
//
//   local (one keyboard): ./netfight
//   online, window A:     ./netfight --player 0 --port 7000 --peer 127.0.0.1:7001
//   online, window B:     ./netfight --player 1 --port 7001 --peer 127.0.0.1:7000
//   bad network test:     add --lag 60 --jitter 20 --loss 10 (to both)
//
// Keys: F1 render FPS 30/60/144, F2 hitboxes, F9 corrupt local state (desync demo).
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

// ---------- rendering (the only place fixed-point becomes float) ----------

float to_f(int32_t v) { return static_cast<float>(v) / FP; }

void draw_fighter(const Fighter& p, Color base, bool show_hitbox) {
  Color body = base;
  if (p.state == FState::Hitstun) body = (p.stun / 3) % 2 ? WHITE : base;  // hit flash
  if (p.state == FState::Blockstun) body = GRAY;
  if (p.state == FState::KO) body = DARKGRAY;

  const Vector3 pos = {to_f(p.x), to_f(p.y) + 0.5f, 0.0f};
  DrawCube(pos, 1.0f, 1.0f, 1.0f, body);
  DrawCubeWires(pos, 1.0f, 1.0f, 1.0f, BLACK);

  int32_t x0, x1, y0, y1;
  if (sim_hitbox(p, x0, x1, y0, y1)) {
    // Active frames: draw the "fist" exactly where the hitbox is.
    const Vector3 c = {(to_f(x0) + to_f(x1)) / 2, (to_f(y0) + to_f(y1)) / 2, 0.0f};
    const Vector3 size = {to_f(x1 - x0), to_f(y1 - y0), 0.5f};
    DrawCubeV(c, size, p.move == Move::Heavy ? RED : ORANGE);
    if (show_hitbox) DrawCubeWiresV(c, {size.x, size.y, 1.2f}, RED);
  } else if (p.state == FState::Attack) {
    // Startup or recovery: a small fist next to the body.
    DrawCube({pos.x + 0.6f * p.facing, pos.y, 0.0f}, 0.25f, 0.25f, 0.25f, MAROON);
  } else {
    DrawCube({pos.x + 0.55f * p.facing, pos.y + 0.2f, 0.0f}, 0.2f, 0.2f, 0.2f, DARKGRAY);
  }
  if (show_hitbox) DrawCubeWires(pos, 1.0f, 1.0f, 1.05f, BLUE);  // hurtbox
}

void draw_hud(const GameState& s) {
  const int w = GetScreenWidth();
  const int bar_w = 480, bar_h = 26, top = 92;
  for (int i = 0; i < 2; ++i) {
    const int x = i == 0 ? 40 : w - 40 - bar_w;
    const float ratio = static_cast<float>(s.f[i].hp) / MAX_HP;
    DrawRectangle(x, top, bar_w, bar_h, LIGHTGRAY);
    const int fill = static_cast<int>(bar_w * ratio);
    // P1's bar drains toward the center from the left, P2's from the right.
    DrawRectangle(i == 0 ? x + bar_w - fill : x, top, fill, bar_h, i == 0 ? SKYBLUE : ORANGE);
    DrawRectangleLines(x, top, bar_w, bar_h, DARKGRAY);
    DrawText(TextFormat("P%d  wins: %d", i + 1, s.wins[i]), x, top + bar_h + 6, 20, DARKGRAY);
    if (s.f[i].state == FState::Hitstun && s.f[i].combo_hits >= 2) {
      DrawText(TextFormat("%d HIT COMBO", s.f[i].combo_hits), i == 0 ? w - 300 : 60, 200, 32,
               RED);
    }
  }
  const int secs = (s.timer + TICK_RATE - 1) / TICK_RATE;
  const char* t = TextFormat("%d", secs);
  DrawText(t, w / 2 - MeasureText(t, 48) / 2, top - 12, 48, DARKGRAY);

  if (s.round == Round::Over) {
    const bool ko = s.f[0].hp == 0 || s.f[1].hp == 0;
    const char* head = ko ? "K.O." : "TIME";
    const char* who = s.winner == 2 ? "DRAW" : (s.winner == 0 ? "P1 WINS" : "P2 WINS");
    DrawText(head, w / 2 - MeasureText(head, 80) / 2, 230, 80, MAROON);
    DrawText(who, w / 2 - MeasureText(who, 40) / 2, 320, 40, DARKGRAY);
  }
}

// ---------- command line ----------

struct Args {
  bool online = false;
  SessionConfig cfg;
  std::string peer_host = "127.0.0.1";
};

bool parse_args(int argc, char** argv, Args& a) {
  for (int i = 1; i < argc; ++i) {
    const bool has_value = i + 1 < argc;
    const char* k = argv[i];
    if (!has_value) return false;
    const char* v = argv[++i];
    if (!std::strcmp(k, "--player")) {
      a.cfg.player = std::atoi(v);
    } else if (!std::strcmp(k, "--port")) {
      a.cfg.local_port = static_cast<uint16_t>(std::atoi(v));
    } else if (!std::strcmp(k, "--delay")) {
      a.cfg.input_delay = std::atoi(v);
    } else if (!std::strcmp(k, "--lag")) {
      a.cfg.lag_ms = std::atoi(v);
    } else if (!std::strcmp(k, "--jitter")) {
      a.cfg.jitter_ms = std::atoi(v);
    } else if (!std::strcmp(k, "--loss")) {
      a.cfg.loss_percent = std::atoi(v);
    } else if (!std::strcmp(k, "--peer")) {
      const std::string peer = v;  // host:port
      const auto colon = peer.rfind(':');
      if (colon == std::string::npos) return false;
      a.peer_host = peer.substr(0, colon);
      a.cfg.peer_port = static_cast<uint16_t>(std::atoi(peer.c_str() + colon + 1));
      a.online = true;
    } else {
      return false;
    }
  }
  const SessionConfig& c = a.cfg;
  return (c.player == 0 || c.player == 1) && c.input_delay >= 0 && c.input_delay <= 30 &&
         c.lag_ms >= 0 && c.jitter_ms >= 0 && c.loss_percent >= 0 && c.loss_percent <= 100;
}

}  // namespace

int main(int argc, char** argv) {
  Args args;
  if (!parse_args(argc, argv, args)) {
    std::fprintf(stderr,
                 "usage: netfight [--player 0|1 --port N --peer host:port]\n"
                 "                [--delay frames] [--lag ms] [--jitter ms] [--loss percent]\n");
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
                      : args.cfg.player == 0 ? "netfight - P1 (blue)"
                                             : "netfight - P2 (orange)";
  InitWindow(1280, 720, title);

  const int fps_options[] = {30, 60, 144};
  int fps_idx = 1;
  SetTargetFPS(fps_options[fps_idx]);
  bool show_hitboxes = false;

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
    if (IsKeyPressed(KEY_F2)) show_hitboxes = !show_hitboxes;
    if (!args.online && IsKeyPressed(KEY_F5)) state = sim_init();
    // Desync demo: change our copy of the state behind the protocol's back.
    // The peer's copy is untouched, so the next hash checkpoint must disagree.
    if (args.online && IsKeyPressed(KEY_F9)) state.f[0].x += 1;

    // Fixed timestep: run as many 1/60 s ticks as real time has accumulated.
    // Clamp prevents a "spiral of death" after a long stall (window drag, breakpoint).
    accumulator += GetFrameTime();
    if (accumulator > 0.25) accumulator = 0.25;
    while (accumulator >= TICK_DT) {
      if (args.online) {
        session.tick(state, read_p1());  // online: you always play with WASD + J/K
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
    draw_fighter(state.f[0], SKYBLUE, show_hitboxes);
    draw_fighter(state.f[1], ORANGE, show_hitboxes);
    EndMode3D();
    draw_hud(state);

    // Debug overlay
    DrawText(TextFormat("render FPS %d (F1)  sim %d Hz  frame %u  hash %016llx", GetFPS(),
                        TICK_RATE, state.frame,
                        static_cast<unsigned long long>(sim_hash(state))),
             10, 8, 18, GRAY);
    if (args.online) {
      const SessionConfig& c = session.config();
      DrawText(TextFormat("P%d  ping %d ms  jitter %d ms  input delay %d (%d ms)  stalls %u  "
                          "sim lag %d+%d ms loss %d%%",
                          session.player() + 1, session.rtt_ms(), session.jitter_ms(),
                          session.input_delay(), session.input_delay() * 1000 / TICK_RATE,
                          session.stalls(), c.lag_ms, c.jitter_ms, c.loss_percent),
               10, 30, 18, GRAY);
      if (session.desynced()) {
        DrawText(TextFormat("DESYNC detected at frame %u", session.desync_frame()), 10, 52, 18,
                 RED);
      } else {
        DrawText(TextFormat("in sync: hashes verified up to frame %u   (F9 = corrupt local state)",
                            session.last_verified_frame()),
                 10, 52, 18, DARKGREEN);
      }
      if (!session.peer_seen()) {
        DrawText("waiting for peer...", 500, 400, 30, MAROON);
      } else if (session.ticks_since_packet() > TICK_RATE) {
        DrawText("connection lost - waiting...", 440, 400, 30, MAROON);
      }
      DrawText("you: A/D move (hold back = block), W jump, J light, K heavy   F2 hitboxes", 10,
               692, 20, GRAY);
    } else {
      DrawText("P1: A/D W J/K   P2: arrows , .   hold back = block   F2 hitboxes  F5 reset", 10,
               692, 20, GRAY);
    }
    EndDrawing();
  }

  CloseWindow();
  return 0;
}
