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

From the unzipped bundle, install as a service on the default port (9183):

```bat
install.bat
```

Pass a port to override the default:

```bat
install.bat 9100
```

Add `/defender` to also add a Windows Defender exclusion for the exe:

```bat
install.bat 9100 /defender
```

`install.bat` arch-detects, copies the right exe to
`%ProgramFiles%\litewin_exporter\litewin_exporter.exe`, creates the
`litewin_exporter` service with `start= auto`, opens the metrics port in the
firewall, and starts it. It is idempotent — re-running upgrades an existing
install in place.

`uninstall.bat` stops and deletes the service, then removes the firewall rule,
the Defender exclusion, and the install directory:

```bat
uninstall.bat
```

### Fleet install / upgrade — `install-litewin.ps1`

A PowerShell 2.0+ script (works on Windows 7 / Server 2008 R2 and later) that
downloads a versioned bundle from your fileserver, verifies its checksum, runs
`install.bat`, and scopes the firewall rule to your Prometheus scraper(s).
Upgrading the fleet is just bumping `-LitewinVersion`.

First install — replace `<scraper-ip>` with your Prometheus server's IP (e.g.
`10.20.30.50`); `-VerifyChecksum` checks the zip against its `.sha256` sidecar:

```powershell
powershell -ExecutionPolicy Bypass -File install-litewin.ps1 `
    -ScraperIPs <scraper-ip> -LitewinVersion 0.1.0 -VerifyChecksum
```

To upgrade, run the same command again and change only `-LitewinVersion`
to the new release (here `0.1.0` → `0.2.0`). The script compares this against
the installed version and, since it differs, downloads the new zip and
reinstalls the service:

```powershell
powershell -ExecutionPolicy Bypass -File install-litewin.ps1 `
    -ScraperIPs <scraper-ip> -LitewinVersion 0.2.0 -VerifyChecksum
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
sc create litewin_exporter binPath= "\"C:\Path\to\litewin_exporter.exe\" --web.listen-address :9183" start= auto
```

Then control it with the usual `sc` verbs:

```bat
sc start litewin_exporter
sc query litewin_exporter
sc stop litewin_exporter
sc delete litewin_exporter
```

Run the same exe without the SCM (from a console) and it serves in the
foreground instead — handy for a quick check: `litewin_exporter.exe --web.listen-address :9183`.

## Development

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

Build both exes (`dist/litewin_exporter_amd64.exe` and `_386.exe`):

```sh
make
```

Remove `dist/` and `build/`:

```sh
make clean
```

### Testing

Unit-test with **[Unity](https://github.com/ThrowTheSwitch/Unity)**, under
`tests/vendor/unity/`.

Run the native unit tests:

```sh
make -f Makefile.test unit
```

Validate a captured `/metrics` sample's format (the contract test):

```sh
make -f Makefile.test contract
```

Run both:

```sh
make -f Makefile.test test
```

Run the exe and probe it over HTTP — under Wine by default; on real Windows
(Git-Bash) it runs natively (`RUNNER=""`), which the sync workflow below uses:

```sh
bash tests/smoke.sh
```

### Load testing

`tests/loadtest.sh` simulates many Prometheus instances scraping at once and
finds the exporter's breaking point. It ramps 1 → 2 → 5 → 10 → 20 → 50 → 100 →
200 concurrent workers, each scraping back-to-back (far harsher than a real
scraper's 15–60 s interval), prints scrapes/errors/p50/p95/max per step, and
stops when a step exceeds 5% errors or 5 s p95 latency. It needs only bash,
curl, and awk, and runs from any host that can reach the exporter — for a
remote target, the metrics port must be open in the Windows firewall
(`install.bat` opens it).

Run against a target with the defaults (15 s per step, ramp up to 50 workers):

```sh
tests/loadtest.sh http://<host>:9183/metrics
```

Override the per-step duration (seconds) and maximum concurrency:

```sh
tests/loadtest.sh http://<host>:9183/metrics 15 200
```

For a side-by-side quality comparison against a windows_exporter running on
the same host (family coverage, values that must agree, payload size, scrape
latency):

```sh
scripts/compare-exporters.sh http://<host>:9183/metrics http://<host>:9182/metrics
```

### Continuous dev against a Windows box

For real-hardware testing, `scripts/sync-win.sh` cross-compiles on the dev host,
pushes the tree (rsync, or tar-over-ssh if the remote has no rsync) to a Windows
machine, and runs the **native** smoke test there — exercising the actual Win32
APIs (e.g. real `GlobalMemoryStatusEx` values), no Wine.

Run with no arguments to watch `src/`, `tests/`, and the Makefiles, rebuilding,
syncing, and smoke-testing on each save:

```sh
scripts/sync-win.sh
```

Do a single build/sync/smoke cycle, then exit:

```sh
scripts/sync-win.sh --once
```

Build and sync, then run the exporter live on the Windows box — browse
`http://localhost:<port>/metrics` there; Ctrl-C stops it and kills the remote
exe:

```sh
scripts/sync-win.sh serve
```

Override targets via env: `REMOTE` (ssh host,
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