# cube-fighter-netcode (netfight)

A minimal two-player fighting game in C++17 whose point is the **netcode**:
deterministic **lockstep with input delay** over raw UDP sockets, with packet-loss
tolerance, RTT/jitter measurement, desync detection and a built-in bad-network
simulator.

Two cubes, a flat arena, light/heavy attacks, a 3-hit combo, blocking, HP bars
and a round timer. The gameplay is deliberately small so the network layer is
easy to read.

## Run

Download a build from the
[Releases](https://github.com/0hJonny/cube-fighter-netcode/releases) page:

| OS | File |
|---|---|
| Windows 10/11 x64 | `netfight-windows-x64.zip` |
| macOS (Apple Silicon) | `netfight-macos-arm64.tar.gz` |
| Linux x64 (X11) | `netfight-linux-x64.tar.gz` |

Every build is compiled and tested by GitHub Actions on its own OS.
`SHA256SUMS.txt` lets you verify a download
(`Get-FileHash <file> -Algorithm SHA256` in PowerShell,
`shasum -a 256 -c SHA256SUMS.txt --ignore-missing` on macOS/Linux).
macOS builds are not signed: run `xattr -dr com.apple.quarantine netfight-macos-arm64`
once after unpacking.

Each archive contains launch scripts (`.bat` on Windows, `.sh` on macOS/Linux):

| Script | What it does |
|---|---|
| `play_local` | Two players on one keyboard, no network |
| `play_online_localhost` | Both players on this computer, connected over UDP |
| `play_bad_network` | Same, with 60 ms lag, 0–20 ms jitter, 10% packet loss |
| `play_lan` | Two computers on the same LAN |

The firewall may ask for network access on first launch.

Command line:

```
netfight                                   # local, one keyboard
netfight --player 0 --port 7000 --peer 127.0.0.1:7001
netfight --player 1 --port 7001 --peer 127.0.0.1:7000
         [--delay frames] [--lag ms] [--jitter ms] [--loss percent]
```

Controls (online: every window uses P1 keys)

| | P1 | P2 (local only) |
|---|---|---|
| Move / block (hold back) | A / D | ← / → |
| Jump | W | ↑ |
| Light / Heavy | J / K | , / . |

F1: render FPS 30/60/144 · F2: show hitboxes · F9 (online): corrupt local state to demo desync detection

## Build

```
cmake -S . -B build
cmake --build build --config Release
./build/netfight
```

CMake downloads raylib automatically. Tests: `sim_test`, `combat_test`, `session_test`.

## Architecture

```
src/sim.*      deterministic game simulation (integers only, no raylib, no OS calls)
src/net.*      non-blocking UDP socket + network conditioner (lag / jitter / loss)
src/session.*  lockstep protocol: input exchange, stalls, RTT, desync detection
src/main.cpp   window, keyboard, fixed-timestep loop, rendering
tests/         determinism, gameplay and network tests
```

The simulation knows nothing about the network, and the network knows nothing
about the game rules: `sim_step(state, input_p1, input_p2)` is the only contact
point. The same function is driven by the keyboard (local mode), by the network
(online mode) and by random inputs (tests).

## Netcode design

**Only inputs are sent.** Each player's input is one byte per frame (5 button
bits). Both peers run the full simulation; if they start from the same state and
apply the same inputs, they stay identical. No positions, no HP, no game state
ever crosses the network.

**Determinism.** This only works if `sim_step` produces bit-identical results on
every machine:

- Fixed-point integers for all game logic (1 unit = 1/1000). Floats are not
  bit-reproducible across compilers and CPUs (operation reordering, FMA
  contraction, different `libm`). Floats appear only in rendering.
- No randomness, clocks or OS calls inside the simulation.
- Hits are detected for both fighters first and applied afterwards, so a trade is
  symmetric and does not depend on which fighter the code checks first.
- `sim_hash` hashes fields one by one, not raw struct memory (padding bytes are
  undefined and differ between builds).

**Fixed timestep.** The simulation runs at exactly 60 Hz using an accumulator,
independent of render FPS (press F1). Game logic tied to frame rate is a classic
bug: I hit it myself in an early Unity project, where a clicker's
clicks-per-second depended on the device.

**Lockstep with input delay.** A frame is simulated only when both inputs for it
are known. The local input pressed on frame N is scheduled for frame
N + delay (default 3 frames = 50 ms), which gives the packet that long to arrive
before it is needed. If it is late, the game waits (a *stall*, counted in the
overlay). Rule of thumb: delay ≥ one-way latency + jitter. Try
`play_bad_network.bat` with `--delay 3` versus `--delay 5`.

**Loss tolerance without retransmission timers.** Each packet carries the
sender's current frame as an *ack*, and every packet repeats all inputs the peer
has not acknowledged yet (up to 64 frames; inputs are 1 byte each). A lost packet
is simply covered by the next one; no resend timer is needed. Duplicates are
harmless because the receiver stores inputs by frame number (idempotent).
An earlier version repeated a fixed window of the last 8 frames, which froze
both peers when one fell more than 8 frames behind (found by running the two
windows with different `--delay` values).

