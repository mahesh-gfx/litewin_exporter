#!/usr/bin/env bash
# loadtest.sh - ramp concurrent scrapers against a metrics endpoint until it
# degrades, and report the breaking point.
#
# Simulates N Prometheus instances scraping in a tight loop (worst case: a
# real scraper hits every 15-60s; here every worker scrapes back-to-back).
#
#   tests/loadtest.sh URL [step-seconds] [max-concurrency]
#   tests/loadtest.sh http://192.168.0.223:9183/metrics 10 50
#
# Ramp: 1 2 5 10 20 50 ... concurrent workers, step-seconds each. A step
# fails the endpoint when error rate exceeds 5% or p95 latency exceeds 5s
# (Prometheus default scrape_timeout is 10s); the ramp stops there.
# Needs only bash + curl + awk. Runs from any host that can reach the URL.
set -u

URL="${1:?usage: loadtest.sh URL [step-seconds] [max-concurrency]}"
STEP="${2:-10}"
MAXC="${3:-50}"
OUT="$(mktemp -d "${TMPDIR:-/tmp}/litewin-load.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT

# One worker: scrape in a loop for STEP seconds, log "http_code time_total".
worker() {
    local f="$1" end=$((SECONDS + STEP))
    while [ $SECONDS -lt $end ]; do
        curl -s -o /dev/null -m 10 -w '%{http_code} %{time_total}\n' "$URL" \
            >> "$f" 2>/dev/null || echo "000 10.0" >> "$f"
    done
}

echo "target: $URL   step: ${STEP}s   ramp: 1..$MAXC workers"
printf '%8s %8s %8s %8s %10s %10s %10s\n' workers scrapes ok errors p50 p95 max

for C in 1 2 5 10 20 50 100 200; do
    [ "$C" -gt "$MAXC" ] && break
    rm -f "$OUT"/w*
    for i in $(seq 1 "$C"); do worker "$OUT/w$i" & done
    wait

    # Aggregate: error = non-200 (includes connect refused/timeout as 000).
    stats=$(cat "$OUT"/w* | awk '
        function pidx(q, n,  i) { i = int(q * n + 0.5); return i < 1 ? 1 : (i > n ? n : i) }
        { n++; if ($1 == 200) { ok++; lat[ok] = $2 } else err++ }
        END {
            if (ok) {
                # insertion sort (portable awk, no gawk asort)
                for (i = 2; i <= ok; i++) {
                    v = lat[i]; j = i - 1
                    while (j && lat[j] > v) { lat[j+1] = lat[j]; j-- }
                    lat[j+1] = v
                }
                p50 = lat[pidx(0.50, ok)]
                p95 = lat[pidx(0.95, ok)]
                max = lat[ok]
            }
            printf "%d %d %d %.3f %.3f %.3f", n, ok, err+0, p50+0, p95+0, max+0
        }')
    read -r n ok err p50 p95 max <<EOF
$stats
EOF
    printf '%8d %8d %8d %8d %9ss %9ss %9ss\n' "$C" "$n" "$ok" "$err" "$p50" "$p95" "$max"

    # Breaking point: >5% errors or p95 > 5s.
    if [ "$n" -gt 0 ]; then
        broke=$(awk -v e="$err" -v n="$n" -v p="$p95" \
            'BEGIN { print (e/n > 0.05 || p > 5) ? 1 : 0 }')
        if [ "$broke" = 1 ]; then
            echo
            echo "BREAKING POINT: $C concurrent scrapers (errors: $err/$n, p95: ${p95}s)"
            exit 0
        fi
    fi
done

echo
echo "no breaking point reached up to $MAXC concurrent scrapers"
