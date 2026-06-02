# Update button — how it works & setup

Your app has a **Check for Update** button (bottom-right). When clicked it asks
GitHub for the newest release of `everydaymighty/desk` and compares it to the
version baked into the program (`APP_VERSION` in `main.c`).

- Same version  → "Up to date".
- Newer on GitHub → the app **downloads the new build itself and relaunches**.
- No internet / no releases yet → "Could not check".

## IMPORTANT: release asset file names must match
The in-app updater downloads a file named exactly:
- **Windows:** `black.exe`  + a checksum file `black.exe.sha256`
- **macOS:** `black`  + a checksum file `black.sha256`

Attach the built file under that exact name on the release, or the download fails.

## REQUIRED: publish a SHA-256 checksum next to the binary
Before installing, the updater downloads `<binary>.sha256` from the release,
hashes the file it just downloaded, and **only installs if the two match**. It
**fails closed** — if the checksum asset is missing, unreachable, or doesn't
match, the update is aborted and nothing is replaced. This stops a tampered or
corrupted download from being installed.

So every release MUST include the matching `.sha256` asset. Generate it like:

```
# Windows (PowerShell)
(Get-FileHash black.exe -Algorithm SHA256).Hash > black.exe.sha256

# macOS / Linux
shasum -a 256 black > black.sha256
```

The file just needs to contain the 64-character hex digest (a trailing filename
is fine — the updater extracts the first hash it finds). Attach it to the
release alongside the binary.

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
2. Generate its checksum: `(Get-FileHash black.exe -Algorithm SHA256).Hash > black.exe.sha256`.
3. GitHub → Releases → Draft a new release.
4. Tag `v0.1.0` (must match `APP_VERSION` in main.c exactly).
5. Attach BOTH `black.exe` and `black.exe.sha256` as release assets.
6. Publish.

## Shipping an update later
1. Bump `#define APP_VERSION` in main.c (e.g. `v0.2.0`).
2. Rebuild, regenerate the `.sha256`, commit, push.
3. Publish a NEW release tagged `v0.2.0` with the new `black.exe` AND its
   `black.exe.sha256` attached. (Without the checksum asset, clients refuse to
   update.)
