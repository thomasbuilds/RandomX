"""RandomX clean-room implementation (agent W) -- Argon2d memory fill (spec 7.1, Table 7.1.1) from RFC 9106.

Spec 7.1: "The key K is expanded into the Cache using the 'memory fill' function of Argon2d with parameters
according to Table 7.1.1 ... The finalizer and output calculation steps of Argon2 are omitted. The output is
the filled memory array."  Table 7.1.1: parallelism = RANDOMX_ARGON_LANES (1), output size 0,
memory = RANDOMX_ARGON_MEMORY (262144 KiB), iterations 3, version 0x13, type 0, password K,
salt RANDOMX_ARGON_SALT ("RandomX\\x03"), no secret, no associated data.
Ambiguity A9: "output size 0" is hashed into H0 as LE32(0) (literal reading).
"""
import hashlib
import struct
import sys
import time
import numpy as np

M32 = np.uint64(0xFFFFFFFF)
U = np.uint64


def _blake2b(data, n):
    return hashlib.blake2b(data, digest_size=n).digest()


def h_prime(data: bytes, T: int) -> bytes:
    """RFC 9106 3.3 variable-length hash H'^T."""
    if T <= 64:
        return _blake2b(struct.pack('<I', T) + data, T)
    r = -(-T // 32) - 2
    v = _blake2b(struct.pack('<I', T) + data, 64)
    out = [v[:32]]
    for _ in range(r - 1):
        v = _blake2b(v, 64)
        out.append(v[:32])
    out.append(_blake2b(v, T - 32 * r))
    return b''.join(out)


def _rotr(x, n):
    return (x >> U(n)) | (x << U(64 - n))


def _fbla(x, y):
    return x + y + U(2) * (x & M32) * (y & M32)


def _gb(a, b, c, d):
    a = _fbla(a, b); d = _rotr(d ^ a, 32)
    c = _fbla(c, d); b = _rotr(b ^ c, 24)
    a = _fbla(a, b); d = _rotr(d ^ a, 16)
    c = _fbla(c, d); b = _rotr(b ^ c, 63)
    return a, b, c, d


_DB = [5, 6, 7, 4]
_DC = [10, 11, 8, 9]
_DD = [15, 12, 13, 14]


def _P(v):
    """RFC 9106 3.6 permutation P applied to each row of an (8,16) array (8 independent P calls)."""
    a, b, c, d = _gb(v[:, 0:4], v[:, 4:8], v[:, 8:12], v[:, 12:16])
    a, b, c, d = _gb(a, b[:, [1, 2, 3, 0]], c[:, [2, 3, 0, 1]], d[:, [3, 0, 1, 2]])
    out = np.empty_like(v)
    out[:, 0:4] = a
    out[:, _DB] = b
    out[:, _DC] = c
    out[:, _DD] = d
    return out


_COLS = np.array([[2 * j + 16 * t + k for t in range(8) for k in range(2)] for j in range(8)])


def compress(X, Y):
    """RFC 9106 3.5 compression function G on two 128-word blocks."""
    R = X ^ Y
    Q = _P(R.reshape(8, 16)).reshape(128)
    Z = np.empty(128, dtype=np.uint64)
    Z[_COLS] = _P(Q[_COLS])
    return Z ^ R


def argon2d_fill(password: bytes, salt: bytes, m: int, t: int, p: int, T: int = 0, secret=b'', ad=b'',
                 version=0x13, y=0, progress=False):
    """Returns the filled memory as an (m', 128) uint64 array (lanes concatenated)."""
    h0 = _blake2b(struct.pack('<IIIIII', p, T, m, t, version, y) + struct.pack('<I', len(password)) + password +
                  struct.pack('<I', len(salt)) + salt + struct.pack('<I', len(secret)) + secret +
                  struct.pack('<I', len(ad)) + ad, 64)
    mp = 4 * p * (m // (4 * p))
    q = mp // p
    seg = q // 4
    B = np.zeros((mp, 128), dtype=np.uint64)
    for i in range(p):
        for j in range(2):
            B[i * q + j] = np.frombuffer(h_prime(h0 + struct.pack('<II', j, i), 1024), dtype='<u8')
    t0 = time.time()
    for ps in range(t):
        for sl in range(4):
            for ln in range(p):
                base = ln * q
                start_idx = 2 if (ps == 0 and sl == 0) else 0
                for idx in range(start_idx, seg):
                    j = sl * seg + idx
                    prev = base + (j - 1 if j > 0 else q - 1)
                    w0 = int(B[prev, 0])
                    J1, J2 = w0 & 0xFFFFFFFF, w0 >> 32
                    ref_lane = ln if (ps == 0 and sl == 0) else J2 % p
                    same = ref_lane == ln
                    if ps == 0:
                        if sl == 0:
                            area = idx - 1
                        elif same:
                            area = sl * seg + idx - 1
                        else:
                            area = sl * seg - (1 if idx == 0 else 0)
                    else:
                        area = q - seg + idx - 1 if same else q - seg - (1 if idx == 0 else 0)
                    x = (J1 * J1) >> 32
                    rel = area - 1 - ((area * x) >> 32)
                    startpos = 0 if ps == 0 else ((sl + 1) * seg) % q
                    ref = ref_lane * q + (startpos + rel) % q
                    nb = compress(B[prev], B[ref])
                    if ps == 0:
                        B[base + j] = nb
                    else:
                        B[base + j] ^= nb
            if progress:
                print('pass %d slice %d done %.1fs' % (ps, sl, time.time() - t0), file=sys.stderr, flush=True)
    return B, h0


def argon2d_hash(password, salt, m, t, p, T, secret=b'', ad=b''):
    B, h0 = argon2d_fill(password, salt, m, t, p, T, secret, ad)
    q = B.shape[0] // p
    C = B[q - 1].copy()
    for i in range(1, p):
        C ^= B[i * q + q - 1]
    return h_prime(C.astype('<u8').tobytes(), T), h0


def selftest():
    # RFC 9106 5.1 Argon2d test vector
    tag, h0 = argon2d_hash(b'\x01' * 32, b'\x02' * 16, 32, 3, 4, 32, b'\x03' * 8, b'\x04' * 12)
    want_h0 = bytes.fromhex("b8819791a0359660bb7709c85fa48f04d5d82c05c5f215ccdb885491717cf757"
                            "082c28b951be381410b5fc2eb7274033b9fdc7ae672bcaac5d179097a4af3109")
    want = bytes.fromhex("512b391b6f1162975371d30919734294f868e3be3984f3c1a13a4db9fabe4acb")
    print("H0  ", h0.hex(), "match" if h0 == want_h0 else "MISMATCH")
    print("tag ", tag.hex(), "match" if tag == want else "MISMATCH")
    assert tag == want


RANDOMX_ARGON_MEMORY = 262144
RANDOMX_ARGON_ITERATIONS = 3
RANDOMX_ARGON_LANES = 1
RANDOMX_ARGON_SALT = b"RandomX\x03"


def randomx_cache(key: bytes, progress=False):
    """Spec 7.1: Cache = Argon2d memory fill, finalizer omitted."""
    B, _ = argon2d_fill(key, RANDOMX_ARGON_SALT, RANDOMX_ARGON_MEMORY, RANDOMX_ARGON_ITERATIONS,
                        RANDOMX_ARGON_LANES, T=0, progress=progress)
    return B


if __name__ == '__main__':
    if sys.argv[1:2] == ['selftest']:
        selftest()
    elif sys.argv[1:2] == ['cache']:
        key = sys.argv[2].encode()
        out = sys.argv[3]
        t0 = time.time()
        B = randomx_cache(key, progress=True)
        B.astype('<u8').tofile(out)
        print('cache written to %s in %.1fs' % (out, time.time() - t0), file=sys.stderr)
