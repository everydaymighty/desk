=== Black — how to play ===

1. Keep all these files together in THIS folder:
   - black.exe
   - libraylib.dll
   - glfw3.dll
   - desk_server.txt   (already set to the host's address)
   - desk_auth.txt     (same address - login runs on the same server)
   - desk_voice.txt

2. Double-click black.exe.

3. First time: click "New here? Create an account", pick a username +
   password (6+ characters), then "Create account".
   Returning: just type your username + password and "Sign in".

4. Click "Enter Lobby", then "Play 1v1" to join a match.

Controls in the 1v1:
   WASD = move,  Mouse = look,  Left click = shoot,  Esc = back to lobby

Troubleshooting:
- "No response" / stuck connecting  -> the host's PC or server is off, OR the
  address in desk_server.txt / desk_auth.txt is outdated. Ask the host for the
  current address and paste it into BOTH .txt files (same URL in each).
- Both files should contain the SAME https://....ngrok-free.app address.
- Voice chat only works on the same local network, not over the internet.
- Movement may look a little steppy over the internet — that's expected.
