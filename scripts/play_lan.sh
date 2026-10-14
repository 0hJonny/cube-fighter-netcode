#!/bin/sh
# Two computers on the same LAN.
#   usage: ./play_lan.sh <your player: 0|1> <peer IP>
cd "$(dirname "$0")"
if [ "$1" = "0" ]; then
  ./netfight --player 0 --port 7000 --peer "$2:7001"
else
  ./netfight --player 1 --port 7001 --peer "$2:7000"
fi
