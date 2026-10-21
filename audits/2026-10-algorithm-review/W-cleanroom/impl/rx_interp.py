"""Independent, deliberately plain interpreter of one VM program (spec 4.6 + chapter 5), used only to
cross-check the code generator in rx_vm.py (same interpretation choices, different code)."""
import math
import struct
import rx_vm
from rx_vm import (M64, L1_MASK, L2_MASK, L3_MASK, L3_MASK64, DATASET_MASK, set_fprc, sext32, f2b, b2f, cvt_f,
                   mix_v1, mix_v2)


def ror(x, c):
    c &= 63
    return ((x >> c) | (x << (64 - c))) & M64 if c else x


def signed(x):
    return x - (1 << 64) if x >> 63 else x


def run_program(instrs, version, cfg, sp_q, ds_item, iterations=2048, cbranch='static'):
    r = [0] * 8
    A = cfg['a']
    cvt_e = rx_vm.make_cvt_e(*cfg['emask'])
    ma, mx = cfg['ma'], cfg['mx']
    rr = cfg['readReg']
    sp0, sp1 = mx, ma
    n = len(instrs)
    # static branch targets (5.4.2)
    target = [None] * n
    last = [-1] * 8
    for k, (op, dst, src, mod, imm) in enumerate(instrs):
        d, s = dst % 8, src % 8
        if op == 'CBRANCH':
            target[k] = last[d] + 1
            last = [k] * 8
        elif op in ('IMUL_RCP',):
            if imm & (imm - 1):
                last[d] = k
        elif op == 'ISWAP_R':
            if s != d:
                last[d] = last[s] = k
        elif op in ('IADD_RS', 'IADD_M', 'ISUB_R', 'ISUB_M', 'IMUL_R', 'IMUL_M', 'IMULH_R', 'IMULH_M',
                    'ISMULH_R', 'ISMULH_M', 'INEG_R', 'IXOR_R', 'IXOR_M', 'IROR_R', 'IROL_R'):
            last[d] = k
    for it in range(iterations):
        t = r[rr[0]] ^ r[rr[1]]
        sp0 ^= t & 0xFFFFFFFF
        sp1 ^= t >> 32
        a0 = sp0 & L3_MASK64
        a1 = sp1 & L3_MASK64
        for i in range(8):
            r[i] ^= sp_q[a0 // 8 + i]
        f = [list(cvt_f(sp_q[a1 // 8 + i])) for i in range(4)]
        e = [list(cvt_e(sp_q[a1 // 8 + 4 + i])) for i in range(4)]
        pc = 0
        while pc < n:
            op, dst, src, mod, imm = instrs[pc]
            d, s = dst % 8, src % 8
            nxt = pc + 1

            def memaddr(base_reg_val, read=True):
                lvl = mod % 4
                return (base_reg_val + imm) & (L2_MASK if lvl == 0 else L1_MASK)
            if op.startswith('I') and op.endswith('_M'):
                addr = (imm & L3_MASK) if s == d else memaddr(r[s])
                m = sp_q[addr // 8]
                if op == 'IADD_M': r[d] = (r[d] + m) % 2**64
                elif op == 'ISUB_M': r[d] = (r[d] - m) % 2**64
                elif op == 'IMUL_M': r[d] = (r[d] * m) % 2**64
                elif op == 'IMULH_M': r[d] = (r[d] * m) >> 64
                elif op == 'ISMULH_M': r[d] = ((signed(r[d]) * signed(m)) >> 64) % 2**64
                elif op == 'IXOR_M': r[d] ^= m
            elif op == 'IADD_RS':
                v = r[d] + (r[s] << ((mod >> 2) % 4))
                if d == 5:
                    v += sext32(imm)
                r[d] = v % 2**64
            elif op in ('ISUB_R', 'IMUL_R', 'IXOR_R', 'IROR_R', 'IROL_R'):
                v = sext32(imm) if s == d else r[s]
                if op == 'ISUB_R': r[d] = (r[d] - v) % 2**64
                elif op == 'IMUL_R': r[d] = (r[d] * v) % 2**64
                elif op == 'IXOR_R': r[d] = (r[d] ^ v) % 2**64
                elif op == 'IROR_R': r[d] = ror(r[d], v % 64)
                else: r[d] = ror(r[d], 64 - v % 64)
            elif op == 'IMULH_R':
                r[d] = (r[d] * r[s]) >> 64
            elif op == 'ISMULH_R':
                r[d] = ((signed(r[d]) * signed(r[s])) >> 64) % 2**64
            elif op == 'IMUL_RCP':
                if imm != 0 and imm & (imm - 1):
                    r[d] = (r[d] * rx_vm.rcp(imm)) % 2**64
            elif op == 'INEG_R':
                r[d] = (-r[d]) % 2**64
            elif op == 'ISWAP_R':
                r[d], r[s] = r[s], r[d]
            elif op == 'FSWAP_R':
                reg = f[d] if d < 4 else e[d - 4]
                reg[0], reg[1] = reg[1], reg[0]
            elif op == 'FADD_R':
                f[d % 4] = [f[d % 4][0] + A[s % 4][0], f[d % 4][1] + A[s % 4][1]]
            elif op == 'FSUB_R':
                f[d % 4] = [f[d % 4][0] - A[s % 4][0], f[d % 4][1] - A[s % 4][1]]
            elif op in ('FADD_M', 'FSUB_M'):
                lo, hi = cvt_f(sp_q[memaddr(r[s]) // 8])
                if op == 'FADD_M':
                    f[d % 4] = [f[d % 4][0] + lo, f[d % 4][1] + hi]
                else:
                    f[d % 4] = [f[d % 4][0] - lo, f[d % 4][1] - hi]
            elif op == 'FSCAL_R':
                f[d % 4] = [b2f(f2b(x) ^ 0x80F0000000000000) for x in f[d % 4]]
            elif op == 'FMUL_R':
                e[d % 4] = [e[d % 4][0] * A[s % 4][0], e[d % 4][1] * A[s % 4][1]]
            elif op == 'FDIV_M':
                lo, hi = cvt_e(sp_q[memaddr(r[s]) // 8])
                e[d % 4] = [e[d % 4][0] / lo, e[d % 4][1] / hi]
            elif op == 'FSQRT_R':
                e[d % 4] = [math.sqrt(x) for x in e[d % 4]]
            elif op == 'CFROUND':
                v = ror(r[s], imm % 64)
                if version == 1 or (v >> 2) % 16 == 0:
                    set_fprc(v % 4)
            elif op == 'CBRANCH':
                b = (mod >> 4) + 8
                cimm = sext32(imm) | (1 << b)
                cimm &= ~(1 << (b - 1))
                r[d] = (r[d] + cimm) % 2**64
                if (r[d] >> b) % 256 == 0:
                    nxt = target[pc]
            elif op == 'ISTORE':
                lvl = L3_MASK if (mod >> 4) >= 14 else (L2_MASK if mod % 4 == 0 else L1_MASK)
                sp_q[((r[d] + imm) & lvl) // 8] = r[s]
            pc = nxt
        mt = ma
        if version == 1:
            mx ^= (r[rr[2]] ^ r[rr[3]]) & 0xFFFFFFFF
        else:
            ma ^= (r[rr[2]] ^ r[rr[3]]) & 0xFFFFFFFF
        item = ds_item(cfg['datasetOffset'] // 64 + (mt & DATASET_MASK) // 64)
        for i in range(8):
            r[i] ^= item[i]
        ma, mx = mx, ma
        for i in range(8):
            sp_q[a1 // 8 + i] = r[i]
        F = tuple(f2b(x) for reg in f for x in reg)
        E = tuple(f2b(x) for reg in e for x in reg)
        F = mix_v1(F, E) if version == 1 else mix_v2(F, E)
        for i in range(8):
            sp_q[a0 // 8 + i] = F[i]
        sp0 = sp1 = 0
    Abits = tuple(f2b(x) for reg in A for x in reg)
    return tuple(r), F, E, Abits
