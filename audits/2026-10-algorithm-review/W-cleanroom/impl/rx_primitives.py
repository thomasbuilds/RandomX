"""RandomX clean-room implementation (agent W) -- primitives.

Written from doc/specs.md only (branch d3dd967).  Section numbers refer to that document.
  1.1   Hash256 / Hash512 = Blake2b with 256 / 512-bit output (hashlib, unkeyed, default parameters)
  3.1   AES encryption / decryption round (single round, x86 AESENC / AESDEC semantics)
  3.2   AesGenerator1R
  3.3   AesGenerator4R
  3.4   AesHash1R
  3.5   BlakeGenerator

AES byte order (ambiguity A6 in logs/ambiguities-prereading.md): a 16-byte string is mapped onto the AES
state as in FIPS-197 (byte i -> row i % 4, column i // 4).  The hex strings of chapter 3 are read as byte
strings in memory order.  Both choices are checked by selftest(): the spec says how the constants were
generated (Hash512 of an ASCII string), so the byte order can be verified from the spec alone.
"""
import hashlib
import struct


# ---------------------------------------------------------------- 1.1 Hash256 / Hash512
def hash256(data: bytes) -> bytes:
    return hashlib.blake2b(data, digest_size=32).digest()


def hash512(data: bytes) -> bytes:
    return hashlib.blake2b(data, digest_size=64).digest()


# ---------------------------------------------------------------- 3.1 AES rounds (FIPS-197)
def _build_sbox():
    # FIPS-197 5.1.1: multiplicative inverse in GF(2^8) followed by the affine transformation.
    def gmul(a, b):
        p = 0
        while b:
            if b & 1:
                p ^= a
            a = ((a << 1) ^ 0x11B) if a & 0x80 else (a << 1)
            b >>= 1
        return p
    inv = [0] * 256
    for x in range(1, 256):
        for y in range(1, 256):
            if gmul(x, y) == 1:
                inv[x] = y
                break
    sbox = []
    for x in range(256):
        b = inv[x]
        s = b
        for i in range(1, 5):
            s ^= ((b << i) | (b >> (8 - i))) & 0xFF
        sbox.append(s ^ 0x63)
    isbox = [0] * 256
    for x, s in enumerate(sbox):
        isbox[s] = x
    return sbox, isbox, gmul


SBOX, ISBOX, _gmul = _build_sbox()


def _xt(a, b):
    return _gmul(a, b)


# T-tables on little-endian 32-bit column words: column word c holds bytes 4c..4c+3, byte 4c+r = s[r][c].
# Encryption round (ShiftRows, SubBytes, MixColumns): input byte from row k contributes M[r][k]*S[x] to row r,
# MixColumns matrix rows: [2 3 1 1; 1 2 3 1; 1 1 2 3; 3 1 1 2].
_TE = [[0] * 256 for _ in range(4)]
_TD = [[0] * 256 for _ in range(4)]
for _x in range(256):
    _s = SBOX[_x]
    _col = [_xt(_s, 2), _s, _s, _xt(_s, 3)]                        # contribution of row-0 input to rows 0..3
    _w = _col[0] | (_col[1] << 8) | (_col[2] << 16) | (_col[3] << 24)
    for _k in range(4):
        _TE[_k][_x] = ((_w << (8 * _k)) | (_w >> (32 - 8 * _k))) & 0xFFFFFFFF
    _si = ISBOX[_x]
    _col = [_xt(_si, 14), _xt(_si, 9), _xt(_si, 13), _xt(_si, 11)]  # InvMixColumns [e b d 9; 9 e b d; d 9 e b; b d 9 e]
    _w = _col[0] | (_col[1] << 8) | (_col[2] << 16) | (_col[3] << 24)
    for _k in range(4):
        _TD[_k][_x] = ((_w << (8 * _k)) | (_w >> (32 - 8 * _k))) & 0xFFFFFFFF
TE0, TE1, TE2, TE3 = _TE
TD0, TD1, TD2, TD3 = _TD


def aes_enc_w(s, k):
    """One AES encryption round on 4 LE column words (spec 3.1): ShiftRows, SubBytes, MixColumns, XOR key."""
    s0, s1, s2, s3 = s
    return (TE0[s0 & 255] ^ TE1[(s1 >> 8) & 255] ^ TE2[(s2 >> 16) & 255] ^ TE3[s3 >> 24] ^ k[0],
            TE0[s1 & 255] ^ TE1[(s2 >> 8) & 255] ^ TE2[(s3 >> 16) & 255] ^ TE3[s0 >> 24] ^ k[1],
            TE0[s2 & 255] ^ TE1[(s3 >> 8) & 255] ^ TE2[(s0 >> 16) & 255] ^ TE3[s1 >> 24] ^ k[2],
            TE0[s3 & 255] ^ TE1[(s0 >> 8) & 255] ^ TE2[(s1 >> 16) & 255] ^ TE3[s2 >> 24] ^ k[3])


