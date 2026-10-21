"""Per-bit P(1) of the high bits (52-63) of each 8-byte word of the F block, and of E/F register fields (v1 vs v2)."""
import sys, os, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from an_common import bit_bias_from_byte_hist, entropy_bits
for pre in sys.argv[1:]:
    h = np.fromfile(pre + "_fhist.bin", dtype=np.uint64).reshape(64, 256)
    bits, n = bit_bias_from_byte_hist(h)
    print(f"{pre}: N={int(n)} F blocks; P(bit=1) for bits 52..63 of each word (rows = word 0..7: f0.lo f0.hi f1.lo ...)")
    for w in range(8):
        print(f"  word {w}: " + " ".join(f"{bits[w*64+b]:.3f}" for b in range(52, 64)))
    # 12-bit field histogram of the F-block words, of F lanes and E lanes
    for nm in ("fblk12", "fexp", "eexp"):
        x = np.fromfile(f"{pre}_{nm}.bin", dtype=np.uint64)
        print(f"  {nm}: entropy {entropy_bits(x):.3f} bits; P(top12 bit 62 set) = {x[np.arange(4096) & 0x400 > 0].sum()/x.sum():.4f}")
    e16 = np.fromfile(pre + "_etop16.bin", dtype=np.uint64)
    print(f"  E lanes top-16-bit entropy {entropy_bits(e16):.3f} of 16 bits (as AES round-key bytes 6-7 of each lane)")
