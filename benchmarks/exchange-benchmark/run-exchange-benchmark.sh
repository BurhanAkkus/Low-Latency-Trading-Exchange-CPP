#!/usr/bin/env bash
# Builds the exchange and the exchange benchmark, starts the exchange with its threads pinned, runs
# the benchmark's clients against it, stops the exchange with SIGTERM and appends the result to
# results.csv next to this script.
#
# Cores (CPUs 2-15 isolated, see ../matching-engine-benchmark/results.md): the exchange's matching
# engine on 8 and order server on 10 (the 5.2 GHz cores), market data publisher 12, snapshot
# synthesizer 2. The benchmark's sender 4, receiver 6, market data receiver 14 (its defaults).
# Loggers are unpinned and run on the OS's CPUs (0-1, 16-23).
# 2 MB pages and no address randomisation for both processes, as in run-benchmark.sh.
#
# results.csv gets one row per run: the commit, whether the code had uncommitted changes (dirty) and
# a fingerprint of them (runs with the same fingerprint ran the same code), the benchmark options,
# the send rate, answered requests, tcp seq gaps, lost market updates, the exchange's requests/s
# and market updates/s, and p50 p90 p99 p99.9 max in us of request -> final response, of
# request -> ADD / CANCEL update and of the sender's schedule lag. Latencies leave out the first
# 10% of requests. After the row, scripts/trace-analysis.py prints each stage of a request's path
# through the exchange (not recorded).
#
# Usage:   run-exchange-benchmark.sh [benchmark options]
#          run-exchange-benchmark.sh --rate 200000 --clients 64
# Env:     BENCH_DIR      directory for the logs (default /tmp/exchange-bench)
#          BENCH_RESULTS  file to record to (default results.csv next to this script)
#          EXCHANGE_ARGS  the exchange's options (default the cores above)
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
BUILD=$REPO/exchange/matching-engine/cmake-build-release
DIR=${BENCH_DIR:-/tmp/exchange-bench}
RESULTS=${BENCH_RESULTS:-$HERE/results.csv}
EXCHANGE_ARGS=${EXCHANGE_ARGS:---engine-core 8 --order-server-core 10 --mdp-core 12 --snapshot-core 2}
PORT=12345  # exchange-main's order server

# Rebuild, so the binaries match the code the row says they ran. Configure every time: a build
# directory from before the exchange targets existed doesn't know them.
cmake -S "$REPO/exchange/matching-engine" -B "$BUILD" > /dev/null
cmake --build "$BUILD" -j --target exchange exchange_benchmark > /dev/null

# The code both binaries are built from, results.csv itself is left out.
code=(common exchange benchmarks/exchange-benchmark/exchange-benchmark.cpp)
commit=$(git -C "$REPO" rev-parse --short HEAD)
if git -C "$REPO" diff --quiet HEAD -- "${code[@]}"; then
    dirty=no
    changes=-
else
    dirty=yes
    changes=$(git -C "$REPO" diff HEAD -- "${code[@]}" | sha1sum | cut -c1-8)
fi

if ss -ltn | grep -q ":$PORT "; then
    echo "port $PORT is in use, is an exchange still running?" >&2
    exit 1
fi

# The exchange writes its logs to the current directory.
mkdir -p "$DIR"
cd "$DIR"
rm -f exchange*.log benchmark*.log

# setarch execs the exchange, so its pid is the exchange's.
# shellcheck disable=SC2086  # EXCHANGE_ARGS is a list of options.
GLIBC_TUNABLES=glibc.malloc.hugetlb=1 setarch -R "$BUILD/exchange" $EXCHANGE_ARGS > exchange.out 2>&1 &
exchange_pid=$!
# Stops the exchange, also when the benchmark fails or the script is interrupted.
# SIGTERM, not SIGINT: a script's background commands ignore SIGINT, until the exchange sets its
# handler a SIGINT is lost. Before then SIGTERM just ends the process, there is nothing to clean up.
# Up to 30 s for a clean stop: waits for whichever ends first, the exchange or a 30 s sleep (bash
# 5.1's wait -n -p), then ends the other. The sleep must not outlive the script, it would keep the
# script's output open and a reader such as tee would wait for it.
exchange_status=
stop_exchange() {
    [[ -n $exchange_status ]] && return
    kill -TERM "$exchange_pid" 2>/dev/null || true
    sleep 30 &
    local timer=$! ended=
    exchange_status=0
    wait -n -p ended "$exchange_pid" "$timer" || exchange_status=$?
    if [[ $ended == "$timer" ]]; then
        echo "exchange didn't stop within 30 s of SIGTERM, killing it" >&2
        kill -KILL "$exchange_pid" 2>/dev/null || true
        exchange_status=0
        wait "$exchange_pid" || exchange_status=$?
    else
        kill "$timer" 2>/dev/null || true
        wait "$timer" 2>/dev/null || true
    fi
}
trap stop_exchange EXIT

# The benchmark connects once the order server listens, the exchange takes ~8 s to start.
if ! GLIBC_TUNABLES=glibc.malloc.hugetlb=1 setarch -R "$BUILD/exchange_benchmark" "$@" | tee benchmark.out; then
    echo "the benchmark failed, nothing recorded (exchange output in $DIR/exchange.out)" >&2
    exit 1
fi
stop_exchange
if [[ $exchange_status != 0 ]]; then
    echo "the exchange exited with $exchange_status, nothing recorded (output in $DIR/exchange.out)" >&2
    exit 1
fi

result=$(sed -n 's/^csv://p' benchmark.out)
if [[ -z $result ]]; then
    echo "no csv line in the benchmark's output, nothing recorded" >&2
    exit 1
fi
if [[ ! -f $RESULTS ]]; then
    latency=""
    for m in response md lag; do
        for p in p50 p90 p99 p99.9 max; do latency+=",${m}_$p"; done
    done
    echo "date,commit,dirty,changes,options,sent_per_s,answered,tcp_gaps,md_lost,requests_per_s,md_updates_per_s$latency" > "$RESULTS"
fi
echo "$(date '+%Y-%m-%d %H:%M'),$commit,$dirty,$changes,\"$*\",$result" >> "$RESULTS"
echo "recorded in $RESULTS"

# Where requests spend their time, from the exchange's TRACE lines and the benchmark's
# benchmark_trace.log, complete once the exchange has stopped. Printed only, not recorded.
python3 "$REPO/scripts/trace-analysis.py" || true