def aes_dec_w(s, k):
    """One AES decryption round (spec 3.1): InvShiftRows, InvSubBytes, InvMixColumns, XOR key."""
    s0, s1, s2, s3 = s
    return (TD0[s0 & 255] ^ TD1[(s3 >> 8) & 255] ^ TD2[(s2 >> 16) & 255] ^ TD3[s1 >> 24] ^ k[0],
            TD0[s1 & 255] ^ TD1[(s0 >> 8) & 255] ^ TD2[(s3 >> 16) & 255] ^ TD3[s2 >> 24] ^ k[1],
            TD0[s2 & 255] ^ TD1[(s1 >> 8) & 255] ^ TD2[(s0 >> 16) & 255] ^ TD3[s3 >> 24] ^ k[2],
            TD0[s3 & 255] ^ TD1[(s2 >> 8) & 255] ^ TD2[(s1 >> 16) & 255] ^ TD3[s0 >> 24] ^ k[3])


def to_w(b16: bytes):
    return struct.unpack('<4I', b16)


def from_w(w) -> bytes:
    return struct.pack('<4I', *w)


# Straightforward byte-level versions (used only to cross-check the T-table versions).
def _aes_enc_bytes(state: bytes, key: bytes) -> bytes:
    s = [[state[r + 4 * c] for c in range(4)] for r in range(4)]
    s = [[SBOX[s[r][(c + r) % 4]] for c in range(4)] for r in range(4)]          # ShiftRows + SubBytes
    out = bytearray(16)
    for c in range(4):
        a = [s[r][c] for r in range(4)]
        col = [_xt(a[0], 2) ^ _xt(a[1], 3) ^ a[2] ^ a[3], a[0] ^ _xt(a[1], 2) ^ _xt(a[2], 3) ^ a[3],
               a[0] ^ a[1] ^ _xt(a[2], 2) ^ _xt(a[3], 3), _xt(a[0], 3) ^ a[1] ^ a[2] ^ _xt(a[3], 2)]
        for r in range(4):
            out[r + 4 * c] = col[r] ^ key[r + 4 * c]
    return bytes(out)


def _aes_dec_bytes(state: bytes, key: bytes) -> bytes:
    s = [[state[r + 4 * c] for c in range(4)] for r in range(4)]
    s = [[ISBOX[s[r][(c - r) % 4]] for c in range(4)] for r in range(4)]         # InvShiftRows + InvSubBytes
    out = bytearray(16)
    for c in range(4):
        a = [s[r][c] for r in range(4)]
        col = [_xt(a[0], 14) ^ _xt(a[1], 11) ^ _xt(a[2], 13) ^ _xt(a[3], 9),
               _xt(a[0], 9) ^ _xt(a[1], 14) ^ _xt(a[2], 11) ^ _xt(a[3], 13),
               _xt(a[0], 13) ^ _xt(a[1], 9) ^ _xt(a[2], 14) ^ _xt(a[3], 11),
               _xt(a[0], 11) ^ _xt(a[1], 13) ^ _xt(a[2], 9) ^ _xt(a[3], 14)]
        for r in range(4):
            out[r + 4 * c] = col[r] ^ key[r + 4 * c]
    return bytes(out)


def aes_enc_round(state: bytes, key: bytes) -> bytes:
    return from_w(aes_enc_w(to_w(state), to_w(key)))


def aes_dec_round(state: bytes, key: bytes) -> bytes:
    return from_w(aes_dec_w(to_w(state), to_w(key)))


# ---------------------------------------------------------------- 3.2 - 3.4 constants
def _hx(s):
    return bytes.fromhex(s.replace(' ', ''))


GEN1R_KEYS = [_hx(s) for s in (
    "53 a5 ac 6d 09 66 71 62 2b 55 b5 db 17 49 f4 b4",
    "07 af 7c 6d 0d 71 6a 84 78 d3 25 17 4e dc a1 0d",
    "f1 62 12 3f c6 7e 94 9f 4f 79 c0 f4 45 e3 20 3e",
    "35 81 ef 6a 7c 31 ba b1 88 4c 31 16 54 91 16 49")]
