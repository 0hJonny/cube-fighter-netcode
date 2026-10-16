#!/bin/bash
# Records the demo video end to end, with no human at the keyboard.
#
# Two real game windows run side by side on a virtual X display (Xvfb) and talk
# over real UDP. xdotool plays a fixed script of key presses (a tiny TAS), and
# ffmpeg records the screen. Three scenes are recorded, then joined with a
# title card and an end card, and captions are burned in.
#
# This also works as an end-to-end test of things one keyboard cannot do,
# e.g. blocking, which needs input in BOTH windows at the same time.
#
# Linux only. Needs: Xvfb, xdotool, ffmpeg (with drawtext), a DejaVu font,
# and a built game: cmake -S . -B build && cmake --build build
#
#   usage: tools/record_demo.sh [output.mp4]      (default: demo.mp4)
set -u
cd "$(dirname "$0")/.."
OUT="${1:-demo.mp4}"
BIN="$PWD/build/netfight"
DISP=:97
WORK=$(mktemp -d)
FONT=/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf
FONTB=/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf

for tool in Xvfb xdotool ffmpeg ffprobe; do
  command -v "$tool" >/dev/null || { echo "missing: $tool"; exit 1; }
done
[ -x "$BIN" ] || { echo "build the game first: $BIN not found"; exit 1; }
[ -f "$FONT" ] && [ -f "$FONTB" ] || { echo "missing DejaVu fonts"; exit 1; }

Xvfb $DISP -screen 0 2560x720x24 >/dev/null 2>&1 &
XVFB=$!
trap 'pkill -x netfight 2>/dev/null; kill $XVFB 2>/dev/null; rm -rf "$WORK"' EXIT
export DISPLAY=$DISP LIBGL_ALWAYS_SOFTWARE=1
sleep 2

focus() { xdotool windowfocus --sync "$1"; sleep 0.05; }
hold()  { focus "$1"; xdotool keydown "$2"; sleep "$3"; xdotool keyup "$2"; }
tap()   { hold "$1" "$2" 0.05; }
combo() { tap "$1" j; sleep 0.07; tap "$1" j; sleep 0.07; tap "$1" k; }

# Records one scene: starts both players, runs the scene function, stops.
record_scene() {
  local scene=$1 out=$2; shift 2
  pkill -x netfight 2>/dev/null; sleep 0.5
  "$BIN" --player 0 --port 7000 --peer 127.0.0.1:7001 "$@" >/dev/null 2>&1 &
  local p1=$!; sleep 0.3
  "$BIN" --player 1 --port 7001 --peer 127.0.0.1:7000 "$@" >/dev/null 2>&1 &
  local p2=$!
  W1=$(xdotool search --sync --onlyvisible --pid $p1 | tail -1)
  W2=$(xdotool search --sync --onlyvisible --pid $p2 | tail -1)
  xdotool windowmove "$W1" 0 0; xdotool windowmove "$W2" 1280 0
  sleep 0.5
  ffmpeg -loglevel error -y -f x11grab -draw_mouse 0 -framerate 30 -video_size 2560x720 \
    -i $DISP -c:v libx264 -preset ultrafast -crf 18 "$out" &
  local rec=$!
  sleep 1
  "$scene"
  kill -INT $rec; wait $rec 2>/dev/null
  pkill -x netfight 2>/dev/null
}

scene_fight() {
  sleep 2
  hold $W1 d 1.6                 # P1 walks in
  hold $W2 a 0.5                 # P2 steps forward
  sleep 0.8
  tap $W1 j; sleep 2.1           # single light attack
  tap $W1 F2; tap $W2 F2         # hitboxes on in both windows
  sleep 1.0
  combo $W1                      # light > light > heavy
  sleep 2.5
  hold $W1 d 1.2
  tap $W2 j; sleep 1.5           # P2 pokes back
  # Block: hold "back" in P2's window WITHOUT focusing it. A focus change makes
  # a window release all its keys, so P2 must keep holding while P1 attacks.
  focus $W1
  xdotool keydown --window "$W2" d; sleep 0.3
  xdotool keydown k; sleep 0.05; xdotool keyup k
  sleep 1.2
  xdotool keyup --window "$W2" d
  sleep 1
  hold $W1 w 0.1; sleep 1.2      # jump
  hold $W1 d 0.8
  combo $W1
  sleep 2.5
}

scene_badnet() {
  sleep 3
  hold $W1 d 1.8
  hold $W2 a 0.4
  sleep 1
  combo $W1
  sleep 2.5
  hold $W2 a 1.0
  tap $W2 k; sleep 1.0
  hold $W1 w 0.1; sleep 1.5
  hold $W1 d 0.6
  combo $W1
  sleep 4
}

