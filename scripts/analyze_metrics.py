#!/usr/bin/env python3
"""Compute A20–A22 / A28 / A29 numbers from a metrics CSV."""

import argparse
import csv
import math
import os
from collections import defaultdict


def nearest_rank(values, p):
    if not values:
        return 0
    values = sorted(values)
    n = len(values)
    index = math.ceil(p / 100.0 * n) - 1
    return values[min(max(index, 0), n - 1)]


def size_class(filename, nbytes):
    name = filename.lower()
    if "small" in name:
        return "small"
    if "medium" in name:
        return "medium"
    if "large" in name:
        return "large"
    if "long" in name:
        return "longline"
    if nbytes <= 2 * 1024:
        return "small"
    if nbytes <= 40 * 1024:
        return "medium"
    return "large"


def load_rows(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def analyze(path):
    rows = load_rows(path)
    if not rows:
        print(f"{path}: no rows")
        return

    waiting = []
    response = []
    arrivals = []
    finishes = []
    forfeited = 0
    a14_proxy_rounds = 0
    by_class = defaultdict(list)
    forfeit_by_class = defaultdict(int)

    for row in rows:
        arrival = int(row["arrival_ns"])
        start = int(row["start_ns"])
        finish = int(row["finish_ns"])
        nbytes = int(row["bytes"])
        forfeited += int(row["forfeited_bytes"])
        wait = max(0, start - arrival)
        resp = max(0, finish - arrival)
        waiting.append(wait)
        response.append(resp)
        arrivals.append(arrival)
        finishes.append(finish)
        klass = size_class(row["filename"], nbytes)
        slowdown = (resp / nbytes) if nbytes else 0
        by_class[klass].append(slowdown)
        forfeit_by_class[klass] += int(row["forfeited_bytes"])
        if int(row["forfeited_bytes"]) == 0 and row["op"] == "GET" and int(row["rounds"]) > 1:
            a14_proxy_rounds += 0

    window = max(finishes) - min(arrivals)
    n = len(rows)
    throughput = n / (window / 1e9) if window else 0

    print(f"file: {path}")
    print(f"N: {n}")
    print(f"waiting_p50_ns: {nearest_rank(waiting, 50)}")
    print(f"waiting_p99_ns: {nearest_rank(waiting, 99)}")
    print(f"throughput_rps: {throughput:.4f}")
    print(f"forfeited_bytes_total: {forfeited}")
    print("normalised_slowdown_ns_per_byte (response/bytes):")
    for klass in ("small", "medium", "large", "longline"):
        vals = by_class.get(klass, [])
        if not vals:
            continue
        print(
            f"  {klass}: n={len(vals)} median={nearest_rank(vals, 50):.2f} "
            f"p99={nearest_rank(vals, 99):.2f} forfeited={forfeit_by_class[klass]}"
        )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", nargs="+")
    args = parser.parse_args()
    for path in args.csv:
        if os.path.isfile(path):
            analyze(path)
            print()


if __name__ == "__main__":
    main()
