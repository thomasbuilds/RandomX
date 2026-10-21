"""Stage 4: light-mode hashes (v1/v2) and commitments vs reference values in ref/hashes.txt and ref/commit.txt.

usage: run_hashes.py <key-name> <v1|v2|both> [variant=value ...] [--only N]
key-name: one of k000, k001, empty, k60 (cache files in ../data/)
"""
import sys, time
import numpy as np
sys.path.insert(0, '.')
import rx_superscalar as ss
import rx_vm

KEYS = {'k000': (b'test key 000', '../data/cache_testkey000.bin'),
        'k001': (b'test key 001', '../data/cache_testkey001.bin'),
        'empty': (b'', '../data/cache_empty.bin'),
        'k60': (b'RandomX example key with exactly sixty bytes of length .....', '../data/cache_k60.bin')}


def load_refs():
    refs = []
    for line in open('../ref/hashes.txt'):
        ver = line[:2]
        ih = line.index(' input_hex=')
        inp = bytes.fromhex(line[ih + 11:].strip())
        head = line[3:ih]
        key, h = head[:-65], head[-64:]
        refs.append((ver, key.encode(), inp, h))
    return refs


def main():
    kname, vsel = sys.argv[1], sys.argv[2]
    variant = {}
    only = None
    args = sys.argv[3:]
    while args:
        a = args.pop(0)
        if a == '--only':
            only = int(args.pop(0))
        else:
            k, v = a.split('=')
            variant[k] = v if v not in ('1', '0') else bool(int(v))
    layout = variant.pop('layout', 'section_order')
    key, cfile = KEYS[kname]
    progs = ss.superscalar_init(key)
    cache = np.fromfile(cfile, dtype='<u8')
    item = ss.make_item_function(progs, cache)
    refs = [r for r in load_refs() if r[1] == key and (vsel == 'both' or r[0] == vsel)]
    if only is not None:
        refs = refs[only:only + 1]
    nm = nb = 0
    for ver, _, inp, want in refs:
        t = time.time()
        got = rx_vm.randomx_hash(inp, item, version=int(ver[1]), variant=variant, layout=layout).hex()
        ok = got == want
        nm += ok; nb += not ok
        print('%s key=%-14r input=%-28r %s %s (%.1fs)%s' % (ver, key[:14], inp[:28], got,
              'MATCH' if ok else 'MISMATCH', time.time() - t, '' if ok else ' want ' + want), flush=True)
    print('summary key=%r versions=%s variant=%s layout=%s: %d match, %d mismatch' % (key, vsel, variant, layout, nm, nb))


if __name__ == '__main__':
    main()
