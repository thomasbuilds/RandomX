"""Analysis of `rxs stats` output (Q1, Q2 coverage/final scratchpad, Q4, Q5, Q6).
usage: python3 -I an_stats.py <data prefix v1> <data prefix v2>  (e.g. data/s_v1 data/s_v2)"""
import sys, math, os
import numpy as np
import pandas as pd
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from an_common import chi2_uniform, entropy_bits, wilson, fmt_ci, bit_bias_from_byte_hist, chi2_sf

ITERS = 2048


def load(prefix, name, dtype=np.uint64, shape=None):
    a = np.fromfile(f"{prefix}_{name}.bin", dtype=dtype)
    return a.reshape(shape) if shape else a


def block_report(h, label):
    n = int(h[0].sum())
    chis, ps, ents = [], [], []
    for p in range(64):
        x, pv = chi2_uniform(h[p])
        chis.append(x); ps.append(pv); ents.append(entropy_bits(h[p]))
    chis, ps, ents = np.array(chis), np.array(ps), np.array(ents)
    bits, _ = bit_bias_from_byte_hist(h)
    z = (bits - 0.5) / math.sqrt(0.25 / n)
    tot_x = chis.sum()
    tot_p = chi2_sf(tot_x, 64 * 255)
    print(f"  {label}: N={n} blocks; per-byte chi2 (df=255): min p={ps.min():.3g}, #p<1e-6={int((ps < 1e-6).sum())}/64; "
          f"total chi2={tot_x:.0f} (df={64*255}) p={tot_p:.3g}")
    print(f"    byte entropy: mean {ents.mean():.5f} bits, min {ents.min():.5f} (pos {int(ents.argmin())}); "
          f"sum of deficits (8-H) over 64 bytes = {(8 - ents).sum():.4f} bits/block (plug-in bias ~{64*255/(2*n*math.log(2)):.2e})")
    print(f"    per-bit P(1): max |z| = {np.abs(z).max():.2f} at bit {int(np.abs(z).argmax())}; #|z|>5: {int((np.abs(z) > 5).sum())}/512; "
          f"range [{bits.min():.5f}, {bits.max():.5f}]")
    # per byte position within the 8-byte word (mean over 8 words)
    e8 = ents.reshape(8, 8).mean(axis=0)
    print("    mean entropy by byte position in word (0=LSB..7=MSB): " + " ".join(f"{x:.4f}" for x in e8))
    return ents, bits, z


def exp_hist_report(hist12, label):
    """hist12: counts of the top 12 bits (sign+exponent) of doubles."""
    h = hist12.astype(np.float64)
    n = h.sum()
    sign = h[2048:].sum() / n
    ex = h[:2048] + h[2048:]
    infn = ex[0x7FF]
    zero = ex[0]
    e = np.arange(2048) - 1023
    finite = (np.arange(2048) > 0) & (np.arange(2048) < 0x7FF)
    lo = e[finite & (ex > 0)].min() if (finite & (ex > 0)).any() else None
    hi = e[finite & (ex > 0)].max() if (finite & (ex > 0)).any() else None
    print(f"  {label}: lanes={int(n)}; P(sign=1)={sign:.4f}; exponent range of finite non-zero [{lo}, {hi}] (2^e); "
          f"inf={int(infn)} ({infn/n:.3e}); zero/denormal-exp={int(zero)}; sign+exp entropy {entropy_bits(h):.3f} bits of 12")
    # decade histogram like design.md figure: bins of 10^20
    vals = {}
    for i in range(1, 0x7FF):
        if ex[i] == 0:
            continue
        dec = math.floor((i - 1023) * math.log10(2) / 20) * 20
        vals[dec] = vals.get(dec, 0) + ex[i]
    s = ", ".join(f"1e{d:+d}:{v/n*100:.2f}%" for d, v in sorted(vals.items()))
    print(f"    decade bins (|x|, left edge, width 1e20): {s}; inf: {infn/n*100:.3f}%")


