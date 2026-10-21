"""Second SuperscalarHash generator, written only from the pseudocode of Appendix A of the report (proposed
replacement text for spec 6.3), to check that the proposed text is complete.  Compared with rx_superscalar.py
(which reproduces the reference) for several keys."""
import sys
sys.path.insert(0, '.')
from rx_primitives import BlakeGenerator

LAT = 170
OPS = {  # macro-op: (latency, ports) ; ports: tuple of allowed single ports, or 'P1+P5', or None (no port)
    'sub_rr': (1, ('P5', 'P0', 'P1')), 'xor_rr': (1, ('P5', 'P0', 'P1')), 'lea_sib': (1, ('P0', 'P1')),
    'imul_rr': (3, ('P1',)), 'ror_ri': (1, ('P5', 'P0')), 'add_ri': (1, ('P5', 'P0', 'P1')),
    'xor_ri': (1, ('P5', 'P0', 'P1')), 'mov_rr': (0, None), 'mul_r': (4, 'P1+P5'), 'imul_r': (4, 'P1+P5'),
    'mov_ri': (1, ('P5', 'P0', 'P1'))}
INS = {  # name: (macro-ops, srcOp, dstOp, resultOp)
    'ISUB_R': (['sub_rr'], 0, 0, 0), 'IXOR_R': (['xor_rr'], 0, 0, 0), 'IADD_RS': (['lea_sib'], 0, 0, 0),
    'IMUL_R': (['imul_rr'], 0, 0, 0), 'IROR_C': (['ror_ri'], None, 0, 0), 'IADD_C': (['add_ri'], None, 0, 0),
    'IXOR_C': (['xor_ri'], None, 0, 0), 'IMULH_R': (['mov_rr', 'mul_r', 'mov_rr'], 1, 0, 1),
    'ISMULH_R': (['mov_rr', 'imul_r', 'mov_rr'], 1, 0, 1), 'IMUL_RCP': (['mov_ri', 'imul_rr'], None, 1, 1),
    'NONE': ([], None, None, None)}
GROUPS = [(4, 8, 4), (7, 3, 3, 3), (3, 7, 3, 3), (4, 9, 3), (4, 4, 4, 4), (3, 3, 10)]
PI = {'P0': 0, 'P1': 1, 'P5': 2}