GEN4R_KEYS = [_hx(s) for s in (
    "dd aa 21 64 db 3d 83 d1 2b 6d 54 2f 3f d2 e5 99",
    "50 34 0e b2 55 3f 91 b6 53 9d f7 06 e5 cd df a5",
    "04 d9 3e 5c af 7b 5e 51 9f 67 a4 0a bf 02 1c 17",
    "63 37 62 85 08 5d 8f e7 85 37 67 cd 91 d2 de d8",
    "73 6f 82 b5 a6 a7 d6 e3 6d 8b 51 3d b4 ff 9e 22",
    "f3 6b 56 c7 d9 b3 10 9c 4e 4d 02 e9 d2 b7 72 b2",
    "e7 c9 73 f2 8b a3 65 f7 0a 66 a9 2b a7 ef 3b f6",
    "09 d6 7c 7a de 39 58 91 fd d1 06 0c 2d 76 b0 c0")]
HASH1R_STATE = [_hx(s) for s in (
    "0d 2c b5 92 de 56 a8 9f 47 db 82 cc ad 3a 98 d7",
    "6e 99 8d 33 98 b7 c7 15 5a 12 9e f5 57 80 e7 ac",
    "17 00 77 6a d0 c7 62 ae 6b 50 79 50 e4 7c a0 e8",
    "0c 24 0a 63 8d 82 ad 07 05 00 a1 79 48 49 99 7e")]
HASH1R_XKEYS = [_hx(s) for s in (
    "89 83 fa f6 9f 94 24 8b bf 56 dc 90 01 02 89 06",
    "d1 63 b2 61 3c e0 f4 51 c6 43 10 ee 9b f9 18 ed")]

_G1K = [to_w(k) for k in GEN1R_KEYS]
_G4K = [to_w(k) for k in GEN4R_KEYS]
_XK = [to_w(k) for k in HASH1R_XKEYS]


