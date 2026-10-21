"""Analytic expectations used as reference values in the agent S report."""
import math
IT = 2048 * 8
for ver, N in ((1, 256), (2, 384)):
    blocks = 2 * IT
    p_block_unwritten = (1 - 1 / 32768) ** blocks
    istL3 = N * (16 / 256) * (2 / 16)          # L3 ISTOREs per iteration
    istL12 = N * (16 / 256) * (14 / 16)        # L1/L2 ISTOREs per iteration
    p_word_no_istore = math.exp(-istL3 * IT / 262144)
    cov_L3only = 1 - p_block_unwritten * p_word_no_istore
    p_no_cfround = (255 / 256) ** N
    cbr = N * 25 / 256
    p_jump_iter = 1 - (255 / 256) ** cbr
    print(f"v{ver}: program size {N}")
    print(f"  L3-only coverage (block writes + L3 ISTORE): {cov_L3only*100:.3f}%  [blocks alone {100*(1-p_block_unwritten):.3f}%]")
    print(f"  ISTORE per iteration: L3 {istL3:.2f}, L1/L2 {istL12:.2f}")
    print(f"  CFROUND static per program mean {N/256:.3f}; P(no CFROUND) = {p_no_cfround:.4f}; P(all 8 programs without CFROUND) = {p_no_cfround**8:.3e}")
    w = N / 256 * (1 if ver == 1 else 1 / 16)
    print(f"  fprc writes per iteration ~{w:.4f}; mode changes per iteration ~{w*0.75:.4f} (mean {1/(w*0.75):.2f} iterations between changes)")
    print(f"  CBRANCH per iteration {cbr:.2f}; P(>=1 jump per iteration) = {p_jump_iter*100:.3f}%; P(double | jump) = 1/512 = {1/512:.5f}")
    rne = 0.25 + 0.75 / 8 * sum(p_no_cfround ** (k + 1) for k in range(8))
    print(f"  approx RNE residency (ignoring the delay to the first write) = {rne*100:.2f}%")
