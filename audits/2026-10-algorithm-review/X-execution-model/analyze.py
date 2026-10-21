#!/usr/bin/env python3
"""Agent X: aggregate xsim output (PROG/LIVE/DIST/HASH/TOTAL lines) into the tables used in the report.
usage: python3 -I analyze.py out/mainA.txt out/mainB.txt [...]"""
import sys, re, math, statistics as st
from collections import defaultdict

def parse(files):
    progs = []          # dicts
    live = []
    totals = {}
    hashes = []
    for fn in files:
        cur_total = None
        for line in open(fn):
            line = line.rstrip('\n')
            if line.startswith('PROG '):
                f = line.split(' ')
                d = {'tag': f[1], 'v': int(f[2][1]), 'h': int(f[3][2:]), 'p': int(f[4][2:])}
                for tok in f[5:]:
                    if ':' in tok:
                        name, rest = tok.split(':', 1)
                        for kv in rest.split(','):
                            k, val = kv.split('=')
                            d[name + '.' + k] = float(val)
                    elif '=' in tok:
                        k, val = tok.split('=')
                        d[k] = float(val)
                progs.append(d)
            elif line.startswith('LIVE '):
                f = line.split(' ')
                d = {'v': int(f[2][1]), 'h': f[3]}
                for tok in f[4:]:
                    kind, rest = tok.split(':', 1)
                    for kv in rest.split(','):
                        k, val = kv.split('=')
                        d[kind + '.' + k] = float(val)
                live.append(d)
            elif line.startswith('HASH '):
                hashes.append(line)
            elif line.startswith('TOTAL '):
                v = int(line.split()[1][1])
                cur_total = totals.setdefault(v, defaultdict(float))
                cur_total['iterations'] += float(line.split('=')[1])
            elif cur_total is not None and line.startswith('  '):
                m = re.match(r'\s+VM (\S+)\s+([\d.]+) per iteration', line)
                if m:
                    cur_total['vm.' + m.group(1) + '.sum'] += float(m.group(2))
                    cur_total['vm.' + m.group(1) + '.n'] += 1
                m = re.match(r'\s+UOP (\S+)\s+([\d.]+) per iteration', line)
                if m:
                    cur_total['uop.' + m.group(1) + '.sum'] += float(m.group(2))
                    cur_total['uop.' + m.group(1) + '.n'] += 1
                m = re.match(r'\s+slots ([\d.]+) per iteration', line)
                if m:
                    cur_total['slots.sum'] += float(m.group(1)); cur_total['slots.n'] += 1
                for key, rx in [('taken', r'taken CBRANCH ([\d.]+) per iteration \(exec ([\d.]+)\), re-executed VM instr ([\d.]+) per iteration \(([\d.]+) per taken\)'),
                                ('cfr', r'CFROUND exec ([\d.]+) open ([\d.]+) fprc-changes ([\d.]+)'),
                                ('mload', r'M-loads L1 ([\d.]+) L2 ([\d.]+) L3 ([\d.]+)'),
                                ('istore', r'ISTORE L1 ([\d.]+) L2 ([\d.]+) L3 ([\d.]+)'),
                                ('raw', r'loads with RAW .* ([\d.]+) per iteration, of which .* ([\d.]+)$')]:
                    m = re.search(rx, line)
                    if m:
                        for i, g in enumerate(m.groups()):
                            cur_total[f'{key}{i}.sum'] += float(g)
                        cur_total[f'{key}.n'] += 1
    return progs, live, totals, hashes

def mean_ci(xs):
    n = len(xs)
    if n == 0:
        return float('nan'), float('nan'), 0
    m = sum(xs) / n
    if n < 2:
        return m, float('nan'), n
    s = st.stdev(xs)
    return m, 1.96 * s / math.sqrt(n), n

def col(progs, v, key):
    return [d[key] for d in progs if d['v'] == v and key in d]

def fmt(m, ci):
    if math.isnan(ci):
        return f'{m:.1f}'
    return f'{m:.1f} ± {ci:.1f}'

