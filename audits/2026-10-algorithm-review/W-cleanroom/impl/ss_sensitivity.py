"""Chapter 6 sensitivity: for each rule that is only in the source, switch to the natural spec-only reading and
count how many of the 8 generated programs (per key) change, and whether Dataset item 0 changes."""
import sys
import numpy as np
sys.path.insert(0, '.')
import rx_superscalar as ss

ALTS = [
    ('slot7_spec_order', 'Table 6.3.2 lists "IADD_C, IXOR_C": index 0 -> IADD_C (source: IXOR_C first)'),
    ('isub_own_group', 'ISUB_R is its own group for the repetition rule (source: shares IADD_RS group)'),
    ('draw_single', 'a register draw is made even when only one register qualifies'),
    ('draw_byte', 'register choice uses getByte() % n (source: getUInt32() % n)'),
    ('imulh_src_first', '6.3.4 "source operand is selected first" also for IMULH_R/ISMULH_R'),
    ('chain_all_mul', '"cannot be multiplied twice in a row" applied to all 4 multiplications'),
    ('imulh_par_src', 'IMULH/ISMULH repetition rule uses the source register (source: random uint32)'),
    ('iror_u32', 'IROR_C count drawn from getUInt32() (source: getByte())'),
    ('shift_byte', 'IADD_RS shift = byte & 3 (source: mod byte, shift = bits 2-3)'),
    ('restore_cycle', 'cycle counter restored when an instruction is thrown away'),
    ('lookahead1', 'no stall: operand must be ready at the port cycle (source: up to 4 cycles)'),
    ('lookahead_inf', 'stall until an operand is ready (up to 200 cycles)'),
    ('no_r5_rule', 'no special case for IADD_RS when only {r5, x} are ready'),
    ('addr_cpu_latency', 'address register = latest-ready register in the reference-CPU model'),
    ('addr_tie_high', 'address register ties broken towards the highest index'),
    ('rcp_group_rev', 'after IMUL_RCP: odd byte -> group 3 (source: group 0)'),
    ('group_u32', 'random decode group = getUInt32() % 4 (source: getByte() & 3)'),
    ('rcp_rule_first', 'IMUL_RCP rule checked before the multiplication-count rule'),
    ('mulcount_imul_only', 'only IMUL_R counts as a multiplication for the group-4 rule'),
    ('drop_last', 'the instruction scheduled at cycle >= 170 is dropped'),
    ('rcp_no_dep', "IMUL_RCP's imul_rr does not wait for mov_ri"),
]
keys = [(b'test key 000', '../data/cache_testkey000.bin'), (b'test key 001', '../data/cache_testkey001.bin')]
base = {}
caches = {}
for k, c in keys:
    ss.ALT.clear()
    base[k] = ss.superscalar_init(k)
    caches[k] = np.fromfile(c, dtype='<u8')
base_item = {k: ss.make_item_function(base[k], caches[k])(0) for k, _ in keys}
print('%-20s %-11s %-12s %s' % ('alternative', 'progs diff', 'item0 diff', 'description'))
for name, desc in ALTS:
    ss.ALT.clear(); ss.ALT.add(name)
    nd = 0; idiff = 0; first = None
    for k, _ in keys:
        progs = ss.superscalar_init(k)
        for i, (a, b) in enumerate(zip(base[k], progs)):
            if a.instrs != b.instrs or a.address_register != b.address_register:
                nd += 1
        idiff += ss.make_item_function(progs, caches[k])(0) != base_item[k]
    print('%-20s %2d / 16     %d / 2        %s' % (name, nd, idiff, desc), flush=True)
ss.ALT.clear()
