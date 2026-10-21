#!/usr/bin/env python3
"""Agent X, task 5: explicit energy / area-time model of a specialised RandomX implementation vs CPUs, v1 vs v2.
Operation counts per iteration come from the measured dynamic traces (TOTAL lines of xsim output);
per-operation energies are stated assumptions (7 nm-class logic, see report for sources) with low/central/high values.
usage: python3 -I energy.py out/mainA.txt out/mainB.txt"""
import sys, itertools
sys.path.insert(0, '.')
from analyze import parse

ITER_PER_HASH = 8 * 2048
FIXED_AES = {1: 131072 + 131072 + 8 + 8 * 34 * 16, 2: 131072 + 131072 + 8 + 8 * 50 * 16}  # fill + fingerprint + program generation

# per-operation energy in pJ: (low, central, high). Values for a 7 nm-class ASIC datapath.
E = {
    'int':   (0.03, 0.05, 0.1),   # 64-bit add/sub/xor/rotate/neg/swap (datapath only)
    'mul':   (1.0, 2.0, 4.0),     # 64x64 -> 64 multiply
    'mulh':  (1.5, 3.0, 6.0),     # 64x64 -> 128 (high half)
    'fadd':  (1.0, 2.0, 4.0),     # 2 x FP64 add/sub
    'fmul':  (2.5, 5.0, 10.0),    # 2 x FP64 multiply
    'fdiv':  (10., 20., 40.),     # 2 x FP64 divide
    'fsqrt': (10., 20., 40.),     # 2 x FP64 square root
    'flog':  (0.05, 0.1, 0.2),    # FSCAL/FSWAP/masking
    'aes':   (1.0, 2.0, 4.0),     # one 128-bit AES round
    'sp1':   (0.5, 1.0, 2.0),     # 8-byte access to the 16 KiB level
    'sp2':   (2.0, 4.0, 8.0),     # 8-byte access to the 256 KiB level
    'sp3':   (5.0, 10., 20.),     # 8-byte access to the 2 MiB level
    'blk':   (20., 40., 80.),     # 64-byte access to the 2 MiB level (steps 2, 3, 9, 11)
    'ovh':   (1.0, 3.0, 10.),     # per VM instruction: fetch/decode, register file, operand network, pipeline, clock
    'dram':  (2000., 5000., 15000.),  # one random 64-byte Dataset read (activate + burst + I/O)
}

# measured CPU data from doc/design_v2.md: (name, hash/J v1, hash/J v2, H/s v1, H/s v2)
CPUS = [
    ('Ryzen 9 9950X @285W (Zen 5)', 95.38, 94.01, 27186.1, 26791.7),
    ('Ryzen 9 9950X @100W (Zen 5)', 199.122, 173.462, 19912.2, 17346.2),
    ('Ryzen AI 9 HX 370 @28W (Zen 5)', 235.61, 254.35, 6597.15, 7121.69),
    ('Ryzen AI 9 365 @28W (Zen 5)', 217.5, 237.5, 6091, 6649),
    ('Ryzen 9 7945HX @62W (Zen 4)', 260.1, 246.9, 16126, 15308),
    ('Ryzen 5 8600G @45W (Zen 4)', 130.59, 119.45, 5876.47, 5375.29),
    ('Ryzen 9 5950X @122-126W (Zen 3)', 117.03, 105.78, 14745.9, 12905.3),
    ('Ryzen 9 3950X @131W (Zen 2)', 114.88, 105.87, 15049.34, 13868.64),
    ('Ryzen 7 3700X @88W (Zen 2)', 98, 83.65, 8624, 7361),
    ('Ryzen 7 1700X @95W (Zen 1)', 50.87, 51.27, 4832.73, 4870.41),
    ('Core i9-12900K @125W', 69.16, 66.49, 8644.47, 8310.78),
    ('Core i7-8650U @15W', 122.08, 94.33, 1831.15, 1415),
    ('Core i7-6820HQ @45W', 43.75, 33.07, 1968.56, 1488.25),
]

