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