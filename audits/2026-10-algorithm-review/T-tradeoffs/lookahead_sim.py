#!/usr/bin/env python3
"""Event simulation of the RandomX main loop dependency structure (spec 4.6.2) for one hash instance.
Program i starts when program i-1 has finished AND item i-1 has been XORed into r0-r7 (step 7).
The address of item i is fixed at step 5 of iteration i-k (k=1 in v1, k=2 in v2), i.e. right after program i-k,
before item i-k is consumed. Item latency L (DRAM read in fast mode, item computation in light mode).
Unlimited items in flight. Prints the steady-state period and compares with max(T,(T+L)/(k+1)) and max(T,L/k)."""
def period(T, L, k, n=20000):
    S, E, R = {}, {}, {}
    for i in range(n):
        start = 0.0 if i == 0 else max(E[i - 1], R.get(i - 1, 0.0))
        S[i] = start; E[i] = start + T
        A = E[i - k] if i - k >= 0 else 0.0       # address of item i known when program i-k ends
        R[i] = A + L
    return (S[n - 1] - S[n // 2]) / (n - 1 - n // 2)
print("| T_prog | L | k | simulated period | max(T,(T+L)/(k+1)) | max(T,L/k) (brief) |")
print("|---|---|---|---|---|---|")
for T, L in ((50, 80), (75, 80), (100, 280), (129, 280), (50, 746), (75, 746), (100, 1760), (129, 1760), (45, 560), (58.5, 560)):
    for k in (1, 2):
        print(f"| {T} | {L} | {k} | {period(T, L, k):.1f} | {max(T, (T + L) / (k + 1)):.1f} | {max(T, L / k):.1f} |")
