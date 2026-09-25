#!/usr/bin/env python3
"""Generate the A27 workload: size mix + short vs long lines."""

import os
import random

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "workload")
Q = 4096


def fill_short_lines(path, nbytes, lo=60, hi=80):
    rng = random.Random(path)
    written = 0
    with open(path, "w", newline="\n") as f:
        while written < nbytes:
            length = rng.randint(lo, hi)
            remaining = nbytes - written
            if remaining <= 1:
                f.write("\n")
                written += 1
                break
            body = min(length, remaining - 1)
            f.write(("x" * body) + "\n")
            written += body + 1


def fill_long_lines(path, line_len, n_lines):
    with open(path, "w", newline="\n") as f:
        for _ in range(n_lines):
            f.write(("L" * line_len) + "\n")


def main():
    os.makedirs(OUT, exist_ok=True)
    fill_short_lines(os.path.join(OUT, "small.txt"), 1024)
    fill_short_lines(os.path.join(OUT, "medium.txt"), 30 * 1024)
    fill_short_lines(os.path.join(OUT, "large.txt"), 150 * 1024)
    # Lines longer than Q so RR forfeits (A14) and DRR waits on deficit.
    fill_long_lines(os.path.join(OUT, "longline.txt"), 20000, 8)

    print("Wrote workload/ with Q reference", Q)
    for name in ("small.txt", "medium.txt", "large.txt", "longline.txt"):
        path = os.path.join(OUT, name)
        print(f"  {name}: {os.path.getsize(path)} bytes")


if __name__ == "__main__":
    main()
