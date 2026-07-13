#!/usr/bin/env bash
# smoke.sh - integration test: run the Windows exe under Wine and probe
# the HTTP server. 
#
# Usage: tests/smoke.sh [exe] [port]
# Exit 0 if all checks pass, non-zero on the first failure.
set -u

EXE="${1:-dist/litewin_exporter_amd64.exe}"
PORT="${2:-9182}"
BASE="http://127.0.0.1:${PORT}"

export WINEDEBUG="${WINEDEBUG:--all}"
export WINEPREFIX="${WINEPREFIX:-/tmp/wineprefix}"

fail() { echo "SMOKE FAIL: $*" >&2; cleanup; exit 1; }
cleanup() { [ -n "${PID:-}" ] && kill "$PID" 2>/dev/null; wineserver -k 2>/dev/null; true; }
trap cleanup EXIT

[ -f "$EXE" ] || fail "exe not found: $EXE"

echo "Starting $EXE on :$PORT under Wine..."
wine "$EXE" --web.listen-address ":$PORT" >/tmp/smoke_exporter.log 2>&1 &
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

echo "SMOKE PASS"
cleanup
trap - EXIT
exit 0
