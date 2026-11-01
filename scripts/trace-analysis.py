#!/usr/bin/env python3
"""Where requests spend their time: per-stage latency from the exchange benchmark's traces.

The exchange logs a TRACE line (common/perf-utils.h) at every stage of a request's path,
"<stage> <key> <tsc>", the benchmark writes its own stages to benchmark_trace.log:
    C0   <request key> <tsc>     the request goes into its TCP connection (send order)
    C7   <request key> <tsc>     its final response arrives
    C7m  <update key> <tsc>      an ADD / CANCEL update arrives
    LINK <request key> <update key>  the update a request caused (from its ACCEPTED / CANCELED)
Stages are joined by key. All times are TSC cycles, one clock for every core and process.

    response:     C0 -> T1 -> T2 -> T3 -> T4 -> T5 -> T6 -> C7
    market data:  T4 -> M5 -> M6 -> C7m

T6 is logged per socket send, by fd (T6fd maps each client to its fd): a response's T6 is the first
send on its client's socket at or after its T5. M6 is logged once per publisher iteration: an
update's M6 is the next M6 after its M5 in the publisher's log.

The first --warmup-pct of requests (in send order) are left out, as in the benchmark's own report.
Each path's table only has requests traced at every one of its stages, so the stage means add up to
the total's mean.

Usage:
    python3 scripts/trace-analysis.py                 # logs in the current directory
    python3 scripts/trace-analysis.py /tmp/exchange-bench --tsc-ghz 2.4192
"""
import argparse
import bisect
import glob
import importlib.util
import os
import sys
from collections import defaultdict

# The TSC frequency and percentiles from perf-analysis.py.
_spec = importlib.util.spec_from_file_location(
    "perf_analysis", os.path.join(os.path.dirname(os.path.abspath(__file__)), "perf-analysis.py"))
perf = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(perf)

REQUEST_STAGES = ("T1", "T2", "T3", "T4", "T5")
RESPONSE_PATH = [
    ("C0", "T1", "TCP in, waiting for the order server to read the socket"),
    ("T1", "T2", "rest of the socket pass, sequencer, batch published"),
    ("T2", "T3", "waiting in the request queue"),
    ("T3", "T4", "matching engine"),
    ("T4", "T5", "waiting in the response queue"),
    ("T5", "T6", "waiting for the next send pass"),
    ("T6", "C7", "TCP out, client read"),
]
MARKET_DATA_PATH = [
    ("C0", "T4", "request in, up to the engine publishing (as above)"),
    ("T4", "M5", "waiting in the market update queue"),
    ("M5", "M6", "market data publisher"),
    ("M6", "C7m", "UDP multicast, client read"),
]


def read_benchmark(path):
    """Request keys in send order with their C0, and C7 / C7m / LINK by key."""
    keys, c0, c7, c7m, link = [], [], {}, {}, {}
    with open(path) as f:
        for line in f:
            tag, a, b = line.split()
            a, b = int(a), int(b)
            if tag == "C0":
                keys.append(a)
                c0.append(b)
            elif tag == "C7":
                c7.setdefault(a, b)
            elif tag == "C7m":
                c7m.setdefault(a, b)
            elif tag == "LINK":
                link[a] = b
    return keys, c0, c7, c7m, link


def read_exchange(paths):
    """Request stages by key, socket sends by fd, client fds, M5 / M6 by update key."""
    stages = {s: {} for s in REQUEST_STAGES}
    sends = defaultdict(list)  # fd -> send tscs, in time order: one logger writes them in order
    client_fd = {}
    m5, m6 = {}, {}
    for path in paths:
        pending = []  # M5 keys waiting for the next M6, only the publisher's log has either
        with open(path, errors="replace") as f:
            for line in f:
                p = line.split()
                if len(p) != 3:
                    continue
                tag = p[0]
                try:
                    a, b = int(p[1]), int(p[2])
                except ValueError:
                    continue
                if tag in stages:
                    stages[tag].setdefault(a, b)
                elif tag == "T6":
                    sends[a].append(b)
                elif tag == "T6fd":
                    client_fd[a >> 32] = a & 0xFFFFFFFF
                elif tag == "M5":
                    m5.setdefault(a, b)
                    pending.append(a)
                elif tag == "M6":
                    for k in pending:
                        m6.setdefault(k, b)
                    pending.clear()
    return stages, sends, client_fd, m5, m6


class Path:
    """A path's stage durations, in cycles: one list per stage, the total last."""

    def __init__(self, title, stages, total):
        self.title, self.stages = title, stages + [(total[0], total[1], "total")]
        self.cycles = [[] for _ in self.stages]

    def add(self, t):
        for (a, b, _), v in zip(self.stages, self.cycles):
            v.append(t[b] - t[a])

    def print(self, tsc_ghz):
        """One row per stage and the total, in us: mean, share of the total's mean, percentiles."""
        if not self.cycles[-1]:
            return
        us = 1000.0 * tsc_ghz
        total_mean = sum(self.cycles[-1]) / len(self.cycles[-1]) / us
        print(f"\n{self.title}: {len(self.cycles[-1])} requests (us)")
        print(f"{'stage':<11} {'':<52} {'mean':>8} {'share':>6} {'p50':>8} {'p90':>8} {'p99':>8} {'p99.9':>8} {'max':>8}")
        for (a, b, what), cycles in zip(self.stages, self.cycles):
            v = sorted(c / us for c in cycles)
            mean = sum(v) / len(v)
            cells = " ".join(f"{perf.percentile(v, p):8.2f}" for p in perf.PERCENTILES)
            print(f"{a + ' -> ' + b:<11} {what:<52} {mean:8.2f} {mean / total_mean:6.1%} {cells} {v[-1]:8.2f}")


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

    keys, c0, c7, c7m, link = read_benchmark(bench)
    stages, sends, client_fd, m5, m6 = read_exchange(logs)
    warmup = int(len(keys) * args.warmup_pct / 100)

    response = Path("request -> final response", RESPONSE_PATH, ("C0", "C7"))
    market_data = Path("request -> ADD / CANCEL update", MARKET_DATA_PATH, ("C0", "C7m"))
    incomplete = 0
    for i in range(warmup, len(keys)):
        key = keys[i]
        t = {"C0": c0[i], "C7": c7.get(key)}
        for s in REQUEST_STAGES:
            t[s] = stages[s].get(key)
        fd = client_fd.get(key >> 32)
        if t["T5"] is not None and fd is not None:
            fd_sends = sends.get(fd, [])
            j = bisect.bisect_left(fd_sends, t["T5"])
            t["T6"] = fd_sends[j] if j < len(fd_sends) else None
        if any(v is None for v in t.values()):
            incomplete += 1
            continue
        response.add(t)
        update = link.get(key)
        if update is not None and update in m5 and update in m6 and update in c7m:
            market_data.add({"C0": t["C0"], "T4": t["T4"], "M5": m5[update], "M6": m6[update], "C7m": c7m[update]})

    print(f"TSC: {tsc_ghz:.4f} GHz, {len(keys)} requests, first {warmup} left out as warm-up, "
          f"{incomplete} of the rest not traced at every stage")
    response.print(tsc_ghz)
    market_data.print(tsc_ghz)


if __name__ == "__main__":
    main()