def generate(gen):
    ready = [0] * 8; lastGroup = [None] * 8; lastPar = [-1] * 8
    busy = [[False] * 3 for _ in range(LAT + 4)]
    st = {'name': 'NONE'}
    m = 0; cycle = 0; depCycle = 0; done = False; program = []; mulCount = 0; throwAway = 0

    def schedule(op, c, commit, is_rcp_imul):
        if is_rcp_imul:
            c = max(c, depCycle)
        lat, ports = OPS[op]
        if ports is None:
            return c
        while c <= LAT + 3:
            if ports == 'P1+P5':
                if not busy[c][1] and not busy[c][2]:
                    if commit:
                        busy[c][1] = busy[c][2] = True
                    return c
            else:
                for p in ports:
                    if not busy[c][PI[p]]:
                        if commit:
                            busy[c][PI[p]] = True
                        return c
            c += 1
        return -1

    def choose(lst):
        if not lst:
            return None
        if len(lst) == 1:
            return lst[0]
        return lst[gen.get_uint32() % len(lst)]

    def select(size, group, last):
        if size == 3:
            name = ['ISUB_R', 'IXOR_R', 'IMULH_R', 'ISMULH_R'][gen.get_byte() & 3] if last else ['ISUB_R', 'IXOR_R'][gen.get_byte() & 1]
        elif size == 4:
            name = 'IMUL_R' if (group == 4 and not last) else ['IROR_C', 'IADD_RS'][gen.get_byte() & 1]
        elif size in (7, 8, 9):
            name = ['IXOR_C', 'IADD_C'][gen.get_byte() & 1]
        else:
            name = 'IMUL_RCP'
        c = {'name': name, 'src': None, 'dst': None, 'mod': 0, 'imm': 0, 'par_is_src': False}
        if name in ('ISUB_R', 'IADD_RS'):
            c['group'] = 'IADD_RS'; c['par_is_src'] = True
            if name == 'IADD_RS':
                c['mod'] = gen.get_byte()
        elif name in ('IXOR_R', 'IMUL_R'):
            c['group'] = name; c['par_is_src'] = True
        elif name == 'IROR_C':
            while True:
                c['imm'] = gen.get_byte() & 63
                if c['imm'] != 0:
                    break
            c['group'] = name; c['par'] = -1
        elif name in ('IADD_C', 'IXOR_C'):
            c['imm'] = gen.get_uint32(); c['group'] = name; c['par'] = -1
        elif name in ('IMULH_R', 'ISMULH_R'):
            v = gen.get_uint32(); c['par'] = v - (1 << 32) if v >= 1 << 31 else v; c['group'] = name
        else:
            while True:
                c['imm'] = gen.get_uint32()
                if c['imm'] != 0 and c['imm'] & (c['imm'] - 1) != 0:
                    break
            c['group'] = name; c['par'] = -1
        return c

    def selectSource(cur, c):
        R = [r for r in range(8) if ready[r] <= c]
        if cur['name'] == 'IADD_RS' and len(R) == 2 and 5 in R:
            cur['src'] = 5; cur['par'] = 5
            return True
        s = choose(R)
        if s is None:
            return False
        cur['src'] = s
        if cur['par_is_src']:
            cur['par'] = s
        return True

    def selectDestination(cur, c, allowChainedMul):
        R = [r for r in range(8) if ready[r] <= c
             and (cur['name'] in ('IMULH_R', 'ISMULH_R') or r != cur['src'])
             and (allowChainedMul or cur['name'] != 'IMUL_R' or lastGroup[r] != 'IMUL_R')
             and (lastGroup[r] != cur['group'] or lastPar[r] != cur['par'])
             and not (cur['name'] == 'IADD_RS' and r == 5)]
        d = choose(R)
        if d is None:
            return False
        cur['dst'] = d
        return True

    decodeCycle = 0
    while decodeCycle < LAT and not done and len(program) < 3 * LAT + 2:
        if st['name'] in ('IMULH_R', 'ISMULH_R'):
            group = 5
        elif mulCount <= decodeCycle:
            group = 4
        elif st['name'] == 'IMUL_RCP':
            group = 0 if gen.get_byte() & 1 else 3
        else:
            group = gen.get_byte() & 3
        slots = GROUPS[group]
        j = 0
        while j < len(slots):
            top = cycle
            ops, srcOp, dstOp, resultOp = INS[st['name']]
            if m >= len(ops):
                if done or len(program) >= 3 * LAT + 2:
                    break
                st = select(slots[j], group, j == len(slots) - 1); m = 0
                ops, srcOp, dstOp, resultOp = INS[st['name']]
            op = ops[m]
            rcp_imul = st['name'] == 'IMUL_RCP' and m == 1
            sc = schedule(op, cycle, False, rcp_imul)
            if sc < 0:
                done = True; break
            discard = abandon = False
            for role in ('src', 'dst'):
                if (role == 'src' and m == srcOp) or (role == 'dst' and m == dstOp):
                    ok = False
                    for _ in range(4):
                        ok = selectSource(st, sc) if role == 'src' else selectDestination(st, sc, throwAway > 0)
                        if ok:
                            break
                        sc += 1; cycle += 1
                    if not ok:
                        if throwAway < 256:
                            throwAway += 1; m = len(ops); discard = True
                        else:
                            st = {'name': 'NONE'}; abandon = True
                        break
            if discard:
                continue
            if abandon:
                break
            throwAway = 0
            sc = schedule(op, sc, True, rcp_imul)
            if sc < 0:
                done = True; break
            depCycle = sc + OPS[op][0]
            if m == resultOp:
                ready[st['dst']] = depCycle; lastGroup[st['dst']] = st['group']; lastPar[st['dst']] = st['par']
            j += 1; m += 1
            if sc >= LAT:
                done = True
            cycle = top
            if m == len(ops):
                src = st['src'] if st['src'] is not None else st['dst']
                program.append((st['name'], st['dst'], src, st['mod'], st['imm']))
                if st['name'] in ('IMUL_R', 'IMULH_R', 'ISMULH_R', 'IMUL_RCP'):
                    mulCount += 1
        cycle += 1
        decodeCycle += 1
    lat = [0] * 8
    for name, d, s, mod, imm in program:
        lat[d] = max(lat[d] + 1, lat[s] + 1 if s != d else 0)
    addr = max(range(8), key=lambda r: (lat[r], -r))
    return program, addr


if __name__ == '__main__':
    import rx_superscalar as ss
    ss.ALT.clear()
    n_same = n = 0
    for key in [b'test key 000', b'test key 001', b''] + [b'appendix key %d' % i for i in range(200)]:
        ga = BlakeGenerator(key[:60])
        ref = ss.superscalar_init(key)
        for p in ref:
            prog, addr = generate(ga)
            n += 1
            n_same += (prog == p.instrs and addr == p.address_register)
    print('Appendix A generator vs rx_superscalar: %d/%d programs identical (instructions and address register)' % (n_same, n))