scene_desync() {
  sleep 2
  hold $W1 d 1.4
  tap $W1 j; sleep 1.5
  tap $W1 F9                     # corrupt P1's local state
  sleep 3
  hold $W1 d 0.6
  tap $W1 j; sleep 0.07; tap $W1 k
  sleep 4
}

echo "recording scenes..."
record_scene scene_fight  "$WORK/fight.mkv"
record_scene scene_badnet "$WORK/badnet.mkv" --lag 60 --jitter 20 --loss 10 --delay 6
record_scene scene_desync "$WORK/desync.mkv"

# Title and end cards (text files avoid escaping problems in drawtext).
card() {  # card out.mkv title line1 line2 line3
  local out=$1 i=0 y=(140 300 380 480) size=(96 44 44 40) color=(white 0xd0d6e0 0xd0d6e0 0x7fb3ff)
  local font filters=""
  shift
  for text in "$@"; do
    printf '%s' "$text" > "$WORK/card$i.txt"
    font=$FONT; [ $i -eq 0 ] && font=$FONTB
    filters+="${filters:+,}drawtext=expansion=none:fontfile=$font:textfile=$WORK/card$i.txt"
    filters+=":fontsize=${size[$i]}:fontcolor=${color[$i]}:x=(w-text_w)/2:y=${y[$i]}"
    i=$((i + 1))
  done
  ffmpeg -loglevel error -y -f lavfi -i color=c=0x1e2430:s=2560x720:r=30:d=5 -vf "$filters" \
    -c:v libx264 -preset ultrafast -crf 18 -pix_fmt yuv420p "$out"
}
card "$WORK/title.mkv" "netfight" \
  "Two-player fighting game with lockstep netcode over raw UDP (C++17)" \
  "github.com/0hJonny/cube-fighter-netcode"
card "$WORK/end.mkv" "netfight" \
  "Lockstep with input delay  |  ack-based resend  |  fixed-point determinism" \
  "RTT and jitter  |  desync detection by state hashes  |  peer restart handling" \
  "Tested in CI on Windows, macOS, Linux"

echo "joining..."
ffmpeg -loglevel error -y \
  -i "$WORK/title.mkv" -i "$WORK/fight.mkv" -i "$WORK/badnet.mkv" -i "$WORK/desync.mkv" -i "$WORK/end.mkv" \
  -filter_complex "[0:v]fps=30,format=yuv420p[a];[1:v]fps=30,format=yuv420p[b];[2:v]fps=30,format=yuv420p[c];[3:v]fps=30,format=yuv420p[d];[4:v]fps=30,format=yuv420p[e];[a][b][c][d][e]concat=n=5:v=1:a=0[v]" \
  -map "[v]" -c:v libx264 -preset fast -crf 18 "$WORK/raw.mkv"

# Captions: offsets are relative to each scene's start and were checked
# against the recorded frames (hit flash, blockstun, DESYNC text).
dur() { ffprobe -v error -show_entries format=duration -of csv=p=0 "$1"; }
A=5
B=$(echo "$A + $(dur "$WORK/fight.mkv")" | bc)
C=$(echo "$B + $(dur "$WORK/badnet.mkv")" | bc)
D=$(echo "$C + $(dur "$WORK/desync.mkv")" | bc)
cap() { printf '%s %s %s\n' "$(echo "$1" | bc)" "$(echo "$2" | bc)" "$3"; }
{
  cap "$A+0.3"  "$A+6.6"  "Two clients on one PC, P2P over UDP. Only inputs are sent - 1 byte per player per frame"
  cap "$A+6.6"  "$A+12.6" "Deterministic fixed-point simulation. F2 shows hitboxes: light > light > heavy combo"
  cap "$A+12.6" "$A+17.4" "Holding back blocks the heavy attack: blockstun, no damage"
  cap "$A+17.4" "$B-0.2"  "Both windows stay in sync: state hashes are compared every 30 frames"
  cap "$B+0.2"  "$B+10"   "Simulated bad network: 60 ms lag, 0-20 ms jitter, 10% packet loss, input delay 6 frames"
  cap "$B+10"   "$C-0.2"  "Lost packets are covered by resending unacknowledged inputs - still in sync"
  cap "$C+0.2"  "$C+4.4"  "Normal network again, in sync"
  cap "$C+4.4"  "$C+6.4"  "F9: P1 corrupts its own local state behind the protocol's back"
  cap "$C+6.4"  "$D-0.2"  "The next hash checkpoint disagrees: desync detected by both peers"
} > "$WORK/captions.txt"

echo "captioning..."
tools/add_captions.sh "$WORK/raw.mkv" "$WORK/captions.txt" "$OUT" >/dev/null 2>&1
ls -lh "$OUT"
