#!/usr/bin/env bash
# compare-exporters.sh - side-by-side comparison of litewin_exporter and
# windows_exporter running on the same host.
#
#   scripts/compare-exporters.sh LITEWIN_URL WINDOWS_EXPORTER_URL
#   scripts/compare-exporters.sh http://host:9183/metrics http://host:9182/metrics
#
# Compares: metric family coverage (shared / missing / extra), values of the
# shared families that should agree (memory, cpu count, disk sizes), payload
# size, series count, and scrape latency (p50 of 20 scrapes each).
#
# Run windows_exporter on the same box first, e.g.:
#   windows_exporter.exe --collectors.enabled cpu,logical_disk,memory,net,service,system
# Needs only bash + curl + awk + sort. Run from any host that reaches both.
set -eu

LW_URL="${1:?usage: compare-exporters.sh LITEWIN_URL WINDOWS_EXPORTER_URL}"
WE_URL="${2:?usage: compare-exporters.sh LITEWIN_URL WINDOWS_EXPORTER_URL}"
OUT="$(mktemp -d "${TMPDIR:-/tmp}/litewin-cmp.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT

curl -sf -m 15 "$LW_URL" > "$OUT/lw" || { echo "cannot scrape $LW_URL" >&2; exit 1; }
curl -sf -m 15 "$WE_URL" > "$OUT/we" || { echo "cannot scrape $WE_URL" >&2; exit 1; }

families() { awk '$1 == "#" && $2 == "TYPE" { print $3 }' "$1" | sort -u; }
families "$OUT/lw" > "$OUT/lw.fam"
families "$OUT/we" > "$OUT/we.fam"

echo "== metric families =="
echo "-- shared (both exporters):"
comm -12 "$OUT/lw.fam" "$OUT/we.fam" | sed 's/^/     /'
echo "-- windows_exporter only (coverage gap in litewin):"
comm -13 "$OUT/lw.fam" "$OUT/we.fam" | sed 's/^/     /'
echo "-- litewin only:"
comm -23 "$OUT/lw.fam" "$OUT/we.fam" | sed 's/^/     /'

echo
echo "== values that should agree (same host, same instant) =="
# Unlabelled or easily-matched gauges where both exporters read the same API.
for m in windows_memory_physical_total_bytes \
         windows_cpu_logical_processor \
         windows_system_processes \
         windows_system_threads; do
    lw=$(awk -v m="$m" '$1 == m { print $2; exit }' "$OUT/lw")
    we=$(awk -v m="$m" '$1 == m { print $2; exit }' "$OUT/we")
    printf '  %-45s litewin=%-16s windows_exporter=%s\n' "$m" "${lw:-n/a}" "${we:-n/a}"
done

echo
echo "== payload =="
for f in lw we; do
    name=$([ $f = lw ] && echo litewin || echo windows_exporter)
    bytes=$(wc -c < "$OUT/$f")
    series=$(grep -vc '^#' "$OUT/$f")
    printf '  %-18s %8s bytes  %6s series\n' "$name" "$bytes" "$series"
done

echo
echo "== scrape latency (20 sequential scrapes, seconds) =="
lat() {
    for _ in $(seq 1 20); do curl -s -o /dev/null -m 15 -w '%{time_total}\n' "$1"; done |
        sort -n | awk '{ a[NR] = $1 } END { printf "p50=%.3f p95=%.3f max=%.3f", a[int(NR*.5)+1], a[int(NR*.95)], a[NR] }'
}
printf '  %-18s %s\n' litewin "$(lat "$LW_URL")"
printf '  %-18s %s\n' windows_exporter "$(lat "$WE_URL")"

echo
echo "Footprint (run ON the Windows box, PowerShell):"
echo '  Get-Process litewin_exporter,windows_exporter | Select Name,WorkingSet64,CPU'
