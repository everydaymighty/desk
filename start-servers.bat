@echo off
REM Launches the servers, each in its own window. Leave them open to host.
cd /d "%~dp0"
start "lobby-server" cmd /k node servers/server.js
start "voice-server" cmd /k node servers/voice-server.js
echo Two server windows opened:
echo   lobby-server (8080) - login, chat, friends, leaderboard, game
echo   voice-server (8081) - voice relay (LAN only)
echo Close those windows to stop the servers.
echo.
echo For internet play, also run:  ngrok http 8080
