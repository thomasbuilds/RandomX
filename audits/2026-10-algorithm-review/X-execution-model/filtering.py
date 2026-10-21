#!/usr/bin/env python3
"""Agent X, task 6: per-program runtime distributions from the models and a re-evaluation of design.md Appendix A
(program filtering with N = 8 chained programs), v1 vs v2.
usage: python3 -I filtering.py out/mainA.txt out/mainB.txt [...]"""
import sys, math, statistics as st
sys.path.insert(0, '.')
from analyze import parse

def app_a_formula(Q, N=8):
    # design.md Appendix A, strategy II: the first program is filtered for free; after executing program i the next
    # one is rejected with probability 1-Q, wasting i executions. Programs executed per valid result:
    if N == 1:
        return 1.0
    w = sum(i * Q ** (i - 1) * (1 - Q) for i in range(1, N))      # expected waste per attempt (2.5 for Q=0.75, N=8)
    p = Q ** (N - 1)                                              # attempt succeeds
    return N + (1 / p - 1) * w / (1 - p)

def strategy3(samples, Q=0.75, N=8, overhead=0.0):
    """Oracle filter on the first program of the chain only: accept the fastest fraction Q.
    overhead = cost of producing a new first program (Hash512 + 2 MiB scratchpad fill + generation) in units of one
    average program execution. Returns the speed relative to the honest miner."""
    xs = sorted(samples)
    mu = sum(xs) / len(xs)
    k = max(1, int(round(Q * len(xs))))
    acc = sum(xs[:k]) / k
    honest = overhead * mu + N * mu
    filtered = overhead * mu / Q + acc + (N - 1) * mu
    return honest / filtered, 1 - acc / mu

def main():
    progs, live, totals, hashes = parse(sys.argv[1:])
    print('## Appendix A formula check (design.md: N(II) for Q = 0.75)')
    for N in (1, 2, 4, 8):
        n2 = app_a_formula(0.75, N)
        print(f'  N={N}: programs per result (strategy II) = {n2:.2f}; speed with 1.5x faster execution = {1.5 * N / n2:.2f}')
    keys = [('Z0.rec', 'dataflow, Zen-4 latencies, memory hidden'),
            ('Z5g75.rec', 'dataflow, Zen-4 latencies, 5 GHz, DRAM 75 ns'),
            ('C0.rec', 'dataflow, ASIC latencies, memory hidden'),
            ('C1g100.rec', 'dataflow, ASIC latencies, 1 GHz, DRAM 100 ns'),
            ('z4r320.it', 'OoO Zen 4 (ROB 320), memory hidden'),
            ('z5m75.it', 'OoO Zen 5, 5.2 GHz, DRAM 75 ns'),
            ('smt_z5m75.t1', 'OoO Zen 5 SMT (per thread), DRAM 75 ns'),
            ('skxm90.it', 'OoO Skylake-SP host model, DRAM 90 ns'),
            ('A0.rec', 'Appendix B model (all ops 1 cycle)')]
    print('\n## Per-program runtime distribution and strategy III (oracle rejection of the slowest 25% of first programs)')
    print('overhead = cost of a new first program (fill + generation) in units of one mean program execution')
    print('| model | v | n | mean | CV % | first-program gain % | speed N=8, overhead 0 | overhead 0.2 | N=1, overhead 0 |')
    print('|---|---|---|---|---|---|---|---|---|')
    for k, label in keys:
        for v in (1, 2):
            xs = [d[k] for d in progs if d['v'] == v and k in d]
            if len(xs) < 4:
                continue
            m = sum(xs) / len(xs); s = st.stdev(xs)
            sp0, g = strategy3(xs, 0.75, 8, 0.0)
            sp2, _ = strategy3(xs, 0.75, 8, 0.2)
            sp1, _ = strategy3(xs, 0.75, 1, 0.0)
            print(f'| {label} | v{v} | {len(xs)} | {m:.1f} | {100 * s / m:.1f} | {100 * g:.1f} | {sp0:.4f} | {sp2:.4f} | {sp1:.4f} |')
    # which program features explain the runtime?
    print('\n## Correlation of per-program runtime with program features (Pearson r)')
    feats = ['nIMULH', 'nFDIV', 'nFSQRT', 'nCBR', 'nCFR', 'taken', 'cfopen']
    for k in ['Z0.rec', 'C0.rec', 'z5m75.it']:
        for v in (1, 2):
            rows = [d for d in progs if d['v'] == v and k in d]
            if len(rows) < 6:
                continue
            ys = [d[k] for d in rows]
            out = []
            for f in feats:
                xs = [d[f] for d in rows]
                if st.pstdev(xs) == 0:
                    continue
                r = st.correlation(xs, ys)
                out.append(f'{f} {r:+.2f}')
            print(f'  {k} v{v}: ' + ', '.join(out))

if __name__ == '__main__':
    main()