def main():
    files = sys.argv[1:]
    progs, live, totals, hashes = parse(files)
    nv = {v: len([d for d in progs if d['v'] == v]) for v in (1, 2)}
    print(f'# programs: v1 {nv[1]}, v2 {nv[2]}; hashes {len(hashes)}; mismatches {sum("MISMATCH" in h for h in hashes)}')
    keys = sorted({k for d in progs for k in d if '.' in k})
    names = []
    for k in keys:
        n = k.split('.')[0]
        if n not in names:
            names.append(n)
    # ---- dataflow models
    print('\n## Dataflow (unbounded resources): cycles per iteration (mean ± 95% CI over programs)')
    print('| model | metric | v1 | v2 | v2/v1 |')
    print('|---|---|---|---|---|')
    for n in names:
        if not (n[0] in 'AZCS' and n[0:3] != 'smt'):
            continue
        for metric in ['rec', 'isoCP', 'isoRec', 'isoFp', 'lag', 'maxlag']:
            k = f'{n}.{metric}'
            a, b = col(progs, 1, k), col(progs, 2, k)
            if not a or not b:
                continue
            ma, ca, _ = mean_ci(a); mb, cb, _ = mean_ci(b)
            r = mb / ma if ma else float('nan')
            print(f'| {n} | {metric} | {fmt(ma, ca)} | {fmt(mb, cb)} | {r:.3f} |')
    # ---- uops and ILP
    print('\n## Work per iteration and available parallelism')
    for v in (1, 2):
        u = [d['uops'] / 2048 for d in progs if d['v'] == v]
        s = [d['slots'] / 2048 for d in progs if d['v'] == v]
        print(f'v{v}: uops/iter {mean_ci(u)[0]:.1f}, x86 slots/iter {mean_ci(s)[0]:.1f}')
        for n in ['A0', 'Z0', 'C0', 'S0']:
            k = f'{n}.rec'
            ilp = [d['uops'] / 2048 / d[k] for d in progs if d['v'] == v and k in d]
            vmilp = [(256 if v == 1 else 384) * (1 + d['reexec'] / 2048 / (256 if v == 1 else 384)) / d[k] for d in progs if d['v'] == v and k in d]
            if ilp:
                print(f'   {n}: uops per cycle {mean_ci(ilp)[0]:.2f}, VM instructions per cycle {mean_ci(vmilp)[0]:.2f}')
    # ---- OoO / SMT
    print('\n## Out-of-order models: cycles per iteration (single thread: per iteration; smt: core cycles per thread-iteration)')
    print('| config | v1 | v2 | v2/v1 | v1 ROB-stall | v2 ROB-stall | v1 DS-wait | v2 DS-wait |')
    print('|---|---|---|---|---|---|---|---|')
    for n in names:
        if n[0] in 'AZCS' and not n.startswith('smt'):
            continue
        if n.startswith('smt'):
            k = f'{n}.core'
        else:
            k = f'{n}.it'
        a, b = col(progs, 1, k), col(progs, 2, k)
        if not a or not b:
            continue
        ma, ca, _ = mean_ci(a); mb, cb, _ = mean_ci(b)
        extra = ''
        if not n.startswith('smt'):
            ra, rb = mean_ci(col(progs, 1, n + '.robw'))[0], mean_ci(col(progs, 2, n + '.robw'))[0]
            da, db = mean_ci(col(progs, 1, n + '.dsw'))[0], mean_ci(col(progs, 2, n + '.dsw'))[0]
            extra = f' {ra:.1f} | {rb:.1f} | {da:.1f} | {db:.1f} |'
        else:
            extra = ' | | | |'
        print(f'| {n} | {fmt(ma, ca)} | {fmt(mb, cb)} | {mb / ma:.3f} |{extra}')
    # ---- branches and CFROUND
    print('\n## Branches, CFROUND, memory per iteration (from TOTAL lines)')
    for v in sorted(totals):
        t = totals[v]
        def g(key):
            return t[key + '.sum'] / max(1, t[key.split('.')[0][:-1] + '.n'] if False else 1)
        n = t['taken.n']
        print(f'v{v}: taken CBRANCH/iter {t["taken0.sum"]/n:.4f} (exec {t["taken1.sum"]/n:.2f}), re-executed VM instr/iter {t["taken2.sum"]/n:.3f}, per taken {t["taken3.sum"]/n:.2f}; '
              f'CFROUND exec {t["cfr0.sum"]/n:.3f}, LDMXCSR (gate open) {t["cfr1.sum"]/n:.4f}, fprc changes {t["cfr2.sum"]/n:.4f}; '
              f'M-loads L1/L2/L3 {t["mload0.sum"]/n:.2f}/{t["mload1.sum"]/n:.2f}/{t["mload2.sum"]/n:.2f}; ISTORE L1/L2/L3 {t["istore0.sum"]/n:.2f}/{t["istore1.sum"]/n:.2f}/{t["istore2.sum"]/n:.2f}; '
              f'RAW-through-memory loads {t["raw0.sum"]/n:.2f} (recent {t["raw1.sum"]/n:.3f})')
        vm = {k[3:-4]: t[k] / t['vm.' + k[3:-4] + '.n'] for k in t if k.startswith('vm.') and k.endswith('.sum')}
        uop = {k[4:-4]: t[k] / t['uop.' + k[4:-4] + '.n'] for k in t if k.startswith('uop.') and k.endswith('.sum')}
        print('   VM/iter: ' + ', '.join(f'{k} {x:.2f}' for k, x in vm.items()))
        print('   uop/iter: ' + ', '.join(f'{k} {x:.2f}' for k, x in uop.items()))
    # ---- per-program runtime distribution
    print('\n## Per-program runtime distribution (coefficient of variation over programs, %)')
    for k in ['A0.rec', 'Z0.rec', 'C0.rec', 'z4r320.it', 'z5m75.it', 'smt_z5m75.core', 'Z5g75.rec', 'C1g100.rec']:
        for v in (1, 2):
            xs = col(progs, v, k)
            if len(xs) > 2:
                m = sum(xs) / len(xs); s = st.stdev(xs)
                print(f'  {k} v{v}: mean {m:.1f}, sd {s:.1f}, CV {100 * s / m:.1f}% (n={len(xs)})')
    # ---- liveness
    print('\n## Scratchpad liveness (first reader of each written block/word, summed over hashes)')
    for v in (1, 2):
        agg = defaultdict(float)
        for d in live:
            if d['v'] == v:
                for k, x in d.items():
                    if '.' in k:
                        agg[k] += x
        for kind in ['IBLK', 'FBLK', 'FQ', 'ISTORE']:
            tot = sum(agg[f'{kind}.{r}'] for r in ['blk0', 'blk1', 'mL1', 'mL2', 'mL3', 'final', 'dead'])
            if tot == 0:
                continue
            parts = ', '.join(f'{r} {100 * agg[f"{kind}.{r}"] / tot:.3f}%' for r in ['blk0', 'blk1', 'mL1', 'mL2', 'mL3', 'final', 'dead'])
            print(f'  v{v} {kind} (n={tot:.0f}): {parts}; dead count {agg[f"{kind}.dead"]:.0f}')

if __name__ == '__main__':
    main()
