# Running the lobby server on your PC (free)

This is the backbone that lets the 10 computers see each other. It runs on your
machine. While it's running and reachable, people are connected; when you close
it, the lobby goes offline.

## Step 1 — Install Node.js (one time)
Download the LTS installer from https://nodejs.org and install it (defaults are
fine). Check it works — in PowerShell:
```
node --version
```
You should see something like `v20.x.x`.

## Step 2 — Start the server
```
cd C:\Users\illum\Desktop\Projects\desk
node server.js
```
You should see:
```
Lobby server listening on port 8080
```
Leave this window OPEN. Closing it stops the lobby.

## Step 3 — Test on your own machine first
Your own PC reaches the server at `ws://localhost:8080`. Once the app's
networking is wired in (next coding step), running the app on this same PC
should show your username in the online list.

## Step 4 — Let OTHER computers connect (the tunnel)
Other people can't reach `localhost`. You expose your server to the internet
with a free tunnel. Easiest is **ngrok**:

1. Make a free account at https://ngrok.com and install ngrok.
2. Run:
   ```
   ngrok http 8080
   ```
3. It prints a public address like:
   ```
   Forwarding  https://abc123.ngrok-free.app -> http://localhost:8080
   ```
4. The WebSocket address others use is that host with `wss://`, e.g.
   `wss://abc123.ngrok-free.app`. You give this address to the app (we'll add a
   field for it) so the other 9 computers connect through the tunnel to your PC.

### Important free-tier facts (firm)
- The ngrok address **changes every time you restart ngrok** (on the free plan).
  Each new session = new address you have to re-share. A fixed address costs money.
- The lobby is only online while BOTH `node server.js` AND `ngrok` are running on
  your PC, and your PC is on and online.
- This is fine for testing with friends. For an always-on lobby you'd eventually
  move the server to a free always-on host (Oracle/Fly.io). Same server.js works there.

## What this server does (and doesn't) yet
- DOES: tracks who is connected and broadcasts the online list.
- NOT YET: accounts/passwords, friend lists, hosting rooms together. Those build
  on top of this once presence works.
