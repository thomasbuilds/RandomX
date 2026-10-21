#!/usr/bin/env python3
"""Parse the benchmark tables of doc/design_v2.md and report, per configuration, the v2/v1 ratios of
hashrate, hashes per joule and the derived 'Relative work/Joule' (= H/J ratio x ops_v2/ops_v1).
Run: python3 -I design_v2_tables.py doc/design_v2.md
"""
import re
import sys

OPS_V1, OPS_V2 = 4194304 + 262144, 6291456 + 524288


def main(path):
    text = open(path, encoding="utf-8").read()
    sections = re.split(r"^### ", text, flags=re.M)[1:]
    rows = []
    for sec in sections:
        title = sec.splitlines()[0].strip()
        v1 = re.search(r"^RandomX v1\|([^|]+)\|[^|]*\|([^|]+)\|", sec, flags=re.M)
        v2 = re.search(r"^RandomX v2\|([^|]+)\|[^|]*\|([^|]+)\|", sec, flags=re.M)
        if not (v1 and v2):
            continue
        h1 = float(v1.group(1).split("@")[0]); hj1 = float(v1.group(2))
        h2 = float(v2.group(1).split("@")[0]); hj2 = float(v2.group(2))
        rows.append((title, h2 / h1, hj2 / hj1, hj2 / hj1 * OPS_V2 / OPS_V1))
    down = sum(1 for r in rows if r[2] < 1.0)
    for t, hr, hjr, work in rows:
        print(f"{t[:48]:48s} hashrate {hr:7.2%}  hashes/J {hjr:7.2%}  'work/J' {work:7.2%}")
    print(f"\n{len(rows)} configurations; hashes per joule lower under v2 in {down}, higher in {len(rows) - down}")
    print(f"min/max hashes-per-joule ratio: {min(r[2] for r in rows):.2%} / {max(r[2] for r in rows):.2%}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "doc/design_v2.md")
