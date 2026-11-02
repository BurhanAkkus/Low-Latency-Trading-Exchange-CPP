#!/usr/bin/env python3
"""Where requests spend their time: per-stage latency from the exchange benchmark's traces.

The exchange logs a TRACE line (common/perf-utils.h) at every stage of a request's path,
"<stage> <key> <tsc>", the benchmark writes its own stages to benchmark_trace.log:
    C0   <request key> <tsc>     the request goes into its TCP connection (send order)
    C1   <request key> <tsc>     its send() returned
    C7   <request key> <tsc>     its final response arrives
    C7m  <update key> <tsc>      an ADD / CANCEL update arrives
    LINK <request key> <update key>  the update a request caused (from its ACCEPTED / CANCELED)
Stages are joined by key. All times are TSC cycles, one clock for every core and process.

    response:     C0 -> P1 -> R0 -> R1 -> T1 -> T2 -> T3 -> T4 -> T5 -> S0 -> T6 -> C7
    market data:  T4 -> M5 -> M6 -> C7m

The order server's socket stages are logged per poll or per socket, not per request:
    P1   epoll_wait returned with something (shared memory: a pass over the clients' rings that read
         something started): a request's P1 is the last one before its R0, or before its T1
         without R0/R1 lines.
    R0, R1  a read on a socket started and returned, by fd (T6fd maps each client to its fd): a
         request's read is the last one on its client's socket at or before its T1. Not logged by
         the shared memory order server, its path goes P1 -> T1.
    S0, T6  a send on a socket started and returned, by fd: a response's send is the first one on
         its client's socket at or after its T5.
M6 is logged once per publisher iteration: an update's M6 is the next M6 after its M5 in the
publisher's log.

C1 isn't on the path: on loopback the kernel puts the request into the order server's socket
inside the client's send(), so the order server can read it before that send() returns. C0 -> C1
is printed on its own, it's part of C0 -> P1.

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
    ("C0", "P1", "client send(), until the order server's epoll_wait"),
    ("P1", "R0", "reads of other sockets in the same poll"),
    ("R0", "R1", "recvmsg"),
    ("R1", "T1", "parse"),
    ("T1", "T2", "rest of the socket pass, sequencer, batch published"),
    ("T2", "T3", "waiting in the request queue"),
    ("T3", "T4", "matching engine"),
    ("T4", "T5", "waiting in the response queue"),
    ("T5", "S0", "rest of the response drain, other sockets' sends"),
    ("S0", "T6", "send() into TCP"),
    ("T6", "C7", "TCP out, client read"),
]
# Without R0/R1 lines (the shared memory order server): P1 straight to T1.
RESPONSE_PATH_NO_READS = [RESPONSE_PATH[0], ("P1", "T1", "the clients' rings up to this one, parse")] + RESPONSE_PATH[4:]
# The exchange's own work on a request: the response path without the syscalls (recvmsg, send())
# and the client's side.
SYSCALL_STAGES = {("C0", "P1"), ("R0", "R1"), ("S0", "T6"), ("T6", "C7")}


def no_syscall_paths(response_path):
    """The response path without the syscalls, and the market data path up to the publisher
    reading the update (M5 -> M6 has the publisher's UDP sends in it, it isn't split)."""
    no_syscalls = [s for s in response_path if s[:2] not in SYSCALL_STAGES]
    md = [s for s in no_syscalls if s[1] in ("R0", "T1", "T2", "T3", "T4")] + [
        ("T4", "M5", "waiting in the market update queue")]
    return no_syscalls, md


MARKET_DATA_PATH = [
    ("C0", "T4", "request in, up to the engine publishing (as above)"),
    ("T4", "M5", "waiting in the market update queue"),
    ("M5", "M6", "market data publisher"),
    ("M6", "C7m", "UDP multicast, client read"),
]


def read_benchmark(path):
    """Request keys in send order with their C0 and C1, and C7 / C7m / LINK by key."""
    keys, c0, c1, c7, c7m, link = [], [], [], {}, {}, {}
    with open(path) as f:
        for line in f:
            tag, a, b = line.split()
            a, b = int(a), int(b)
            if tag == "C0":
                keys.append(a)
                c0.append(b)
            elif tag == "C1":
                c1.append(b)
            elif tag == "C7":
                c7.setdefault(a, b)
            elif tag == "C7m":
                c7m.setdefault(a, b)
            elif tag == "LINK":
                link[a] = b
    return keys, c0, c1, c7, c7m, link


class Exchange:
    """The exchange's stages: by request key, by fd, by poll and by update key."""

    def __init__(self, paths):
        self.stages = {s: {} for s in REQUEST_STAGES}
        self.outputs = {}  # request key -> (responses, market updates) the engine produced for it
        # fd -> starts and ends of reads / sends, in time order: one logger writes them in order.
        self.reads = defaultdict(lambda: ([], []))
        self.sends = defaultdict(lambda: ([], []))
        self.polls = []
        self.client_fd = {}
        self.m5, self.m6 = {}, {}
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
                    if tag in self.stages:
                        self.stages[tag].setdefault(a, b)
                    elif tag == "T4n":
                        self.outputs.setdefault(a, (b >> 32, b & 0xFFFFFFFF))
                    elif tag in ("R0", "R1"):
                        self.reads[a][tag == "R1"].append(b)
                    elif tag in ("S0", "T6"):
                        self.sends[a][tag == "T6"].append(b)
                    elif tag == "P1":
                        self.polls.append(b)
                    elif tag == "T6fd":
                        self.client_fd[a >> 32] = a & 0xFFFFFFFF
                    elif tag == "M5":
                        self.m5.setdefault(a, b)
                        pending.append(a)
                    elif tag == "M6":
                        for k in pending:
                            self.m6.setdefault(k, b)
                        pending.clear()

    def socket_stages(self, t, client):
        """P1, R0, R1 of the read that brought the request in, S0, T6 of the send that took its
        response out. Leaves out what it can't find."""
        fd = self.client_fd.get(client)
        if fd is None:
            return
        if t.get("T1") is not None:
            starts, ends = self.reads.get(fd, ([], []))
            j = bisect.bisect_right(ends, t["T1"]) - 1
            if j >= 0:
                t["R0"], t["R1"] = starts[j], ends[j]
            # The poll before the read, or before T1 when there are no reads (R0/R1 not logged).
            k = bisect.bisect_right(self.polls, t.get("R0", t["T1"])) - 1
            if k >= 0:
                t["P1"] = self.polls[k]
        if t.get("T5") is not None:
            starts, ends = self.sends.get(fd, ([], []))
            j = bisect.bisect_left(ends, t["T5"])
            if j < len(ends):
                t["S0"], t["T6"] = starts[j], ends[j]


