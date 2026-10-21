"""Analysis of `rxs infl` output (Q3). usage: python3 -I an_infl.py infl_v1.csv infl_v2.csv"""
import sys, os
import numpy as np
import pandas as pd
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from an_common import wilson, fmt_ci

CLS = ["input", "spad", "R_start", "F_start", "E_start", "R_end", "F_end", "E_end"]


def per_bit_table(sub, nbits, label):
    rows = []
    for b in range(nbits):
        s = sub[sub.bit == b]
        if len(s) == 0:
            continue
        k = int((s.outcome == 0).sum())
        p, lo, hi = wilson(k, len(s))
        rows.append((b, k, len(s), p, lo, hi))
    rows = pd.DataFrame(rows, columns=["bit", "unchanged", "n", "p", "lo", "hi"])
    sig = rows[rows.lo > 0.0]
    print(f"    {label}: bits with P(unchanged) CI excluding 0: {len(sig)}/{len(rows)}")
    if len(sig):
        top = sig.sort_values("p", ascending=False).head(12)
        print("      top: " + "; ".join(f"bit{int(r.bit)}={r.p:.2f}[{r.lo:.2f},{r.hi:.2f}] n={int(r.n)}" for r in top.itertuples()))
    return rows


def main(files):
    allres = {}
    for fn in files:
        df = pd.read_csv(fn)
        ver = int(df.version.iloc[0])
        print(f"\n================ v{ver}: {fn} ({len(df)} trials, {df.input.nunique()} inputs) ================")
        for c in CLS:
            s = df[df.cls == c]
            if len(s) == 0:
                continue
            k = int((s.outcome == 0).sum())
            hd = s[s.hd > 0].hd
            hds = f"HD n={len(hd)} mean {hd.mean():.2f} sd {hd.std():.2f} min {hd.min()} max {hd.max()}" if len(hd) else "HD n=0"
            print(f"  {c:8s}: P(output unchanged) = {fmt_ci(k, len(s))}; mean programs run {s.programsRun.mean():.2f}; {hds}")
            allres[(ver, c)] = (k, len(s))
        # per-bit views
        print("  per-bit-position analysis (positions aggregated over registers/lanes):")
        for c, nb in (("R_start", 64), ("F_start", 64), ("E_start", 64), ("R_end", 64)):
            s = df[df.cls == c]
            if len(s): per_bit_table(s, nb, c)
        for c in ("F_end", "E_end"):
            s = df[df.cls == c].copy()
            if len(s):
                s["bit"] = s.bit % 64
                per_bit_table(s, 64, c + " (bit within lane)")
        # E_end by register index: e3 is the last AES round key in v2
        s = df[df.cls == "E_end"]
        if len(s):
            print("  E_end by register: " + "; ".join(f"e{r}: {fmt_ci(int((s[s.reg == r].outcome == 0).sum()), int((s.reg == r).sum()))}" for r in range(4)))
        s = df[df.cls == "F_end"]
        if len(s):
            print("  F_end by register: " + "; ".join(f"f{r}: {fmt_ci(int((s[s.reg == r].outcome == 0).sum()), int((s.reg == r).sum()))}" for r in range(4)))
        # F_start by int32 half (bits 0-31 low int, 32-63 high int) and bit groups
        for c in ("F_start", "E_start"):
            s = df[df.cls == c]
            if len(s):
                for lo, hi in ((0, 8), (8, 16), (16, 24), (24, 31), (31, 32), (32, 40), (40, 48), (48, 56), (56, 63), (63, 64)):
                    t = s[(s.bit >= lo) & (s.bit < hi)]
                    print(f"    {c} source-word bits {lo}-{hi-1}: {fmt_ci(int((t.outcome == 0).sum()), len(t))}")
        # scratchpad flips by region and first event
        s = df[df.cls == "spad"]
        if len(s):
            region = np.where(s.reg < 16384, "L1", np.where(s.reg < 262144, "L2only", "L3only"))
            for r in ("L1", "L2only", "L3only"):
                t = s[region == r]
                print(f"  spad {r}: P(unchanged) {fmt_ci(int((t.outcome == 0).sum()), len(t))}")
            for ev, nm in ((0, "untouched by the end"), (1, "read first"), (2, "overwritten first")):
                t = s[s.watchEvent == ev]
                print(f"  spad first event = {nm}: n={len(t)}, P(unchanged) {fmt_ci(int((t.outcome == 0).sum()), len(t))}")
            for k in range(8):
                t = s[s.k == k]
                print(f"    spad flip before program {k}: P(unchanged) {fmt_ci(int((t.outcome == 0).sum()), len(t))}")
        # R_start by bit groups
        s = df[df.cls == "R_start"]
        if len(s):
            for lo, hi in ((0, 8), (8, 24), (24, 56), (56, 63), (63, 64)):
                t = s[(s.bit >= lo) & (s.bit < hi)]
                print(f"    R_start bits {lo}-{hi-1}: {fmt_ci(int((t.outcome == 0).sum()), len(t))}")
    return allres


if __name__ == "__main__":
    main(sys.argv[1:])