def main(*prefixes):
    res = {}
    for pre in prefixes:
        ver = int(open(pre + "_summary.txt").read().split("version=")[1].split()[0])
        print(f"\n===================== RandomX v{ver} ({pre}) =====================")
        summ = open(pre + "_summary.txt").read()
        print(summ.strip())
        prog = pd.read_csv(pre + "_prog.csv")
        hsh = pd.read_csv(pre + "_hash.csv")
        nh, npg = len(hsh), len(prog)
        print(f"hashes={nh} programs={npg}")
        # ---------------- Q1 blocks
        print("[Q1] Scratchpad blocks written per iteration")
        rh = load(pre, "rhist", shape=(64, 256))
        fh = load(pre, "fhist", shape=(64, 256))
        block_report(rh, "R block (step 9)")
        ents, bits, z = block_report(fh, "F block (step 11)")
        f12 = load(pre, "fblk12")
        print(f"    F-block words: top-12-bit (sign+exponent) field entropy {entropy_bits(f12):.3f} bits of 12; "
              f"chi2 p={chi2_uniform(f12)[1]:.3g}")
        top = np.argsort(f12)[::-1][:6]
        print("    most frequent top-12 values: " + ", ".join(f"0x{t:03x}:{f12[t]/f12.sum()*100:.2f}%" for t in top))
        # ---------------- Q4 FP values
        print("[Q4] FP register values at the end of each iteration (after step 4, before the mix)")
        exp_hist_report(load(pre, "fexp"), "F lanes")
        exp_hist_report(load(pre, "eexp"), "E lanes")
        it = int(summ.split("iters=")[1].split()[0])
        it_einf = int(summ.split("itersEInf=")[1].split()[0])
        print(f"  iterations ending with >=1 infinite E lane: {fmt_ci(it_einf, it, 6)}")
        k = int((prog.eInfIters > 0).sum())
        print(f"  programs with >=1 infinite E value (end of some iteration): {fmt_ci(k, npg)}")
        for kk in range(8):
            sub = prog[prog.k == kk]
            print(f"     k={kk}: {fmt_ci(int((sub.eInfIters > 0).sum()), len(sub))}", end="")
        print()
        flags = prog.mxcsr.values
        for bit, nm in ((0, "IE invalid(NaN)"), (1, "DE denormal operand"), (2, "ZE divide-by-zero"), (3, "OE overflow"), (4, "UE underflow"), (5, "PE inexact")):
            c = int(((flags >> bit) & 1).sum())
            print(f"  programs with MXCSR {nm} flag: {c}/{npg}")
        print(f"  F zero lanes per program>0: {int((prog.fZeroLanes > 0).sum())}/{npg}; E zero lanes per program>0: {int((prog.eZeroLanes > 0).sum())}/{npg}")
        # ---------------- Q5 CFROUND
        print("[Q5] CFROUND and rounding modes")
        iters = npg * ITERS
        cfE, cfW, cfC = prog.cfExec.sum(), prog.cfWrite.sum(), prog.cfChange.sum()
        print(f"  CFROUND executions per iteration {cfE/iters:.4f}, fprc writes per iteration {cfW/iters:.5f} "
              f"(write fraction {cfW/cfE:.4f}), mode changes per iteration {cfC/iters:.5f}")
        print(f"  per hash: executions {cfE/nh:.1f}, writes {cfW/nh:.1f}, changes {cfC/nh:.1f}")
        if cfC > 0:
            print(f"  mean iterations between mode changes: {iters/cfC:.2f}")
        s0 = int((prog.sCfround == 0).sum())
        print(f"  programs with no CFROUND instruction: {fmt_ci(s0, npg)}; static count mean {prog.sCfround.mean():.3f}")
        nochg = int((prog.cfChange == 0).sum())
        print(f"  programs in which the mode never changes: {fmt_ci(nochg, npg)}")
        fp = prog[["fp0", "fp1", "fp2", "fp3"]].sum().values.astype(float)
        print("  FP-op residency by mode (RNE, down, up, zero): " + ", ".join(f"{x/fp.sum()*100:.3f}%" for x in fp))
        a = int(hsh.allRNE.sum())
        print(f"  hashes entirely in roundTiesToEven: {fmt_ci(a, nh, 5)}")
        # analytic: P(all 8 programs leave mode 0 untouched), using per-program fraction with no write of a nonzero mode
        # program-level: mode stays 0 if it starts at 0 and no change occurs
        stay = prog[(prog.modeStart == 0)]
        p_stay = (stay.cfChange == 0).mean() if len(stay) else float('nan')
        print(f"  P(program leaves mode 0 unchanged | starts in 0) = {p_stay:.4f} (n={len(stay)}); ^8 = {p_stay**8:.3e}")
        # ---------------- Q6 CBRANCH
        print("[Q6] CBRANCH")
        m = summ.split("cbranchExec=")[1].split()
        ce = int(m[0]); ct = int(summ.split("cbranchTaken=")[1].split()[0])
        print(f"  taken rate per execution: {fmt_ci(ct, ce, 6)} (1/256 = {1/256:.6f})")
        jp = load(pre, "jumpsPerIter")
        print(f"  jumps per iteration histogram: " + ", ".join(f"{i}:{int(c)}" for i, c in enumerate(jp) if c))
        print(f"  P(iteration has >=1 jump) = {fmt_ci(int(jp[1:].sum()), int(jp.sum()), 5)}; mean jumps/iter {np.dot(np.arange(16), jp)/jp.sum():.4f}")
        rp = load(pre, "reexecPerIter").astype(float)
        mean_re = np.dot(np.arange(4097), rp) / rp.sum()
        cum = np.cumsum(rp) / rp.sum()
        q = lambda x: int(np.searchsorted(cum, x))
        print(f"  re-executed instructions per iteration: mean {mean_re:.3f}; p50={q(0.5)} p90={q(0.9)} p99={q(0.99)} p99.9={q(0.999)} max={int(np.nonzero(rp)[0].max())}")
        dh = load(pre, "distHist").astype(float)
        nzd = np.nonzero(dh)[0]
        cumd = np.cumsum(dh) / dh.sum()
        print(f"  re-executed instructions per taken jump: mean {np.dot(np.arange(len(dh)), dh)/dh.sum():.2f}, median {int(np.searchsorted(cumd, 0.5))}, p99 {int(np.searchsorted(cumd, 0.99))}, max {int(nzd.max())}")
        chh = load(pre, "consecHist")
        print(f"  consecutive-jump runs of one CBRANCH instance (len 1,2,3,...): " + ", ".join(f"{i}:{int(c)}" for i, c in enumerate(chh) if c)
              + f"; max consecutive over all programs = {int(prog.maxConsec.max())}")
        print(f"  dynamic instructions per program: mean {prog.dynInstr.mean():.0f}, per iteration {prog.dynInstr.mean()/ITERS:.2f}; "
              f"overhead vs static {prog.dynInstr.mean()/ITERS/(256 if ver==1 else 384)*100-100:.2f}%")
        # ---------------- Q2 coverage
        print("[Q2] Scratchpad write coverage per hash (8-byte words written at least once)")
        L1w, L2w, L3w = 2048, 32768 - 2048, 262144 - 32768
        for nm, col, tot in (("L1 (16 KiB)", "written_L1", L1w), ("L2 only (240 KiB)", "written_L2only", L2w), ("L3 only (1792 KiB)", "written_L3only", L3w)):
            fr = hsh[col] / tot
            print(f"  {nm}: written {fr.mean()*100:.3f}% (sd {fr.std()*100:.3f}, min {fr.min()*100:.2f}, max {fr.max()*100:.2f})")
        allw = (hsh.written_L1 + hsh.written_L2only + hsh.written_L3only) / 262144
        allc = (hsh.changed_L1 + hsh.changed_L2only + hsh.changed_L3only) / 262144
        print(f"  whole L3: written {allw.mean()*100:.3f}%; words differing from the initial fill {allc.mean()*100:.3f}%")
        fcw = load(pre, "finalClassWords").astype(float)
        print("  final scratchpad last writer (fill, R block, F block, ISTORE): " + ", ".join(f"{x/fcw.sum()*100:.2f}%" for x in fcw))
        fhh = load(pre, "finalHist", shape=(4, 8, 256))
        for cls, nm in ((0, "fill"), (1, "R block"), (2, "F block"), (3, "ISTORE")):
            ents = [entropy_bits(fhh[cls][p]) for p in range(8)]
            ps = [chi2_uniform(fhh[cls][p])[1] for p in range(8)]
            print(f"    final words last written by {nm}: byte entropy by position " + " ".join(f"{e:.4f}" for e in ents)
                  + f"; min chi2 p={min(ps):.3g}")
        allh = fhh.sum(axis=0)
        ents = [entropy_bits(allh[p]) for p in range(8)]
        ps = [chi2_uniform(allh[p])[1] for p in range(8)]
        tot = allh.sum(axis=0)
        print(f"    whole final scratchpads: byte entropy by position " + " ".join(f"{e:.5f}" for e in ents)
              + f"; min chi2 p={min(ps):.3g}; order-0 entropy (all bytes) {entropy_bits(tot):.6f} bits/byte")
        res[ver] = dict(prog=prog, hsh=hsh)
    return res


if __name__ == "__main__":
    main(*sys.argv[1:])
