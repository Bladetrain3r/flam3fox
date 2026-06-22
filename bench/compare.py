#!/usr/bin/env python3
"""Compare two binary PPM (P6) images and report difference metrics.

Pure standard library (no PIL/numpy) so it runs anywhere. The fractal flame
chaos game is stochastic and floating-point accumulation order is not stable
across optimizations (e.g. per-thread buckets, -march=native, SIMD), so we
compare against tolerances rather than requiring a bit-exact match.

Exit code 0 if all metrics are within tolerance, 1 otherwise (2 on error).
"""
import sys
import argparse


def read_ppm(path):
    """Read a binary PPM (P6). Returns (width, height, bytes)."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] != b"P6":
        raise ValueError(f"{path}: not a binary PPM (P6)")

    # Parse header: P6, width, height, maxval -- whitespace separated,
    # '#' comments allowed until end of line.
    pos = 2
    fields = []
    while len(fields) < 3:
        # skip whitespace
        while pos < len(data) and data[pos:pos + 1].isspace():
            pos += 1
        # skip comment line
        if data[pos:pos + 1] == b"#":
            while pos < len(data) and data[pos:pos + 1] != b"\n":
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(int(data[start:pos]))
    width, height, maxval = fields
    pos += 1  # single whitespace after maxval
    if maxval != 255:
        raise ValueError(f"{path}: only 8-bit PPM supported (maxval={maxval})")
    pixels = data[pos:pos + width * height * 3]
    if len(pixels) != width * height * 3:
        raise ValueError(f"{path}: truncated pixel data")
    return width, height, pixels


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("image_a")
    ap.add_argument("image_b")
    ap.add_argument("--max-abs", type=float, default=16.0,
                    help="fail if max per-channel abs diff exceeds this (0-255)")
    ap.add_argument("--mean-abs", type=float, default=1.0,
                    help="fail if mean per-channel abs diff exceeds this (0-255)")
    ap.add_argument("--pct-thresh", type=float, default=8.0,
                    help="a channel value 'differs' if abs diff exceeds this")
    ap.add_argument("--max-pct", type=float, default=2.0,
                    help="fail if more than this %% of channels differ")
    ap.add_argument("-q", "--quiet", action="store_true")
    args = ap.parse_args()

    try:
        wa, ha, a = read_ppm(args.image_a)
        wb, hb, b = read_ppm(args.image_b)
    except (OSError, ValueError) as e:
        print(f"compare: error: {e}", file=sys.stderr)
        return 2

    if (wa, ha) != (wb, hb):
        print(f"compare: dimensions differ: {wa}x{ha} vs {wb}x{hb}",
              file=sys.stderr)
        return 1

    n = len(a)
    max_abs = 0
    sum_abs = 0
    sum_sq = 0
    over = 0
    thr = args.pct_thresh
    for i in range(n):
        d = a[i] - b[i]
        if d < 0:
            d = -d
        sum_abs += d
        sum_sq += d * d
        if d > max_abs:
            max_abs = d
        if d > thr:
            over += 1

    mean_abs = sum_abs / n
    rms = (sum_sq / n) ** 0.5
    pct = 100.0 * over / n

    ok = (max_abs <= args.max_abs and
          mean_abs <= args.mean_abs and
          pct <= args.max_pct)

    if not args.quiet:
        status = "PASS" if ok else "FAIL"
        print(f"[{status}] {wa}x{ha}  "
              f"max_abs={max_abs} (<= {args.max_abs:g})  "
              f"mean_abs={mean_abs:.4f} (<= {args.mean_abs:g})  "
              f"rms={rms:.4f}  "
              f"differing={pct:.4f}% (<= {args.max_pct:g}%)")

    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
