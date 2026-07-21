#!/usr/bin/env bash
# smoke.sh - integration test: run the exe and probe the HTTP server.
#
# On real Windows (Git-Bash) the exe runs natively. Elsewhere it runs under
# Wine. Override with RUNNER=... (RUNNER="" forces native).
#
# Usage: tests/smoke.sh [exe] [port]
# Exit 0 if all checks pass, non-zero on the first failure.
set -u

EXE="${1:-dist/litewin_exporter_amd64.exe}"
PORT="${2:-9183}"
BASE="http://127.0.0.1:${PORT}"

if [ -z "${RUNNER+set}" ]; then
    command -v wine >/dev/null 2>&1 && RUNNER="wine" || RUNNER=""
fi

# Launch the exe with MSYS path-conversion disabled *for this command only*, so
# args like `--telemetry.path /probe` reach a native Windows exe unmangled.
# Scoped here (not global) so curl's `-o /dev/null` keeps working.
run_exe() { MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' $RUNNER "$EXE" "$@"; }
export WINEDEBUG="${WINEDEBUG:--all}"
export WINEPREFIX="${WINEPREFIX:-/tmp/wineprefix}"

fail() { echo "SMOKE FAIL: $*" >&2; cleanup; exit 1; }
cleanup() {
    [ -n "${PID:-}" ] && kill "$PID" 2>/dev/null
    # MSYS `kill` can't stop a native Windows exe; taskkill can. Without this the
    # exe lingers, holds the ssh channel open, and the remote invocation hangs.
    command -v taskkill >/dev/null 2>&1 && taskkill //F //IM "$(basename "$EXE")" >/dev/null 2>&1
    command -v wineserver >/dev/null 2>&1 && wineserver -k 2>/dev/null
    true
}
trap cleanup EXIT

[ -f "$EXE" ] || fail "exe not found: $EXE"

echo "Starting $EXE on :$PORT (${RUNNER:-native})..."
run_exe --web.listen-address ":$PORT" --telemetry.path /probe >/tmp/smoke_exporter.log 2>&1 </dev/null &
PID=$!

# Wait until the server answers (up to ~30s), rather than a fixed sleep.
ready=0
for _ in $(seq 1 30); do
    if curl -s -m 2 -o /dev/null "$BASE/"; then ready=1; break; fi
    sleep 1
done
[ "$ready" = 1 ] || fail "server did not become ready (see /tmp/smoke_exporter.log)"

# 1. landing page returns 200
code=$(curl -s -o /dev/null -w '%{http_code}' -m 5 "$BASE/")
[ "$code" = 200 ] || fail "GET / expected 200, got $code"
echo "  ok  GET /            -> 200"

# 2. unknown path returns 404
code=$(curl -s -o /dev/null -w '%{http_code}' -m 5 "$BASE/does-not-exist")
[ "$code" = 404 ] || fail "GET /does-not-exist expected 404, got $code"
echo "  ok  GET /unknown     -> 404"

# 3. server is stable across repeated requests (connection-per-request)
for i in $(seq 1 20); do
    code=$(curl -s -o /dev/null -w '%{http_code}' -m 5 "$BASE/")
    [ "$code" = 200 ] || fail "request $i expected 200, got $code"
done
echo "  ok  20x GET /        -> all 200"

# 3b. metrics endpoint (on the custom path) serves the meta metric
curl -s -m 5 "$BASE/probe" | grep -q '^process_start_time_seconds [0-9]' \
    || fail "GET /probe did not expose process_start_time_seconds"
echo "  ok  GET /probe       -> process_start_time_seconds"

# 3c. memory collector (enabled by default) emits its family with a value > 0
curl -s -m 5 "$BASE/probe" | grep -Eq '^windows_memory_physical_total_bytes [1-9][0-9]*$' \
    || fail "GET /probe did not expose a positive windows_memory_physical_total_bytes"
echo "  ok  GET /probe       -> windows_memory_physical_total_bytes"

# 3d. native only: PDH-backed collectors must report success on real Windows.
# Wine can't drive PDH, so skip this assertion unless running natively (RUNNER="").
if [ -z "$RUNNER" ]; then
    metrics=$(curl -s -m 5 "$BASE/probe")
    for col in cpu logical_disk net system; do
        echo "$metrics" | grep -q "^windows_exporter_collector_success{collector=\"$col\"} 1$" \
            || fail "collector_success{$col} != 1 (see /tmp/smoke_exporter.log)"
    done
    echo "  ok  collector_success{cpu,logical_disk,net,system} = 1"
fi

# 4. landing page reflects --telemetry.path
curl -s -m 5 "$BASE/" | grep -q 'href="/probe"' \
    || fail "landing page does not reflect --telemetry.path /probe"
echo "  ok  --telemetry.path reflected on landing page"

# 5. --version prints the name and exits 0
run_exe --version 2>/dev/null | grep -q litewin_exporter \
    || fail "--version did not print the exporter name"
echo "  ok  --version"

# 6. bad flag exits 2
run_exe --bogus >/dev/null 2>&1
rc=$?
[ "$rc" = 2 ] || fail "--bogus expected exit 2, got $rc"
echo "  ok  --bogus          -> exit 2"

# 7. unknown collector exits 2 and points at --collectors.print
run_exe --collectors.enabled bogus >/tmp/smoke_col.log 2>&1
rc=$?
[ "$rc" = 2 ] || fail "--collectors.enabled bogus expected exit 2, got $rc"
grep -q -- '--collectors.print' /tmp/smoke_col.log \
    || fail "unknown-collector error did not point at --collectors.print"
echo "  ok  bad collector    -> exit 2"

# 8. --collectors.print exits 0
run_exe --collectors.print >/dev/null 2>&1
rc=$?
[ "$rc" = 0 ] || fail "--collectors.print expected exit 0, got $rc"
echo "  ok  --collectors.print -> exit 0"

# Optional: save a full scrape for the contract fixture (SAVE_SCRAPE=path).
if [ -n "${SAVE_SCRAPE:-}" ]; then
    curl -s -m 5 "$BASE/probe" > "$SAVE_SCRAPE" \
        || fail "failed to save scrape to $SAVE_SCRAPE"
    echo "  ok  saved scrape       -> $SAVE_SCRAPE"
fi

echo "SMOKE PASS"
cleanup
trap - EXIT
exit 0
