import sys
sys.path.insert(0, sys.argv[1])
import rx_superscalar as s
n = int(sys.argv[2])
out = []
for k in range(n):
    progs = s.superscalar_init(("sshcmp-%d" % k).encode())
    for p, prog in enumerate(progs):
        out.append("K%d P%d addr=%d size=%d" % (k, p, prog.address_register, len(prog.instrs)))
        for (name, dst, src, mod, imm) in prog.instrs:
            out.append("%s %d %d %d %d" % (name, dst, src, mod, imm & 0xffffffff))
print("\n".join(out))
