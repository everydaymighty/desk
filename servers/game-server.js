// game-server.js — UDP relay for the 1v1 arena (fast position/aim/fire/hit sync).
//   node game-server.js   (listens on UDP 8082)
//
// Each player sends a small text packet ~20x/sec describing their state:
//   "S|name|x|y|z|yaw|pitch|hp|score"
// and fire/hit events:
//   "F|shooter|targetName"   (a hit was registered locally and reported)
//
// The server just forwards every packet to all OTHER known peers. The clients
// do the actual game logic (movement, hit detection). This is a "relay" model:
// simple, good enough for 2 players + spectators, not cheat-proof.
//
// LIMITS: UDP — works on localhost/LAN; will NOT traverse free ngrok.

const dgram = require("dgram");
const server = dgram.createSocket("udp4");

const PORT = 8082;
const TIMEOUT_MS = 8000;
const peers = new Map(); // "ip:port" -> { addr, port, last }

server.on("message", (msg, rinfo) => {
  const key = rinfo.address + ":" + rinfo.port;
  peers.set(key, { addr: rinfo.address, port: rinfo.port, last: Date.now() });

  const now = Date.now();
  for (const [k, p] of peers) {
    if (now - p.last > TIMEOUT_MS) { peers.delete(k); continue; }
    if (k === key) continue;            // don't echo to sender
    server.send(msg, p.port, p.addr);
  }
});

server.on("listening", () => {
  console.log(`Game relay listening on UDP ${server.address().port}`);
  console.log("Works on localhost / same-LAN (UDP). Not over free ngrok.");
});

server.bind(PORT);
