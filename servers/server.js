// server.js — tiny lobby presence + chat server you run on YOUR PC.
// Plain HTTP (works great through ngrok and is easy for the C app to call).
//
// Run it:   node server.js     (listens on port 8080)
//
// Endpoints:
//   GET /hello?name=NAME   -> marks NAME online (call every ~2s as a heartbeat)
//   GET /online            -> plain text, one online name per line
//   GET /say?name=&msg=    -> post a chat line
//   GET /chat              -> recent chat lines
//   GET /bye?name=NAME     -> marks NAME offline immediately
//
// NOTE: accounts/login are handled by the SEPARATE auth-server.js. This server
// only does presence + chat.

const http = require("http");
const url = require("url");
const fs = require("fs");
const crypto = require("crypto");

const PORT = 8080;
const TIMEOUT_MS = 6000;       // drop users we haven't heard from in this long

// ---- Accounts (merged in so everything runs on ONE port / one ngrok tunnel) ----
const USERS_FILE = "users.json";
let users = {};
try { users = JSON.parse(fs.readFileSync(USERS_FILE, "utf8")); } catch (e) { users = {}; }
function saveUsers() { try { fs.writeFileSync(USERS_FILE, JSON.stringify(users)); } catch (e) {} }
function hashPw(pw, salt) { return crypto.scryptSync(pw, salt, 32).toString("hex"); }
// Names must be safe to embed in URLs/leaderboards: letters, digits, _ and -.
function validName(n) { return /^[A-Za-z0-9_-]{1,24}$/.test(n); }
// Read a urlencoded POST body (used for auth so the password stays out of the URL).
function readBody(req, cb) {
  let data = "";
  req.on("data", chunk => { data += chunk; if (data.length > 4096) req.destroy(); });
  req.on("end", () => cb(new URLSearchParams(data)));
  req.on("error", () => cb(new URLSearchParams("")));
}

const seen = new Map();        // name -> last heartbeat timestamp (ms)
const chat = [];               // recent chat lines: "name: message"
const gstates = {};            // name -> { st, last }  live game state (HTTP relay)
const ghits = {};              // name -> pending damage total

// Friends: { username: [friendName, ...] }, persisted to friends.json
const FRIENDS_FILE = "friends.json";
let friends = {};
try { friends = JSON.parse(fs.readFileSync(FRIENDS_FILE, "utf8")); } catch (e) { friends = {}; }
function saveFriends() { try { fs.writeFileSync(FRIENDS_FILE, JSON.stringify(friends)); } catch (e) {} }

// Leaderboard: { name: { w, l } }, persisted to leaderboard.json
const LB_FILE = "leaderboard.json";
let lb = {};
try { lb = JSON.parse(fs.readFileSync(LB_FILE, "utf8")); } catch (e) { lb = {}; }
function saveLb() { try { fs.writeFileSync(LB_FILE, JSON.stringify(lb)); } catch (e) {} }
function recordResult(winner, loser) {
  if (winner) { if (!lb[winner]) lb[winner] = { w:0, l:0 }; lb[winner].w++; }
  if (loser)  { if (!lb[loser])  lb[loser]  = { w:0, l:0 }; lb[loser].l++; }
  saveLb();
}

// ---- 1v1 match state (best of 3) ----
// Two player slots. A player claims a slot via /join; others spectate.
const PLAYER_TIMEOUT = 8000;   // free a slot if a player stops pinging
let match = {
  players: [null, null],       // { name, last } per slot
  scores:  [0, 0],             // rounds won
  round:   1,                  // 1..3
  winner:  null,               // name of match winner, or null
  lastScoreAt: 0               // ms timestamp of last accepted /score (dedup window)
};
const SCORE_DEDUP_MS = 2000;   // ignore repeat /score reports within this window

