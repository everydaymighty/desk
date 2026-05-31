# Update button — how it works & setup

Your app has a **Check for Update** button (bottom-right). When clicked it asks
GitHub for the newest release of `everydaymighty/desk` and compares it to the
version baked into the program (`APP_VERSION` in `main.c`).

- Same version  → "Up to date".
- Newer on GitHub → the app **downloads the new build itself and relaunches**.
  No browser, no website — it all happens inside the program.
- No internet / no releases yet → "Could not check".

## IMPORTANT: release asset file names must match
The in-app updater downloads a file named exactly:
- **Windows:** `black.exe`
- **macOS:** `black`

So when you publish a release you MUST attach the built file under that exact
name. If the asset is named anything else, the download will fail silently.

How it swaps itself: a running program can't overwrite its own file, so the app
downloads the new build next to itself (`black.new` / `black.new.exe`), then
launches a tiny helper that waits ~2 seconds for the app to close, replaces the
old file, and relaunches the new one.

### macOS note
The downloaded binary isn't code-signed, so Gatekeeper may block it the first
time ("cannot be opened"). The user clears it once via System Settings →
Privacy & Security → "Open Anyway", or by running `xattr -dr com.apple.quarantine black`.
After that, updates run cleanly.

## It needs a published release to work

Until you publish at least one release, the button will say "Could not check".
Do this once:

### 1. Push your code to the repo
```
git remote add origin https://github.com/everydaymighty/desk.git
git add .
git commit -m "initial"
git push -u origin main
```

### 2. Build the program, then publish a release
1. Build `black` / `black.exe` (see the platform README).
2. On GitHub: **Releases → Draft a new release**.
3. **Tag**: type `v0.1.0` (must match `APP_VERSION` in main.c exactly).
4. Attach the built `black.exe` (and/or the Mac `black`) as release assets.
5. **Publish release**.

Now the button will report "Up to date (v0.1.0)".

## Shipping an update later
1. In `main.c`, bump `#define APP_VERSION "v0.1.0"` to e.g. `"v0.2.0"`.
2. Rebuild, commit, push.
3. Publish a **new** release tagged `v0.2.0` with the new build attached.

Anyone running the old version clicks the button → it sees `v0.2.0` is newer →
opens the download page. (Having the button download & replace the .exe
automatically is a bigger feature we can add later.)

## Notes
- macOS uses `curl` (built in). Windows uses `PowerShell` (built in). No extra
  installs needed for the check itself.
- Tags must be consistent — always `vMAJOR.MINOR.PATCH` and always match
  `APP_VERSION`.
