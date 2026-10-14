@echo off
rem Same as play_online_localhost.bat, but every packet is delayed 60 ms
rem (+0..20 ms jitter) and 10% of packets are dropped, in both directions.
rem Input delay 6 frames (100 ms) covers the ~70 ms one-way latency.
start "" netfight.exe --player 0 --port 7000 --peer 127.0.0.1:7001 --lag 60 --jitter 20 --loss 10 --delay 6
start "" netfight.exe --player 1 --port 7001 --peer 127.0.0.1:7000 --lag 60 --jitter 20 --loss 10 --delay 6
