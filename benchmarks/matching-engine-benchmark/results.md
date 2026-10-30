# Matching engine benchmark results

Latency of the matching engine, measured with
[`matching-engine-benchmark.cpp`](matching-engine-benchmark.cpp) and analysed with
[`scripts/perf-analysis.py`](../../scripts/perf-analysis.py). See the
[README](README.md) for how the benchmark works.

All values are nanoseconds.

## Current result

Commit `2930952`, 5 runs, default settings (below), median [range]:

| | p50 | p90 | p99 | p99.9 | throughput |
|---|---|---|---|---|---|
| `processClientRequest` | 90 [90-95] | 171 [170-191] | 288 [286-318] | 412 [403-466] | 131.4 [131.0-141.3] ns/request |

`processClientRequest` is the time from the engine reading a request off its queue to having
written and published every response and market update for it. It is the only measurement
left in the hot path, see [what is measured](#what-is-measured). Throughput is the engine's
whole loop per request, measured by the driver.

## Setup

**Machine:** Intel i7-14650HX, Ubuntu 26.04.1.

**Kernel boot parameters** (results from "core isolation" on):

```
isolcpus=nohz,domain,managed_irq,4-13 nohz_full=4-13 rcu_nocbs=4-13 irqaffinity=0-3,14-23 nmi_watchdog=0
```

Physical cores 2-6 (CPUs 4-13) are reserved for the benchmark: the scheduler, device interrupts,
the 1 kHz timer tick and RCU callbacks stay off them. Only the benchmark's threads, which pin
themselves, run there. Until 2026-10-30 it was cores 2-4 (CPUs 4-9), the market data publisher
stand-in needed a fourth core.

| Parameter | What it does |
|---|---|
| `isolcpus=…,4-13` | Takes the CPUs out of the kernel's general use. The flags before the list choose what is taken away. |
| `domain` | Removes the CPUs from the scheduler's load balancing: it never places or moves a thread there by itself. Only threads that pin themselves to those CPUs run on them. |
| `nohz` | Stops the timer tick on those CPUs while only one thread runs there (see `nohz_full`). |
| `managed_irq` | Keeps managed interrupts off the CPUs where possible. These are devices with one hardware queue per CPU whose interrupts the kernel spreads over all CPUs, such as the NVMe disk. |
| `nohz_full=4-13` | Stops the periodic timer tick on a CPU that runs a single thread. The kernel normally interrupts every busy CPU 1,000 times a second (`CONFIG_HZ=1000`) to decide whether to switch threads, account CPU time and run expired timers; with one thread there is nothing to switch to, and other CPUs do the bookkeeping. About one tick per second remains. |
| `rcu_nocbs=4-13` | RCU callbacks (deferred freeing of kernel data) run in kernel threads on other CPUs instead of on the CPU that queued them. Implied by `nohz_full` on current kernels. |
| `irqaffinity=0-3,14-23` | Default CPUs for all other device interrupts. Only a default: a driver can pin its own interrupts. The Wi-Fi driver does, one receive queue per CPU, so its queues on CPUs 4-13 stay there. They fire with network traffic: at idle traffic a few times per 5 s on the engine's CPU, and Wi-Fi on or off made no measurable difference (170 vs 169-173 ns per request). Avoid heavy network traffic during runs, or move them by writing `0-3,14-23` to `/proc/irq/<n>/smp_affinity_list`. |
| `nmi_watchdog=0` | Turns off the hard-lockup detector, which sends every CPU a non-maskable interrupt every few seconds to check it still responds. An NMI can't be blocked, so it interrupts the engine whatever it is doing. |

Together they took the interrupts on the engine's CPU from ~6,200 per run to 2-7.

**Threads:** matching engine on CPU 4, its logger on CPU 6, benchmark driver (the order server
stand-in) on CPU 8, market data publisher stand-in on CPU 10. The hyper-thread siblings 5, 7, 9,
11 stay idle, CPUs 12-13 are kept for the snapshot synthesizer.

**Uncore clock:** pinned to its maximum, 4.5 GHz, by
[`prepare-machine.sh`](prepare-machine.sh) (`sudo`, after every boot, `--undo` restores the
default). The uncore is the ring and L3 cache between the cores. Left free it follows system
activity between 0.8 and 4.5 GHz, and with it the cost of every cache line the engine and the
threads reading its queues pass between them. Compared across two sessions, so only roughly,
pinning made the engine ~5-8 ns per request faster at a tight consumer loop and ~3 ns at 1 µs.
The script also checks the isolation, AC power, power profile and turbo, and warns about what
differs.

**Workload** (benchmark defaults since `2930952`):

| Setting | Value |
|---|---|
| requests | 500,000, first 50,000 results skipped as warm-up |
| sending | back to back, at most 131,072 unfinished requests queued: the engine never waits for a request |
| consumers | the driver reads the client responses like the order server, a thread on CPU 10 reads the market updates like the market data publisher. Each loop of theirs has a 16 µs busy wait standing in for their syscalls |
| prices | uniform in 1-256: the whole book, sparse levels |
| quantity | 1-100 |
| cancels | 25% of requests cancel a random earlier order (most are already filled → `CANCEL_REJECTED`) |
| clients / tickers | 16 / 8 |
| seed | 42: every run sends the identical request stream |

The consumers' loop time changes the results a lot: every cache line they read while the engine
is still writing it has to move back to the engine. Sweeping it from 0 to 16 µs took throughput
from ~215 to ~131 ns per request, flattening out at the end (8 -> 16 µs: 3%). 16 µs is where the
exact value matters least, and about an order server loop with recv/send on 16 sockets. That is
an estimate: revisit it once the real components exist.

**Run settings** for comparisons (since the noise investigation, see [noise](#noise)), as
[`run-benchmark.sh`](run-benchmark.sh) runs it:

```bash
GLIBC_TUNABLES=glibc.malloc.hugetlb=1 setarch -R matching_engine_benchmark
```

2 MB pages for large allocations and no address randomisation. This removes a memory
placement effect that otherwise changes some runs.

## How results are compared

- Compare versions **within one session, alternating runs**, never across sessions or reboots:
  the whole level moved by ~30 ns per request between two boots on 2026-10-30, with the same
  code and options. The only known change was the wider isolation, and moving the market data
  publisher back to a core that isn't isolated didn't undo it.
- Compare **p90** (and p99). p50 is a poor summary here.
- **p99.9 and max are dominated by noise** we have not fully explained, don't use them to
  compare code changes. My suspicion is that the order lookup client_orders_[client_id][order_id]. The array is 24MB with 30 MB L3 cache.

## What is measured

The engine's hot path logs only `processClientRequest` (one RDTSC measurement per request).
Every measurement costs the engine thread a timestamp and ~8 log queue writes (~60-70 ns), and
nested measurements include each other's logging. With all of them on, the numbers mostly
measured the logging itself, see `31ac48a` below. The detailed measurements (`add`, `cancel`,
`match`, `checkForMatch`, `addOrder`, `removeOrder`) and the TTT hops (T3, T4, T4t) are
commented out in the code and can be turned back on for a closer look.

Throughput is measured by the driver, the engine has no code for it: from publishing the first
request to reading the last request's response, divided by the number of requests. Sent back to
back, the engine always has requests queued, so this is its busy time per request. It covers the
whole loop, including the log line and reading the request, which `processClientRequest`
leaves out.

## History

The optimisations in order, with the numbers measured at the time. The workload and the
machine setup changed along the way, so compare numbers within a table, not across tables.

## Reproduce

```bash
# once per boot: pin the uncore clock, check the setup
sudo benchmarks/matching-engine-benchmark/prepare-machine.sh

# build (from the repo root)
cmake -S exchange/matching-engine -B exchange/matching-engine/cmake-build-release
cmake --build exchange/matching-engine/cmake-build-release -j --target matching_engine_benchmark

# run, analyse and record (the log goes to /tmp/me-bench)
benchmarks/matching-engine-benchmark/run-benchmark.sh
```

To compare two commits, or one commit with different options, use
[`ab-test.sh`](ab-test.sh), see the [benchmark README](README.md#5-compare-changes).
