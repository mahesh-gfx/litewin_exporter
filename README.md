# litewin_exporter

Lightweight Windows exporter built using C for prometheus

## Pain

1. windows_exporter prometheus exporter no longer support Windows 7
  1. Older versions before the Go update are still supported, but...
    1. It is still resource hungry
    2. Not suitable for low spec machines which needs monitoring
    3. Not maintained anymore for supporting Windows 7
2. No suitable, lightweight drop-in replacement (yet).

## Goal

1. Create a lighteweight prometheus exporter which primarily supports Windows 7.
2. Takes less than 20mb RAM, and minimal CPU.
3. Uses APIs, solutions already baked into Windows
4. Drop-in replacement for windows_exporter
5. Configurable, just like windows_exporter

## Installation (Windows)

The release bundle (`litewin_exporter-vX.Y.Z.zip`) contains both architecture
exes and the installer scripts. Everything below must run **elevated** (an
Administrator `cmd`/PowerShell).

### Single host — `install.bat`

From the unzipped bundle:

```bat
install.bat                 :: install as a service on the default port (9182)
install.bat 9100            :: install on a custom port
install.bat 9100 /defender  :: also add a Windows Defender exclusion for the exe
```

`install.bat` arch-detects, copies the right exe to
`%ProgramFiles%\litewin_exporter\litewin_exporter.exe`, creates the
`litewin_exporter` service with `start= auto`, opens the metrics port in the
firewall, and starts it. It is idempotent — re-running upgrades an existing
install in place.

```bat
uninstall.bat               :: stop + delete service, remove firewall rule,
                            :: Defender exclusion, and the install directory
```

### Fleet install / upgrade — `install-litewin.ps1`

A PowerShell 2.0+ script (works on Windows 7 / Server 2008 R2 and later) that
downloads a versioned bundle from your fileserver, verifies its checksum, runs
`install.bat`, and scopes the firewall rule to your Prometheus scraper(s).
Upgrading the fleet is just bumping `-LitewinVersion`.

```powershell
# First install: allow scraper 10.20.30.50, verify the .sha256 sidecar
powershell -ExecutionPolicy Bypass -File install-litewin.ps1 `
    -ScraperIPs 10.20.30.50 -LitewinVersion 0.1.0 -VerifyChecksum

# Later, upgrade the whole fleet: identical command, bumped version
powershell -ExecutionPolicy Bypass -File install-litewin.ps1 `
    -ScraperIPs 10.20.30.50 -LitewinVersion 0.2.0 -VerifyChecksum
```

Set `-LitewinBaseUrl` (once, or edit its default at the top of the script) to
your release directory; the zip URL is `{BaseUrl}/litewin_exporter-v{Version}.zip`.
Re-running with an unchanged version is a no-op (use `-Force` to reinstall);
firewall rules are always reconciled. See the script's comment header for all
parameters (`-LitewinZipPath`, `-LitewinSha256`, `-AllowPing`, ...).

### Manual service control (`sc`)

If you'd rather not use the installers, the exe runs as a service whenever the
SCM launches it — no install flag needed. Create it yourself (note the required
space after each `=`):

```bat
sc create litewin_exporter binPath= "\"C:\Path\to\litewin_exporter.exe\" --web.listen-address :9182" start= auto
sc start   litewin_exporter
sc query   litewin_exporter
sc stop    litewin_exporter
sc delete  litewin_exporter
```

Run the same exe without the SCM (from a console) and it serves in the
foreground instead — handy for a quick check: `litewin_exporter.exe --web.listen-address :9182`.

## Development

Being rebuilt from the single-file reference (`exporter.c`)`.`

### Toolchain

MinGW-w64 GCC (both architectures), plus Wine to run the `.exe` on the build
host for smoke tests:


| Host             | Cross-compilers                                     | Wine (for `make smoke`)           |
| ---------------- | --------------------------------------------------- | --------------------------------- |
| macOS (Homebrew) | `brew install mingw-w64`                            | `brew install --cask wine-stable` |
| Debian/Ubuntu    | `apt install gcc-mingw-w64`                         | `apt install wine`                |
| Windows (MSYS2)  | `pacman -S mingw-w64-x86_64-gcc mingw-w64-i686-gcc` | n/a (runs natively)               |


The Makefile needs `x86_64-w64-mingw32-gcc` and `i686-w64-mingw32-gcc` on
`PATH` (on a non-standard Homebrew prefix like `~/homebrew`, put its `bin/` on
`PATH` first).

### Build

```sh
make          # dist/litewin_exporter_amd64.exe + _386.exe
make clean    # remove dist/
```

The exporter.c still compiles.

### Testing

Unit-test with **[Unity](https://github.com/ThrowTheSwitch/Unity)**, under `tests/vendor/unity/`.

```sh
make -f Makefile.test unit       # unit tests
make -f Makefile.test contract   # validate a captured /metrics sample's format
make -f Makefile.test test       # unit + contract
bash tests/smoke.sh              # run the exe under Wine and probe it
```

`smoke.sh` runs the exe under Wine by default; on real Windows (Git-Bash) it
runs natively (`RUNNER=""`), which the sync workflow below uses.

### Continuous dev against a Windows box

For real-hardware testing, `scripts/sync-win.sh` cross-compiles on the dev host,
pushes the tree (rsync, or tar-over-ssh if the remote has no rsync) to a Windows
machine, and runs the **native** smoke test there — exercising the actual Win32
APIs (e.g. real `GlobalMemoryStatusEx` values), no Wine.

```sh
scripts/sync-win.sh          # watch src/tests/Makefile*; build + sync + smoke on each save
scripts/sync-win.sh --once   # one build/sync/smoke cycle
scripts/sync-win.sh serve    # build/sync, then run the exporter live on Windows
```

With `serve`, browse `http://localhost:<port>/metrics` on the Windows box; Ctrl-C
stops it and kills the remote exe. Override targets via env: `REMOTE` (ssh host,
default `litewin-win-dev`), `RPATH` (remote dir, Git-Bash form), `PORT` (default
9183), `BUILD=0` to skip the cross-compile. See the script header for details.

**Prerequisites** on the Windows box: OpenSSH Server running with the dev host's
public key authorised (for an admin account, in
`C:\ProgramData\ssh\administrators_authorized_keys`), and Git for Windows
(supplies `bash`, `tar`, `curl`). Define the `litewin-win-dev` host in
`~/.ssh/config` (or set `REMOTE`).

> The HTTP server is single-threaded (connection-per-request). Accepted sockets
> carry recv/send timeouts, so a slow or half-open client trips the timeout and
> the accept loop continues rather than wedging.