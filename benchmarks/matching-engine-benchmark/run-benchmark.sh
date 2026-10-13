#!/usr/bin/env bash
# Runs the matching engine benchmark with a fixed memory layout, then analyses the log.
#
# 2 MB pages for large allocations (GLIBC_TUNABLES) and no address randomisation (setarch -R)
# give every run the same cache placement. Without them ~1 in 3 runs is ~40 ns slower at p50,
# see results.md. Both only apply to the benchmark process.
#
# Usage:   run-benchmark.sh [benchmark options]
#          run-benchmark.sh --price-min 101 --price-max 110
# Env:     BENCH_DIR  directory for the engine's log (default /tmp/me-bench)
#          BENCH_BIN  benchmark binary (default the Release build)
set -euo pipefail

REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
BIN=${BENCH_BIN:-$REPO/matching-engine/cmake-build-release/matching_engine_benchmark}
DIR=${BENCH_DIR:-/tmp/me-bench}

if [[ ! -x $BIN ]]; then
    echo "no benchmark binary at $BIN, build it first (see README.md)" >&2
    exit 1
fi

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
python3 "$REPO/scripts/perf-analysis.py" --skip-first $((requests / 10))
