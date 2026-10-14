@echo off
rem Two PCs on the same LAN. On PC 1:  netfight.exe --player 0 --port 7000 --peer IP_OF_PC2:7001
rem                          On PC 2:  netfight.exe --player 1 --port 7001 --peer IP_OF_PC1:7000
set /p PEER=Peer IP address: 
set /p ME=Your player number (0 or 1): 
if "%ME%"=="0" (start "" netfight.exe --player 0 --port 7000 --peer %PEER%:7001) else (start "" netfight.exe --player 1 --port 7001 --peer %PEER%:7000)