class Path:
    """A path's stage durations, in cycles: one list per stage, the total last. total None: the
    total is the sum of the stages, for stages that don't follow each other."""

    def __init__(self, title, stages, total, total_label="total"):
        self.title, self.total = title, total
        self.stages = stages + [(total[0], total[1], total_label) if total else ("", "", total_label)]
        self.cycles = [[] for _ in self.stages]

    def add(self, t):
        durations = [t[b] - t[a] for a, b, _ in self.stages[:-1]]
        durations.append(t[self.total[1]] - t[self.total[0]] if self.total else sum(durations))
        for v, d in zip(self.cycles, durations):
            v.append(d)

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
            print(f"{a + ' -> ' + b if a else '':<11} {what:<52} {mean:8.2f} {mean / total_mean:6.1%} {cells} {v[-1]:8.2f}")


def print_engine_by_fills(rows, tsc_ghz):
    """The engine's time (T3 -> T4) by the number of fills the request made, rows are (cycles,
    responses). A NEW gets an ACCEPTED, then per fill a FILLED for itself and one for the resting
    order. A CANCEL gets one response. Last column: how the slowest 0.1% engine times split."""
    if not rows:
        return
    us = 1000.0 * tsc_ghz
    slowest = sorted(c for c, _ in rows)[int(len(rows) * 0.999)]
    slow_total = sum(1 for c, _ in rows if c >= slowest)
    print(f"\nmatching engine (T3 -> T4) by the request's fills: {len(rows)} requests (us)")
    print(f"{'fills':<6} {'requests':>9} {'share':>6} {'mean':>8} {'p50':>8} {'p90':>8} {'p99':>8} {'p99.9':>8} {'max':>8}"
          f" {'of slowest 0.1%':>16}")
    for lo, hi, label in ((0, 0, "0"), (1, 1, "1"), (2, 2, "2"), (3, 4, "3-4"), (5, 8, "5-8"), (9, None, "9+")):
        inside = [c for c, r in rows if lo <= (r - 1) // 2 and (hi is None or (r - 1) // 2 <= hi)]
        if not inside:
            continue
        v = sorted(c / us for c in inside)
        cells = " ".join(f"{perf.percentile(v, p):8.2f}" for p in perf.PERCENTILES)
        slow = sum(1 for c in inside if c >= slowest)
        print(f"{label:<6} {len(v):>9} {len(v) / len(rows):6.1%} {sum(v) / len(v):8.2f} {cells} {v[-1]:8.2f}"
              f" {slow / slow_total:16.1%}")


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

    keys, c0, c1, c7, c7m, link = read_benchmark(bench)
    exchange = Exchange(logs)
    warmup = int(len(keys) * args.warmup_pct / 100)

    # The TCP order server logs R0/R1 around its recvmsg, the shared memory one doesn't.
    response_path = RESPONSE_PATH if exchange.reads else RESPONSE_PATH_NO_READS
    no_syscall_path, no_syscall_md_path = no_syscall_paths(response_path)
    response = Path("request -> final response", response_path, ("C0", "C7"))
    market_data = Path("request -> ADD / CANCEL update", MARKET_DATA_PATH, ("C0", "C7m"))
    client_send = Path("the client's send() on its own, part of C0 -> P1", [], ("C0", "C1"), "send()")
    no_syscalls = Path("request -> final response without syscalls and the client's side", no_syscall_path, None,
                       "sum: the order server and the engine")
    no_syscalls_md = Path("request -> publisher reads its update, without syscalls", no_syscall_md_path, None,
                          "sum: the order server, the engine, the queue")
    path_stages = {s for a, b, _ in response_path for s in (a, b)}
    engine_rows = []
    incomplete = 0
    for i in range(warmup, len(keys)):
        key = keys[i]
        t = {"C0": c0[i], "C7": c7.get(key)}
        for s in REQUEST_STAGES:
            t[s] = exchange.stages[s].get(key)
        exchange.socket_stages(t, key >> 32)
        if any(t.get(s) is None for s in path_stages):
            incomplete += 1
            continue
        response.add(t)
        no_syscalls.add(t)
        if key in exchange.outputs:
            engine_rows.append((t["T4"] - t["T3"], exchange.outputs[key][0]))
        if i < len(c1):
            client_send.add({"C0": c0[i], "C1": c1[i]})
        update = link.get(key)
        if update is not None and update in exchange.m5 and update in exchange.m6 and update in c7m:
            market_data.add({"C0": t["C0"], "T4": t["T4"], "M5": exchange.m5[update],
                             "M6": exchange.m6[update], "C7m": c7m[update]})
            no_syscalls_md.add({**t, "M5": exchange.m5[update]})

    print(f"TSC: {tsc_ghz:.4f} GHz, {len(keys)} requests, first {warmup} left out as warm-up, "
          f"{incomplete} of the rest not traced at every stage")
    response.print(tsc_ghz)
    client_send.print(tsc_ghz)
    market_data.print(tsc_ghz)
    no_syscalls.print(tsc_ghz)
    no_syscalls_md.print(tsc_ghz)
    print_engine_by_fills(engine_rows, tsc_ghz)


if __name__ == "__main__":
    main()
