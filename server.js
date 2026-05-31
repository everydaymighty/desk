// server.js — tiny lobby presence server you run on YOUR PC.
// Plain HTTP (works great through ngrok and is easy for the C app to call).
//
// Run it:   node server.js     (listens on port 8080)
//
// Endpoints:
//   GET /hello?name=NAME   -> marks NAME online (call every ~2s as a heartbeat)
//   GET /online            -> plain text, one online name per line
//   GET /bye?name=NAME      -> marks NAME offline immediately (optional)
//   GET /                   -> health check
//
// A user is considered online if we've heard a /hello from them in the last
// 6 seconds. No database, no accounts yet — just presence.

const http = require("http");
const url = require("url");

const PORT = 8080;
const TIMEOUT_MS = 6000;       // drop users we haven't heard from in this long

const seen = new Map();        // name -> last heartbeat timestamp (ms)
const chat = [];               // recent chat lines: "name: message"

function onlineNames() {
  const now = Date.now();
  for (const [name, ts] of seen) if (now - ts > TIMEOUT_MS) seen.delete(name);
  return [...seen.keys()];
}

const server = http.createServer((req, res) => {
  const parsed = url.parse(req.url, true);
  const path = parsed.pathname;
  res.setHeader("Access-Control-Allow-Origin", "*");

  if (path === "/hello") {
    const name = (parsed.query.name || "").toString().slice(0, 24).trim();
    if (name) {
      seen.set(name, Date.now());
      console.log("hello:", name, "| online:", onlineNames().join(", "));
    }
    res.writeHead(200, { "Content-Type": "text/plain" });
    res.end("ok\n");
    return;
  }

  if (path === "/bye") {
    const name = (parsed.query.name || "").toString().slice(0, 24).trim();
    if (name) seen.delete(name);
    res.writeHead(200, { "Content-Type": "text/plain" });
    res.end("ok\n");
    return;
  }

  if (path === "/online") {
    res.writeHead(200, { "Content-Type": "text/plain" });
    res.end(onlineNames().join("\n") + "\n");
    return;
  }

  if (path === "/say") {
    const name = (parsed.query.name || "").toString().slice(0, 24).trim();
    const msg  = (parsed.query.msg  || "").toString().slice(0, 200).replace(/[\r\n]/g, " ").trim();
    if (name && msg) {
      chat.push(name + ": " + msg);
      while (chat.length > 30) chat.shift();   // keep last 30
      console.log("chat:", name + ": " + msg);
    }
    res.writeHead(200, { "Content-Type": "text/plain" });
    res.end("ok\n");
    return;
  }

  if (path === "/chat") {
    res.writeHead(200, { "Content-Type": "text/plain" });
    res.end(chat.join("\n") + "\n");
    return;
  }

  res.writeHead(200, { "Content-Type": "text/plain" });
  res.end("Lobby server is running.\n");
});

server.listen(PORT, () => {
  console.log(`Lobby server listening on port ${PORT}`);
  console.log(`Local test:  http://localhost:${PORT}/online`);
  console.log("Expose to others with:  ngrok http " + PORT);
});
