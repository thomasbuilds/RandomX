# Agent C2: classify the 64-bit halves of F registers after step 10 (captured by sptrack) as IEEE-754 doubles.
import struct, sys
for path in sys.argv[1:]:
    data = open(path, 'rb').read()
    n = len(data) // 192
    nan = inf = big = zero_sub = total = 0
    for k in range(n):
        fpost = data[k*192 + 128: k*192 + 192]
        for h in range(8):
            (v,) = struct.unpack_from('<Q', fpost, 8*h)
            e = (v >> 52) & 0x7FF; m = v & ((1 << 52) - 1)
            total += 1
            if e == 0x7FF: (nan if m else inf).__class__  # placeholder
            if e == 0x7FF and m: nan += 1
            elif e == 0x7FF: inf += 1
            elif e == 0: zero_sub += 1
            else:
                x = struct.unpack('<d', struct.pack('<Q', v))[0]
                if abs(x) > 3.0e14: big += 1
    print(f"{path.split('/')[-1]}: halves {total}: NaN patterns {nan/total:.4%}, inf {inf/total:.4%}, zero/subnormal {zero_sub/total:.4%}, finite |x| > 3e14 {big/total:.4%}")
