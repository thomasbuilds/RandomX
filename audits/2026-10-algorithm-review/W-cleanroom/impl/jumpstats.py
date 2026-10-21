"""A3 quantification: how often is a CBRANCH taken twice in a row (static reading)?  Only then do the static and
dynamic readings of 5.4.2 differ.  Uses the real VM with a pseudo-random stand-in Dataset (the statistic does not
depend on Dataset contents)."""
import sys, struct, hashlib
sys.path.insert(0, '.')
import rx_vm
from rx_primitives import hash512, AesGenerator1R, AesGenerator4R

def fake_item(n):
    return list(struct.unpack('<8Q', hashlib.blake2b(n.to_bytes(8, 'little'), digest_size=64).digest()))

N = int(sys.argv[1]) if len(sys.argv) > 1 else 20
for version in (1, 2):
    tot = [0, 0]; hashes_with_double = 0
    for n in range(N):
        stats = [0, 0]
        inp = b'jumpstats %d' % n
        S = hash512(inp); g1 = AesGenerator1R(S); sp = bytearray(g1.generate(2097152)); g4 = AesGenerator4R(g1.state)
        spq = memoryview(sp).cast('Q')
        size = rx_vm.PROGRAM_SIZE[version]
        rx_vm.set_fprc(0)
        for p in range(8):
            data = g4.generate(128 + 8 * size)
            cfg = rx_vm.parse_config(data[:128]); ins = rx_vm.decode(data[128:], 'section_order', size)
            run, _ = rx_vm.compile_program(ins, version, cfg, {'count_jumps': True, 'stats': stats})
            R, F, E, A = run(spq, fake_item, cfg['ma'], cfg['mx'], cfg['datasetOffset'] // 64,
                             rx_vm.make_cvt_e(*cfg['emask']), cfg['a'])
            g4.set_state(hash512(struct.pack('<32Q', *R, *F, *E, *A)))
        rx_vm.set_fprc(0)
        tot[0] += stats[0]; tot[1] += stats[1]; hashes_with_double += stats[1] > 0
    print('v%d: %d hashes: taken jumps/hash %.1f, twice-in-a-row/hash %.3f, hashes with >=1 twice-in-a-row: %d/%d'
          % (version, N, tot[0] / N, tot[1] / N, hashes_with_double, N), flush=True)
