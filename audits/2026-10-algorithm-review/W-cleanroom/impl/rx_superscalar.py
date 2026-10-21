"""RandomX clean-room implementation (agent W) -- SuperscalarHash (spec chapter 6) and Dataset items (chapter 7).

Spec chapter 6 does not define the generator precisely enough to reproduce it (gaps G6.* in logs/gaps.md).
Every rule below that is NOT stated in specs.md is marked [SRC] and was taken from src/superscalar.cpp and
src/blake2_generator.cpp (consultations 3-5 in logs/consultations.md).  Rules marked [SPEC] are in the text.
"""
from rx_primitives import BlakeGenerator

M64 = (1 << 64) - 1
RANDOMX_SUPERSCALAR_LATENCY = 170
RANDOMX_CACHE_ACCESSES = 8
SUPERSCALAR_MAX_SIZE = 3 * RANDOMX_SUPERSCALAR_LATENCY + 2          # [SPEC] 6.3 condition 2
CYCLE_MAP_SIZE = RANDOMX_SUPERSCALAR_LATENCY + 4                     # [SRC] port table horizon (cycles 0..173)
LOOK_FORWARD_CYCLES = 4                                              # [SRC] operand stall limit
MAX_THROWAWAY_COUNT = 256                                            # [SRC]

# ------------------------------------------------------------------ 6.2 reference CPU, Table 6.2.1
P0, P1, P5 = 1, 2, 4
P01, P05, P015 = P0 | P1, P0 | P5, P0 | P1 | P5


class MacroOp:
    def __init__(self, name, size, latency=0, uop1=0, uop2=0, dependent=False):
        self.name, self.size, self.latency, self.uop1, self.uop2 = name, size, latency, uop1, uop2
        self.dependent = dependent          # [SRC] IMUL_RCP's imul_rr cannot start before mov_ri's result

    def with_dep(self):
        return MacroOp(self.name, self.size, self.latency, self.uop1, self.uop2, True)


SUB_RR = MacroOp('sub_rr', 3, 1, P015)
XOR_RR = MacroOp('xor_rr', 3, 1, P015)
LEA_SIB = MacroOp('lea_sib', 4, 1, P01)
IMUL_RR = MacroOp('imul_rr', 4, 3, P1)
ROR_RI = MacroOp('ror_ri', 4, 1, P05)
ADD_RI = MacroOp('add_ri', 7, 1, P015)
XOR_RI = MacroOp('xor_ri', 7, 1, P015)
MOV_RR = MacroOp('mov_rr', 3)                     # 0 latency, no port ("eliminated")
MUL_R = MacroOp('mul_r', 3, 4, P1, P5)
IMUL_R1 = MacroOp('imul_r', 3, 4, P1, P5)
MOV_RI = MacroOp('mov_ri', 10, 1, P015)


class Info:
    """Superscalar instruction: macro-ops, and the index of the macro-op that reads src / selects dst /
    produces the result [SRC] (spec 6.1 lists the macro-ops but not which one does what)."""

    def __init__(self, name, ops, result_op=0, dst_op=0, src_op=0):
        self.name, self.ops = name, ops
        self.result_op, self.dst_op, self.src_op = result_op, dst_op, src_op
        self.size = len(ops)


ISUB_R = Info('ISUB_R', [SUB_RR])
IXOR_R = Info('IXOR_R', [XOR_RR])
IADD_RS = Info('IADD_RS', [LEA_SIB])
IMUL_R = Info('IMUL_R', [IMUL_RR])
IROR_C = Info('IROR_C', [ROR_RI], src_op=-1)
IADD_C = Info('IADD_C', [ADD_RI], src_op=-1)       # sizes 7/8/9 behave identically ([SRC]: same group)
IXOR_C = Info('IXOR_C', [XOR_RI], src_op=-1)
IMULH_R = Info('IMULH_R', [MOV_RR, MUL_R, MOV_RR], result_op=1, dst_op=0, src_op=1)    # [SRC] dst chosen first
ISMULH_R = Info('ISMULH_R', [MOV_RR, IMUL_R1, MOV_RR], result_op=1, dst_op=0, src_op=1)
IMUL_RCP = Info('IMUL_RCP', [MOV_RI, IMUL_RR.with_dep()], result_op=1, dst_op=1, src_op=-1)
NOP = Info('NOP', [], src_op=-1)                    # [SRC] "Null" instruction at start / after an abort

MULTIPLICATIONS = {'IMUL_R', 'IMULH_R', 'ISMULH_R', 'IMUL_RCP'}     # [SRC] what counts for the group-4 rule

