#!/bin/sh
# Opens BOTH players on this computer, connected over UDP (localhost).
# Click a window to control it: A/D move, W jump, J light, K heavy.
cd "$(dirname "$0")"
./netfight --player 0 --port 7000 --peer 127.0.0.1:7001 &
./netfight --player 1 --port 7001 --peer 127.0.0.1:7000 &
wait
