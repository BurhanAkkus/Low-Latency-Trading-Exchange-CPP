#!/usr/bin/env python3
"""The exchange's part of each request, from the exchange benchmark's two trace points:

    C1  <request key> <tsc>   benchmark_trace.log: the client's write of the request into its
                              shared memory ring returned (send order)
    T6  <request key> <tsc>   the order server's log (TRACE in common/perf-utils.h): the request's
                              final response is in its client's ring

C1 -> T6 is everything the exchange does for a request: waiting for the order server's next poll,
its read, the request queue, the matching engine, the response queue and the write into the
client's ring. The benchmark's own report has the end to end latency, the client's sides included.
All times are TSC cycles, one clock for every core and process.

The trace points of the stages in between are commented out in the exchange, they cost the hot
path a log line each. The version of this script that split the path at them is in git history
(git log -- scripts/trace-analysis.py).

The first --warmup-pct of requests (in send order) are left out, as in the benchmark's own report.

Usage:
    python3 scripts/trace-analysis.py                 # logs in the current directory
    python3 scripts/trace-analysis.py /tmp/exchange-bench --tsc-ghz 2.4192
"""
import argparse
import glob
import importlib.util
import os
import sys

# The TSC frequency and percentiles from perf-analysis.py.
_spec = importlib.util.spec_from_file_location(
    "perf_analysis", os.path.join(os.path.dirname(os.path.abspath(__file__)), "perf-analysis.py"))
perf = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(perf)


def read_c1(path):
    """Request keys in send order and their C1."""
    keys, c1 = [], []
    with open(path) as f:
        for line in f:
            p = line.split()
            if len(p) == 3 and p[0] == "C1":
                keys.append(int(p[1]))
                c1.append(int(p[2]))
    return keys, c1


def read_t6(paths):
    """T6 by request key: the first one, a request has one final response."""
    t6 = {}
    for path in paths:
        with open(path, errors="replace") as f:
            for line in f:
                if not line.startswith("T6 "):
                    continue
                p = line.split()
                if len(p) != 3:
                    continue
                try:
                    t6.setdefault(int(p[1]), int(p[2]))
                except ValueError:
                    continue
    return t6


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dir", nargs="?", default=".", help="directory with exchange*.log and benchmark_trace.log")
    ap.add_argument("--tsc-ghz", type=float, help="TSC frequency in GHz (default: read from kernel log)")
    ap.add_argument("--warmup-pct", type=float, default=10, help="leave out the first N%% of requests (default 10)")
    args = ap.parse_args()

    bench = os.path.join(args.dir, "benchmark_trace.log")
    logs = sorted(glob.glob(os.path.join(args.dir, "exchange*.log")))
    if not os.path.exists(bench) or not logs:
        sys.exit(f"need benchmark_trace.log and exchange*.log in {args.dir}")
    tsc_ghz = args.tsc_ghz or perf.detect_tsc_ghz()
    if not tsc_ghz:
        sys.exit("could not detect TSC frequency, pass --tsc-ghz")

    keys, c1 = read_c1(bench)
    t6 = read_t6(logs)
    warmup = int(len(keys) * args.warmup_pct / 100)
    us = 1000.0 * tsc_ghz
    v, missing = [], 0
    for key, start in zip(keys[warmup:], c1[warmup:]):
        end = t6.get(key)
        if end is None:
            missing += 1
            continue
        v.append((end - start) / us)

    print(f"TSC: {tsc_ghz:.4f} GHz, {len(keys)} requests, first {warmup} left out as warm-up, "
          f"{missing} of the rest without a T6")
    if not v:
        return
    v.sort()
    print(f"\nthe exchange, request in its ring -> final response in the client's ring: {len(v)} requests (us)")
    heads = " ".join(f"{'p%g' % p:>8}" for p in perf.PERCENTILES)
    print(f"{'':<10} {'mean':>8} {heads} {'max':>8}")
    cells = " ".join(f"{perf.percentile(v, p):8.2f}" for p in perf.PERCENTILES)
    print(f"{'C1 -> T6':<10} {sum(v) / len(v):8.2f} {cells} {v[-1]:8.2f}")
    # For run-exchange-benchmark.sh's results-exchange.csv: requests, missing, then mean, the
    # percentiles and max in us.
    us_values = [sum(v) / len(v)] + [perf.percentile(v, p) for p in perf.PERCENTILES] + [v[-1]]
    print("csv:" + ",".join([str(len(v)), str(missing)] + [f"{x:.2f}" for x in us_values]))


if __name__ == "__main__":
    main()
