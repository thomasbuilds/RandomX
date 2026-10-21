#!/usr/bin/env python3
"""Lead check of ALG-5: the "Relative work/Joule" column of doc/design_v2.md equals the Hash/Joule ratio x 1.5294.

1.5294 = 6,815,744 / 4,456,448, the VM instructions plus AES rounds per hash counted by design_v2.md
(v1: 8 x 2048 x 256 + 262,144; v2: 8 x 2048 x 384 + 524,288).
Usage: python3 design_v2_hashj.py [path/to/doc/design_v2.md]   (default: doc/design_v2.md)
"""
import re, sys
path = sys.argv[1] if len(sys.argv) > 1 else "doc/design_v2.md"
factor = (8 * 2048 * 384 + 524288) / (8 * 2048 * 256 + 262144)
cpu, rows, lower = None, 0, 0
for line in open(path, encoding="utf-8"):
    if line.startswith("### "):
        cpu, v1 = line[4:].strip(), None
    m = re.match(r"RandomX (v[12])\|[^|]*\|[^|]*\|([0-9.]+)\|[^|]*\|[^|]*\|\**([0-9.]+)%", line)
    if not m:
        continue
    if m.group(1) == "v1":
        v1 = float(m.group(2))
        continue
    hj = float(m.group(2)) / v1
    rows += 1
    lower += hj < 1
    print(f"{cpu}: hash/J v2/v1 {hj * 100:6.2f} %  x{factor:.4f} = {hj * factor * 100:6.2f} %  published {m.group(3)} %")
print(f"\n{rows} configurations; v2 has lower hash/J in {lower}")
