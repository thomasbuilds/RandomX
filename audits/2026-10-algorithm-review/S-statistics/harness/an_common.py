"""Shared statistics helpers for agent S analysis (no scipy on this host)."""
import math
import numpy as np


def gammaincc(a, x):
    """Regularized upper incomplete gamma Q(a, x) (Numerical Recipes gammq)."""
    if x < 0 or a <= 0:
        raise ValueError
    if x == 0:
        return 1.0
    gln = math.lgamma(a)
    if x < a + 1.0:
        ap, s, d = a, 1.0 / a, 1.0 / a
        for _ in range(100000):
            ap += 1.0
            d *= x / ap
            s += d
            if abs(d) < abs(s) * 1e-15:
                break
        return max(0.0, 1.0 - s * math.exp(-x + a * math.log(x) - gln))
    b = x + 1.0 - a
    c = 1.0 / 1e-300
    d = 1.0 / b
    h = d
    for i in range(1, 100000):
        an = -i * (i - a)
        b += 2.0
        d = an * d + b
        if abs(d) < 1e-300:
            d = 1e-300
        c = b + an / c
        if abs(c) < 1e-300:
            c = 1e-300
        d = 1.0 / d
        de = d * c
        h *= de
        if abs(de - 1.0) < 1e-15:
            break
    return math.exp(-x + a * math.log(x) - gln) * h


def chi2_sf(x, df):
    return gammaincc(df / 2.0, x / 2.0)


def chi2_uniform(counts):
    """Chi-square statistic and p-value of counts against the uniform distribution."""
    c = np.asarray(counts, dtype=np.float64)
    n = c.sum()
    k = c.size
    e = n / k
    x = float(((c - e) ** 2).sum() / e)
    return x, chi2_sf(x, k - 1)


def entropy_bits(counts):
    c = np.asarray(counts, dtype=np.float64)
    n = c.sum()
    p = c[c > 0] / n
    return float(-(p * np.log2(p)).sum())


def wilson(k, n, z=1.96):
    if n == 0:
        return (float('nan'), float('nan'), float('nan'))
    p = k / n
    den = 1 + z * z / n
    c = (p + z * z / (2 * n)) / den
    h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / den
    return (p, max(0.0, c - h), min(1.0, c + h))


def fmt_ci(k, n, digits=4):
    p, lo, hi = wilson(k, n)
    return f"{p:.{digits}f} [{lo:.{digits}f}, {hi:.{digits}f}] ({k}/{n})"


def bit_bias_from_byte_hist(h):
    """h: (positions, 256) byte histograms -> (positions*8,) P(bit=1), n."""
    h = np.asarray(h, dtype=np.float64)
    n = h[0].sum()
    vals = np.arange(256)
    out = []
    for p in range(h.shape[0]):
        for b in range(8):
            mask = ((vals >> b) & 1).astype(bool)
            out.append(h[p][mask].sum() / n)
    return np.array(out), n
