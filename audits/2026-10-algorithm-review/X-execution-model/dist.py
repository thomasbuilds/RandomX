#!/usr/bin/env python3
"""Agent X addendum: distribution of the distance (iterations) between a scratchpad write and its first read
(log2 buckets from DIST lines; bucket b holds distances d with 2^(b-1) <= d < 2^b, b=0: d=0)."""
import sys
agg = {}
for fn in sys.argv[1:]:
    for line in open(fn):
        if not line.startswith('DIST '):
            continue
        f = line.split()
        v = f[2]
        for tok in f[4:]:
            kind, rest = tok.split(':')
            vals = [float(x) for x in rest.split(',')]
            a = agg.setdefault((v, kind), [0.0] * len(vals))
            for i, x in enumerate(vals):
                a[i] += x
for (v, kind), h in sorted(agg.items()):
    tot = sum(h)
    cum = 0; q = {}
    for b, x in enumerate(h):
        cum += x
        for p in (0.1, 0.25, 0.5, 0.75, 0.9):
            if p not in q and cum >= p * tot:
                q[p] = (0 if b == 0 else 2 ** (b - 1), 2 ** b - 1)
    within1 = h[0] + h[1]
    print(f'{v} {kind}: n(read before end)={tot:.0f}; read within 1 iteration {100 * within1 / tot:.2f}%; ' +
          ', '.join(f'p{int(p * 100)} in [{lo},{hi}]' for p, (lo, hi) in q.items()))
