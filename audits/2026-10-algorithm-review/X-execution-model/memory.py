#!/usr/bin/env python3
"""Agent X, task 4: Dataset latency overlap. Steady-state recurrence of one VM instance:
   the line read in iteration j+k is requested when program j ends, so
   T_iter = max(CP, (L_mem + CP) / (k + 1)),   k = 1 (v1), 2 (v2)
where CP is the loop-carried critical path per iteration with the Dataset line available (dataflow model).
Validated against the dataflow simulation with explicit Dataset latency (Z5g*, C1g*, C2g*).
usage: python3 -I memory.py out/mainA.txt out/mainB.txt"""
import sys
sys.path.insert(0, '.')
from analyze import parse

def mean(progs, v, key):
    xs = [d[key] for d in progs if d['v'] == v and key in d]
    return sum(xs) / len(xs) if xs else float('nan')

def T(cp, L, k):
    return max(cp, (L + cp) / (k + 1))

def main():
    progs, _, _, _ = parse(sys.argv[1:])
    cpZ = {v: mean(progs, v, 'Z0.rec') for v in (1, 2)}
    cpC = {v: mean(progs, v, 'C0.rec') for v in (1, 2)}
    print('## Validation of the recurrence against the dataflow simulation (cycles per iteration)')
    print('| model | L (cycles) | v1 sim | v1 formula | v2 sim | v2 formula |')
    print('|---|---|---|---|---|---|')
    for key, L, cp in [('Z5g50.rec', 250, cpZ), ('Z5g75.rec', 375, cpZ), ('Z5g100.rec', 500, cpZ),
                       ('C1g50.rec', 50, cpC), ('C1g75.rec', 75, cpC), ('C1g100.rec', 100, cpC), ('C2g50.rec', 100, cpC), ('C2g100.rec', 200, cpC)]:
        print(f'| {key} | {L} | {mean(progs, 1, key):.1f} | {T(cp[1], L, 1):.1f} | {mean(progs, 2, key):.1f} | {T(cp[2], L, 2):.1f} |')
    print(f'\nCP (Zen-4 latencies, dataflow): v1 {cpZ[1]:.1f}, v2 {cpZ[2]:.1f} cycles; CP (ASIC latencies): v1 {cpC[1]:.1f}, v2 {cpC[2]:.1f} cycles')
    print(f'Largest Dataset latency hidden by one instance: L_max = k*CP')
    for f in (3.0, 4.0, 5.0, 5.5):
        print(f'  CPU-class core at {f} GHz: v1 {cpZ[1] / f:.0f} ns, v2 {2 * cpZ[2] / f:.0f} ns')
    for f in (1.0, 1.5, 2.0):
        print(f'  ASIC-class core at {f} GHz: v1 {cpC[1] / f:.0f} ns, v2 {2 * cpC[2] / f:.0f} ns')
    print('\n## Single instance (one thread), CPU-class latencies: time per iteration (ns) and Dataset stall share')
    print('| clock GHz | DRAM ns | v1 ns/iter | v1 stall % | v2 ns/iter | v2 stall % | v2/v1 time per hash |')
    print('|---|---|---|---|---|---|---|')
    for f in (3.0, 4.0, 5.0, 5.5):
        for ns in (50, 75, 100):
            L = ns * f
            t1, t2 = T(cpZ[1], L, 1), T(cpZ[2], L, 2)
            print(f'| {f} | {ns} | {t1 / f:.1f} | {100 * (1 - cpZ[1] / t1):.0f} | {t2 / f:.1f} | {100 * (1 - cpZ[2] / t2):.0f} | {t2 / t1:.3f} |')
    print('\n## Same for an ASIC-class instance (optimistic latencies)')
    print('| clock GHz | DRAM ns | v1 ns/iter | v1 stall % | v2 ns/iter | v2 stall % | v2/v1 |')
    print('|---|---|---|---|---|---|---|')
    for f in (1.0, 1.5, 2.0):
        for ns in (50, 75, 100):
            L = ns * f
            t1, t2 = T(cpC[1], L, 1), T(cpC[2], L, 2)
            print(f'| {f} | {ns} | {t1 / f:.1f} | {100 * (1 - cpC[1] / t1):.0f} | {t2 / f:.1f} | {100 * (1 - cpC[2] / t2):.0f} | {t2 / t1:.3f} |')

if __name__ == '__main__':
    main()
