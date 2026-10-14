@echo off
rem Opens BOTH players on this PC, connected over UDP (localhost).
rem Click a window to control it: A/D move, W jump, J light, K heavy.
start "" netfight.exe --player 0 --port 7000 --peer 127.0.0.1:7001
start "" netfight.exe --player 1 --port 7001 --peer 127.0.0.1:7000
