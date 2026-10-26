# Matching engine benchmark

[`matching-engine-benchmark.cpp`](matching-engine-benchmark.cpp) measures how long the matching engine takes to process a
request: from reading it off its request queue (`T3`) to writing the client response (`T4t`)
and market updates (`T4`). It also times the code blocks inside the engine and the order book
(`add`, `cancel`, `match`, ...).

It stands in for the order server: it generates a reproducible stream of random NEW and CANCEL
requests and pushes them straight into the engine's request queue, back to back by default.
Nothing reads the engine's output queues, the engine overwrites them. TCP → engine (`T1`/`T2`)
is not measured until the order server exists.

The measurements are written to the engine's log by the macros in `common/perf-utils.h` and
analysed afterwards with `scripts/perf-analysis.py`. Results and what changed them over time are
in [results.md](results.md).

## 1. Build

The benchmark is built by the CMake project in `exchange/matching-engine/` (Release, `-O3` by default).
From the repo root:

```bash
cd exchange/matching-engine
cmake -B cmake-build-release
cmake --build cmake-build-release -j --target matching_engine_benchmark
```

`exchange/matching-engine/build.sh` builds everything (Release, Ninja) into the same directory.

## 2. Prepare the machine

Plug the laptop in, close heavy apps (browser, IDE indexing) and use the same power profile for
every run you compare: background load and power mode change the CPU's clock speed, which
shows up in the numbers.

## 3. Run

[`run-benchmark.sh`](run-benchmark.sh) rebuilds the benchmark, runs it, analyses the log
(step 4) and records the result in [`results.csv`](results.csv) (step 5), in one go. Options are
passed through to the benchmark:

```bash
<repo>/benchmarks/matching-engine-benchmark/run-benchmark.sh
<repo>/benchmarks/matching-engine-benchmark/run-benchmark.sh --price-min 101 --price-max 110
```

It runs the benchmark in `/tmp/me-bench` (override with `BENCH_DIR`): the engine writes
`exchange_matching_engine.log` to the current directory (~15 MB with the defaults). It also runs
it with 2 MB memory pages and no address randomisation
(`GLIBC_TUNABLES=glibc.malloc.hugetlb=1 setarch -R`), which gives every run the same memory
layout.

To run the binary by hand instead:

```bash
mkdir -p /tmp/me-bench && cd /tmp/me-bench && rm -f *.log
GLIBC_TUNABLES=glibc.malloc.hugetlb=1 setarch -R <repo>/exchange/matching-engine/cmake-build-release/matching_engine_benchmark
```

| Option | Default | Meaning |
|---|---|---|
| `--requests N` | 200000 | number of requests to send, at most 262144 with `--interval-us 0` |
| `--interval-us N` | 0 | time between requests, 0 sends them back to back |
| `--seed N` | 42 | random seed, same seed = same request stream |
| `--cancel-pct N` | 25 | % of requests that cancel an earlier order (may already be filled → `CANCEL_REJECTED`) |
| `--clients N` | 16 | number of clients, each has `ME_MAX_ORDER_PER_CLIENT` order ids |
| `--price-min N` | 1 | lowest NEW order price, prices are uniform in `[min, max]` |
| `--price-max N` | 256 | highest NEW order price, at most `ME_MAX_PRICE_LEVELS` |
| `--engine-core N` | 4 | core for the matching engine thread |
| `--logger-core N` | 6 | core for the engine's logger thread |
| `--driver-core N` | 8 | core for the benchmark's own thread |

A core of `-1` leaves that thread unpinned. Pin each thread to a **different physical core**:
check `lscpu -e`: CPUs with the same `CORE` are hyper-thread siblings, and a lower `MAXMHZ`
marks the slower E-cores. The defaults assume an
i7-14650HX, where CPUs 0-15 are P-cores (siblings 0-1, 2-3, ...) and 16-23 are slower E-cores.
Keep all three on P-cores, a thread landing on an E-core skews the numbers.

The default price range is the whole book: a sparse book where price levels are far apart, so
finding the next best price after a level empties has to skip many empty levels. A narrow
range such as `--price-min 101 --price-max 110` gives a dense book where levels are adjacent.

By default requests are sent back to back. The driver is ~3× faster than the engine (~40 ns vs
~110 ns per request), so requests queue up and the engine processes them one after another with
warm caches: p50 is ~30 ns lower than for a request arriving at an idle engine
(`--interval-us 10`). The engine keeps up from `--interval-us 1`. Compare runs with the same
interval.

With no gap the driver can get the whole run ahead of the engine, and the queue doesn't stop it
from overwriting requests the engine hasn't read yet. The log would still show one measurement
per request. So `--interval-us 0` is limited to `--requests` ≤ 262144 (`ME_MAX_CLIENT_UPDATES`),
a run that fits in the queue can't overwrite. A run takes `requests × interval` plus ~2.5 s to
set up.

Short runs see fewer interrupts and look better at the tail: compare runs of the same length.

## 4. Analyse

From the directory with the log:

```bash
python3 <repo>/scripts/perf-analysis.py --skip-first 20000
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
`--requests`. If it is lower, measurement lines were lost (the logger fell behind): use fewer
`--requests` or set `--interval-us`.

Measurements include the cost of logging them: every measurement writes a log line, which is
counted in any measurement around it. Compare runs with each other rather than reading the
numbers as the code's absolute cost.

## 5. Compare changes

- Run 3-5 times per version and compare medians, single runs are noisy.
- Change one thing at a time and keep everything else (options, cores, machine setup) the same.
- Every `run-benchmark.sh` run appends a row to [`results.csv`](results.csv):

| Column | Meaning |
|---|---|
| `date` | when the run finished |
| `commit` | `HEAD` at the time |
| `dirty` | `yes` if the engine code (`common/`, `exchange/` and the benchmark driver) had uncommitted changes |
| `changes` | fingerprint of those changes: rows with the same commit and fingerprint ran the same code |
| `options` | the options passed to the benchmark |
| `count` ... `max` | `processClientRequest` stats in ns, after skipping the first 10% |

  Set `BENCH_RESULTS=<file>` to record somewhere else, e.g. for experiments you don't want in
  the history. A run with `BENCH_BIN` set isn't rebuilt, its row notes the binary.
