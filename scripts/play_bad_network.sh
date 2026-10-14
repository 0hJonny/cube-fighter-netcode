#!/bin/sh
# Same as play_online_localhost.sh, but every packet is delayed 60 ms
# (+0..20 ms jitter) and 10% of packets are dropped, in both directions.
# Input delay 6 frames (100 ms) covers the ~70 ms one-way latency.
cd "$(dirname "$0")"
./netfight --player 0 --port 7000 --peer 127.0.0.1:7001 --lag 60 --jitter 20 --loss 10 --delay 6 &
./netfight --player 1 --port 7001 --peer 127.0.0.1:7000 --lag 60 --jitter 20 --loss 10 --delay 6 &
wait
