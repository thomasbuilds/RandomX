"""RandomX clean-room implementation (agent W) -- virtual machine and hash (spec chapters 2, 4, 5).

Written from doc/specs.md only.  Choices the text does not make (see logs/ambiguities-prereading.md):
  A1 instruction word layout (Fig. 5.1 is an external image, unavailable): LAYOUT below, default
     'section order' = opcode byte 0, dst byte 1, src byte 2, mod byte 3, imm32 bytes 4-7 (little-endian).
  A2 opcode -> instruction: contiguous ranges in the order of Tables 5.2.1, 5.3.1, 5.4.1, 5.5.1.
  A3 CBRANCH target: 'static' (program order, computed once per program) by default; 'dynamic' variant.
  A4 fp memory operands (FADD_M/FSUB_M/FDIV_M): never L3 by default; variant 'fpmem_l3' = L3 when dst==src index.
The variants exist only to show that the ambiguous readings give different hashes.
"""
import ctypes
import math
import struct

from rx_primitives import hash256, hash512, AesGenerator1R, AesGenerator4R, aes_hash_1r, aes_enc_w, aes_dec_w

M64 = (1 << 64) - 1
RANDOMX_PROGRAM_ITERATIONS = 2048
RANDOMX_PROGRAM_COUNT = 8
RANDOMX_SCRATCHPAD_L3 = 2097152
RANDOMX_SCRATCHPAD_L2 = 262144
RANDOMX_SCRATCHPAD_L1 = 16384
RANDOMX_DATASET_BASE_SIZE = 2147483648
RANDOMX_DATASET_EXTRA_SIZE = 33554368
RANDOMX_JUMP_BITS = 8
RANDOMX_JUMP_OFFSET = 8
PROGRAM_SIZE = {1: 256, 2: 384}

# Table 4.2.1
L1_MASK = (RANDOMX_SCRATCHPAD_L1 - 1) & ~7
L2_MASK = (RANDOMX_SCRATCHPAD_L2 - 1) & ~7
L3_MASK = (RANDOMX_SCRATCHPAD_L3 - 1) & ~7
L3_MASK64 = (RANDOMX_SCRATCHPAD_L3 - 1) & ~63
DATASET_MASK = (RANDOMX_DATASET_BASE_SIZE - 1) & ~63          # 4.3

# Table 4.3.1 -> glibc x86-64 fesetround constants
_libm = ctypes.CDLL("libm.so.6")
FE = [0x000, 0x400, 0x800, 0xC00]   # roundTiesToEven, roundTowardNegative, roundTowardPositive, roundTowardZero


def set_fprc(v):
    _libm.fesetround(FE[v])


# A2: Tables 5.2.1, 5.3.1, 5.4.1, 5.5.1 in order, with their frequencies
OPCODE_TABLE = [
    ('IADD_RS', 16), ('IADD_M', 7), ('ISUB_R', 16), ('ISUB_M', 7), ('IMUL_R', 16), ('IMUL_M', 4),
    ('IMULH_R', 4), ('IMULH_M', 1), ('ISMULH_R', 4), ('ISMULH_M', 1), ('IMUL_RCP', 8), ('INEG_R', 2),
    ('IXOR_R', 15), ('IXOR_M', 5), ('IROR_R', 8), ('IROL_R', 2), ('ISWAP_R', 4),
    ('FSWAP_R', 4), ('FADD_R', 16), ('FADD_M', 5), ('FSUB_R', 16), ('FSUB_M', 5), ('FSCAL_R', 6),
    ('FMUL_R', 32), ('FDIV_M', 4), ('FSQRT_R', 6),
    ('CBRANCH', 25), ('CFROUND', 1),
    ('ISTORE', 16)]


def build_opcode_map(table):
    m = []
    for name, n in table:
        m += [name] * n
    assert len(m) == 256
    return m


OPCODES = build_opcode_map(OPCODE_TABLE)
LAYOUTS = {
    # name: (opcode byte, dst byte, src byte, mod byte, imm32 byte offset)
    'section_order': (0, 1, 2, 3, 4),
    'imm_first': (4, 5, 6, 7, 0),
}

