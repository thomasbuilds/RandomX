# Agent C2: AES rounds per hash (design_v2.md "doubles the amount of AES computations").
fill = 2097152 // 64 * 4            # AesGenerator1R: one round per 16-byte lane block
fp = fill + 8                       # AesHash1R: one round per lane block + 2 extra rounds x 4 lanes
gen_impl = 8 * (3200 // 64) * 4 * 4 # AesGenerator4R: 3200 bytes generated per program (v1 and v2), 4 rounds per lane block
gen_v1min = 8 * ((128 + 8 * 256) // 64) * 4 * 4
mix = 2048 * 8 * 16                 # v2 step 10: 4 registers x 4 rounds per iteration
v1 = fill + gen_impl + fp; v2 = v1 + mix
print('fill', fill, 'fingerprint', fp, 'gen4 (3200 B/program, impl.)', gen_impl, 'gen4 v1 spec minimum', gen_v1min, 'v2 mix', mix)
print('v1 total', v1, '(spec-minimum generation:', fill + gen_v1min + fp, ') v2 total', v2, 'ratio %.4f' % (v2 / v1))
