#!/usr/bin/env python3
"""Benchmark runner stub for on-device performance captures (Phase 1+).

Pulls cl_showfps-style frame counters out of a logcat capture and summarizes
them (avg/p95/p99 fps) into logs/device-tests/.

Usage: python3 bench_summary.py logcat-....txt
"""
import re
import sys
from pathlib import Path

FPS_RE = re.compile(r'(\d+(?:\.\d+)?)\s*fps', re.IGNORECASE)


def summarize(path: str) -> None:
    text = Path(path).read_text(errors='ignore')
    samples = [float(m.group(1)) for m in FPS_RE.finditer(text)]
    if not samples:
        print(f'{path}: no fps samples found')
        return
    samples.sort()
    n = len(samples)
    pct = lambda p: samples[min(n - 1, int(n * p))]
    print(f'{path}: n={n} avg={sum(samples)/n:.1f} p50={pct(0.50):.1f} '
          f'p95={pct(0.95):.1f} p99={pct(0.99):.1f} min={samples[0]:.1f} max={samples[-1]:.1f}')


if __name__ == '__main__':
    for p in sys.argv[1:]:
        summarize(p)
