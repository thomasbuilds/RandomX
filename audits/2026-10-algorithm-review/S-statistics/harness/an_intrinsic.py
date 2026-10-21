"""Program-dependent (intrinsic) runtime variance: dispersion of the 8 program times within each hash,
which removes slow host-level drift common to the whole hash. Strategy III recomputed on that distribution.
usage: python3 -I an_intrinsic.py data/time_fast7.csv"""
import sys, numpy as np, pandas as pd
df = pd.read_csv(sys.argv[1])
P = [f"p{k}" for k in range(8)]
for v in (1, 2):
    d = df[df.version == v]
    x = d[P[1:]].values.astype(float)            # programs 1..7 (program 0 has a systematic offset)
    rel = x / x.mean(axis=1, keepdims=True)      # within-hash normalisation
    cv_within = rel.std(ddof=1) * np.sqrt(7 / 6)  # correct for normalising by a 7-sample mean
    T = rel.ravel() * x.mean()                    # intrinsic-like distribution in us
    t = np.sort(T); ET = t.mean()
    fill, fin = d.fill.mean(), d.final.mean()
    honest = fill + 8 * ET + fin
    print(f"v{v}: within-hash per-program cv = {cv_within*100:.2f}% (raw cv {x.std()/x.mean()*100:.2f}%); "
          f"p0/mean(p1..p7) = {(d.p0 / d[P[1:]].mean(axis=1)).mean():.4f}")
    for r in (0.1, 0.25, 0.5):
        acc = t[: int(len(t) * (1 - r))]
        boost = ET / acc.mean()
        cost = fill / (1 - r) + acc.mean() + 7 * ET + fin
        free = acc.mean() + 7 * ET + fin + fill
        print(f"   reject {r*100:.0f}%: first-program speedup x{boost:.4f}; hashrate x{honest/cost:.4f} (x{honest/free:.4f} if rejected nonces were free)")
    best = max(((r, honest / (fill / (1 - r) + t[: max(1, int(len(t) * (1 - r)))].mean() + 7 * ET + fin)) for r in np.linspace(0, 0.9, 91)), key=lambda z: z[1])
    print(f"   best rejection {best[0]*100:.0f}%: hashrate x{best[1]:.4f}; per-nonce overhead (Blake2b+fill+gen) = {fill/honest*100:.2f}% of a hash")
