#!/usr/bin/env bash
# Builds and runs the matching engine benchmark with a fixed memory layout, analyses the log and
# appends the result to results.csv next to this script.
#
# 2 MB pages for large allocations (GLIBC_TUNABLES) and no address randomisation (setarch -R)
# give every run the same cache placement. Without them ~1 in 3 runs is ~40 ns slower at p50,
# see results.md. Both only apply to the benchmark process.
#
# results.csv gets one row per run: the commit, whether the engine code had uncommitted changes
# (dirty) and a fingerprint of those changes (runs with the same fingerprint ran the same code),
# the options and processClientRequest's stats in ns.
#
# Usage:   run-benchmark.sh [benchmark options]
#          run-benchmark.sh --price-min 101 --price-max 110
# Env:     BENCH_DIR  directory for the engine's log (default /tmp/me-bench)
#          BENCH_BIN  benchmark binary, not rebuilt (default: build the Release binary first)
#          BENCH_RESULTS  file to record to (default results.csv next to this script)
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
BUILD=$REPO/matching-engine/cmake-build-release
BIN=${BENCH_BIN:-$BUILD/matching_engine_benchmark}
DIR=${BENCH_DIR:-/tmp/me-bench}
RESULTS=${BENCH_RESULTS:-$HERE/results.csv}

# Rebuild, so the binary matches the code the row says it ran.
if [[ -z ${BENCH_BIN:-} ]]; then
    [[ -d $BUILD ]] || cmake -S "$REPO/matching-engine" -B "$BUILD" > /dev/null
    cmake --build "$BUILD" -j --target matching_engine_benchmark > /dev/null
fi
if [[ ! -x $BIN ]]; then
    echo "no benchmark binary at $BIN, build it first (see README.md)" >&2
    exit 1
fi

# The code the benchmark is built from, results.csv itself is left out.
code=(common matching-engine/matcher matching-engine/order-book matching-engine/order-server
      matching-engine/market-data benchmarks/matching-engine-benchmark/matching-engine-benchmark.cpp)
commit=$(git -C "$REPO" rev-parse --short HEAD)
if git -C "$REPO" diff --quiet HEAD -- "${code[@]}"; then
    dirty=no
    changes=-
else
    dirty=yes
    changes=$(git -C "$REPO" diff HEAD -- "${code[@]}" | sha1sum | cut -c1-8)
fi
[[ -n ${BENCH_BIN:-} ]] && changes="$changes (BENCH_BIN=$BENCH_BIN)"

# Skip the first 10% of requests as warm-up.
requests=500000
args=("$@")
for ((i = 0; i + 1 < ${#args[@]}; i++)); do
    [[ ${args[i]} == --requests ]] && requests=${args[i + 1]}
done

# The engine writes its log to the current directory.
mkdir -p "$DIR"
cd "$DIR"
rm -f exchange*.log
GLIBC_TUNABLES=glibc.malloc.hugetlb=1 setarch -R "$BIN" "$@"
analysis=$(python3 "$REPO/scripts/perf-analysis.py" --skip-first $((requests / 10)))
echo "$analysis"

# count mean p50 p90 p99 p99.9 max
stats=$(awk '$1 == "Exchange_MatchingEngine_processClientRequest" {print $2","$3","$4","$5","$6","$7","$8}' <<< "$analysis")
if [[ -z $stats ]]; then
    echo "no processClientRequest line in the analysis, nothing recorded" >&2
    exit 1
fi
[[ -f $RESULTS ]] || echo "date,commit,dirty,changes,options,count,mean,p50,p90,p99,p99.9,max" > "$RESULTS"
echo "$(date '+%Y-%m-%d %H:%M'),$commit,$dirty,$changes,\"$*\",$stats" >> "$RESULTS"
echo "recorded in $RESULTS"