def counts(totals, v):
    t = totals[v]
    n = t['taken.n']
    vm = {k[3:-4]: t[k] / t['vm.' + k[3:-4] + '.n'] for k in t if k.startswith('vm.') and k.endswith('.sum')}
    c = {}
    c['vm'] = sum(vm.values())
    c['int'] = sum(vm[k] for k in ['IADD_RS', 'IADD_M', 'ISUB_R', 'ISUB_M', 'IXOR_R', 'IXOR_M', 'INEG_R', 'IROR_R', 'IROL_R', 'ISWAP_R', 'CBRANCH', 'CFROUND', 'ISTORE'])
    c['mul'] = vm['IMUL_R'] + vm['IMUL_M'] + vm['IMUL_RCP']
    c['mulh'] = vm['IMULH_R'] + vm['IMULH_M'] + vm['ISMULH_R'] + vm['ISMULH_M']
    c['fadd'] = vm['FADD_R'] + vm['FADD_M'] + vm['FSUB_R'] + vm['FSUB_M']
    c['fmul'] = vm['FMUL_R']
    c['fdiv'] = vm['FDIV_M']
    c['fsqrt'] = vm['FSQRT_R']
    c['flog'] = vm['FSCAL_R'] + vm['FSWAP_R'] + vm['FDIV_M']
    c['sp1'] = t['mload0.sum'] / n + t['istore0.sum'] / n
    c['sp2'] = t['mload1.sum'] / n + t['istore1.sum'] / n
    c['sp3'] = t['mload2.sum'] / n + t['istore2.sum'] / n
    c['blk'] = 4.0
    c['aes_loop'] = 16.0 if v == 2 else 0.0
    c['dram'] = 1.0
    return c

def asic_energy(c, v, pick):
    """energy per hash in microjoules, split into parts; pick(name) -> energy in pJ"""
    parts = {}
    parts['VM ops'] = sum(c[k] * pick(k) for k in ['int', 'mul', 'mulh', 'fadd', 'fmul', 'fdiv', 'fsqrt', 'flog']) * ITER_PER_HASH
    parts['VM overhead'] = c['vm'] * pick('ovh') * ITER_PER_HASH
    parts['scratchpad words'] = (c['sp1'] * pick('sp1') + c['sp2'] * pick('sp2') + c['sp3'] * pick('sp3')) * ITER_PER_HASH
    parts['scratchpad blocks'] = c['blk'] * pick('blk') * ITER_PER_HASH
    parts['AES main loop'] = c['aes_loop'] * pick('aes') * ITER_PER_HASH
    parts['AES fill/hash/gen'] = FIXED_AES[v] * pick('aes') + 2 * 2 ** 21 / 8 * pick('sp3') / 8 * 8  # + writing/reading 2 MiB once each
    parts['DRAM'] = c['dram'] * pick('dram') * ITER_PER_HASH
    return {k: x / 1e6 for k, x in parts.items()}  # pJ -> uJ

