import sys
sys.path.insert(0, sys.argv[1])
from rx_primitives import BlakeGenerator
import appendixA_impl as A
n = int(sys.argv[2])
out = []
for k in range(n):
    gen = BlakeGenerator(("sshcmp-%d" % k).encode()[:60])
    for p in range(8):
        prog, addr = A.generate(gen)
        out.append("K%d P%d addr=%d size=%d" % (k, p, addr, len(prog)))
        for (name, dst, src, mod, imm) in prog:
            out.append("%s %d %d %d %d" % (name, dst, src, mod, imm & 0xffffffff))
print("\n".join(out))
