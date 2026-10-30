#!/usr/bin/env python3
"""Compares the two variants of an A/B csv written by benchmarks/matching-engine-benchmark/ab-test.sh.

For p50, p90, p99, p99.9 and throughput: each variant's median and range, the difference of the
medians (B - A) and a permutation test on the difference of the means. The p-value is the share
of all splits of the runs into two groups of the original sizes whose means differ at least as
much as the measured ones: exact up to 100,000 splits, 100,000 random splits above that.
With 5 runs each the smallest possible p-value is 2/252 = 0.008.

Outliers: a run whose throughput is more than 5% from its variant's median is flagged. The
benchmark has a two-mode effect (see results.md) that moves a whole run by 10-20%, so the
p-values are printed again without the flagged runs.

Usage:
    python3 scripts/ab-compare.py benchmarks/matching-engine-benchmark/ab-results/<file>.csv
"""
import csv
import itertools
import math
import random
import statistics
import sys

METRICS = ("p50", "p90", "p99", "p99.9", "ns_per_request")
OUTLIER = 0.05
MAX_EXACT = 100_000


def permutation_p(x, y):
    pool, k = x + y, len(x)
    observed = abs(statistics.mean(y) - statistics.mean(x)) - 1e-9
    total = sum(pool)

    def differs(group):
        mean_x = sum(group) / k
        mean_y = (total - sum(group)) / (len(pool) - k)
        return abs(mean_y - mean_x) >= observed

    if math.comb(len(pool), k) <= MAX_EXACT:
        splits = list(itertools.combinations(pool, k))
        return sum(differs(s) for s in splits) / len(splits)
    rng = random.Random(1)
    return sum(differs(rng.sample(pool, k)) for _ in range(MAX_EXACT)) / MAX_EXACT


def table(groups, title):
    a, b = groups["A"], groups["B"]
    print(f"\n{title}: A {len(a)} runs, B {len(b)} runs")
    print(f"{'metric':<16}{'A median [range]':>22}{'B median [range]':>22}{'B - A':>9}{'p':>8}")
    for m in METRICS:
        x = [float(r[m]) for r in a]
        y = [float(r[m]) for r in b]
        cells = [f"{statistics.median(v):.1f} [{min(v):.0f}-{max(v):.0f}]" for v in (x, y)]
        diff = statistics.median(y) - statistics.median(x)
        print(f"{m:<16}{cells[0]:>22}{cells[1]:>22}{diff:>+9.1f}{permutation_p(x, y):>8.3f}")


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    rows = list(csv.DictReader(open(sys.argv[1])))
    groups = {v: [r for r in rows if r["variant"] == v] for v in ("A", "B")}
    for v, g in groups.items():
        if not g:
            sys.exit(f"no runs of variant {v} in {sys.argv[1]}")
        print(f"{v}: {g[0]['label']} ({g[0]['commit']})")

    table(groups, "All runs")

    flagged = {}
    for v, g in groups.items():
        median = statistics.median(float(r["ns_per_request"]) for r in g)
        flagged[v] = [r for r in g if abs(float(r["ns_per_request"]) / median - 1) > OUTLIER]
    if any(flagged.values()):
        print(f"\nOutliers, throughput more than {OUTLIER:.0%} from the variant's median:")
        for v, g in flagged.items():
            for r in g:
                print(f"  {v} run {r['run']}: {r['ns_per_request']} ns/request")
        kept = {v: [r for r in g if r not in flagged[v]] for v, g in groups.items()}
        if all(len(g) >= 3 for g in kept.values()):
            table(kept, "Without outliers")
        else:
            print("  fewer than 3 runs left in a variant, no comparison without them")


if __name__ == "__main__":
    main()