def main():
    progs, live, totals, hashes = parse(sys.argv[1:])
    c1, c2 = counts(totals, 1), counts(totals, 2)
    print('## Operation counts per iteration (measured, dynamic, incl. re-executed instructions)')
    print('| class | v1 | v2 | v2/v1 |')
    print('|---|---|---|---|')
    for k in ['vm', 'int', 'mul', 'mulh', 'fadd', 'fmul', 'fdiv', 'fsqrt', 'flog', 'sp1', 'sp2', 'sp3', 'blk', 'aes_loop', 'dram']:
        r = c2[k] / c1[k] if c1[k] else float('inf')
        print(f'| {k} | {c1[k]:.2f} | {c2[k]:.2f} | {r:.3f} |')
    print(f'AES rounds per hash: v1 {FIXED_AES[1]}, v2 {FIXED_AES[2] + 16 * ITER_PER_HASH}')
    print()
    print('## ASIC energy per hash (uJ), central assumptions')
    pc = lambda k: E[k][1]
    e1, e2 = asic_energy(c1, 1, pc), asic_energy(c2, 2, pc)
    print('| part | v1 | v2 |')
    print('|---|---|---|')
    for k in e1:
        print(f'| {k} | {e1[k]:.1f} | {e2[k]:.1f} |')
    s1, s2 = sum(e1.values()), sum(e2.values())
    print(f'| total | {s1:.1f} | {s2:.1f} |')
    print(f'ASIC energy ratio v2/v1 (central) = {s2 / s1:.3f}')
    print()
    # sensitivity: vary the three dominant groups independently
    print('## Sensitivity of the ASIC v2/v1 energy-per-hash ratio')
    print('| DRAM nJ/line | overhead pJ/VM instr | op energies | E_v1 uJ | E_v2 uJ | ratio |')
    print('|---|---|---|---|---|---|')
    ratios = []
    for di, oi, xi in itertools.product(range(3), range(3), range(3)):
        def pick(k, di=di, oi=oi, xi=xi):
            if k == 'dram':
                return E[k][di]
            if k == 'ovh':
                return E[k][oi]
            return E[k][xi]
        a, b = sum(asic_energy(c1, 1, pick).values()), sum(asic_energy(c2, 2, pick).values())
        ratios.append(b / a)
        print(f'| {E["dram"][di] / 1000:.0f} | {E["ovh"][oi]:.0f} | {["low", "central", "high"][xi]} | {a:.0f} | {b:.0f} | {b / a:.3f} |')
    print(f'range of ASIC ratio: {min(ratios):.3f} .. {max(ratios):.3f}')
    # upper bound: DRAM free (ASIC with the Dataset in on-package memory at negligible energy)
    def nodram(k):
        return 0.0 if k == 'dram' else E[k][1]
    a, b = sum(asic_energy(c1, 1, nodram).values()), sum(asic_energy(c2, 2, nodram).values())
    print(f'compute-only (DRAM energy excluded) ratio: {b / a:.3f}')
    print()
    print('## Change of the ASIC/CPU energy-efficiency gap from v1 to v2')
    print('CPU ratio = energy per hash v2/v1 measured by the authors (design_v2.md). Gap change = ASIC ratio / CPU ratio')
    print('(> 1: CPUs gain relative to the ASIC; < 1: the ASIC gains). Claimed "relative work/Joule" shown for comparison.')
    print('| CPU | CPU E/hash v2/v1 | claimed work/J | gap change (ASIC low) | (central) | (high) | (compute-only) |')
    print('|---|---|---|---|---|---|---|')
    lo, hi = min(ratios), max(ratios)
    cen = s2 / s1
    comp = b / a
    work = (6291456 + FIXED_AES[2] + 16 * ITER_PER_HASH) / (4194304 + FIXED_AES[1])
    for name, hj1, hj2, h1, h2 in CPUS:
        cr = hj1 / hj2
        claimed = work * hj2 / hj1
        print(f'| {name} | {cr:.3f} | {claimed:.3f} | {lo / cr:.3f} | {cen / cr:.3f} | {hi / cr:.3f} | {comp / cr:.3f} |')
    print(f'(VM+AES work per hash v2/v1 = {work:.4f})')
    print()
    # ---- area-time model: SRAM-dominated latency-bound instances
    print('## Area-time per hash for a latency-bound specialised instance (2 MiB SRAM per VM instance)')
    print('T_iter from the dataflow model with the optimistic ASIC latencies (C*), mean over programs:')
    import statistics as st
    def m(key, v):
        xs = [d[key] for d in progs if d['v'] == v and key in d]
        return sum(xs) / len(xs) if xs else float('nan')
    print('| regime | v1 cycles/iter | v2 cycles/iter | AT ratio v2/v1 (SRAM-dominated) | with core area 30% of instance |')
    print('|---|---|---|---|---|')
    for key, label in [('C0.rec', 'memory latency hidden'), ('C1g50.rec', '1 GHz, DRAM 50 ns'), ('C1g75.rec', '1 GHz, DRAM 75 ns'),
                       ('C1g100.rec', '1 GHz, DRAM 100 ns'), ('C2g50.rec', '2 GHz, DRAM 50 ns'), ('C2g100.rec', '2 GHz, DRAM 100 ns')]:
        t1, t2 = m(key, 1), m(key, 2)
        # area: SRAM fixed; core area grows with the extra AES unit only (~5% of core) -> 0.7 + 0.3*1.05
        print(f'| {label} | {t1:.1f} | {t2:.1f} | {t2 / t1:.3f} | {t2 / t1 * (0.7 + 0.3 * 1.05):.3f} |')

if __name__ == '__main__':
    main()
