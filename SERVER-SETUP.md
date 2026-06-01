# Hosting the server (one port, one tunnel)

Accounts/login are now MERGED into the lobby server, so EVERYTHING runs on a
single port (8080) and needs only ONE ngrok tunnel. This removes the old
two-tunnel (8090) headache.

## Run order (on the HOST PC — that's you)
```
node servers/server.js          # lobby + chat + friends + game + ACCOUNTS (8080)
node servers/voice-server.js    # voice relay (UDP 8081, LAN only)
```
(You can ignore servers/auth-server.js now — login lives in server.js.)

## Expose to friends over the internet
```
ngrok http 8080
```
Copy the https URL it prints, e.g. https://abcd1234.ngrok-free.app

## What each computer's config needs (next to black.exe)
- desk_server.txt  -> your 8080 ngrok URL
- desk_auth.txt    -> the SAME 8080 ngrok URL (login is on the lobby server now)
- desk_voice.txt   -> 127.0.0.1 (voice is LAN-only; leave as is)

## Firm rules
- Your PC + `node servers/server.js` + `ngrok http 8080` must ALL stay running,
  and your PC must not sleep, or friends get stuck on "connecting...".
- The ngrok URL CHANGES every time you restart ngrok. If it changes, update
  desk_server.txt and desk_auth.txt (both to the new URL) and resend, or just
  send friends the new URL to paste.
- Everyone must run the SAME build of black.exe (old builds won't talk to new).
