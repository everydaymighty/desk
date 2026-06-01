# Update button — how it works & setup

Your app has a **Check for Update** button (bottom-right). When clicked it asks
GitHub for the newest release of `everydaymighty/desk` and compares it to the
version baked into the program (`APP_VERSION` in `main.c`).

- Same version  → "Up to date".
- Newer on GitHub → the app **downloads the new build itself and relaunches**.
- No internet / no releases yet → "Could not check".

## IMPORTANT: release asset file names must match
The in-app updater downloads a file named exactly:
- **Windows:** `black.exe`
- **macOS:** `black`

Attach the built file under that exact name on the release, or the download fails.

How it swaps itself: a running program can't overwrite its own file, so the app
downloads the new build next to itself (`black.new` / `black.new.exe`), then a
tiny helper waits ~2 seconds for the app to close, replaces the old file, and
relaunches the new one.

### macOS note
The downloaded binary isn't code-signed, so Gatekeeper may block it the first
time. Clear it once via System Settings → Privacy & Security → "Open Anyway",
or run `xattr -dr com.apple.quarantine black`.

## Publishing a release (one-time, then per update)
1. Build `black.exe` (see root README).
2. GitHub → Releases → Draft a new release.
3. Tag `v0.1.0` (must match `APP_VERSION` in main.c exactly).
4. Attach the built `black.exe` as a release asset.
5. Publish.

## Shipping an update later
1. Bump `#define APP_VERSION` in main.c (e.g. `v0.2.0`).
2. Rebuild, commit, push.
3. Publish a NEW release tagged `v0.2.0` with the new `black.exe` attached.
