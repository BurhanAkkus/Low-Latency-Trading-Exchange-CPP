#!/usr/bin/env bash
# A/B test: runs two variants of the benchmark in alternation and compares them.
#
# A variant is a git ref, optionally followed by benchmark options, quoted as one argument. Each
# distinct commit is unpacked with git archive and built under AB_DIR (Release, the CMake
# project's default), so the working tree isn't touched and every binary comes from a commit.
# Builds are reused.
#
# Runs alternate A, B, A, B, ... for RUNS rounds. Before each run it waits at least 2 s and until
# the CPU package is back within 2 C of its idle temperature (at most 60 s), so every run starts
# from the same thermal state. Each run's start temperature, huge page coverage, ME_req stats and
# throughput go to a CSV in ab-results/, then scripts/ab-compare.py prints the comparison.
#
# Usage:   ab-test.sh <variant A> <variant B>
#          ab-test.sh main ab/03-power-of-two
#          ab-test.sh "main --consumer-loop-ns 2000" "ab/09-publish-per-message --consumer-loop-ns 2000"
# Env:     RUNS    rounds (default 5)
#          AB_DIR  builds and the engine's log (default /tmp/me-ab)
#          OUT     CSV to write (default ab-results/<date>_<A>_vs_<B>.csv next to this script)
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
RUNS=${RUNS:-5}
AB_DIR=${AB_DIR:-/tmp/me-ab}

if [[ $# -ne 2 ]]; then
    sed -n '/^# Usage:/,/^# Env:/p' "$0" | sed '$d' >&2
    exit 1
fi
read -r -a A <<< "$1"
read -r -a B <<< "$2"

# Builds the commit of a ref once, prints the binary's path.
build() {
    local sha dir
    sha=$(git -C "$REPO" rev-parse --short=12 "$1^{commit}")
    dir=$AB_DIR/build/$sha
    if [[ ! -x $dir/matching_engine_benchmark ]]; then
        echo "building $1 ($sha)" >&2
        rm -rf "$dir"
        mkdir -p "$dir/src"
        git -C "$REPO" archive "$sha" | tar -x -C "$dir/src"
        cmake -S "$dir/src/exchange/matching-engine" -B "$dir/build" > /dev/null
        cmake --build "$dir/build" -j --target matching_engine_benchmark > /dev/null
        cp "$dir/build/matching_engine_benchmark" "$dir/"
    fi
    echo "$dir/matching_engine_benchmark"
}
bin_a=$(build "${A[0]}")
bin_b=$(build "${B[0]}")
commit_a=$(git -C "$REPO" rev-parse --short "${A[0]}^{commit}")
commit_b=$(git -C "$REPO" rev-parse --short "${B[0]}^{commit}")

label() { tr ' /' '_-' <<< "$*" | tr -s '_'; }
OUT=${OUT:-$HERE/ab-results/$(date '+%Y-%m-%d_%H%M')_$(label "$1")_vs_$(label "$2").csv}
mkdir -p "$(dirname "$OUT")"

for z in /sys/class/thermal/thermal_zone*; do
    [[ $(cat "$z/type") == x86_pkg_temp ]] && zone=$z/temp
done
temp() { echo $(($(cat "$zone") / 1000)); }
# Idle: once the temperature stops falling (the builds heat the package), at most 60 s.
idle=$(temp)
for _ in $(seq 20); do
    sleep 3
    now=$(temp)
    ((now >= idle)) && break
    idle=$now
done
echo "idle package temperature: $idle C"

echo "run,variant,label,commit,start_temp,huge_pct,count,mean,p50,p90,p99,p99.9,max,ns_per_request" > "$OUT"
mkdir -p "$AB_DIR/run"
cd "$AB_DIR/run"
for ((i = 1; i <= RUNS; i++)); do
    for v in A B; do
        if [[ $v == A ]]; then bin=$bin_a; commit=$commit_a; spec=("${A[@]}"); arg=$1
        else bin=$bin_b; commit=$commit_b; spec=("${B[@]}"); arg=$2; fi
        sleep 2
        for _ in $(seq 116); do
            (($(temp) <= idle + 2)) && break
            sleep 0.5
        done
        start_temp=$(temp)
        rm -f exchange*.log
        GLIBC_TUNABLES=glibc.malloc.hugetlb=1 setarch -R "$bin" "${spec[@]:1}" > benchmark.out 2>&1 &
        pid=$!
        # Peak share of the process's anonymous memory on 2 MB pages.
        huge=0; anon=0
        while kill -0 "$pid" 2> /dev/null; do
            if r=$(cat "/proc/$pid/smaps_rollup" 2> /dev/null); then
                h=$(awk '/^AnonHugePages:/ {print $2}' <<< "$r")
                a=$(awk '/^Anonymous:/ {print $2}' <<< "$r")
                if [[ -n $h && -n $a ]] && ((h >= huge)); then huge=$h; anon=$a; fi
            fi
            sleep 0.2
        done
        wait "$pid" || { echo "run $i $v failed, see $AB_DIR/run/benchmark.out" >&2; exit 1; }
        throughput=$(awk '/^throughput:/ {print $(NF-1)}' benchmark.out)
        requests=$(sed -n 's/^requests:\([0-9]*\).*/\1/p' benchmark.out)
        stats=$(python3 "$REPO/scripts/perf-analysis.py" --skip-first $((requests / 10)) |
                awk '$1 == "ME_req" {print $2","$3","$4","$5","$6","$7","$8}')
        pct=$(awk -v h="$huge" -v a="$anon" 'BEGIN {printf "%.1f", a ? 100 * h / a : 0}')
        echo "$i,$v,\"$arg\",$commit,$start_temp,$pct,$stats,$throughput" | tee -a "$OUT"
    done
done
echo "recorded in $OUT"
python3 "$REPO/scripts/ab-compare.py" "$OUT"
