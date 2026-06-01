@echo off
REM Launches all three servers, each in its own window. Leave them open.
cd /d "%~dp0"
start "auth-server"  cmd /k node servers/auth-server.js
start "lobby-server" cmd /k node servers/server.js
start "voice-server" cmd /k node servers/voice-server.js
echo Three server windows opened (auth 8090, lobby 8080, voice 8081).
echo Close those windows to stop the servers.
