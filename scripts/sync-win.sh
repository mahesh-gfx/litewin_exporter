#!/usr/bin/env bash
# sync-win.sh - cross-build on this Mac, push the tree to the Windows dev box,
# and run the smoke test there against the real (native) exe.
#
# The loop: edit -> `make` (mingw cross-compile) -> rsync to Windows -> run
# tests/smoke.sh natively on Windows (no Wine). Unit tests stay on the Mac; the
# Windows box exists to exercise the real Win32 APIs.
#
#   scripts/sync-win.sh          # watch + sync + test on every change
#   scripts/sync-win.sh --once   # one build/sync/test cycle, then exit
#   scripts/sync-win.sh serve    # build/sync, then run live on Windows (browse it there)
#
# Override via env:
#   REMOTE  ssh target        (default litewin-win-dev, an alias in ~/.ssh/config)
#   RPATH   remote dir, git-bash form  (default /c/Users/dev/Documents/Projects/litewin_exporter)
#   PORT    smoke test port   (default 9183)
#   BUILD   0 to skip `make`  (default 1)
set -eu

REMOTE="${REMOTE:-litewin-win-dev}"
RPATH="${RPATH:-/c/Users/dev/Documents/Projects/litewin_exporter}"
PORT="${PORT:-9183}"
BUILD="${BUILD:-1}"

cd "$(dirname "$0")/.."

sync() {
    ssh "$REMOTE" "mkdir -p '$RPATH'"
    # tar over ssh: needs only tar+ssh on Windows (Git-Bash has no rsync).
    # dist/ is intentionally included (the exes the Windows box runs); .git and
    # native build artifacts are not.
    tar czf - --exclude .git --exclude build --exclude '*.o' . \
        | ssh "$REMOTE" "tar xzf - -C '$RPATH'"
}

remote_smoke() {
    # RUNNER="" -> run the exe natively (not under Wine).
    ssh "$REMOTE" "cd '$RPATH' && RUNNER='' bash tests/smoke.sh dist/litewin_exporter_amd64.exe $PORT"
}

cycle() {
    [ "$BUILD" = 1 ] && { echo ">> make (cross-compile)"; make; }
    echo ">> sync -> $REMOTE:$RPATH"; sync
    echo ">> smoke on Windows"; remote_smoke
    echo ">> ok"
}

# Sync, then run the exporter live on the Windows box. Browse it there at
# http://localhost:$PORT/metrics. Ctrl-C stops it and kills the remote exe.
serve() {
    [ "$BUILD" = 1 ] && { echo ">> make (cross-compile)"; make; }
    echo ">> sync -> $REMOTE:$RPATH"; sync
    echo ">> running on Windows — browse http://localhost:$PORT/metrics ON the Windows box  (Ctrl-C to stop)"
    local sshpid=
    # background ssh + wait so the trap can kill the child on any signal (a
    # foreground child survives a parent-only kill and orphans the remote exe).
    trap 'kill "$sshpid" 2>/dev/null; ssh "$REMOTE" "taskkill //F //IM litewin_exporter_amd64.exe" >/dev/null 2>&1; echo; echo ">> stopped"' INT TERM EXIT
    ssh "$REMOTE" "cd '$RPATH' && MSYS_NO_PATHCONV=1 ./dist/litewin_exporter_amd64.exe --web.listen-address :$PORT" &
    sshpid=$!
    wait "$sshpid"
}

# Fail fast if the box is unreachable, with a clear hint.
ssh -o BatchMode=yes -o ConnectTimeout=5 "$REMOTE" true 2>/dev/null \
    || { echo "cannot ssh to $REMOTE — is OpenSSH Server running and the key authorized?" >&2; exit 1; }

case "${1:-}" in
    serve)  serve ;;
    --once) cycle ;;
    "")     cycle
            echo ">> watching src tests Makefile* for changes (Ctrl-C to stop)"
            fswatch -o src tests Makefile Makefile.test | while read -r _; do
                cycle || echo ">> cycle failed, still watching"
            done ;;
    *)      echo "usage: $0 [--once | serve]" >&2; exit 2 ;;
esac
