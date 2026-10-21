"""V-12: key longer than 60 bytes.  Reading of randomx.h: Argon2d uses the whole key, the BlakeGenerator the
first 60 bytes.  Compare with the reference hash in ../ref/hash_k64.txt (the key is 65 bytes long)."""
import sys, numpy as np
sys.path.insert(0, '.')
import rx_superscalar as ss, rx_vm
key = b"RandomX example key that is sixty-four bytes long, past the limit"
assert len(key) == 65
want = open('../ref/hash_k64.txt').read().split('\n')[0].split()[-2]
progs = ss.superscalar_init(key)                       # uses key[:60]
cache = np.fromfile('../data/cache_k65.bin', dtype='<u8')  # Argon2d with the full 65-byte key
got = rx_vm.randomx_hash(b'This is a test', ss.make_item_function(progs, cache), version=1).hex()
print('65-byte key, v1, "This is a test":', got, 'MATCH' if got == want else 'MISMATCH want ' + want)
