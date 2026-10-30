# Matching engine benchmark

[`matching-engine-benchmark.cpp`](matching-engine-benchmark.cpp) measures how long the matching engine takes to process a
request: from reading it off its request queue (`T3`) to publishing the client responses (`T4t`)
and market updates (`T4`), and the engine's throughput. Timing the code blocks inside the engine
and the order book (`add`, `cancel`, `match`, ...) is possible but commented out, see
[results.md](results.md#what-is-measured).

The benchmark stands in for the exchange components that aren't built yet:

- **Order server:** the driver thread generates a reproducible stream of random NEW and CANCEL
  requests, pushes them into the engine's request queue and reads the client responses, in one
  loop like the real order server.
- **Market data publisher:** a thread on its own core reads the market updates.

Both only read what the engine publishes, they don't send anything. A busy wait in each loop
stands in for the syscalls the real ones would make (`--consumer-loop-ns`). TCP → engine
(`T1`/`T2`) is not measured until the order server exists.

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

The CPUs the benchmark threads run on are isolated with kernel boot parameters, see
[results.md](results.md#setup). After every boot, run

```bash
sudo <repo>/benchmarks/matching-engine-benchmark/prepare-machine.sh
```

It pins the uncore clock (the ring and L3 cache between the cores) to its maximum and warns if
the isolation, AC power, power profile or turbo differ from the setup in results.md.
`--undo` restores the uncore clock's default.

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
| `--requests N` | 500000 | number of requests to send |
| `--interval-us N` | 0 | time between requests, 0 sends back to back |
| `--consumer-loop-ns N` | 16000 | busy wait per loop of the order server and market data publisher stand-ins, 0 polls in a tight loop |
| `--seed N` | 42 | random seed, same seed = same request stream |
| `--cancel-pct N` | 25 | % of requests that cancel an earlier order (may already be filled → `CANCEL_REJECTED`) |
| `--clients N` | 16 | number of clients, each has `ME_MAX_ORDER_PER_CLIENT` order ids |
| `--price-min N` | 1 | lowest NEW order price, prices are uniform in `[min, max]` |
| `--price-max N` | 256 | highest NEW order price, at most `ME_MAX_PRICE_LEVELS` |
| `--engine-core N` | 4 | core for the matching engine thread |
| `--logger-core N` | 6 | core for the engine's logger thread |
| `--driver-core N` | 8 | core for the driver, the order server stand-in |
| `--mdp-core N` | 10 | core for the market data publisher stand-in |

A core of `-1` leaves that thread unpinned. Pin each thread to a **different physical core**:
check `lscpu -e`: CPUs with the same `CORE` are hyper-thread siblings, and a lower `MAXMHZ`
marks the slower E-cores. The defaults assume an
i7-14650HX, where CPUs 0-15 are P-cores (siblings 0-1, 2-3, ...) and 16-23 are slower E-cores.
Keep all four on P-cores, a thread landing on an E-core skews the numbers.

The default price range is the whole book: a sparse book where price levels are far apart, so
finding the next best price after a level empties has to skip many empty levels. A narrow
range such as `--price-min 101 --price-max 110` gives a dense book where levels are adjacent.

By default requests are sent back to back. The driver is faster than the engine, so requests
queue up and the engine processes them one after another with warm caches, which measures lower
than a request arriving at an idle engine (`--interval-us 10`). The engine keeps up from
`--interval-us 1`.

The driver keeps at most 131072 unfinished requests queued, half the request queue: still
~17 ms of work, so the engine never waits for a request. It counts a request as finished when
it reads its ACCEPTED, REJECTED, CANCELED or CANCEL_REJECTED response, every request gets exactly
one. The request queue never fills, so the driver never reads the engine's read index to find
space. Like the order server's FIFO sequencer, it publishes the requests of one loop with one
index update, at most 256.

The consumers' loop time changes the numbers a lot, see [results.md](results.md#setup) for the
sweep behind the 16 µs default. A run takes ~0.1 s of requests plus ~2.5 s to set up.

Short runs see fewer interrupts and look better at the tail: compare runs of the same length.

## 4. Analyse

From the directory with the log:

```bash
python3 <repo>/scripts/perf-analysis.py --skip-first 50000
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

The benchmark itself prints the throughput: `throughput: N requests in T ns, X ns/request`.

**Sanity checks:**

- The script prints `skipped first N of M requests`. `M` should equal `--requests`. If it is
  lower, measurement lines were lost.
- The benchmark prints how many client responses and market updates the stand-ins read. With
  the default options they are 1079640 and 835100. It also stops with an error if it counted
  more finished requests than it sent.

Measurements include the cost of logging them: every measurement writes a log line, which is
counted in any measurement around it. Compare runs with each other rather than reading the
numbers as the code's absolute cost.

## 5. Compare changes

[`ab-test.sh`](ab-test.sh) compares two variants: a git ref, optionally followed by benchmark
options, quoted as one argument.

```bash
benchmarks/matching-engine-benchmark/ab-test.sh main ab/03-power-of-two
benchmarks/matching-engine-benchmark/ab-test.sh "main --consumer-loop-ns 0" "main --consumer-loop-ns 16000"
```

It builds each commit from `git archive` under `/tmp/me-ab` (your working tree isn't touched),
then runs A, B, A, B, ... for 5 rounds (`RUNS=N`), waiting before each run until the CPU package
is back at its idle temperature. Every run goes to a CSV in [`ab-results/`](ab-results/), then
[`scripts/ab-compare.py`](../../scripts/ab-compare.py) prints each variant's median and range for
p50, p90, p99, p99.9 and throughput, and a permutation test on the difference. Runs more than 5%
off their variant's median throughput are flagged and the test is repeated without them, see the
two-mode effect in [results.md](results.md#setup). With 5 runs each, the smallest possible
p-value is 0.008: complete separation.

- Compare within one session only, never with runs from another session or boot.
- Change one thing at a time and keep everything else (options, cores, machine setup) the same.
- Every `run-benchmark.sh` run appends a row to [`results.csv`](results.csv):

| Column | Meaning |
|---|---|
| `date` | when the run finished |
| `commit` | `HEAD` at the time |
| `dirty` | `yes` if the engine code (`common/`, `exchange/` and the benchmark driver) had uncommitted changes |
| `changes` | fingerprint of those changes: rows with the same commit and fingerprint ran the same code |
| `options` | the options passed to the benchmark |
| `count` ... `max` | `ME_req` (`processClientRequest` + `publishOutputs`) stats in ns, after skipping the first 10% |
| `ns_per_request` | throughput: the benchmark's ns per request over the whole run, warm-up included |

  Rows above the `BENCHMARK CHANGED` line in results.csv come from the earlier benchmark (no
  consumers, no backpressure, paced sending) and aren't comparable with the rows below it.

  Set `BENCH_RESULTS=<file>` to record somewhere else, e.g. for experiments you don't want in
  the history. A run with `BENCH_BIN` set isn't rebuilt, its row notes the binary.
