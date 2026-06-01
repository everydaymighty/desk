// auth-server.js — dedicated account/login service, separate from the lobby.
// Run it:  node auth-server.js   (listens on port 8090)
//
// Design goals (friends-grade, but done right):
//  - Passwords arrive in the POST BODY, never in the URL (so they're not logged).
//  - Stored as scrypt hash + per-user random salt. Raw passwords are never saved.
//  - Basic rate limiting to slow password guessing.
//  - On success, issues a signed TOKEN. The main app uses the token; it never
//    stores or forwards the password to the lobby server.
//
// IMPORTANT: real confidentiality requires HTTPS. Run this behind ngrok
// (https://...) so the POST body is encrypted in transit. Over plain http on a
// hostile network, the password is exposed regardless of this code.

const http = require("http");
const fs = require("fs");
const crypto = require("crypto");

const PORT = 8090;
const USERS_FILE = "auth-users.json";

// A server secret used to sign tokens. Generated once and persisted so tokens
// survive restarts. Keep this file private — anyone with it can forge tokens.
const SECRET_FILE = "auth-secret.key";
let SECRET;
try { SECRET = fs.readFileSync(SECRET_FILE); }
catch (e) { SECRET = crypto.randomBytes(32); fs.writeFileSync(SECRET_FILE, SECRET); }

let users = {};
try { users = JSON.parse(fs.readFileSync(USERS_FILE, "utf8")); } catch (e) { users = {}; }
function saveUsers() { try { fs.writeFileSync(USERS_FILE, JSON.stringify(users)); } catch (e) {} }

function hashPw(pw, salt) { return crypto.scryptSync(pw, salt, 32).toString("hex"); }

// Tokens: base64("name.expiry").hmac — verifiable without storing sessions.
function makeToken(name) {
  const exp = Date.now() + 1000 * 60 * 60 * 12; // 12 hours
  const body = Buffer.from(name + "." + exp).toString("base64");
  const sig = crypto.createHmac("sha256", SECRET).update(body).digest("hex");
  return body + "." + sig;
}

// --- Rate limiting: max attempts per IP per window ---
const attempts = new Map(); // ip -> { count, resetAt }
const MAX_ATTEMPTS = 8, WINDOW_MS = 60 * 1000;
function rateLimited(ip) {
  const now = Date.now();
  let a = attempts.get(ip);
  if (!a || now > a.resetAt) { a = { count: 0, resetAt: now + WINDOW_MS }; attempts.set(ip, a); }
  a.count++;
  return a.count > MAX_ATTEMPTS;
}

function readBody(req, cb) {
  let data = "";
  req.on("data", (c) => { data += c; if (data.length > 4096) req.destroy(); });
  req.on("end", () => { try { cb(JSON.parse(data || "{}")); } catch (e) { cb(null); } });
}

const server = http.createServer((req, res) => {
  res.setHeader("Access-Control-Allow-Origin", "*");
  res.setHeader("Content-Type", "application/json");
  const ip = req.socket.remoteAddress || "?";

  if (req.method !== "POST") { res.writeHead(200); res.end('{"ok":false,"err":"use POST"}'); return; }

  if (rateLimited(ip)) {
    res.writeHead(429); res.end('{"ok":false,"err":"too many attempts, wait a minute"}'); return;
  }

  if (req.url === "/register" || req.url === "/login") {
    readBody(req, (b) => {
      if (!b || typeof b.name !== "string" || typeof b.pw !== "string") {
        res.writeHead(400); res.end('{"ok":false,"err":"bad request"}'); return;
      }
      const name = b.name.slice(0, 24).trim();
      const pw = b.pw;

      if (req.url === "/register") {
        if (!name || pw.length < 6) { res.end('{"ok":false,"err":"password must be 6+ chars"}'); return; }
        if (users[name]) { res.end('{"ok":false,"err":"username taken"}'); return; }
        const salt = crypto.randomBytes(16).toString("hex");
        users[name] = { salt, hash: hashPw(pw, salt) };
        saveUsers();
        console.log("registered:", name);
        res.end(JSON.stringify({ ok: true, token: makeToken(name) }));
      } else {
        const u = users[name];
        if (!u || hashPw(pw, u.salt) !== u.hash) {
          res.end('{"ok":false,"err":"wrong username or password"}'); return;
        }
        console.log("login:", name);
        res.end(JSON.stringify({ ok: true, token: makeToken(name) }));
      }
    });
    return;
  }

  res.writeHead(404); res.end('{"ok":false,"err":"unknown endpoint"}');
});

server.listen(PORT, () => {
  console.log(`Auth server listening on port ${PORT}`);
  console.log("Run it behind ngrok (https) so passwords are encrypted in transit:");
  console.log("  ngrok http " + PORT);
});