_pk_d = struct.Struct('<d').pack
_up_d = struct.Struct('<d').unpack
_pk_q = struct.Struct('<Q').pack
_up_q = struct.Struct('<Q').unpack


def f2b(x):
    return _up_q(_pk_d(x))[0]


def b2f(b):
    return _up_d(_pk_q(b))[0]


def sext32(x):
    return (x | 0xFFFFFFFF00000000) if x & 0x80000000 else x


def rcp(d):
    """5.2.6"""
    return (1 << (63 + d.bit_length())) // d


def is_zero_or_pow2(x):
    return x & (x - 1) == 0


def cvt_f(w):
    """4.3.1: 8 bytes -> pair of int32 (low 32 bits -> low half) -> doubles (exact)."""
    lo = w & 0xFFFFFFFF
    hi = w >> 32
    return float(lo - ((lo & 0x80000000) << 1)), float(hi - ((hi & 0x80000000) << 1))


def make_cvt_e(emask_lo, emask_hi):
    """4.3.2 with the program's masks (4.5.6).  emask = (fraction mask bits 0-21, exponent mask bits 60-63)."""
    keep = 0x000FFFFFFFC00000 | 0x00F0000000000000        # fraction bits 22-51 and exponent bits 0-3
    or_lo = 0x3000000000000000 | ((emask_lo >> 60) << 56) | (emask_lo & 0x3FFFFF)
    or_hi = 0x3000000000000000 | ((emask_hi >> 60) << 56) | (emask_hi & 0x3FFFFF)

    def cvt_e(w):
        lo = w & 0xFFFFFFFF
        hi = w >> 32
        bl = f2b(float(lo - ((lo & 0x80000000) << 1)))
        bh = f2b(float(hi - ((hi & 0x80000000) << 1)))
        return b2f((bl & keep) | or_lo), b2f((bh & keep) | or_hi)
    return cvt_e


# ------------------------------------------------------------------ program decoding + compilation
def decode(prog_bytes, layout, size):
    ob, db, sb, mb, ib = LAYOUTS[layout]
    out = []
    for i in range(size):
        w = prog_bytes[8 * i:8 * i + 8]
        out.append((OPCODES[w[ob]], w[db], w[sb], w[mb], int.from_bytes(w[ib:ib + 4], 'little')))
    return out


def mem_mask(mod, src_eq_dst):
    """Table 5.1.4 for reads."""
    if src_eq_dst:
        return L3_MASK
    return L2_MASK if (mod & 3) == 0 else L1_MASK