function pruneMatch() {
  const now = Date.now();
  for (let i = 0; i < 2; i++) {
    if (match.players[i] && now - match.players[i].last > PLAYER_TIMEOUT)
      match.players[i] = null;
  }
  // if a slot empties mid-match, reset the match
  if (match.winner === null && (!match.players[0] || !match.players[1])
      && (match.scores[0] || match.scores[1])) {
    if (!match.players[0] && !match.players[1]) {
      match.scores = [0,0]; match.round = 1; match.winner = null; match.lastScoreAt = 0;
    }
  }
}
function playerSlot(name) {
  for (let i=0;i<2;i++) if (match.players[i] && match.players[i].name === name) return i;
  return -1;
}

// A fake "bot" user that's always online, for testing the lobby with 2+ people.
// Set to "" to disable.
const TEST_BOT = "TestBot";

function onlineNames() {
  const now = Date.now();
  for (const [name, ts] of seen) if (now - ts > TIMEOUT_MS) seen.delete(name);
  const list = [...seen.keys()];
  if (TEST_BOT && !list.includes(TEST_BOT)) list.push(TEST_BOT);
  return list;
}

const server = http.createServer((req, res) => {
  const parsed = url.parse(req.url, true);
  const path = parsed.pathname;
  res.setHeader("Access-Control-Allow-Origin", "*");

  // ---- Account: register / login ----
  // Password comes in the POST body (never the URL). Name may be in the query
  // (it is not secret) or the body. Returns OK or ERR <reason>.
  if (path === "/register" || path === "/login") {
    readBody(req, (body) => {
      const name = (body.get("name") || parsed.query.name || "").toString().slice(0,24).trim();
      const pw   = (body.get("pw")   || "").toString();
      res.writeHead(200, { "Content-Type": "text/plain" });

      if (path === "/register") {
        if (!validName(name)) { res.end("ERR name: letters, digits, _ or - (max 24)\n"); return; }
        if (pw.length < 6)    { res.end("ERR password must be 6+ chars\n"); return; }
        if (users[name])      { res.end("ERR username taken\n"); return; }
        const salt = crypto.randomBytes(16).toString("hex");
        users[name] = { salt, hash: hashPw(pw, salt) };
        saveUsers();
        console.log("registered:", name);
        res.end("OK\n");
      } else { // /login
        const u = users[name];
        if (!u || hashPw(pw, u.salt) !== u.hash) { res.end("ERR wrong username or password\n"); return; }
        console.log("login:", name);
        res.end("OK\n");
      }
    });
    return;
  }

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

  // ---- Game: join a player slot (or become spectator if full) ----
  if (path === "/join") {
    pruneMatch();
    const name = (parsed.query.name || "").toString().slice(0,24).trim();
    res.writeHead(200, { "Content-Type": "text/plain" });
    if (!name) { res.end("ERR\n"); return; }
    let slot = playerSlot(name);
    if (slot < 0) {
      // claim first free slot
      for (let i=0;i<2;i++) if (!match.players[i]) { slot = i; match.players[i] = { name, last: Date.now() }; break; }
    } else {
      match.players[slot].last = Date.now();
    }
    res.end(slot >= 0 ? ("PLAYER " + slot + "\n") : "SPECTATOR\n");
    return;
  }

  // ---- Game: report a round win, advance best-of-3 ----
  if (path === "/score") {
    pruneMatch();
    const name = (parsed.query.name || "").toString().slice(0,24).trim();
    const slot = playerSlot(name);
    const now = Date.now();
    res.writeHead(200, { "Content-Type": "text/plain" });
    // Must be an active player and the match not already won. Reject reports that
    // arrive within SCORE_DEDUP_MS of the last accepted one: a single death often
    // gets reported by both clients almost simultaneously, and rounds always last
    // far longer than this window, so legitimate next-round scores still pass.
    if (slot < 0 || match.winner) { res.end("ERR\n"); return; }
    if (now - match.lastScoreAt < SCORE_DEDUP_MS) { res.end("ERR duplicate\n"); return; }
    match.lastScoreAt = now;
    match.scores[slot]++;
    if (match.scores[slot] >= 2) {
      match.winner = name;                                 // best of 3
      const otherSlot = slot === 0 ? 1 : 0;
      const loser = match.players[otherSlot] ? match.players[otherSlot].name : null;
      recordResult(name, loser);                           // update leaderboard
    } else match.round++;
    res.end("OK\n");
    return;
  }

  // Leaderboard: sorted by wins, "name W-L" per line
  if (path === "/leaderboard") {
    const rows = Object.entries(lb)
      .sort((a,b) => (b[1].w - a[1].w) || (a[1].l - b[1].l))
      .slice(0, 10)
      .map(([n,r]) => `${n} ${r.w}-${r.l}`);
    res.writeHead(200, { "Content-Type": "text/plain" });
    res.end(rows.join("\n") + "\n");
    return;
  }

  // ---- Game: reset the match ----
  if (path === "/resetmatch") {
    match.scores = [0,0]; match.round = 1; match.winner = null; match.lastScoreAt = 0;
    res.writeHead(200, { "Content-Type": "text/plain" });
    res.end("OK\n");
    return;
  }

  // ---- Game state relay over HTTP (works through ngrok; no UDP needed) ----
  // Each player POSTs/marks their state via /gset; others read all via /gget.
  // State string per player: "x|y|z|yaw|pitch|hp|score". Hits via /ghit.
  if (path === "/gset") {
    const name = (parsed.query.name || "").toString().slice(0,24).trim();
    const st   = (parsed.query.st   || "").toString().slice(0,120);
    if (name) {
      gstates[name] = { st, last: Date.now() };
      // also keep this player's match slot alive (worker calls /gset constantly)
      const sl = playerSlot(name);
      if (sl >= 0) match.players[sl].last = Date.now();
    }
    res.writeHead(200, { "Content-Type": "text/plain" }); res.end("ok\n");
    return;
  }
  if (path === "/gget") {
    const now = Date.now();
    const lines = [];
    for (const [n,v] of Object.entries(gstates)) {
      if (now - v.last > 5000) { delete gstates[n]; continue; }
      lines.push(n + "|" + v.st);
    }
    res.writeHead(200, { "Content-Type": "text/plain" });
    res.end(lines.join("\n") + "\n");
    return;
  }
  if (path === "/ghit") {
    const target = (parsed.query.target || "").toString().slice(0,24).trim();
    const dmg    = parseInt(parsed.query.dmg || "0", 10) || 0;
    if (target) { ghits[target] = (ghits[target] || 0) + dmg; }
    res.writeHead(200, { "Content-Type": "text/plain" }); res.end("ok\n");
    return;
  }
  if (path === "/gdmg") {
    const name = (parsed.query.name || "").toString().slice(0,24).trim();
    const d = ghits[name] || 0; ghits[name] = 0;
    res.writeHead(200, { "Content-Type": "text/plain" });
    res.end(d + "\n");
    return;
  }

  // ---- Game: current match state as JSON ----
  if (path === "/gamestate") {
    pruneMatch();
    res.writeHead(200, { "Content-Type": "application/json" });
    res.end(JSON.stringify({
      p0: match.players[0] ? match.players[0].name : "",
      p1: match.players[1] ? match.players[1].name : "",
      s0: match.scores[0], s1: match.scores[1],
      round: match.round, winner: match.winner || ""
    }) + "\n");
    return;
  }

  // Add a friend (mutual): /addfriend?name=ME&friend=THEM
  if (path === "/addfriend") {
    const me     = (parsed.query.name   || "").toString().slice(0,24).trim();
    const friend = (parsed.query.friend || "").toString().slice(0,24).trim();
    res.writeHead(200, { "Content-Type": "text/plain" });
    if (!me || !friend || me === friend) { res.end("ERR\n"); return; }
    if (!friends[me])     friends[me] = [];
    if (!friends[friend]) friends[friend] = [];
    if (!friends[me].includes(friend))     friends[me].push(friend);
    if (!friends[friend].includes(me))     friends[friend].push(me);   // mutual
    saveFriends();
    console.log("friend:", me, "<->", friend);
    res.end("OK\n");
    return;
  }

  // List a user's friends: /friends?name=ME  -> one per line
  if (path === "/friends") {
    const me = (parsed.query.name || "").toString().slice(0,24).trim();
    res.writeHead(200, { "Content-Type": "text/plain" });
    res.end((friends[me] || []).join("\n") + "\n");
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
