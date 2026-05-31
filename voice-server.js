// voice-server.js — UDP voice relay. Run on YOUR PC alongside server.js.
//   node voice-server.js        (listens on UDP port 8081)
//
// How it works: each app sends small UDP audio packets here. The server
// remembers everyone it has heard from recently and forwards every incoming
// packet to all the OTHERS. No mixing, no codec — raw relay. Basic on purpose.
//
// LIMITS (firm): UDP voice works on localhost and same-LAN computers. It will
// NOT travel through ngrok's free tier (no UDP). Internet voice for remote
// people needs a real always-on server later.

const dgram = require("dgram");
const server = dgram.createSocket("udp4");

const PORT = 8081;
const TIMEOUT_MS = 8000;

// key "ip:port" -> { addr, port, last }
const peers = new Map();

server.on("message", (msg, rinfo) => {
  const key = rinfo.address + ":" + rinfo.port;
  peers.set(key, { addr: rinfo.address, port: rinfo.port, last: Date.now() });

  const now = Date.now();
  for (const [k, p] of peers) {
    if (now - p.last > TIMEOUT_MS) { peers.delete(k); continue; }
    if (k === key) continue;                 // don't echo back to sender
    server.send(msg, p.port, p.addr);
  }
});

server.on("listening", () => {
  const a = server.address();
  console.log(`Voice relay listening on UDP ${a.port}`);
  console.log("Works on localhost / same-LAN. (ngrok free does not carry UDP.)");
});

server.bind(PORT);
