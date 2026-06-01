# Accounts & login — how it works and how to run it

Accounts are handled by a SEPARATE service (`servers/auth-server.js`) so the main
app never stores or forwards passwords. The app sends username+password to the
auth server, gets back a signed **token**, and uses that token afterward.

## What makes this reasonably safe
- Passwords are sent in the POST **body**, never in the URL.
- Stored only as a scrypt hash + random per-user salt in `auth-users.json`.
  Raw passwords are never written anywhere.
- Basic rate limiting (8 attempts/min/IP).
- A signed token (not the password) is what the app holds during a session.

## The ONE thing you must do for real safety: HTTPS
Code can't encrypt the connection by itself. Run the auth server behind ngrok:
```
node servers/auth-server.js     # listens on 8090
ngrok http 8090                 # gives https://XXXX.ngrok-free.app
```
Then put that https address in every computer's `desk_auth.txt` (next to the exe):
```
https://XXXX.ngrok-free.app
```
Over plain http on a shared network, the password is exposed regardless of code.

## Files this creates (KEEP PRIVATE — already in .gitignore)
- `auth-users.json` — account database (hashes, not passwords).
- `auth-secret.key` — token-signing secret. Never share or commit it.

## Honest limits
"Good for friends," not bank-grade. No email verification, reset, or 2FA, and it
runs on your PC. Tell users: use a throwaway password.
