#!/usr/bin/env python3
"""Parse agent T timing logs: per configuration and version the median / min / max of ms per hash, and the
paired v2/v1 ratio (v1 and v2 measured back to back in the same repetition), median and range.
Usage: python3 -I analyze.py logs/tpart-hw-lp1.txt [more logs]"""
import re, sys, statistics as st

for path in sys.argv[1:]:
    data = {}
    for line in open(path):
        m = re.match(r"rep (\d+) (\S+)\s+v(\d) n=\d+ ([\d.]+) ms/hash", line)
        if m:
            r, cfg, v, ms = int(m.group(1)), m.group(2), int(m.group(3)), float(m.group(4))
        else:
            m = re.match(r"rep (\d+) v(\d) ([\d.]+) ms/hash", line)
            if not m:
                continue
            r, cfg, v, ms = int(m.group(1)), "-", int(m.group(2)), float(m.group(3))
        data.setdefault(cfg, {}).setdefault(v, {})[r] = ms
    print(f"## {path}")
    print("| config | v1 median | v1 min | v2 median | v2 min | v1 spread % | v2 spread % | paired v2/v1 median | paired range | n |")
    print("|---|---|---|---|---|---|---|---|---|---|")
    for cfg, d in data.items():
        a, b = d.get(1, {}), d.get(2, {})
        if not a or not b:
            continue
        va, vb = list(a.values()), list(b.values())
        pr = [b[r] / a[r] for r in a if r in b]
        sp = lambda x: 100 * (max(x) - min(x)) / st.median(x)
        print(f"| {cfg} | {st.median(va):.3f} | {min(va):.3f} | {st.median(vb):.3f} | {min(vb):.3f} | {sp(va):.0f} | {sp(vb):.0f} | "
              f"{st.median(pr):.3f} | {min(pr):.3f}-{max(pr):.3f} | {len(pr)} |")
    print()
