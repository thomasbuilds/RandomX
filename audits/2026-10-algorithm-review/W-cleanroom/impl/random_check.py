"""Extra coverage: 10 random inputs (seeded, lengths 0-200) x v1/v2, key "test key 000", vs ../ref/hashes_random.txt."""
import sys, time, numpy as np
sys.path.insert(0, '.')
import rx_superscalar as ss, rx_vm
item = ss.make_item_function(ss.superscalar_init(b'test key 000'), np.fromfile('../data/cache_testkey000.bin', dtype='<u8'))
ok = bad = 0
for line in open('../ref/hashes_random.txt'):
    f = line.split()
    ver, want, inp = int(f[0][1]), f[4], bytes.fromhex(f[5][len('input_hex='):])
    t = time.time()
    got = rx_vm.randomx_hash(inp, item, version=ver).hex()
    ok += got == want; bad += got != want
    print('v%d len=%3d %s %s (%.1fs)' % (ver, len(inp), got, 'MATCH' if got == want else 'MISMATCH want ' + want, time.time() - t), flush=True)
print('random inputs: %d match, %d mismatch' % (ok, bad))