# Alternative ("spec-only") readings of the [SRC] rules, switched on by name for the sensitivity experiment
# (ss_sensitivity.py).  Empty set = reference behaviour.
ALT = set()
IMULH_R_SRCFIRST = Info('IMULH_R', [MOV_RR, MUL_R, MOV_RR], result_op=1, dst_op=0, src_op=0)
ISMULH_R_SRCFIRST = Info('ISMULH_R', [MOV_RR, IMUL_R1, MOV_RR], result_op=1, dst_op=0, src_op=0)
IMUL_RCP_NODEP = Info('IMUL_RCP', [MOV_RI, IMUL_RR], result_op=1, dst_op=1, src_op=-1)

# ------------------------------------------------------------------ 6.3.1 decoder groups (Table 6.3.1)
DECODE_GROUPS = {0: (4, 8, 4), 1: (7, 3, 3, 3), 2: (3, 7, 3, 3), 3: (4, 9, 3), 4: (4, 4, 4, 4), 5: (3, 3, 10)}


def LFC():
    if 'lookahead1' in ALT:
        return 1
    if 'lookahead_inf' in ALT:
        return 200
    return LOOK_FORWARD_CYCLES


def fetch_next(cur_name, decode_cycle, mul_count, gen):
    """6.3.1 rules, in priority order.  [SRC]: priority order, the draws (getByte()&1 picks group 0 vs 3,
    getByte()&3 picks group 0-3), 'currently processed instruction' = the instruction whose macro-ops were being
    issued when the previous group ended (possibly unfinished, NOP at the start)."""
    if cur_name in ('IMULH_R', 'ISMULH_R'):
        return 5
    if 'rcp_rule_first' in ALT and cur_name == 'IMUL_RCP':
        return 0 if (gen.get_byte() & 1) else 3
    if mul_count <= decode_cycle:                     # [SPEC] "less than or equal to the current decoding cycle"
        return 4
    if cur_name == 'IMUL_RCP':
        if 'rcp_group_rev' in ALT:
            return 3 if (gen.get_byte() & 1) else 0
        return 0 if (gen.get_byte() & 1) else 3       # [SRC] odd byte -> group 0 (4-8-4), even -> group 3 (4-9-3)
    if 'group_u32' in ALT:
        return gen.get_uint32() % 4
    return gen.get_byte() & 3                         # [SRC]


# ------------------------------------------------------------------ 6.3.2 instruction selection (Table 6.3.2)
SLOT_3 = [ISUB_R, IXOR_R]
SLOT_3L = [ISUB_R, IXOR_R, IMULH_R, ISMULH_R]
SLOT_4 = [IROR_C, IADD_RS]
SLOT_7 = [IXOR_C, IADD_C]                             # [SRC] note: IXOR_C first (spec table lists IADD_C first)


def s32(x):
    return x - (1 << 32) if x & 0x80000000 else x


