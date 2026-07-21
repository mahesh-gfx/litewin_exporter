#!/usr/bin/env bash
# perf-report.sh - render the load-test + comparison results as a markdown
# report (tables + mermaid charts; GitHub step summaries render both).
#
#   scripts/perf-report.sh LITEWIN_LOAD_LOG WINEXP_LOAD_LOG COMPARE_LOG > report.md
#
# The load logs are `tests/loadtest.sh` output; the compare log is
# `scripts/compare-exporters.sh` output. Needs only bash + awk.
set -eu

LW="${1:?usage: perf-report.sh LITEWIN_LOAD_LOG WINEXP_LOAD_LOG COMPARE_LOG}"
WE="${2:?usage: perf-report.sh LITEWIN_LOAD_LOG WINEXP_LOAD_LOG COMPARE_LOG}"
CMP="${3:?usage: perf-report.sh LITEWIN_LOAD_LOG WINEXP_LOAD_LOG COMPARE_LOG}"

# Extract "workers scrapes ok errors p50 p95 max" data rows (strip 's' units).
rows() { awk 'NF == 7 && $1 ~ /^[0-9]+$/ { gsub(/s$/, "", $5); gsub(/s$/, "", $6); gsub(/s$/, "", $7); print }' "$1"; }
rows "$LW" > /tmp/perf_lw.$$
rows "$WE" > /tmp/perf_we.$$
trap 'rm -f /tmp/perf_lw.$$ /tmp/perf_we.$$' EXIT

col() { awk -v c="$2" '{ printf "%s%s", sep, $c; sep = ", " }' "$1"; }
# Per-worker throughput needs the step duration; scrapes/step-seconds.
thr() { awk -v d="$2" '{ printf "%s%.1f", sep, $2 / d; sep = ", " }' "$1"; }

STEP_S="${STEP_SECONDS:-10}"
LW_STEPS=$(wc -l < /tmp/perf_lw.$$)
WE_STEPS=$(wc -l < /tmp/perf_we.$$)
N=$((LW_STEPS < WE_STEPS ? LW_STEPS : WE_STEPS))

echo "# Exporter performance report"
echo
echo "_Tight-loop scrapers (every worker scrapes back-to-back) — far harsher"
echo "than real Prometheus instances on a 15–60 s interval. Numbers from a"
echo "shared CI runner: shapes and error counts are meaningful, absolute"
echo "latency is noisy._"
echo
echo "## Load ramp — side by side"
echo
echo "| workers | litewin errors | litewin p95 | windows_exporter errors | windows_exporter p95 |"
echo "|---:|---:|---:|---:|---:|"
# Join on step index; a missing row means that exporter's ramp stopped earlier.
awk 'NR == FNR { lw[FNR] = $1 "|" $4 "|" $6; lwn = FNR; next }
     { we[FNR] = $4 "|" $6; wen = FNR }
     END {
         n = lwn > wen ? lwn : wen
         for (i = 1; i <= n; i++) {
             split((i in lw) ? lw[i] : "—|—|—", a, "|")
             split((i in we) ? we[i] : "—|—", b, "|")
             printf "| %s | %s | %s | %s | %s |\n", a[1], a[2],
                    (a[3] == "—" ? "—" : a[3] " s"), b[1],
                    (b[2] == "—" ? "—" : b[2] " s")
         }
     }' /tmp/perf_lw.$$ /tmp/perf_we.$$
echo
grep -h "^BREAKING POINT\|^no breaking point" "$LW" | sed 's/^/- **litewin**: /'
grep -h "^BREAKING POINT\|^no breaking point" "$WE" | sed 's/^/- **windows_exporter**: /'
echo
# xychart-beta has no legend; pin the plot colours and show a matching key.
MERMAID_INIT="%%{init: {'theme': 'base', 'themeVariables': {'xyChart': {'plotColorPalette': '#2563eb, #f59e0b'}}}}%%"
LEGEND="🔵 **litewin**&nbsp;&nbsp;&nbsp;🟠 **windows_exporter**"

echo "## p95 latency vs concurrency"
echo
echo "$LEGEND"
echo
echo '```mermaid'
echo "$MERMAID_INIT"
echo 'xychart-beta'
echo '    title "p95 scrape latency (s)"'
echo "    x-axis \"concurrent scrapers\" [$(head -n "$N" /tmp/perf_lw.$$ | col /dev/stdin 1)]"
echo '    y-axis "seconds"'
echo "    line [$(head -n "$N" /tmp/perf_lw.$$ | col /dev/stdin 6)]"
echo "    line [$(head -n "$N" /tmp/perf_we.$$ | col /dev/stdin 6)]"
echo '```'
echo
echo "## Throughput vs concurrency"
echo
echo "$LEGEND"
echo
echo '```mermaid'
echo "$MERMAID_INIT"
echo 'xychart-beta'
echo '    title "successful scrapes per second"'
echo "    x-axis \"concurrent scrapers\" [$(head -n "$N" /tmp/perf_lw.$$ | col /dev/stdin 1)]"
echo '    y-axis "scrapes/s"'
echo "    line [$(head -n "$N" /tmp/perf_lw.$$ | thr /dev/stdin "$STEP_S")]"
echo "    line [$(head -n "$N" /tmp/perf_we.$$ | thr /dev/stdin "$STEP_S")]"
echo '```'
echo
echo "## Quality comparison"
echo
echo '```'
sed -n '/^== values that should agree/,$p' "$CMP"
echo '```'
echo
shared=$(sed -n '/-- shared/,/-- windows_exporter only/p' "$CMP" | grep -c '^     ' || true)
gap=$(sed -n '/-- windows_exporter only/,/-- litewin only/p' "$CMP" | grep -c '^     ' || true)
only=$(sed -n '/-- litewin only/,/^$/p' "$CMP" | grep -c '^     ' || true)
echo "Metric families: **$shared shared**, $gap windows_exporter-only (incl. Go runtime introspection), $only litewin-only."
echo
echo "<details><summary>Full family lists and raw load logs</summary>"
echo
echo '```'
cat "$CMP"
echo
echo "--- litewin load log ---"
cat "$LW"
echo
echo "--- windows_exporter load log ---"
cat "$WE"
echo '```'
echo
echo "</details>"
