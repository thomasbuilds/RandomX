"""Stage 2/3: SuperscalarHash generation + Dataset items vs the reference library (ref/items_*.txt)."""
import sys, time, numpy as np
sys.path.insert(0, '.')
import rx_superscalar as ss

def check(key, cachefile, reffile):
    t = time.time()
    progs = ss.superscalar_init(key)
    print('key %r: generated 8 programs in %.2fs; sizes %s; address regs %s' % (
        key, time.time() - t, [len(p.instrs) for p in progs], [p.address_register for p in progs]))
    cache = np.fromfile(cachefile, dtype='<u8')
    item = ss.make_item_function(progs, cache)
    ok = bad = 0
    for line in open(reffile):
        _, num, hx = line.split()
        num = int(num)
        if num >= 34078719:
            continue
        got = b''.join(x.to_bytes(8, 'little') for x in item(num)).hex()
        status = 'MATCH' if got == hx else 'MISMATCH'
        ok += got == hx; bad += got != hx
        print('  item %-9d %s %s' % (num, status, got[:32]))
    return ok, bad

r1 = check(b'test key 000', '../data/cache_testkey000.bin', '../ref/items_testkey000.txt')
r2 = check(b'test key 001', '../data/cache_testkey001.bin', '../ref/items_testkey001.txt')
print('items matched: %d, mismatched: %d' % (r1[0] + r2[0], r1[1] + r2[1]))