class SInstr:
    def __init__(self):
        self.info = NOP
        self.src = self.dst = -1
        self.mod = 0
        self.imm32 = 0
        self.group = None
        self.group_par = -1
        self.can_reuse = False
        self.group_par_is_source = False

    def create_for_slot(self, gen, slot_size, group, is_last):
        if slot_size == 3:
            self.create(SLOT_3L[gen.get_byte() & 3] if is_last else SLOT_3[gen.get_byte() & 1], gen)
        elif slot_size == 4:
            if group == 4 and not is_last:
                self.create(IMUL_R, gen)              # [SPEC] no draw [SRC]
            else:
                self.create(SLOT_4[gen.get_byte() & 1], gen)
        elif slot_size in (7, 8, 9):
            order = [IADD_C, IXOR_C] if 'slot7_spec_order' in ALT else SLOT_7
            self.create(order[gen.get_byte() & 1], gen)
        elif slot_size == 10:
            self.create(IMUL_RCP, gen)
        else:
            raise AssertionError

    def create(self, info, gen):
        """[SRC] immediates, mod, and the 'operation group' used by the 6.3.4 repetition rule."""
        self.info = info
        self.src = self.dst = -1
        self.can_reuse = self.group_par_is_source = False
        self.mod, self.imm32 = 0, 0
        n = info.name
        if n == 'ISUB_R':
            self.group, self.group_par_is_source = 'IADD_RS', True    # [SRC] ISUB_R shares IADD_RS's group
            if 'isub_own_group' in ALT:
                self.group = 'ISUB_R'
        elif n == 'IXOR_R':
            self.group, self.group_par_is_source = 'IXOR_R', True
        elif n == 'IADD_RS':
            self.mod = gen.get_byte()                                  # [SRC] whole byte; mod.shift = bits 2-3
            if 'shift_byte' in ALT:
                self.mod = (self.mod & 3) << 2
            self.group, self.group_par_is_source = 'IADD_RS', True
        elif n == 'IMUL_R':
            self.group, self.group_par_is_source = 'IMUL_R', True
        elif n == 'IROR_C':
            while True:                                                # [SRC] byte & 63, redraw while 0
                self.imm32 = (gen.get_uint32() if 'iror_u32' in ALT else gen.get_byte()) & 63
                if self.imm32:
                    break
            self.group, self.group_par = 'IROR_C', -1
        elif n == 'IADD_C':
            self.imm32 = gen.get_uint32()
            self.group, self.group_par = 'IADD_C', -1
        elif n == 'IXOR_C':
            self.imm32 = gen.get_uint32()
            self.group, self.group_par = 'IXOR_C', -1
        elif n in ('IMULH_R', 'ISMULH_R'):
            self.can_reuse = True                                      # [SRC] dst may equal src
            self.group = n
            if 'imulh_par_src' in ALT:
                self.group_par_is_source = True
            else:
                self.group_par = s32(gen.get_uint32())                 # [SRC] random "parameter" for the rule
        elif n == 'IMUL_RCP':
            while True:                                                # [SRC] redraw while 0 or a power of 2
                self.imm32 = gen.get_uint32()
                if self.imm32 & (self.imm32 - 1):
                    break
            self.group, self.group_par = 'IMUL_RCP', -1

    def select_source(self, cycle, regs, gen):
        avail = [i for i in range(8) if regs[i].latency <= cycle]
        # [SRC] special case: exactly 2 ready registers for IADD_RS and one is r5 -> r5 is the source (no draw)
        if len(avail) == 2 and self.info is IADD_RS and 5 in avail and 'no_r5_rule' not in ALT:
            self.src = self.group_par = 5
            return True
        r = select_register(avail, gen)
        if r is None:
            return False
        self.src = r
        if self.group_par_is_source:
            self.group_par = r
        return True

    def select_destination(self, cycle, allow_chained_mul, regs, gen):
        avail = []
        for i in range(8):
            ri = regs[i]
            if (ri.latency <= cycle
                    and (self.can_reuse or i != self.src)
                    and (allow_chained_mul or (
                        (self.group not in MULTIPLICATIONS or ri.last_group not in MULTIPLICATIONS)
                        if 'chain_all_mul' in ALT else
                        (self.group != 'IMUL_R' or ri.last_group != 'IMUL_R')))   # [SRC] IMUL_R only
                    and (ri.last_group != self.group or ri.last_par != self.group_par)
                    and (self.info is not IADD_RS or i != 5)):
                avail.append(i)
        r = select_register(avail, gen)
        if r is None:
            return False
        self.dst = r
        return True


def select_register(avail, gen):
    """[SRC] uniform choice: index = getUInt32() % count, and NO draw when exactly one register qualifies."""
    if not avail:
        return None
    if len(avail) == 1 and 'draw_single' not in ALT:
        return avail[0]
    if 'draw_byte' in ALT:
        return avail[gen.get_byte() % len(avail)]
    return avail[gen.get_uint32() % len(avail)]


class RegInfo:
    __slots__ = ('latency', 'last_group', 'last_par')

    def __init__(self):
        self.latency, self.last_group, self.last_par = 0, None, -1


# ------------------------------------------------------------------ 6.3.3 port assignment
def schedule_uop(uop, busy, cycle, commit):
    """[SPEC] order P5 -> P0 -> P1; [SRC] search forward from 'cycle' up to CYCLE_MAP_SIZE-1, -1 if none."""
    while cycle < CYCLE_MAP_SIZE:
        b = busy[cycle]
        if uop & P5 and not b[2]:
            if commit:
                b[2] = uop
            return cycle
        if uop & P0 and not b[0]:
            if commit:
                b[0] = uop
            return cycle
        if uop & P1 and not b[1]:
            if commit:
                b[1] = uop
            return cycle
        cycle += 1
    return -1


def schedule_mop(mop, busy, cycle, dep_cycle, commit):
    if mop.dependent:
        cycle = max(cycle, dep_cycle)
    if mop.uop1 == 0:                                  # mov_rr: no port
        return cycle
    if mop.uop2 == 0:
        return schedule_uop(mop.uop1, busy, cycle, commit)
    while cycle < CYCLE_MAP_SIZE:                      # [SPEC] both uops "executed together" = same cycle
        c1 = schedule_uop(mop.uop1, busy, cycle, False)
        c2 = schedule_uop(mop.uop2, busy, cycle, False)
        if c1 >= 0 and c1 == c2:
            if commit:
                schedule_uop(mop.uop1, busy, c1, True)
                schedule_uop(mop.uop2, busy, c2, True)
            return c1
        cycle += 1
    return -1


