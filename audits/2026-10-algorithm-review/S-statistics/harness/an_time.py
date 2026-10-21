"""Runtime distribution (Q7), filtering strategy III (design.md Appendix A) and v1/v2 output independence (Q8).
usage: python3 -I an_time.py <rxtime csv> [label]"""
import sys, os, math
import numpy as np
import pandas as pd
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from an_common import chi2_uniform, wilson

PROGS = [f"p{k}" for k in range(8)]


def pct(a, q):
    return float(np.percentile(a, q))


def describe(a, unit=1e6, name=""):
    a = np.asarray(a, dtype=float) / unit
    return (f"{name}: n={len(a)} mean {a.mean():.3f} sd {a.std():.3f} (cv {a.std()/a.mean()*100:.2f}%) p50 {pct(a,50):.3f} "
            f"p90 {pct(a,90):.3f} p99 {pct(a,99):.3f} p99.9 {pct(a,99.9):.3f} max {a.max():.3f} max/mean {a.max()/a.mean():.3f}")


def strategy3(prog_times, overhead_per_nonce, other_per_hash, rejects=(0.10, 0.25, 0.5)):
    """Miner filters the first program of the chain (oracle knowledge of its runtime).
    prog_times: per-program times (i.i.d. sample); overhead_per_nonce: cost paid for every candidate nonce
    (Blake2b + Scratchpad fill + first program generation); other_per_hash: final AesHash+Blake2b."""
    t = np.sort(np.asarray(prog_times, dtype=float))
    ET = t.mean()
    honest = overhead_per_nonce + 8 * ET + other_per_hash
    out = []
    for r in rejects:
        q = 1 - r
        acc = t[: max(1, int(len(t) * q))]
        Eacc = acc.mean()
        # expected candidates per accepted first program = 1/q, each costing the per-nonce overhead
        cost = overhead_per_nonce / q + Eacc + 7 * ET + other_per_hash
        out.append((r, ET / Eacc, honest / cost, honest / (Eacc + 7 * ET + other_per_hash + overhead_per_nonce)))
    # best possible rejection fraction
    best = None
    for r in np.linspace(0.0, 0.95, 96):
        q = 1 - r
        acc = t[: max(1, int(len(t) * q))]
        cost = overhead_per_nonce / q + acc.mean() + 7 * ET + other_per_hash
        s = honest / cost
        if best is None or s > best[1]:
            best = (r, s)
    return out, best


def main(fn, label=""):
    df = pd.read_csv(fn)
    print(f"=== {fn} {label}: {df.input.nunique()} inputs ===")
    res = {}
    for v in (1, 2):
        d = df[df.version == v]
        print(f"-- v{v}")
        print("  " + describe(d.total, 1e6, "hash time [ms]"))
        allp = d[PROGS].values.ravel()
        print("  " + describe(allp, 1e3, "program time [us] (all 8)"))
        for k in (0, 7):
            print("  " + describe(d[f"p{k}"], 1e3, f"program {k} time [us]"))
        print(f"  fill {d.fill.mean()/1e3:.1f} us, final {d.final.mean()/1e3:.1f} us, mean sum of programs {d[PROGS].sum(axis=1).mean()/1e3:.1f} us")
        out, best = strategy3(allp, d.fill.mean(), d.final.mean())
        for r, boost, speed, speed_nofill in out:
            print(f"  strategy III reject {r*100:.0f}% slowest first programs (oracle): first-program speedup x{boost:.4f}; "
                  f"hashrate x{speed:.4f} (x{speed_nofill:.4f} if rejected nonces were free)")
        print(f"  strategy III best rejection fraction {best[0]*100:.0f}%: hashrate x{best[1]:.4f}")
        res[v] = d
    # v1 / v2 relation
    a = df[df.version == 1].sort_values("input")
    b = df[df.version == 2].sort_values("input")
    if len(a) == len(b) and len(a) > 0:
        ha = np.array([np.frombuffer(bytes.fromhex(h), dtype=np.uint8) for h in a.hash])
        hb = np.array([np.frombuffer(bytes.fromhex(h), dtype=np.uint8) for h in b.hash])
        bits_a = np.unpackbits(ha, axis=1)
        bits_b = np.unpackbits(hb, axis=1)
        agree = (bits_a == bits_b).sum()
        n = bits_a.size
        z = (agree - n / 2) / math.sqrt(n / 4)
        print(f"-- v1/v2 hash independence over {len(a)} inputs: bit agreement {agree/n:.5f} (z={z:.2f}); "
              f"equal hashes: {(ha == hb).all(axis=1).sum()}")
        per_pos = (bits_a == bits_b).mean(axis=0)
        zp = (per_pos - 0.5) / math.sqrt(0.25 / len(a))
        print(f"   per-bit-position agreement: max |z| {np.abs(zp).max():.2f} over 256 positions")
        x = ha ^ hb
        xs, p = chi2_uniform(np.bincount(x.ravel(), minlength=256))
        print(f"   XOR of hashes byte distribution chi2 {xs:.1f} (df 255) p={p:.3g}")
        # correlation of leading 64-bit words as uniform reals
        ua = np.array([int.from_bytes(bytes(r[:8]), 'little') for r in ha], dtype=np.float64) / 2**64
        ub = np.array([int.from_bytes(bytes(r[:8]), 'little') for r in hb], dtype=np.float64) / 2**64
        cc = np.corrcoef(ua, ub)[0, 1]
        print(f"   Pearson correlation of leading 64-bit words: {cc:.4f} (1/sqrt(n) = {1/math.sqrt(len(a)):.4f})")
        # joint 'below target' test for a few difficulties
        for t in (2 ** -2, 2 ** -4, 2 ** -6):
            ja = ua < t
            jb = ub < t
            both = (ja & jb).sum()
            exp = len(a) * t * t
            print(f"   P(v1<T and v2<T) for T=2^{int(math.log2(t))}: {both} observed vs {exp:.1f} expected")
    # per-hash time correlation between versions (is a slow input slow in both?)
    if len(a) == len(b) and len(a) > 0:
        cc = np.corrcoef(a.total.values, b.total.values)[0, 1]
        print(f"-- correlation of per-input hash times v1 vs v2: {cc:.3f}")
    return res


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else "")
