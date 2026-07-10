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