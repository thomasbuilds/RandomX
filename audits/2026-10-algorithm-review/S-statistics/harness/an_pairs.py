"""Chi-square test of independence and mutual information for the byte pairs recorded by rxs_pairs.
usage: python3 -I an_pairs.py pairs_v1.bin pairs_v2.bin"""
import sys, os, math
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from an_common import chi2_sf, entropy_bits

for fn in sys.argv[1:]:
    names = [l.rstrip("\n").split("\t", 1)[1] for l in open(fn + ".txt")]
    h = np.fromfile(fn, dtype=np.uint64).reshape(len(names), 256, 256).astype(np.float64)
    print(f"== {fn}")
    for p, nm in enumerate(names):
        t = h[p]
        n = t.sum()
        r = t.sum(axis=1, keepdims=True)
        c = t.sum(axis=0, keepdims=True)
        e = r @ c / n
        m = e > 0
        x2 = float(((t[m] - e[m]) ** 2 / e[m]).sum())
        df = (int((r > 0).sum()) - 1) * (int((c > 0).sum()) - 1)
        pv = chi2_sf(x2, df) if df > 0 else float('nan')
        pij = t[t > 0] / n
        pi = (r / n).ravel(); pj = (c / n).ravel()
        mi = float((t[t > 0] / n * np.log2((t[t > 0] / n) / (np.outer(pi, pj)[t > 0]))).sum())
        bias = df / (2 * n * math.log(2))
        print(f"  [{p}] {nm}: N={int(n)} chi2={x2:.0f} df={df} p={pv:.3g}; MI={mi:.5f} bits (bias ~{bias:.5f}); "
              f"H(a)={entropy_bits(r):.4f} H(b)={entropy_bits(c):.4f}")
