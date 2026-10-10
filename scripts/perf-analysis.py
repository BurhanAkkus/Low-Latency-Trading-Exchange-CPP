#!/usr/bin/env python3
"""Latency report from the RDTSC / TTT lines written by common/perf-utils.h.

Log lines look like:
    HH:MM:SS.nnnnnnnnn RDTSC <tag> <cycles>      -- START_MEASURE / END_MEASURE
    HH:MM:SS.nnnnnnnnn TTT   <tag> <epoch nanos> -- TTT_MEASURE

RDTSC tags: time spent inside a code block, cycles converted to ns with the TSC frequency.
TTT hops:   for every event of the later tag, latency = its timestamp minus the most
            recent timestamp of the earlier tag. One request can produce several later
            events (e.g. one T3 read -> ACCEPTED + FILLs + market updates as T4/T4t), each
            is measured from the same T3.

Usage:
    python3 scripts/perf-analysis.py                        # exchange*.log in the current dir
    python3 scripts/perf-analysis.py path/to/*.log --tsc-ghz 2.4192
"""
import argparse
import glob
import re
import subprocess
import sys
from collections import defaultdict

# Hops on the exchange side, same naming as the book (Chapter 12).
# T1/T2 are recorded by the order server / FIFO sequencer, T5/T6 by the order server and
# market data publisher. Hops with no data are skipped.
HOPS = [
    ("T1_OrderServer_TCP_read", "T2_OrderServer_LFQueue_write"),
    ("T2_OrderServer_LFQueue_write", "T3_MatchingEngine_LFQueue_read"),
    ("T3_MatchingEngine_LFQueue_read", "T4_MatchingEngine_LFQueue_write"),
    ("T3_MatchingEngine_LFQueue_read", "T4t_MatchingEngine_LFQueue_write"),
    ("T4_MatchingEngine_LFQueue_write", "T5_MarketDataPublisher_LFQueue_read"),
    ("T4t_MatchingEngine_LFQueue_write", "T5t_OrderServer_LFQueue_read"),
    ("T5_MarketDataPublisher_LFQueue_read", "T6_MarketDataPublisher_UDP_write"),
    ("T5t_OrderServer_LFQueue_read", "T6t_OrderServer_TCP_write"),
    # end to end: TCP read -> first message out of the matching engine
    ("T1_OrderServer_TCP_read", "T4t_MatchingEngine_LFQueue_write"),
]

PERCENTILES = (50, 90, 99, 99.9)


def detect_tsc_ghz():
    """Read the TSC frequency the kernel calibrated at boot ("tsc: Detected 2419.200 MHz TSC")."""
    for cmd in (["journalctl", "-k", "-b", "--no-pager"], ["dmesg"]):
        try:
            out = subprocess.run(cmd, capture_output=True, text=True, timeout=10).stdout
        except (OSError, subprocess.SubprocessError):
            continue
        m = re.search(r"tsc: (?:Refined TSC clocksource calibration|Detected): ([\d.]+) MHz", out) \
            or re.search(r"tsc: Detected ([\d.]+) MHz TSC", out)
        if m:
            return float(m.group(1)) / 1000.0
    return None


def percentile(sorted_values, p):
    if not sorted_values:
        return float("nan")
    k = (len(sorted_values) - 1) * p / 100.0
    lo = int(k)
    hi = min(lo + 1, len(sorted_values) - 1)
    return sorted_values[lo] + (sorted_values[hi] - sorted_values[lo]) * (k - lo)


def summarize(values):
    v = sorted(values)
    return [len(v), sum(v) / len(v)] + [percentile(v, p) for p in PERCENTILES] + [v[-1]]


def print_table(title, rows):
    header = ["tag", "count", "mean"] + [f"p{p:g}" for p in PERCENTILES] + ["max"]
    print(f"\n{title} (nanoseconds)")
    width = max([len(header[0])] + [len(r[0]) for r in rows])
    print(f"{header[0]:<{width}}  " + "  ".join(f"{h:>10}" for h in header[1:]))
    for name, stats in rows:
        cells = [f"{stats[0]:>10d}"] + [f"{x:>10.0f}" for x in stats[1:]]
        print(f"{name:<{width}}  " + "  ".join(cells))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logs", nargs="*", help="log files (default: exchange*.log)")
    ap.add_argument("--tsc-ghz", type=float, help="TSC frequency in GHz (default: read from kernel log)")
    args = ap.parse_args()

    files = args.logs or sorted(glob.glob("exchange*.log"))
    if not files:
        sys.exit("no log files found")

    tsc_ghz = args.tsc_ghz or detect_tsc_ghz()
    if not tsc_ghz:
        sys.exit("could not detect TSC frequency, pass --tsc-ghz")

    rdtsc = defaultdict(list)  # tag -> [ns]
    ttt = defaultdict(list)    # tag -> [epoch ns]
    for path in files:
        with open(path, errors="replace") as f:
            for line in f:
                tokens = line.split()
                if len(tokens) != 4 or tokens[1] not in ("RDTSC", "TTT"):
                    continue
                try:
                    value = int(tokens[3])
                except ValueError:
                    continue
                if tokens[1] == "RDTSC":
                    rdtsc[tokens[2]].append(value / tsc_ghz)
                else:
                    ttt[tokens[2]].append(value)

    print(f"files: {', '.join(files)}")
    print(f"TSC: {tsc_ghz:.4f} GHz")

    if rdtsc:
        print_table("Code blocks (RDTSC)", [(tag, summarize(v)) for tag, v in sorted(rdtsc.items())])
    else:
        print("\nno RDTSC lines found")

    hop_rows = []
    for tag_p, tag_n in HOPS:
        if not ttt.get(tag_p) or not ttt.get(tag_n):
            continue
        events = sorted([(t, 0) for t in ttt[tag_p]] + [(t, 1) for t in ttt[tag_n]])
        last_p = None
        latencies = []
        for t, is_n in events:
            if not is_n:
                last_p = t
            elif last_p is not None:
                latencies.append(t - last_p)
        if latencies:
            hop_rows.append((f"{tag_p} -> {tag_n}", summarize(latencies)))
    if hop_rows:
        print_table("Hops (TTT)", hop_rows)
    else:
        print("\nno TTT hops found")


if __name__ == "__main__":
    main()
