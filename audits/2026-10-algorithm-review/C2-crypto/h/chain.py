#!/usr/bin/env python3
# Agent C2, item 7: re-derive design.md Appendix A (chained programs) and evaluate program filtering for v1 and v2.
# Model (as in Appendix A): the first program of a chain can be filtered for free; every later program is
# acceptable with probability Q, independently; a rejected program aborts the chain (all executed programs wasted).

def per_attempt(N, Q):
    """expected programs executed per attempt and success probability"""
    waste = sum(k * Q ** (k - 1) * (1 - Q) for k in range(1, N))   # fail when program k+1 is rejected
    return waste + N * Q ** (N - 1), Q ** (N - 1), waste

def programs_per_result(N, Q):
    e, p, _ = per_attempt(N, Q)
    return e / p

print("Appendix A re-derivation (Q = 0.75, strategy II speed-up 1.5, strategy III first-program boost 1.05)")
print(" N   N(I)  N(II)   N(III)  Speed(I) Speed(II) Speed(III)   wasted/attempt(II)")
for N in (1, 2, 4, 8):
    nII = programs_per_result(N, 0.75)
    _, _, waste = per_attempt(N, 0.75)
    sII = N / nII * 1.5
    sIII = N / ((N - 1) + 1 / 1.05)
    print(f"{N:2d} {N:5d} {nII:6.2f} {N:6d}   {1.0:6.2f}   {sII:6.3f}    {sIII:6.3f}        {waste:.3f}")
Q = 0.75
w = (1 - Q) * sum((k + 1) * Q ** k for k in range(7))
print(f"design.md formula (1-Q)*(1+2Q+...+7Q^6) at Q=0.75: {w:.4f} (design.md: 'approximately 2.5')")

# Feature-omission filtering: hardware that cannot execute instruction X can only run programs without X.
# P(no X in a program) = (1 - f/256)^size; size 256 (v1) or 384 (v2). Chain success needs programs 2..8 to qualify.
freq = {  # instruction frequencies per 256 opcodes (specs.md Tables 5.2.1, 5.3.1, 5.4.1, 5.5.1)
    "IMULH_M": 1, "ISMULH_M": 1, "CFROUND": 1, "INEG_R": 2, "IROL_R": 2,
    "IMULH_R": 4, "ISMULH_R": 4, "IMUL_M": 4, "ISWAP_R": 4, "FSWAP_R": 4, "FDIV_M": 4,
    "FSCAL_R": 6, "FSQRT_R": 6, "IMUL_RCP": 8, "IROR_R": 8, "CBRANCH": 25,
}
print("\nFeature omission: Q = P(program has no X); speed of a miner that is s=1.5x faster on qualifying programs, N = 8")
print(f"{'X':10s} {'f/256':>5s} {'Q v1':>9s} {'Q v2':>9s} {'speed v1':>9s} {'speed v2':>9s}")
for name, f in sorted(freq.items(), key=lambda kv: kv[1]):
    q1 = (1 - f / 256) ** 256
    q2 = (1 - f / 256) ** 384
    s1 = 8 / programs_per_result(8, q1) * 1.5
    s2 = 8 / programs_per_result(8, q2) * 1.5
    print(f"{name:10s} {f:5d} {q1:9.4f} {q2:9.4f} {s1:9.4f} {s2:9.4f}")
# break-even Q for strategy II at N=8 with speed-up s: find Q with 8/programs_per_result * s = 1
for s in (1.5, 2.0, 4.0, 10.0):
    lo, hi = 0.0, 1.0
    for _ in range(100):
        mid = (lo + hi) / 2
        if 8 / programs_per_result(8, mid) * s >= 1: hi = mid
        else: lo = mid
    print(f"break-even Q at N=8 for speed-up {s}: {hi:.4f}")
