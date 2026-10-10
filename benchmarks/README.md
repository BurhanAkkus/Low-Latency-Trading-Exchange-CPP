# Benchmarks

## Matching engine benchmark

`matching-engine-benchmark.cpp` measures how long the matching engine takes to process a
request: from reading it off its request queue (`T3`) to writing the client response (`T4t`)
and market updates (`T4`). It also times the code blocks inside the engine and the order book
(`add`, `cancel`, `match`, ...).

It stands in for the order server: it generates a reproducible stream of random NEW and CANCEL
requests, pushes them straight into the engine's request queue at a fixed rate, and drains the
engine's output queues. TCP → engine (`T1`/`T2`) is not measured until the order server exists.

The measurements are written to the engine's log by the macros in `common/perf-utils.h` and
analysed afterwards with `scripts/perf-analysis.py`.

### 1. Build

The benchmark is built by the CMake project in `matching-engine/` (Release, `-O3` by default).
From the repo root:

```bash
cd matching-engine
cmake -B cmake-build-release
cmake --build cmake-build-release -j --target matching_engine_benchmark
```

`matching-engine/build.sh` builds everything (Release, Ninja) into the same directory.

### 2. Prepare the machine

Plug the laptop in, close heavy apps (browser, IDE indexing) and use the same power profile for
every run you compare: background load and power mode change the CPU's clock speed, which
shows up in the numbers.

Turbo can stay on: a run keeps only three threads busy, far below the CPU's power limits, so
the clock speed should not drop during a run. To confirm, watch the pinned cores' clock speed
(`Bzy_MHz`) while a benchmark runs:

```bash
sudo turbostat --quiet --cpu 4,6,8 --show CPU,Bzy_MHz,CoreTmp --interval 1
```

If `Bzy_MHz` stays flat during a run and is the same across back-to-back runs, the clock speed
is not affecting the results. If it drops during a run, or a second run starts lower, the CPU
is heating up: turn turbo off while benchmarking
(`echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo`, `echo 0` to turn it back on).

### 3. Run

The engine writes `exchange_matching_engine.log` to the **current directory** (tens of MB per
run), so run it from a scratch directory:

```bash
mkdir -p /tmp/me-bench && cd /tmp/me-bench && rm -f *.log
<repo>/matching-engine/cmake-build-release/matching_engine_benchmark --requests 20000
```

| Option | Default | Meaning |
|---|---|---|
| `--requests N` | 100000 | number of requests to send |
| `--interval-us N` | 500 | time between requests |
| `--seed N` | 42 | random seed, same seed = same request stream |
| `--cancel-pct N` | 25 | % of requests that cancel an earlier order (may already be filled → `CANCEL_REJECTED`) |
| `--clients N` | 4 | number of clients, each has `ME_MAX_ORDER_PER_CLIENT` order ids |
| `--engine-core N` | 4 | core for the matching engine thread |
| `--logger-core N` | 6 | core for the engine's logger thread |
| `--driver-core N` | 8 | core for the benchmark's own thread |

A core of `-1` leaves that thread unpinned. Pin each thread to a **different physical core**:
check `lscpu -e`: CPUs with the same `CORE` are hyper-thread siblings, and a lower `MAXMHZ`
marks the slower E-cores. The defaults assume an
i7-14650HX, where CPUs 0-15 are P-cores (siblings 0-1, 2-3, ...) and 16-23 are slower E-cores.
Keep all three on P-cores, a thread landing on an E-core skews the numbers.

Keep `--interval-us` above the engine's per-request time. If requests arrive faster than the
engine handles them they queue up, and the logger falls behind and overwrites unread lines.
100 µs saturates the engine today.

### 4. Analyse

From the directory with the log:

```bash
python3 <repo>/scripts/perf-analysis.py --skip-first 2000
```

| Option | Meaning |
|---|---|
| `logs...` | log files to read, default `exchange*.log` in the current directory |
| `--skip-first N` | ignore everything logged before the (N+1)th request. The first requests run with cold caches, first-touch page faults and an empty book; skip about 10% of the run |
| `--tsc-ghz X` | TSC frequency used to convert RDTSC cycles to ns. Read from the kernel log by default (`tsc: Detected ... MHz TSC`) |

The script prints two tables, all values in nanoseconds:

- **Code blocks (RDTSC):** time spent inside one block, e.g. `Exchange_MEOrderBook_match` is one fill.
- **Hops (TTT):** time between two points on a request's path, e.g. `T3 -> T4t` is reading the
  request to writing a client response. One request produces several responses and market
  updates, each is measured from the same `T3`.

For each: count, mean, p50 (typical request), p90, p99, p99.9 (slow requests, the "tail") and max.

**Sanity check:** the script prints `skipped first N of M requests`. `M` should equal
`--requests`. If it is lower, measurement lines were lost (the logger fell behind): increase
`--interval-us`.

Measurements include the cost of logging them: every measurement writes a log line, which is
counted in any measurement around it. Compare runs with each other rather than reading the
numbers as the code's absolute cost.

### 5. Compare changes

- Run 3-5 times per version and compare medians, single runs are noisy.
- Change one thing at a time and keep everything else (options, cores, machine setup) the same.
- Save each result under the commit it measured:

```bash
mkdir -p <repo>/benchmarks/results
python3 <repo>/scripts/perf-analysis.py --skip-first 2000 > <repo>/benchmarks/results/$(git -C <repo> rev-parse --short HEAD).txt
```