def compile_program(instrs, version, cfg, variant):
    """Generates a Python function that runs the whole loop of 4.6 (2048 iterations) for one program."""
    rr = cfg['readReg']
    L = []
    emit = L.append
    emit('def run(spq, ds_item, ma, mx, ds_off_items, cvt_e, AV):')
    emit('    r0 = r1 = r2 = r3 = r4 = r5 = r6 = r7 = 0')                      # 4.6.1 step 4
    # a registers are passed in as exact doubles (float literals would be parsed under the current fprc)
    emit('    (a0l, a0h), (a1l, a1h), (a2l, a2h), (a3l, a3h) = AV')
    emit('    sp0 = mx; sp1 = ma')                                              # 4.6.1 steps 2-3
    emit('    for ic in range(%d):' % RANDOMX_PROGRAM_ITERATIONS)
    emit('        t = r%d ^ r%d' % (rr[0], rr[1]))                              # 4.6.2 step 1
    emit('        sp0 ^= t & 0xFFFFFFFF; sp1 ^= t >> 32')
    emit('        i0 = (sp0 & %d) >> 3; i1 = (sp1 & %d) >> 3' % (L3_MASK64, L3_MASK64))
    emit('        r0 ^= spq[i0]; r1 ^= spq[i0+1]; r2 ^= spq[i0+2]; r3 ^= spq[i0+3]')   # step 2
    emit('        r4 ^= spq[i0+4]; r5 ^= spq[i0+5]; r6 ^= spq[i0+6]; r7 ^= spq[i0+7]')
    for i in range(4):                                                          # step 3
        emit('        f%dl, f%dh = cvt_f(spq[i1+%d])' % (i, i, i))
    for i in range(4):
        emit('        e%dl, e%dh = cvt_e(spq[i1+%d])' % (i, i, 4 + i))
    # ---- step 4: the program (chapter 5)
    dynamic = variant.get('cbranch') == 'dynamic'
    targets = set()
    body = []                     # list of (index, [lines])
    last_mod = [-1] * 8           # 5.4.2 static "last modified" tracking
    for k, (op, dst, src, mod, imm) in enumerate(instrs):
        d, s = dst & 7, src & 7
        rd, rs = 'r%d' % d, 'r%d' % s
        simm = sext32(imm)
        lines = []
        modifies = []
        if op == 'IADD_RS':
            e = '%s + (%s << %d)' % (rd, rs, (mod >> 2) & 3)
            if d == 5:
                e += ' + %d' % simm
            lines.append('%s = (%s) & M' % (rd, e)); modifies = [d]
        elif op in ('IADD_M', 'ISUB_M', 'IMUL_M', 'IMULH_M', 'ISMULH_M', 'IXOR_M'):
            if s == d:
                m = 'spq[%d]' % ((imm & L3_MASK) >> 3)
            else:
                m = 'spq[((%s + %d) & %d) >> 3]' % (rs, imm, mem_mask(mod, False))
            if op == 'IADD_M':
                lines.append('%s = (%s + %s) & M' % (rd, rd, m))
            elif op == 'ISUB_M':
                lines.append('%s = (%s - %s) & M' % (rd, rd, m))
            elif op == 'IMUL_M':
                lines.append('%s = (%s * %s) & M' % (rd, rd, m))
            elif op == 'IMULH_M':
                lines.append('%s = (%s * %s) >> 64' % (rd, rd, m))
            elif op == 'ISMULH_M':
                lines.append('_m = %s' % m)
                lines.append('%s = (((%s - ((%s >> 63) << 64)) * (_m - ((_m >> 63) << 64))) >> 64) & M' % (rd, rd, rd))
            else:
                lines.append('%s ^= %s' % (rd, m))
            modifies = [d]
        elif op in ('ISUB_R', 'IMUL_R', 'IXOR_R'):
            o = '%d' % simm if s == d else rs
            if op == 'ISUB_R':
                lines.append('%s = (%s - %s) & M' % (rd, rd, o))
            elif op == 'IMUL_R':
                lines.append('%s = (%s * %s) & M' % (rd, rd, o))
            else:
                lines.append('%s ^= %s' % (rd, o if s != d else '%d' % (simm & M64)))
            modifies = [d]
        elif op == 'IMULH_R':
            lines.append('%s = (%s * %s) >> 64' % (rd, rd, rs)); modifies = [d]
        elif op == 'ISMULH_R':
            lines.append('%s = (((%s - ((%s >> 63) << 64)) * (%s - ((%s >> 63) << 64))) >> 64) & M'
                         % (rd, rd, rd, rs, rs)); modifies = [d]
        elif op == 'IMUL_RCP':
            if not is_zero_or_pow2(imm):
                lines.append('%s = (%s * %d) & M' % (rd, rd, rcp(imm))); modifies = [d]
        elif op == 'INEG_R':
            lines.append('%s = (-%s) & M' % (rd, rd)); modifies = [d]
        elif op in ('IROR_R', 'IROL_R'):
            if s == d:
                c = imm & 63
                if op == 'IROL_R':
                    c = (64 - c) & 63
                if c:
                    lines.append('%s = ((%s >> %d) | (%s << %d)) & M' % (rd, rd, c, rd, 64 - c))
                else:
                    lines.append('pass')
            else:
                if op == 'IROR_R':
                    lines.append('_c = %s & 63' % rs)
                else:
                    lines.append('_c = (64 - (%s & 63)) & 63' % rs)
                lines.append('%s = ((%s >> _c) | (%s << (64 - _c))) & M' % (rd, rd, rd))
            modifies = [d]
        elif op == 'ISWAP_R':
            if s != d:
                lines.append('%s, %s = %s, %s' % (rd, rs, rs, rd)); modifies = [d, s]
            else:
                lines.append('pass')
        elif op == 'FSWAP_R':
            reg = ('f%d' % d) if d < 4 else ('e%d' % (d - 4))
            lines.append('%sl, %sh = %sh, %sl' % (reg, reg, reg, reg))
        elif op in ('FADD_R', 'FSUB_R'):
            fd, a = d & 3, s & 3
            o = '+' if op == 'FADD_R' else '-'
            lines.append('f%dl = f%dl %s a%dl; f%dh = f%dh %s a%dh' % (fd, fd, o, a, fd, fd, o, a))
        elif op in ('FADD_M', 'FSUB_M', 'FDIV_M'):
            fd = d & 3
            if variant.get('fpmem_l3') and s == d:
                m = 'spq[%d]' % ((imm & L3_MASK) >> 3)
            else:
                m = 'spq[((%s + %d) & %d) >> 3]' % (rs, imm, mem_mask(mod, False))
            if op == 'FDIV_M':
                lines.append('_l, _h = cvt_e(%s)' % m)
                lines.append('e%dl = e%dl / _l; e%dh = e%dh / _h' % (fd, fd, fd, fd))
            else:
                o = '+' if op == 'FADD_M' else '-'
                lines.append('_l, _h = cvt_f(%s)' % m)
                lines.append('f%dl = f%dl %s _l; f%dh = f%dh %s _h' % (fd, fd, o, fd, fd, o))
        elif op == 'FSCAL_R':
            fd = d & 3
            lines.append('f%dl = b2f(f2b(f%dl) ^ 0x80F0000000000000); f%dh = b2f(f2b(f%dh) ^ 0x80F0000000000000)'
                         % (fd, fd, fd, fd))
        elif op == 'FMUL_R':
            ed, a = d & 3, s & 3
            lines.append('e%dl = e%dl * a%dl; e%dh = e%dh * a%dh' % (ed, ed, a, ed, ed, a))
        elif op == 'FSQRT_R':
            ed = d & 3
            lines.append('e%dl = sqrt(e%dl); e%dh = sqrt(e%dh)' % (ed, ed, ed, ed))
        elif op == 'CFROUND':
            c = imm & 63
            lines.append('_t = ((%s >> %d) | (%s << %d)) & M' % (rs, c, rs, (64 - c) & 63) if c else '_t = %s' % rs)
            if version == 1:
                lines.append('set_fprc(_t & 3)')
            else:
                lines.append('if not (_t & 0x3C): set_fprc(_t & 3)')
        elif op == 'CBRANCH':
            b = (mod >> 4) + RANDOMX_JUMP_OFFSET
            cimm = (simm | (1 << b)) & ~(1 << (b - 1)) & M64
            cmask = ((1 << RANDOMX_JUMP_BITS) - 1) << b
            lines.append('%s = (%s + %d) & M' % (rd, rd, cimm))
            if dynamic:
                lines.append('if not (%s & %d):' % (rd, cmask))
                lines.append('    _s = LM%d + 1' % d)
                lines.append('    LM0 = LM1 = LM2 = LM3 = LM4 = LM5 = LM6 = LM7 = %d' % k)
                lines.append('    continue')
                lines.append('LM0 = LM1 = LM2 = LM3 = LM4 = LM5 = LM6 = LM7 = %d' % k)
            else:
                t = last_mod[d] + 1
                targets.add(t)
                lines.append('if not (%s & %d):' % (rd, cmask))
                if variant.get('count_jumps'):
                    # STATS[0] = taken jumps, STATS[1] = a CBRANCH taken right after its own jump ("twice in a row")
                    lines.append('    STATS[0] += 1')
                    lines.append('    if _lt == %d: STATS[1] += 1' % k)
                    lines.append('    _lt = %d' % k)
                lines.append('    _s = %d' % t)
                lines.append('    continue')
                if variant.get('count_jumps'):
                    lines.append('_lt = -1')
            modifies = list(range(8))
        elif op == 'ISTORE':
            if (mod >> 4) >= 14:
                mk = L3_MASK
            else:
                mk = L2_MASK if (mod & 3) == 0 else L1_MASK
            lines.append('spq[((%s + %d) & %d) >> 3] = %s' % (rd, imm, mk, rs))
        else:
            raise AssertionError(op)
        for m in modifies:
            last_mod[m] = k
        if dynamic and op != 'CBRANCH' and modifies:
            lines.append('; '.join('LM%d = %d' % (m, k) for m in modifies))
        body.append((k, lines))
    if dynamic:
        targets = set(range(len(instrs)))      # any instruction may be a target
        emit('        LM0 = LM1 = LM2 = LM3 = LM4 = LM5 = LM6 = LM7 = -1')
    targets.add(0)
    emit('        _s = 0')
    if variant.get('count_jumps'):
        emit('        _lt = -1')
    emit('        while True:')
    cur_block = None
    for k, lines in body:
        if k in targets:
            emit('            if _s <= %d:' % k)
        for ln in lines:
            emit('                ' + ln)
    emit('            break')
    # ---- steps 5-13
    mp = 'mx' if version == 1 else 'ma'                                      # 4.3: mp alias
    emit('        mt = ma')                                                   # step 5
    emit('        %s ^= (r%d ^ r%d) & 0xFFFFFFFF' % (mp, rr[2], rr[3]))
    # step 6 (prefetch) has no effect on the result; step 7:
    emit('        d0, d1, d2, d3, d4, d5, d6, d7 = ds_item(ds_off_items + ((mt & %d) >> 6))' % DATASET_MASK)
    emit('        r0 ^= d0; r1 ^= d1; r2 ^= d2; r3 ^= d3; r4 ^= d4; r5 ^= d5; r6 ^= d6; r7 ^= d7')
    emit('        ma, mx = mx, ma')                                            # step 8
    emit('        spq[i1] = r0; spq[i1+1] = r1; spq[i1+2] = r2; spq[i1+3] = r3')   # step 9
    emit('        spq[i1+4] = r4; spq[i1+5] = r5; spq[i1+6] = r6; spq[i1+7] = r7')
    emit('        F = mixfe((f2b(f0l), f2b(f0h), f2b(f1l), f2b(f1h), f2b(f2l), f2b(f2h), f2b(f3l), f2b(f3h)),'
         ' (f2b(e0l), f2b(e0h), f2b(e1l), f2b(e1h), f2b(e2l), f2b(e2h), f2b(e3l), f2b(e3h)))')   # step 10
    emit('        spq[i0:i0+8] = F_arr(F)' if False else
         '        spq[i0] = F[0]; spq[i0+1] = F[1]; spq[i0+2] = F[2]; spq[i0+3] = F[3]; '
         'spq[i0+4] = F[4]; spq[i0+5] = F[5]; spq[i0+6] = F[6]; spq[i0+7] = F[7]')   # step 11
    emit('        sp0 = 0; sp1 = 0')                                            # step 12
    emit('    E = (f2b(e0l), f2b(e0h), f2b(e1l), f2b(e1h), f2b(e2l), f2b(e2h), f2b(e3l), f2b(e3h))')
    emit('    A = (f2b(a0l), f2b(a0h), f2b(a1l), f2b(a1h), f2b(a2l), f2b(a2h), f2b(a3l), f2b(a3h))')
    emit('    return (r0, r1, r2, r3, r4, r5, r6, r7), F, E, A')
    src = '\n'.join(L)
    ns = {'M': M64, 'STATS': variant.get('stats'), 'cvt_f': cvt_f, 'f2b': f2b, 'b2f': b2f, 'sqrt': math.sqrt, 'set_fprc': set_fprc,
          'mixfe': mix_v1 if version == 1 else mix_v2}
    exec(compile(src, '<randomx-program>', 'exec'), ns)
    return ns['run'], src