# ------------------------------------------------------------------ 6.3 generator
class SuperscalarProgram:
    def __init__(self, instrs, address_register):
        self.instrs = instrs                  # list of (name, dst, src, mod, imm32)
        self.address_register = address_register
        self.func = compile_program(instrs)


def generate_superscalar(gen):
    busy = [[0, 0, 0] for _ in range(CYCLE_MAP_SIZE)]
    regs = [RegInfo() for _ in range(8)]
    cur = SInstr()                              # NOP
    mop_index = 0
    cycle = dep_cycle = 0
    ports_saturated = False
    program = []
    mul_count = 0
    throwaway = 0
    decode_cycle = 0
    while decode_cycle < RANDOMX_SUPERSCALAR_LATENCY and not ports_saturated and len(program) < SUPERSCALAR_MAX_SIZE:
        group = fetch_next(cur.info.name, decode_cycle, mul_count, gen)
        slots = DECODE_GROUPS[group]
        bi = 0
        while bi < len(slots):
            top_cycle = cycle
            if mop_index >= cur.info.size:
                if ports_saturated or len(program) >= SUPERSCALAR_MAX_SIZE:
                    break
                cur.create_for_slot(gen, slots[bi], group, len(slots) == bi + 1)
                if 'imulh_src_first' in ALT and cur.info.name in ('IMULH_R', 'ISMULH_R'):
                    cur.info = IMULH_R_SRCFIRST if cur.info.name == 'IMULH_R' else ISMULH_R_SRCFIRST
                if 'rcp_no_dep' in ALT and cur.info.name == 'IMUL_RCP':
                    cur.info = IMUL_RCP_NODEP
                mop_index = 0
            mop = cur.info.ops[mop_index]
            sched = schedule_mop(mop, busy, cycle, dep_cycle, False)
            if sched < 0:
                ports_saturated = True
                break
            if mop_index == cur.info.src_op:
                fwd = 0
                while fwd < LFC() and not cur.select_source(sched, regs, gen):
                    sched += 1
                    cycle += 1                  # [SRC] NOT undone when the instruction is thrown away
                    fwd += 1
                if fwd == LFC():
                    if throwaway < MAX_THROWAWAY_COUNT:
                        throwaway += 1
                        mop_index = cur.info.size
                        if 'restore_cycle' in ALT:
                            cycle = top_cycle
                        continue                # same slot, new instruction
                    cur = SInstr()
                    break
            if mop_index == cur.info.dst_op:
                fwd = 0
                while fwd < LFC() and not cur.select_destination(sched, throwaway > 0, regs, gen):
                    sched += 1
                    cycle += 1
                    fwd += 1
                if fwd == LFC():
                    if throwaway < MAX_THROWAWAY_COUNT:
                        throwaway += 1
                        mop_index = cur.info.size
                        if 'restore_cycle' in ALT:
                            cycle = top_cycle
                        continue
                    cur = SInstr()
                    break
            throwaway = 0
            sched = schedule_mop(mop, busy, sched, sched, True)
            if sched < 0:
                ports_saturated = True
                break
            dep_cycle = sched + mop.latency
            if mop_index == cur.info.result_op:
                ri = regs[cur.dst]
                ri.latency = dep_cycle
                ri.last_group = cur.group
                ri.last_par = cur.group_par
            bi += 1
            mop_index += 1
            if sched >= RANDOMX_SUPERSCALAR_LATENCY:          # [SPEC] 6.3 condition 1 ([SRC]: instr. is kept)
                ports_saturated = True
                if 'drop_last' in ALT:
                    break
            cycle = top_cycle
            if mop_index >= cur.info.size:
                src = cur.src if cur.src >= 0 else cur.dst
                program.append((cur.info.name, cur.dst, src, cur.mod, cur.imm32))
                mul_count += (cur.info.name == 'IMUL_R') if 'mulcount_imul_only' in ALT else (cur.info.name in MULTIPLICATIONS)
        cycle += 1
        decode_cycle += 1
    # [SRC] 7.3 step 7 "register with the longest dependency chain": 1 cycle per instruction, dst depends on
    # dst and src (src ignored when src == dst, incl. immediates); ties -> lowest register index.
    asic = [0] * 8
    for name, dst, src, mod, imm in program:
        lat_dst = asic[dst] + 1
        lat_src = asic[src] + 1 if dst != src else 0
        asic[dst] = max(lat_dst, lat_src)
    if 'addr_cpu_latency' in ALT:
        asic = [ri.latency for ri in regs]
    addr = 0
    best = 0
    for i in range(8):
        if asic[i] > best or ('addr_tie_high' in ALT and asic[i] == best):
            best, addr = asic[i], i
    return SuperscalarProgram(program, addr)


