#!/usr/bin/env python3
"""
Validate a Prometheus /metrics sample against the official exposition parser.

Catches malformed lines, HELP/TYPE inconsistencies and duplicate series - the
class of bug a C printf can introduce that a native unit test of the buffer
alone won't see. Run in CI against a scrape captured under Wine or from a real
Windows host.

Usage: validate_exposition.py <sample-file>
Exit 0 if valid, 1 otherwise. Requires: pip install prometheus_client
"""
import sys

def main(path):
    try:
        from prometheus_client.parser import text_string_to_metric_families
    except ImportError:
        print("prometheus_client not installed: pip install prometheus_client", file=sys.stderr)
        return 2

    with open(path, "r", encoding="utf-8") as fh:
        text = fh.read()

    try:
        families = list(text_string_to_metric_families(text))
    except Exception as exc:  # parser raises on malformed input
        print("INVALID exposition format: %s" % exc, file=sys.stderr)
        return 1

    samples = sum(len(f.samples) for f in families)
    if samples == 0:
        print("no samples parsed - empty or unrecognised output", file=sys.stderr)
        return 1

    # Sanity checks specific to this exporter's contract.
    names = {f.name for f in families}
    expected_any = {
        "windows_memory_physical_total_bytes",
        "windows_cpu_logical_processor",
        "process_start_time_seconds",
    }
    missing = expected_any - names
    if missing:
        print("WARNING: sample is missing expected families: %s" % ", ".join(sorted(missing)),
              file=sys.stderr)

    print("VALID: %d families, %d samples" % (len(families), samples))
    return 0

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