**Ring buffers.** Inputs are stored in `slot[frame % 64]`, and each slot records
which frame it holds, so stale data is never mistaken for new data. Packets may
arrive in any order (jitter reorders them).

**Packet format (v5).** Little-endian, written byte by byte (no struct casting):
magic, version, player, sender nonce and receiver nonce, timestamp + echoed
timestamp for RTT, a state-hash checkpoint, the ack, then the input window.
Every field is validated before use: bytes from the network are never trusted.

**RTT and jitter.** Each packet carries the sender's clock; the peer echoes the
latest one back with how long it held it. RTT = now − echoed stamp − hold time,
smoothed with exponential moving averages (as in TCP's SRTT/RTTVAR).

**Desync detection.** Every 30 frames each peer hashes its state and sends the
hash. Mismatch → the first divergent frame is reported. Press F9 to modify the
local state behind the protocol's back and watch it get caught.

**Peer restart.** Found in manual testing: if one window was closed and reopened
mid-match, the new process started at frame 0 while the other side was at frame
N; each discarded the other's inputs as "too old" / "too far ahead" and both
froze. Fix: every packet carries a random per-launch nonce of the sender and the
nonce of the receiver it is meant for. A new sender nonce means the peer
restarted, so both sides start a new match from frame 0; packets addressed to a
previous process are ignored, so inputs, acks and hashes of an old match cannot
leak into the new one. Covered by tests, including restarts in the first frames
where old and new frame numbers overlap.

**Different input delays per player.** Each side fills only its *own* first
`delay` frames with empty input and sends them like any other input. An earlier
version also pre-filled the peer's first frames, which desynced when the two
players used different `--delay` values.

## Tests

- `sim_test` — two simulations, 10 minutes of random inputs, hash compared every frame.
- `combat_test` — damage, the light > light > heavy combo is a true combo
  (no escape between hits), blocking, whiff, no auto-repeat on held buttons, KO, time over.
- `session_test` — two sessions over real UDP: sync with an uneven peer, kill and
  relaunch a peer, injected desync, 40 ms lag + 0–30 ms jitter + 20% loss, and
  input delay 1 vs 10 frames with 30% loss.

CI builds and runs all tests on Windows, macOS and Linux.

### Demo video: recorded by a script

`tools/record_demo.sh` records the demo video with no human at the keyboard.
Two real game windows run side by side on a virtual display (Xvfb) and talk
over real UDP; `xdotool` plays a fixed script of key presses (a tiny TAS);
`ffmpeg` records the screen, adds title and end cards and burns in captions
(`tools/add_captions.sh`). Linux only; needs Xvfb, xdotool and ffmpeg.

It doubles as an end-to-end test of things one person at one keyboard cannot
check, such as blocking: P2 has to hold "back" in its own window while P1
attacks in the other. Doing that revealed a detail of the test setup: a window
that loses focus releases all its keys, so the script sends P2's key to its
window without moving focus. Caption timings were checked against the recorded
frames (hit flash, blockstun, the DESYNC message).

## Trade-offs and next steps

- **Rollback instead of input delay.** Lockstep with delay is simple and correct,
  but responsiveness degrades with latency. Rollback (GGPO-style) predicts the
  remote input, simulates immediately and re-simulates when the real input
  differs. The groundwork is already here: deterministic `sim_step`, a cheap
  copyable `GameState` (84 bytes, so saving a state per frame is free), and
  frame-indexed input buffers. Missing: prediction, state history, re-simulation.
- **Rejoining a match in progress.** Two options: send a state snapshot (tiny
  here, but large in a real game with many objects), or send the input log and
  let the newcomer fast-forward the simulation (a replay; ~2 bytes per frame).
- **Internet play.** P2P across NATs needs hole punching or a relay server.
  Currently LAN / localhost.
- **Security.** In lockstep every peer has the full game state, so information
  is not hidden; a client-server model with an authoritative server is the
  answer for games where that matters. Server-authoritative architectures with
  client-side prediction, reconciliation and interpolation (as in Unreal
  Engine replication) are the other main family of netcode.

## Third-party code

- [raylib 5.5](https://github.com/raysan5/raylib) (zlib license): window, input,
  drawing. Downloaded by CMake at build time; not modified. Used only in `src/main.cpp`.
- OS socket APIs (BSD sockets / Winsock 2).

## Use of generative AI

This project was built with an AI assistant (Claude, by Anthropic). The AI wrote
the code for all files in `src/`, `tests/`, `tools/`, the CI workflow and
scripts, recorded the demo video with `tools/record_demo.sh` (scripted inputs),
and drafted this README. My part: setting the goal and scope, discussing and choosing
the approach (lockstep vs. rollback, fixed-point math, input redundancy,
frame-indexed buffers), building and testing every stage on my machine, finding
bugs in manual testing (the peer-restart freeze and the freeze/desync with
different input delays, both described above) and directing the fixes,
and reviewing the code until I could explain each part.

## License

MIT, see [LICENSE](LICENSE). raylib is under the zlib license.
