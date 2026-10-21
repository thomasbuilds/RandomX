"""design.md 3.4 / App. E claims about SuperscalarHash: average 450 instructions, 155 multiplications,
longest dependency chain 95 instructions.  Measured over N keys x 8 programs with the clean-room generator
(which matches the reference: 12/12 Dataset items, 48/48 hashes)."""
import sys, statistics
sys.path.insert(0, '.')
import rx_superscalar as ss
N = int(sys.argv[1]) if len(sys.argv) > 1 else 500
sizes, muls, chains, addr = [], [], [], [0] * 8
for n in range(N):
    for p in ss.superscalar_init(b'stats key %d' % n):
        sizes.append(len(p.instrs))
        muls.append(sum(1 for i in p.instrs if i[0] in ss.MULTIPLICATIONS))
        asic = [0] * 8
        for name, d, s, mod, imm in p.instrs:
            asic[d] = max(asic[d] + 1, asic[s] + 1 if s != d else 0)
        chains.append(max(asic))
        addr[p.address_register] += 1
def st(x): return 'mean %.1f  sd %.1f  min %d  max %d' % (statistics.mean(x), statistics.pstdev(x), min(x), max(x))
print('programs: %d (keys "stats key 0".."stats key %d")' % (len(sizes), N - 1))
print('instructions    ', st(sizes))
print('multiplications ', st(muls), ' (IMUL_R+IMULH_R+ISMULH_R+IMUL_RCP)')
print('longest chain   ', st(chains), ' (1 cycle per instruction model = address-register latency)')
print('address register histogram r0..r7:', addr)
