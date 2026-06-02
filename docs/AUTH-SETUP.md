# Accounts & login — how it works and how to run it

Accounts are handled by the **same lobby server** (`servers/server.js`, port
8080) as everything else — there is no separate auth server anymore, and the app
never stores passwords on disk.

## What makes this reasonably safe
- The password is sent in the POST **body**, never in the URL. The client writes
  it to a short-lived temp file and hands it to `curl --data-urlencode "pw@FILE"`,
  so the password also never appears in the process argument list, then deletes
  the file. (See `AuthRequest` in `main.c`.)
- Stored only as a scrypt hash + random per-user salt in `auth-users.json`. Raw
  passwords are never written anywhere.
- Usernames are restricted to `[A-Za-z0-9_-]` (max 24 chars) on both the client
  input and the server's `/register`, so names are safe to embed in URLs.

## The ONE thing you must do for real safety: HTTPS
Code can't encrypt the connection by itself. Run the lobby server behind ngrok:
```
node servers/server.js          # listens on 8080 (login + lobby + game)
ngrok http 8080                 # gives https://XXXX.ngrok-free.app
```
Then put that https address in every computer's `desk_server.txt` (next to the
exe):
```
https://XXXX.ngrok-free.app
```
Over plain http on a shared network, the password is exposed regardless of code.

> Note: `desk_auth.txt` is no longer read — auth was merged into the main server.
> You only need to set `desk_server.txt`.

## Files this creates (KEEP PRIVATE — already in .gitignore)
- `auth-users.json` — account database (hashes, not passwords).

## Honest limits
"Good for friends," not bank-grade. No email verification, reset, 2FA, or rate
limiting, and it runs on your PC. Tell users: use a throwaway password.