def mix_v1(F, E):
    """4.6.2 step 10, v1: f_i ^= e_i (both halves)."""
    return tuple(a ^ b for a, b in zip(F, E))


def _w4(lo, hi):
    return (lo & 0xFFFFFFFF, lo >> 32, hi & 0xFFFFFFFF, hi >> 32)


def mix_v2(F, E):
    """4.6.2 step 10, v2: for i = 0..3: f0 = AesEnc(f0, e_i), f1 = AesDec(f1, e_i), f2 = AesEnc, f3 = AesDec."""
    f = [_w4(F[2 * j], F[2 * j + 1]) for j in range(4)]
    for i in range(4):
        k = _w4(E[2 * i], E[2 * i + 1])
        f[0] = aes_enc_w(f[0], k)
        f[1] = aes_dec_w(f[1], k)
        f[2] = aes_enc_w(f[2], k)
        f[3] = aes_dec_w(f[3], k)
    out = []
    for w in f:
        out += [w[0] | (w[1] << 32), w[2] | (w[3] << 32)]
    return tuple(out)


# ------------------------------------------------------------------ 4.5 programming
def parse_config(cfg_bytes):
    q = struct.unpack('<16Q', cfg_bytes)
    a = []
    for i in range(4):                                    # 4.5.2, Table 4.5.2
        pair = []
        for w in (q[2 * i], q[2 * i + 1]):
            frac = w & ((1 << 52) - 1)
            exp = w >> 59
            pair.append(b2f(((1023 + exp) << 52) | frac))
        a.append(tuple(pair))
    return {
        'a': a,
        'ma': q[8] & 0xFFFFFFFF, 'mx': q[10] & 0xFFFFFFFF,                     # 4.5.3
        'readReg': [2 * k + ((q[12] >> k) & 1) for k in range(4)],              # 4.5.4
        'datasetOffset': (q[13] % (RANDOMX_DATASET_EXTRA_SIZE // 64 + 1)) * 64,  # 4.5.5
        'emask': (q[14], q[15]),                                                # 4.5.6
    }


# ------------------------------------------------------------------ chapter 2
def randomx_hash(H: bytes, ds_item, version=1, variant=None, layout='section_order', trace=None):
    variant = variant or {}
    size = PROGRAM_SIZE[version]
    S = hash512(H)                                                    # step 2
    gen1 = AesGenerator1R(S)                                          # step 3
    sp = bytearray(gen1.generate(RANDOMX_SCRATCHPAD_L3))              # step 4
    spq = memoryview(sp).cast('Q')
    gen4 = AesGenerator4R(gen1.state)                                 # step 5
    set_fprc(0)                                                       # step 6
    for p in range(RANDOMX_PROGRAM_COUNT):
        data = gen4.generate(128 + 8 * size)                          # step 7 (4.5)
        cfg = parse_config(data[:128])
        instrs = decode(data[128:], layout, size)
        run, src = compile_program(instrs, version, cfg, variant)
        if trace is not None:
            trace.append((cfg, instrs))
        R, F, E, A = run(spq, ds_item, cfg['ma'], cfg['mx'], cfg['datasetOffset'] // 64,
                         make_cvt_e(*cfg['emask']), cfg['a'])        # step 8
        regfile = struct.pack('<32Q', *R, *F, *E, *A)
        if p != RANDOMX_PROGRAM_COUNT - 1:
            gen4.set_state(hash512(regfile))                          # steps 9-10
    set_fprc(0)
    A = aes_hash_1r(bytes(sp))                                        # step 12
    regfile = regfile[:192] + A                                       # step 13
    return hash256(regfile)                                           # step 14


def commitment(H: bytes, R: bytes) -> bytes:
    """Not in specs.md.  Formula from the audit brief: Blake2b-256(input || hash)."""
    return hash256(H + R)