# ------------------------------------------------------------------ execution (6.1, 5.2.x)
def reciprocal(d):
    """5.2.6: rcp = 2^x / d (integer division) with the largest x such that rcp < 2^64 (d not 0 / power of 2)."""
    return (1 << (63 + d.bit_length())) // d


def sext32(x):
    return (x | 0xFFFFFFFF00000000) if x & 0x80000000 else x


def compile_program(instrs):
    lines = ['def f(r):', '    r0, r1, r2, r3, r4, r5, r6, r7 = r']
    for name, d, s, mod, imm in instrs:
        rd, rs = 'r%d' % d, 'r%d' % s
        if name == 'ISUB_R':
            lines.append('    %s = (%s - %s) & M' % (rd, rd, rs))
        elif name == 'IXOR_R':
            lines.append('    %s ^= %s' % (rd, rs))
        elif name == 'IADD_RS':
            lines.append('    %s = (%s + (%s << %d)) & M' % (rd, rd, rs, (mod >> 2) & 3))
        elif name == 'IMUL_R':
            lines.append('    %s = (%s * %s) & M' % (rd, rd, rs))
        elif name == 'IROR_C':
            c = imm & 63
            lines.append('    %s = ((%s >> %d) | (%s << %d)) & M' % (rd, rd, c, rd, 64 - c))
        elif name == 'IADD_C':
            lines.append('    %s = (%s + %d) & M' % (rd, rd, sext32(imm)))
        elif name == 'IXOR_C':
            lines.append('    %s ^= %d' % (rd, sext32(imm)))
        elif name == 'IMULH_R':
            lines.append('    %s = (%s * %s) >> 64' % (rd, rd, rs))
        elif name == 'ISMULH_R':
            lines.append('    %s = (((%s - ((%s >> 63) << 64)) * (%s - ((%s >> 63) << 64))) >> 64) & M'
                         % (rd, rd, rd, rs, rs))
        elif name == 'IMUL_RCP':
            lines.append('    %s = (%s * %d) & M' % (rd, rd, reciprocal(imm)))
        else:
            raise AssertionError(name)
    lines.append('    return [r0, r1, r2, r3, r4, r5, r6, r7]')
    ns = {'M': M64}
    exec('\n'.join(lines), ns)
    return ns['f']


# ------------------------------------------------------------------ chapter 7.2 / 7.3
def superscalar_init(key: bytes):
    """7.2: BlakeGenerator(K), then 8 SuperscalarHash instances generated in order from the same generator."""
    gen = BlakeGenerator(key[:60])            # randomx.h: only the first 60 bytes of a longer key are used
    return [generate_superscalar(gen) for _ in range(RANDOMX_CACHE_ACCESSES)]


ITEM_MUL = 6364136223846793005
ITEM_XOR = [0, 9298411001130361340, 12065312585734608966, 9306329213124626780, 5281919268842080866,
            10536153434571861004, 3398623926847679864, 9549104520008361294]


def make_item_function(progs, cache_words):
    """Returns item(itemNumber) -> list of 8 uint64 (7.3).  cache_words: flat uint64 array (numpy) of the Cache."""
    n_items = len(cache_words) // 8
    funcs = [p.func for p in progs]
    addr = [p.address_register for p in progs]

    def item(item_number):
        r0 = ((item_number + 1) * ITEM_MUL) & M64
        r = [r0 ^ x for x in ITEM_XOR]
        cache_index = item_number
        for i in range(RANDOMX_CACHE_ACCESSES):
            base = (cache_index % n_items) * 8
            mix = cache_words[base:base + 8].tolist()
            r = funcs[i](r)
            r = [a ^ b for a, b in zip(r, mix)]
            cache_index = r[addr[i]]
        return r
    return item


def selftest_constants():
    """7.3: the XOR constants are bytes 8-63 of Hash512("RandomX SuperScalarHash initialize") as LE uint64,
    with r1 += 2^33+700 and r3 += 2^14."""
    import struct
    from rx_primitives import hash512
    h = hash512(b"RandomX SuperScalarHash initialize")
    w = list(struct.unpack('<8Q', h))
    calc = [0] + w[1:]
    calc[1] += (1 << 33) + 700
    calc[3] += 1 << 14
    assert calc == ITEM_XOR, (calc, ITEM_XOR)
    return "7.3 constants OK"


if __name__ == '__main__':
    print(selftest_constants())