# ---------------------------------------------------------------- 3.2 AesGenerator1R
class AesGenerator1R:
    def __init__(self, seed64: bytes):
        assert len(seed64) == 64
        self.s = [to_w(seed64[16 * i:16 * i + 16]) for i in range(4)]

    @property
    def state(self) -> bytes:
        return b''.join(from_w(w) for w in self.s)

    def generate(self, nbytes: int) -> bytes:
        assert nbytes % 64 == 0
        k0, k1, k2, k3 = _G1K
        s0, s1, s2, s3 = self.s
        out = []
        pk = struct.pack
        for _ in range(nbytes // 64):
            s0 = aes_dec_w(s0, k0)
            s1 = aes_enc_w(s1, k1)
            s2 = aes_dec_w(s2, k2)
            s3 = aes_enc_w(s3, k3)
            out.append(pk('<16I', *s0, *s1, *s2, *s3))
        self.s = [s0, s1, s2, s3]
        return b''.join(out)


# ---------------------------------------------------------------- 3.3 AesGenerator4R
class AesGenerator4R:
    def __init__(self, seed64: bytes):
        self.set_state(seed64)

    def set_state(self, seed64: bytes):
        assert len(seed64) == 64
        self.s = [to_w(seed64[16 * i:16 * i + 16]) for i in range(4)]

    def generate(self, nbytes: int) -> bytes:
        assert nbytes % 64 == 0
        k = _G4K
        s0, s1, s2, s3 = self.s
        out = []
        for _ in range(nbytes // 64):
            for j in range(4):
                s0 = aes_dec_w(s0, k[j])
                s1 = aes_enc_w(s1, k[j])
                s2 = aes_dec_w(s2, k[4 + j])
                s3 = aes_enc_w(s3, k[4 + j])
            out.append(struct.pack('<16I', *s0, *s1, *s2, *s3))
        self.s = [s0, s1, s2, s3]
        return b''.join(out)


# ---------------------------------------------------------------- 3.4 AesHash1R
def aes_hash_1r(data: bytes) -> bytes:
    assert len(data) % 64 == 0
    s0, s1, s2, s3 = [to_w(x) for x in HASH1R_STATE]
    words = struct.unpack('<%dI' % (len(data) // 4), data)
    enc, dec = aes_enc_w, aes_dec_w
    for i in range(0, len(words), 16):
        s0 = enc(s0, words[i:i + 4])
        s1 = dec(s1, words[i + 4:i + 8])
        s2 = enc(s2, words[i + 8:i + 12])
        s3 = dec(s3, words[i + 12:i + 16])
    for xk in _XK:
        s0 = enc(s0, xk)
        s1 = dec(s1, xk)
        s2 = enc(s2, xk)
        s3 = dec(s3, xk)
    return struct.pack('<16I', *s0, *s1, *s2, *s3)


# ---------------------------------------------------------------- 3.5 BlakeGenerator
class BlakeGenerator:
    """3.5: 64-byte state S.  3.5.1: seed written into the state, zero padded, S = Hash512(S).
    3.5.2: 1 or 4 bytes at a time from S; when not enough unused bytes are left, S = Hash512(S).
    Ambiguity A5: the 4-byte value is read little-endian and leftover bytes (1-3) are discarded."""

    def __init__(self, seed: bytes):
        assert len(seed) <= 60, "3.5.1: seed is 0-60 bytes"
        self.S = hash512(seed + bytes(64 - len(seed)))
        self.pos = 0

    def _need(self, n):
        if self.pos + n > 64:
            self.S = hash512(self.S)
            self.pos = 0

    def get_byte(self) -> int:
        self._need(1)
        v = self.S[self.pos]
        self.pos += 1
        return v

    def get_uint32(self) -> int:
        self._need(4)
        v = struct.unpack_from('<I', self.S, self.pos)[0]
        self.pos += 4
        return v


# ---------------------------------------------------------------- self tests
def selftest():
    # FIPS-197 Appendix B, round 1: state after AddRoundKey(w0) -> AesEnc with round key 1 = start of round 2.
    st = _hx("19 3d e3 be a0 f4 e2 2b 9a c6 8d 2a e9 f8 48 08")
    rk1 = _hx("a0 fa fe 17 88 54 2c b1 23 a3 39 39 2a 6c 76 05")
    want = _hx("a4 9c 7f f2 68 9f 35 2b 6b 5b ea 43 02 6a 50 49")
    assert _aes_enc_bytes(st, rk1) == want and aes_enc_round(st, rk1) == want, "AES enc round (FIPS-197 App. B)"
    # Full AES-128 with the FIPS-197 Appendix B vector, encryption with the enc round and decryption with the
    # dec round through the "equivalent inverse cipher" (FIPS-197 5.3.5) -> checks aes_dec_round as well.
    key = _hx("2b 7e 15 16 28 ae d2 a6 ab f7 15 88 09 cf 4f 3c")
    pt = _hx("32 43 f6 a8 88 5a 30 8d 31 31 98 a2 e0 37 07 34")
    ct = _hx("39 25 84 1d 02 dc 09 fb dc 11 85 97 19 6a 0b 32")
    rcon = [1, 2, 4, 8, 16, 32, 64, 128, 27, 54]
    w = [list(key[4 * i:4 * i + 4]) for i in range(4)]
    for i in range(4, 44):
        t = list(w[i - 1])
        if i % 4 == 0:
            t = [SBOX[b] for b in t[1:] + t[:1]]
            t[0] ^= rcon[i // 4 - 1]
        w.append([a ^ b for a, b in zip(w[i - 4], t)])
    rk = [bytes(sum(w[4 * r:4 * r + 4], [])) for r in range(11)]
    x = bytes(a ^ b for a, b in zip(pt, rk[0]))
    for r in range(1, 10):
        x = aes_enc_round(x, rk[r])
    x = [x[i] for i in range(16)]
    x = bytes(SBOX[x[(i % 4) + 4 * (((i // 4) + (i % 4)) % 4)]] ^ rk[10][i] for i in range(16))
    assert x == ct, "AES-128 encryption"

    def inv_mix(b):
        return _aes_dec_bytes(bytes(SBOX[(b[(i % 4) + 4 * (((i // 4) + (i % 4)) % 4)])] for i in range(16)),
                              bytes(16))  # InvMixColumns(b) via dec round on SubBytes/ShiftRows-ed input
    y = bytes(a ^ b for a, b in zip(ct, rk[10]))
    for r in range(9, 0, -1):
        y = aes_dec_round(y, inv_mix(rk[r]))
    y = bytes(ISBOX[y[(i % 4) + 4 * (((i // 4) - (i % 4)) % 4)]] ^ rk[0][i] for i in range(16))
    assert y == pt, "AES-128 decryption (equivalent inverse cipher)"
    # random cross-check of the T-table rounds against the byte-level rounds
    import os
    for _ in range(200):
        a, b = os.urandom(16), os.urandom(16)
        assert aes_enc_round(a, b) == _aes_enc_bytes(a, b)
        assert aes_dec_round(a, b) == _aes_dec_bytes(a, b)
    # Chapter 3 constants: "generated as Hash512(...)" (this fixes the byte order of the hex strings).
    assert b''.join(GEN1R_KEYS) == hash512(b"RandomX AesGenerator1R keys")
    assert b''.join(GEN4R_KEYS[:4]) == hash512(b"RandomX AesGenerator4R keys 0-3")
    assert b''.join(GEN4R_KEYS[4:]) == hash512(b"RandomX AesGenerator4R keys 4-7")
    assert b''.join(HASH1R_STATE) == hash512(b"RandomX AesHash1R state")
    assert b''.join(HASH1R_XKEYS) == hash256(b"RandomX AesHash1R xkeys")
    return "primitives selftest OK"


if __name__ == '__main__':
    print(selftest())
