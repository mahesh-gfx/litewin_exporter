# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.0] - Unreleased

First release: a lightweight, C-based Prometheus exporter for Windows (targeting
Windows 7 and up), rebuilt from the single-file `exporter.c` reference into a
modular `src/` tree over milestones M0–M14.

### Added

- **HTTP server** — single-threaded Winsock, HTTP/1.0, connection-per-request,
  serving `/metrics` (path configurable) plus a landing page. Recv/send socket
  timeouts so a slow or half-open client can't wedge the accept loop.
- **Configuration** — `--web.listen-address`, `--telemetry.path`,
  `--collectors.enabled` (with a `[defaults]` token), `--collectors.print`,
  `--version`, `--help`.
- **Collector framework** — registry, enable-set resolution, and a
  `windows_exporter_collector_success{collector="…"}` meta metric per scrape.
- **Collectors**:
  - `memory` — physical/virtual totals and availability via `GlobalMemoryStatusEx`.
  - `cpu`, `logical_disk`, `net`, `system` — PDH-backed counters.
  - `service` — Windows service states (one-hot per service).
  - `disk_health` — SMART failure-prediction per physical drive (requires elevation).
- **Windows service mode** — the exe auto-detects whether it was launched by the
  SCM and runs as a service, else in console mode. Version and application
  manifest compiled in as a Win32 resource.
- **Packaging** — `install.bat` / `uninstall.bat` (arch-detect, `sc create`
  auto-start service, firewall rule, optional Defender exclusion, idempotent) and
  `install-litewin.ps1`, a PowerShell 2.0–compatible fleet installer/upgrader
  with version-driven install, fileserver download + checksum verification, and a
  scraper-scoped firewall rule.
- **CI** — unit tests (Unity), cross-compiled build of both architectures under
  `-Werror`, a Wine integration smoke test, a native `windows-latest` smoke test
  exercising the PDH collectors, and a `/metrics` exposition-format contract check.
- **Release pipeline** — tag-triggered workflow that builds both architectures,
  generates `SHA256SUMS`, zips a bundle (exes + installers + README + checksums),
  and attaches it to the GitHub Release. Code signing via SignPath Foundation is
  wired but gated until configured.

[Unreleased]: https://github.com/maheshadhikari-gla/litewin_exporter/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/maheshadhikari-gla/litewin_exporter/releases/tag/v0.1.0
