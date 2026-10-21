"""Cross-check generated VM code (rx_vm) against the plain interpreter (rx_interp) with a fake Dataset."""
import sys, struct, hashlib, time
sys.path.insert(0, '.')
import rx_vm, rx_interp
from rx_primitives import hash512, AesGenerator1R, AesGenerator4R

def fake_item(n):
    h = hashlib.blake2b(n.to_bytes(8, 'little'), digest_size=64).digest()
    return list(struct.unpack('<8Q', h))

for version in (1, 2):
  for inp in (b'This is a test', b'Lorem ipsum dolor sit amet', b'x'):
    S = hash512(inp); g1 = AesGenerator1R(S); sp = bytearray(g1.generate(2097152)); g4 = AesGenerator4R(g1.state)
    size = rx_vm.PROGRAM_SIZE[version]
    for p in range(3):
        data = g4.generate(128 + 8 * size)
        cfg = rx_vm.parse_config(data[:128]); ins = rx_vm.decode(data[128:], 'section_order', size)
        sp1 = bytearray(sp); sp2 = bytearray(sp)
        rx_vm.set_fprc(0)
        run, src = rx_vm.compile_program(ins, version, cfg, {})
        t = time.time()
        res1 = run(memoryview(sp1).cast('Q'), fake_item, cfg['ma'], cfg['mx'], cfg['datasetOffset'] // 64,
                   rx_vm.make_cvt_e(*cfg['emask']), cfg['a'])
        t1 = time.time() - t
        rx_vm.set_fprc(0)
        t = time.time()
        res2 = rx_interp.run_program(ins, version, cfg, memoryview(sp2).cast('Q'), fake_item)
        t2 = time.time() - t
        same = res1 == res2 and sp1 == sp2
        print('v%d %-28r prog %d: compiled %.2fs interp %.2fs -> %s' % (version, inp, p, t1, t2, 'SAME' if same else 'DIFFERENT'))
        if not same:
            print(' R', [hex(x) for x in res1[0]]); print(' R', [hex(x) for x in res2[0]])
        g4.set_state(hash512(struct.pack('<32Q', *res1[0], *res1[1], *res1[2], *res1[3])))
        sp = sp1
rx_vm.set_fprc(0)
