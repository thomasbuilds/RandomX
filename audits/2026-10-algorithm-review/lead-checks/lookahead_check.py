#!/usr/bin/env python3
"""Lead check of ALG-3: steady-state time per iteration of the RandomX main loop (spec 4.6.2).

Independent of agent T's lookahead_sim.py. Program i (time T) starts when program i-1 has ended and Dataset item i-1
has been XORed into r0-r7 (step 7 of iteration i-1). The address of item i is fixed at step 5 of iteration i-k
(k = 1 in v1, 2 in v2), i.e. when program i-k ends; the item is available L later (DRAM read in fast mode, item
computation in light mode); any number of items may be in flight. Claim: period = max(T, (T + L) / (k + 1)).
Usage: python3 lookahead_check.py
"""

def simulate(T, L, k, n=30000):
    prog_end = []
    start = 0.0
    item_ready_prev = 0.0                      # item consumed at the end of the previous iteration
    starts = []
    for i in range(n):
        start = max(start, item_ready_prev)
        starts.append(start)
        prog_end.append(start + T)
        addr_time = prog_end[i - k] if i >= k else 0.0   # items 0..k-1 are addressed by the program configuration
        item_ready_prev = max(addr_time + L, prog_end[i])  # step 7 needs item i and program i
        start = prog_end[i]
    a = n // 2 - (n // 2) % 12                 # window length a multiple of 12, so that periodic
    b = a + 12 * ((n - 1 - a) // 12)          # steady states with cycle length k + 1 <= 4 average out
    return (starts[b] - starts[a]) / (b - a)

worst = 0.0
print("| T | L | k | simulated | max(T, (T+L)/(k+1)) |")
print("|---|---|---|---|---|")
for T in (30, 50, 75, 99.5, 129.2):
    for L in (50, 80, 280, 746, 1760):
        for k in (1, 2, 3):
            sim, formula = simulate(T, L, k), max(T, (T + L) / (k + 1))
            worst = max(worst, abs(sim - formula))
            print(f"| {T} | {L} | {k} | {sim:.3f} | {formula:.3f} |")
print(f"\nlargest difference: {worst:.2e} ns over {5 * 5 * 3} cases")
