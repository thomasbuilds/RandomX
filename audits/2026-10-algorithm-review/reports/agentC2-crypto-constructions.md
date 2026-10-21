# Agent C2: cryptographic and pseudo-random constructions of RandomX (master, v1 and v2)

Scope: brief-C items 1-7, branch `claude/modest-meitner-dkvpgd` at d3dd967 (= upstream a901d5b plus hash-neutral audit
fixes). The repository was not modified. All experiments ran on a private copy of the sources with inert
instrumentation hooks (`agentC2/rx`). That copy reproduces the reference vectors with and without a tracker installed
(v1 `639183aa...`, v2 `22ec6b86...`, commitments `d53ccf34...` (v1) and `133be717...` (v2); `logs/vectors.txt`).
Paths below are relative to
`/tmp/claude-0/-home-user-RandomX/cfc10d96-f1d5-53d0-817a-8712f1f55302/scratchpad/agentC2/`, and
`run-all.sh` there rebuilds and reruns every experiment (heavy steps under `taskset -c 0-2 nice -n 10`).

## 1. Findings

No component is broken. Every item is **sound**. Two design statements are wrong or unsupported as written (C2-F1,
C2-F7). Two bounded statistical or structural weaknesses are quantified for the first time here (C2-F2, C2-F10).
Everything else is informational.

| ID | Sev. | Conf. | Item | Finding | Verdict |
|---|---|---|---|---|---|
| C2-F1 | Low | High (measurement); Medium (provenance) | 2 | design.md 2.9.1 says "halving the memory usage increases computational cost 3423 times" for 3-pass Argon2d [27]. That figure does not appear in any version of the Argon2 specification (Oct 2015 to Mar 2017). Their only Argon2d tradeoff table is for 1 pass (1.5x at 1/2 memory). Their summary says the best attack on t-pass Argon2d cuts the time-area product by 1.33. I could not fetch the cited eprint 2015/430 (proxy policy). The conclusion holds anyway, and much more strongly, for RandomX's use of the Cache (random access to the final array). On the real reference graph of the RandomX Cache, dropping only 1/64 of the final blocks makes each miss cost 430,357 block compressions on average (55% of a full Cache initialisation). The cause is Argon2 v1.3's XOR of each block into the previous pass's block at the same position; without that XOR the median miss would cost 1-3 compressions. | sound (mis-cited) |
| C2-F2 | Low | High (measured); model stated | 3 | The initial Scratchpad content can be regenerated on demand: in each lane, one fixed, invertible AES round maps a block to the next. An implementation can store only the words the VM has written, plus AesGenerator1R checkpoints. Measured written fraction, averaged over a hash: 43.8% of words in v1, 46.4% in v2; at the end: 70.6% and 73.1%. Of all word reads, 25.5% (v1) and 19.9% (v2) return initial content. With checkpoints every 16 blocks, memory averages about 52-54% of 2 MiB and peaks at about 78-81%. The cost is about 0.6-0.7 M extra AES rounds per hash. No design claim is violated; the gain needs memory pooled across instances; v2 reduces it slightly. | sound (bounded tradeoff) |
| C2-F3 | Info | High | 4 | The v2 F/E mix uses E unevenly. e0 and e1 are fully diffused. e2 reaches one column (4 of 16 bytes) of each lane. e3 is only the final whitening key, the same in all lanes, so it cancels in the XOR of any two lanes. f0 and f2 share one keyed permutation, and so do f1 and f3. Equal inputs therefore stay equal, the ECB-like property Kudelski flagged for AesGenerator4R in 2019 (KS-RX-O-05, fixed there with separate keys). There is no diffusion across F registers. Not exploitable: avalanche is ideal, every E bit reaches the written block, and that block is statistically uniform. | sound |
| C2-F4 | Info (spec gap) | High | 6 | The commitment `C = Blake2b-256(H \|\| R)` is in the API with test vectors but is not in specs.md. Its encoding is injective and it binds (H, R). By itself it adds no proof-of-work security: verifiers must still check `R = RandomX(K, H)`. Its only security effect is as a cheap pre-filter when R travels with H. With the batch API the output belongs to the previous input. Local history only says "double-hashing" (2023); the intended use is inferred. | sound, under-specified |
| C2-F5 | Info | High | 1 | The Blake2Generator seed encoding is not injective: the key is zero-padded to 60 bytes and its length is not encoded. `K` and `K\|\|00`, and any keys that agree in their first 60 bytes, produce identical SuperscalarHash programs (verified). Argon2 encodes the length, so the Caches differ. Not exploitable: the protocol chooses the key. | sound |
| C2-F6 | Info | High | 1 | There is no domain separation between `Hash512(H)` and `Hash512(RegisterFile)`, nor between the commitment and the final `Hash256(RegisterFile)` (they are the same function when \|H\| = 224). Harmless, because the outputs feed different processes. Hash256 and Hash512 are distinct functions (the digest length is in the Blake2b parameter block). | sound |
| C2-F7 | Info | High | 5 | design.md 2.9.1 says each Dataset item uses "8 random Cache accesses". The first access is to line `itemNumber mod 2^22`; only 7 depend on data. The measured access distribution over all 34,078,719 items is Poisson-uniform for each of the 7 data-dependent steps; no line is read much more often (max 107 reads against a mean of 65). | sound (wording) |
| C2-F8 | Info | High | 4 | specs.md 4.3 presents "arbitrary 128-bit values after step 10" as a v2 property. v1's `f ^ e` is no valid F value either. Measured v1 step-10 outputs: 0.11% NaN patterns, 0.47% zero or subnormal patterns, 10.3% finite values above 3e14. No invariant changes in v2; only the wording needs fixing. | wording |
| C2-F9 | Info | High | 3 | The four 128-bit lanes of AesGenerator1R, AesGenerator4R and AesHash1R never mix. Lane j of `S` alone fixes Scratchpad column j and bytes 16j..16j+15 of every 64-byte block of the first program. A 1-bit Scratchpad change alters only its own fingerprint lane (2000 of 2000 trials). The gen1 to gen4 hand-over makes the first program's seed equal to the last initial Scratchpad line. Knowing the first program therefore costs the whole sequential fill; a one-lane property costs a quarter. Not exploitable, because every seed is a Blake2b output and Blake2b consumes the fingerprint. | sound |
| C2-F10 | Info | High | 5 | SuperscalarHash programs often have constant output bits: 74.3% of 4,000 programs, mean 3.2 of 512 bits, mostly the lowest bits and bits 62-63. For the last program, each such bit passes into every Dataset item as a Cache bit XOR a constant. For key "test key 000" that is 11 bits per item; over the full Dataset bit 450 sits 6.1 sigma off 1/2, exactly as predicted from the Cache's own bit counts. Everywhere else the Cache XOR masks SuperscalarHash's bias: the other 475 bits give chi-square 471 (475 dof). This quantifies Trail of Bits' 2019 "Bias in SuperscalarHash". It is not exploitable, and fixing it would change the Dataset that v1 and v2 share. | sound |

Per-item verdicts (sound, weakened or broken):
1. Blake2b uses: **sound**.
2. Argon2d Cache: **sound**. The cited number is unsupported, but the property holds far more strongly for RandomX's
   access pattern.
3. AES generators and fingerprint: **sound**. Reversibility (known since 2019) has no exploitable consequence; the only
   tradeoff is the bounded one in C2-F2.
4. v2 F/E mix: **sound**, and strictly stronger than v1's XOR.
5. SuperscalarHash and Dataset: **sound**. Statistically clean except the final-step bits of C2-F10.
6. Commitment: **sound** as a construction; missing from the specification.
7. Chaining: **sound**. The arithmetic is exact, and v2's longer programs strengthen it.

## 2. Method

- Private build: `rx/` is a copy of `src/` and `CMakeLists.txt` plus `src/instr_hooks.{hpp,cpp}`. Hooks sit in
  `bytecode_machine.hpp` (memory reads, ISTORE), `vm_interpreted.cpp` (steps 2/3 reads, steps 9/11 writes, step-10
  capture) and `argon2_ref.c` (reference-index log). It is built in `build-instr/`.
- Harnesses are in `h/`. Outputs are in `data/` and `logs/`. The exact commands are in `run-all.sh`.
- Light mode, interpreter, hardware AES unless stated. The host was shared and loaded (load average 9-12). The only
  timing reported (C2-F4) is indicative.

## 3. Item by item

### 3.1 Blake2b uses (item 1)

**Uses.**
- `S = Blake2b-512(H)` seeds the Scratchpad fill.
- `Blake2b-512(RegisterFile)` reseeds AesGenerator4R between programs.
- `R = Blake2b-256(RegisterFile with A = AesHash1R(Scratchpad))` is the final hash.
- Blake2Generator computes `Blake2b-512(K[0..59] || zero padding || nonce32 = 0)`, then iterates `S = Blake2b-512(S)`
  to feed SuperscalarHash generation.
- The commitment is `Blake2b-256(H || R)`.

All calls are unkeyed and unpersonalised.

**Length handling.** H may have any length, including 0. Blake2b encodes the length in its counter, so no ambiguity
arises. Hash256 and Hash512 differ in the parameter block, so they are independent functions.

**Domain separation (C2-F6).** `Hash512(H)` and `Hash512(RF)` are the same function. A 256-byte H equal to some
register file `RF_k` would reproduce that chain's seed `S_{k+1}`, but no work is shared. A seed from H is expanded by
the 2 MiB AesGenerator1R fill, and program 1 comes from the fill's final state. A chain seed goes directly into
AesGenerator4R over the existing Scratchpad. Likewise, for |H| = 224 the commitment and the final RandomX step compute
the same function, but on unrelated inputs.

**Attacker control of H.** Every output of H passes through Blake2b-512. Controlling one 128-bit lane of S, which
C2-F9 would make interesting, needs a 128-bit partial preimage. Landing a lane on a short AES-round cycle needs one of
about 2^15 values out of 2^128. Both are infeasible.

**Blake2Generator encoding (C2-F5).** `bin/keyenc` (`logs/keyenc.txt`) shows identical SuperscalarHash programs, and
different Caches, for each of these pairs:
- "test key 000" and the same key with one trailing 00;
- "abc" and "abc" with four trailing 00;
- two 61-byte keys that differ only in byte 60.

A control pair of different keys differs in both. Not exploitable: Monero's key is a fixed-length 32-byte block hash.

### 3.2 Argon2d Cache and design.md 2.9.1 (item 2)

**The citation.** eprint.iacr.org and the mirrors I tried were refused by the proxy (403). I read every version of
`argon2-specs.pdf` in the PHC repository instead (commits 8fee554, 2015-10-17, to c2cfecf, 2017-03-25;
`dl-phc-argon2-git/spec-*.txt`). None contains "3423".

What they do contain:
- 1-pass Argon2d ranking attack (Table 1): computational penalty C(1/2) = 1.5, C(1/3) = 4, C(1/4) = 20.2,
  C(1/5) = 344, C(1/6) = 4660; depth penalty D(1/2) = 1.5.
- 3-pass Argon2i: about 2^14 at 1/3 memory.
- v1.3 summary: "The best attack on the t-pass Argon2d is the ranking tradeoff attack, which reduces the time-area
  product by the factor 1.33."

So the number cannot be traced to the documentation of version 0x13, which RandomX uses (spec Table 7.1.1). It may stem
from a 2015 analysis of an earlier Argon2 version, whose later passes replaced blocks instead of XORing into them, and
the variant cannot be confirmed (the 3-pass Argon2i figure has a similar magnitude). Status: **cannot be verified; the
citation should be replaced.**

**What matters for RandomX.** RandomX never recomputes Argon2 once with less memory. It needs random access to the
*final* array: 131,072 line reads per light-mode hash, and 8 per Dataset item. In v1.3, pass r >= 1 computes
`B_r[i] = G(B_r[i-1], B[ref]) XOR B_{r-1}[i]`.

I recorded the real reference graph of the Cache for key "test key 000" (3 x 262,144 nodes; references in passes 2-3:
71.1% into the current pass, 28.9% into the previous one). I then counted the distinct compressions needed to rebuild a
missing final block (`bin/argon2graph`, `logs/argon2graph.txt`, 200 random misses per row):

| final blocks dropped | checkpoints of passes 1-2 | memory vs 256 MiB | mean cost per miss | median |
|---|---|---|---|---|
| 1/64 | none | 0.985 | 430,357 | 443,991 |
| 1/2 | none | 0.501 | 464,333 | 483,742 |
| 1/64 | every 16th block | 1.109 | 300,083 | 312,550 |
| 1/2 | every 16th block | 0.624 | 366,109 | 387,211 |
| 1/2 | every 4th block | 1.000 | 5,562 | 4,947 |

A full Cache initialisation is 786,430 compressions, and a light-mode hash would take about 2,000 misses even at 1/64
dropped. No partial-storage strategy is viable.

Counterfactual (`logs/argon2graph-noxor.txt`): the same graph without the `B_{r-1}[i]` input gives a median miss cost of
1-3 compressions for up to 1/4 dropped. So it is v1.3's XOR that makes the Cache incompressible.

**Relevance to mining.** Fast-mode miners touch the Cache only to build the Dataset, once per epoch (2048 blocks).
Light-mode miners, partial-Dataset miners (agent T) and verifiers need the whole Cache permanently. For them 256 MiB is
a hard floor, which is the premise of design.md 1.4.1's area-time argument.

### 3.3 AesGenerator1R, AesGenerator4R, AesHash1R (item 3)

The nothing-up-my-sleeve key derivation was re-derived in round 1.

**Lanes and hand-over (C2-F9).**
- Each generator lane iterates `s_{n+1} = AESDEC or AESENC(s_n, key_j)`.
- gen1's final state is, by construction, the last line of the initial Scratchpad.
- AesGenerator4R seeded with it produces the first program. Lane j yields register `a_j` (quadwords 2j and 2j+1),
  quadwords 8+2j and 9+2j, and instructions whose index is 2j or 2j+1 mod 8.
- Knowing program 1 therefore requires 32,768 dependent AES rounds per lane, with no fast-forward known for an iterated
  AES round. This makes first-program filtering cost about one fill, which Appendix A treats as free.
- AesHash1R lanes are independent too: a 1-bit change of a 2 MiB input gives a mean output distance of 63.84 of 512 bits
  and never touches another lane (`logs/aesbattery.txt`). Blake2b then mixes the lanes.

The reversibility, collision and single-round findings from 2019 (Kudelski KS-RX-O-01, X41 RNDX-PT-19-103/104/105,
Trail of Bits TOB-ARW-001; the last led to AesGenerator4R) remain accurate. design.md 3.2 and 3.3 still hold: states
derive from Blake2b, directly or through a permutation, and the fingerprint input cannot be chosen and goes to Blake2b.

**Battery** (`bin/aesbattery 64 2097152`; TestU01 and PractRand are not installed). 64 buffers of 2 MiB per generator.
Tests: monobit, byte chi-square, per-position byte chi-square, lag-64-byte byte-pair chi-square, lag-64-byte word
correlation, duplicate words, duplicate lane blocks. Every |z| <= 1.6 for AesGenerator1R (Blake2b seeds and the all-zero
seed) and for AesGenerator4R; no duplicates. One AES round already cleans pairwise statistics while the stream stays
fully predictable from the public round function. This battery cannot reproduce Appendix F's TestU01 results.

**Less work than intended?** The fingerprint cannot be shortcut: each lane is a non-linear chain over all 32,768 blocks
in address order, while writes land at random addresses.

The initial content can be left unstored (C2-F2). `bin/sptrack v1|v2 60 7` tracked every access at 8-byte granularity
over 60 hashes per version (`data/sp-v1.txt`, `data/sp-v2.txt`):

| measured | v1 | v2 |
|---|---|---|
| words written by end of hash | 70.6% | 73.1% |
| L3-only region written, assuming the first 256 KiB fully overwritten (design.md 2.8.2: about 66% for v1) | 66.4% | 69.3% |
| lines with at least one write by end of hash | 86.6% | 92.6% |
| written words, averaged over the hash | 43.8% | 46.4% |
| word reads returning initial content: all | 25.5% | 19.9% |
| ...step-2/3 line reads | 56.7% | 54.2% |
| ...program 1 to program 8 | 49.5% -> 11.9% | 42.5% -> 8.7% |

Model. Assumptions: written words stored compactly, plus a 32 KiB bitmap and per-lane checkpoints every c blocks.
Because the round is invertible, regeneration from the nearest checkpoint takes on average c/4 rounds and at most c/2.

For c = 16 (128 KiB of checkpoints):

| | v1 | v2 |
|---|---|---|
| memory, averaged over the hash | about 52% | about 54% |
| memory, peak | about 78% | about 81% |
| lane blocks regenerated per hash | about 155,000 | about 172,000 |
| extra AES rounds per hash | about 0.62 M | about 0.69 M |
| extra AES rounds vs the hash's own AES work | 2.3x of 0.27 M | 1.3x of 0.53 M |

Sensitivity: c = 4 costs 26.6% memory overhead and 0.17 M extra rounds (v2); c = 64 costs 3.1% and 2.7 M. Regeneration
adds at most c/2 dependent rounds of latency to the step-2/3 reads, which lie on the critical path.

Judgement: a constant-factor saving that is complex to implement. The per-instance peak saving is only about 20%; the
average saving needs memory pooled across pipelined instances. Any PRNG-filled Scratchpad has this property; more
rounds only raise the regeneration cost.

### 3.4 The v2 F/E mix (item 4)

**Construction.** Write E for one unkeyed AES round (SubBytes, ShiftRows, MixColumns). Then
`f0' = E(E(E(E(f0)^e0)^e1)^e2)^e3`; f2 goes through the same chain, and f1 and f3 through the corresponding AESDEC
chain. For fixed E each lane is a permutation, the lanes never interact, and AESDEC is not the inverse of AESENC.

I captured 61,440 step-10 states per version and recomputed the mix independently: 0 mismatches (`bin/mixan`,
`logs/mixan-v2.txt`, `logs/mixan-v1.txt`).

| measured (v2 unless noted) | result |
|---|---|
| flip 1 bit of f_j | f_j' changes by 64.00 of 128 bits (min 63.42, max 64.46 over positions); other lanes never change; strict avalanche: max deviation 0.051 over 16,384 bit pairs (1-sigma 0.013, ideal) |
| flip 1 bit of e0 / e1 | about 64.0 / 64.25 bits per lane |
| flip 1 bit of e2 | 16.0-16.2 bits per lane; exactly 4.00 bytes per lane |
| flip 1 bit of e3 | exactly 1.00 bit per lane, the same position in all lanes; f0'^f2' and f1'^f3' unchanged in 10,000 of 10,000 trials (e3 cancels in any pair, since every lane ends with `^e3`) |
| equal lanes (f0 = f2 or f1 = f3) | 0 of 61,440 before and after |
| key bits | 8 constant bits (the sign bits); exponent entropy 8.96 of 11 bits; per-register per-bit entropy at most 122.5 of 128; +inf halves 0.067% (v1: 0.0016%) |
| written F block, v1 | per-bit chi-square (512 dof) 797,136; sum of per-bit entropies 501.5 bits |
| written F block, v2 | per-bit chi-square 491; sum of per-bit entropies 512.0 bits |

Judgement:
- The constrained key bits are irrelevant: keys are only XORed (there is no key schedule), and F's diffusion comes from
  the unkeyed rounds.
- eMask's pattern is mostly gone by step 10.
- AES's column-symmetric invariant subspace is unreachable with these keys.
- Invariants:
  - F after step 10 is only stored, then reloaded or hashed, in both versions (C2-F8, `logs/fpattern.txt`).
  - E still reaches the Scratchpad only through F'.
  - Every E bit affects the written block, which is a bijection of e3 given the rest.
- design_v2.md's claims hold:
  - doubled AES work: 268,552 to 530,696 rounds per hash, x1.976 (`logs/aescount.txt`);
  - more entropy in Scratchpad writes;
  - AES applied to VM registers.
- A future revision could rotate the key order per lane (lane j uses `e_{(i+j) mod 4}`) to diffuse every E register
  fully and remove the shared permutations. It is not needed.

### 3.5 SuperscalarHash and Dataset items (item 5)

**Structure and shortcuts.**
- Item i starts from `r0 = (i+1)*6364136223846793005` and `r_k = r0 ^ c_k`. The first program's input and its Cache
  line (`i mod 2^22`) are known without any computation.
- Steps 1-7 take the low 22 bits of the address register (the longest ASIC dependency chain) *after* the XOR with the
  previous Cache line. The index is therefore uniform whatever SuperscalarHash's bit sensitivity.
- Each load is issued before its program and consumed after it, so a light-mode item is 8 sequential SuperscalarHash
  executions with the Cache latency hidden.
- `SuperscalarHash[0](init(i))` does not depend on the Cache, but precomputing it means a Dataset-sized table, so there
  is no gain.
- Appendix E's precaution holds: bits 3-53 of all 272,629,752 initial register values are distinct. Without the
  specification's adjustments (+2^33+700 for r1, +2^14 for r3) there would be 12 collisions (`bin/initunique`).

**Full Dataset** (key "test key 000", all 34,078,719 items, `bin/dsstat`, `data/ds-full.txt`; self-check against
`randomx::initDatasetItem` on 8,320 items: 0 mismatches):

| measured | result | expectation |
|---|---|---|
| reads per Cache line, each of steps 1-7 | mean 8.125, dispersion 0.9989-1.0003, chi-square 19.6-28.7 (24 dof), max 26-28, unread lines 1,219-1,315 | Poisson: max about 27, 1,242 unread |
| steps 1-7 combined | dispersion 0.9991, chi-square 72.8 (69 dof), max 99 | max about 100 |
| all 8 steps | mean 65.0, max 107, dispersion 0.876 (step 0 reads every line 8-9 times) | no hot lines |
| items whose 8 accesses repeat a line | 233 | 227.5 |
| neighbours (i, i+1) | Hamming distance mean 256.0012, variance 128.01, min 193; per-bit agreement chi-square 506.5 (512 dof) | 256 / 128 |
| duplicate r0 or r7 words | 0 | 3e-5 |
| pairs sharing the step-0 line (i, i + k*2^22): 121,634,808 | equal step-1 index 31 times; r0 Hamming distance 32.0003 | 29.0 / 32 |
| per-bit bias | chi-square 612.2 (512 dof); max deviation 0.000520 at bit 450 (z = -6.07) | explained below |

**Bias explanation** (`bin/dsbias`, `logs/dsbias.txt`, full Dataset from the library's JIT initialiser):
- SuperscalarHash[7] of this key has 11 constant output bits (r2 bits 0-2, r3 bits 62-63, r5 bits 0-2, r7 bits 0-2)
  and 37 bits with |bias| > 0.05.
- Each such item bit equals the step-7 Cache bit XOR a constant, so it follows the bit frequency of the 2^22 Cache
  lines (±0.00026 at 1 sigma). Bit 450: predicted -0.000441, observed -0.000520. Bit 255: predicted -0.000386, observed
  -0.000391.
- The 475 bits with |bias| <= 0.05 give chi-square 471.2 (475 dof).

**Survey** (`bin/sshconst 500 1024`):
- 74.3% of 4,000 programs have at least one constant output bit; mean 3.2, maximum 34.
- 73.6% of keys have one in the last program.
- Constant bits sit at bit 0 (5,514 of about 12,680), halving with each higher bit (multiplications by even constants
  such as IMUL_RCP reciprocals), and at bits 62-63 (373 and 378).

**Judgement (C2-F7, C2-F10).** The Cache XOR masks SuperscalarHash's weaknesses everywhere except the last program's
output. There, about 3 bits per item (in 3 of 4 keys) carry no SuperscalarHash entropy and repeat across the roughly 8
items that share a step-7 line. No mining or verification shortcut follows: the bits remain pseudo-random, the full
program is still needed for the other bits, and miners do not choose items. Changing this would change the Dataset, so
documenting it is enough.

### 3.6 Commitment (item 6)

**History** (read-only git).
- db273f2 (tevador, 2023-04-22) "introduce RandomX v2 with double-hashing" added
  `randomx_calculate_hash_v2(input, v1_hash)` = Blake2b-256(input || v1 hash), with a test vector and a benchmark
  option.
- 07a413b (2023-09-08) renamed it to `randomx_calculate_commitment`.
- tevador's VM changes (1115ec9, 2023-09-07) became v2 in bb6ed2c (#317, 2026) together with the size and prefetch
  changes.
- The commitment stayed a standalone function for both versions (test vectors in tests.cpp) and is in neither specs.md
  nor design_v2.md.
- Batch mode with `--commit` is a TODO in the benchmark.
- No local source states the intended use; agent R covers the public record.

**Properties.**
- Measured: matches an independent `Blake2b-256(input || hash)` for v1 and v2 (`logs/vectors.txt`).
- Encoding: injective, because R has a fixed 32 bytes at the end and Blake2b encodes the total length.
- Binding: (H, R) with 128-bit collision resistance.
- No proof of work of its own: anyone can compute C for any claimed R, so a protocol using C must still verify R.
- Its only security effect appears when R travels with H, in a block or share. A verifier can then reject candidates
  whose C misses the target with one Blake2b call before running RandomX. A forger then needs about D Blake2b calls per
  accepted fake (D = difficulty) instead of none.
- Cost per call: about 0.7 µs here (portable Blake2b, loaded host, `logs/b2speed.txt`; far less on GPUs). Strong at
  block difficulty, weak at share difficulty.
- Miner work is unchanged: C depends on R, so there is no shortcut.
- Pitfall: `randomx_calculate_hash_next` returns the hash of the *previous* input, so the commitment must use that
  previous input.

**Proposed specs.md text** (new 2.1):

> *Commitment (optional).* For input `H` and result `R` (32 bytes), `C = Hash256(H || R)`. `C` is defined identically
> for v1 and v2. It is not a proof of work by itself: a verifier that uses `C` must also check `R` by computing
> RandomX(K, H). Publishing `R` with `H` lets a verifier reject a candidate with one Hash256 call when `C` misses the
> target. Implementations using the batch API must pair each hash with the input that produced it.

### 3.7 Chaining argument (item 7)

`h/chain.py` (`logs/chain.txt`) reproduces Appendix A:
- wasted programs per attempt for N = 8, Q = 0.75: 2.532 ("approximately 2.5");
- N(II) = 1.00 / 2.33 / 6.48 / 26.97 for N = 1 / 2 / 4 / 8;
- Speed(II) = 1.500 / 1.286 / 0.926 / 0.445;
- Speed(III) = 1.050 / 1.024 / 1.012 / 1.006.

These match the table, which rounds to two decimals. The model also treats first-program filtering as free, although
it costs a full sequential fill (3.3).

**Feature-omission filtering.** Hardware that omits instruction X has Q = (1 - f/256)^size. For the rarest
instructions (f = 1: IMULH_M, ISMULH_M, CFROUND), Q is 0.367 in v1 and 0.223 in v2. The v2 CFROUND gate does not help
such hardware: P(no non-zero write in 2048 executions) = (61/64)^2048, about e^-98. Break-even Q at N = 8 is 0.898 for a
1.5x speed-up and 0.610 even at 10x. Omission is therefore hopeless in both versions, and worse in v2.

Nothing in v2 weakens the chain:
- The unchanged first-program seed only shares the fill and a 256-instruction prefix between the v1 and v2 hashes of
  one input, and filtering happens within one version.
- Programs 2-8 are seeded by `Hash512(RF)`.

## 4. Claims of design.md and design_v2.md in this track

| Claim | Status |
|---|---|
| 1.2 / App. A arithmetic | holds exactly; v2 strengthens it (3.7) |
| 2.5.1 XOR of F and E "to maximize entropy" | v1 only; v2 writes are uniform (512.0 vs 501.5 bits) |
| 2.8.2 about 66% of the L3 part overwritten | holds (v1 66.4%; v2 69.3%) |
| 2.9.1 "8 random Cache accesses" | 1 fixed plus 7 random; distribution uniform (C2-F7) |
| 2.9.1 3423x for halving memory with 3 passes | cannot be verified, not in the v1.3 documentation; conclusion holds more strongly (C2-F1) |
| 3.1 AesGenerator1R good distribution | consistent with the small battery |
| 3.2 / 3.3 reversibility harmless; AesHash1R avalanche "in each lane" | hold; no cross-lane avalanche (Blake2b supplies it) |
| App. E unique bits 3-53; low sensitivity | uniqueness holds (0 collisions; 12 without the adjustments); SuperscalarHash even has constant output bits, which matter only in the last program (C2-F10) |
| App. F TestU01 results | not reproducible here; small battery clean |
| design_v2 2: double AES work, better write entropy, AES on VM registers | hold |

## 5. Recommendations (documentation only; no consensus change)

1. design.md 2.9.1: replace the 3423x sentence with the random-access argument (one miss costs about half a Cache
   initialisation in v1.3) and cite the v1.3 specification or RFC 9106. Say "1 fixed and 7 data-dependent Cache
   accesses".
2. specs.md: add the commitment (3.6). In 3.5.1, state that key length is not encoded and that bytes 60-63 hold the
   nonce. In 4.3, say both versions leave non-FP bit patterns in F after step 10.
3. design_v2.md: describe the mix's structure (C2-F3). design.md Appendix E: note the constant output bits and their
   effect on the last step (C2-F10).
4. design.md 2.8 or 3.1: note the regenerable-initial-content tradeoff and its measured bound (C2-F2).

## 6. Artifacts

- `h/`: harnesses `sptrack.cpp`, `mixan.cpp`, `dsstat.cpp`, `dsbias.cpp`, `sshconst.cpp`, `initunique.cpp`,
  `argon2graph.cpp`, `keyenc.cpp`, `aesbattery.cpp`, `vectors.cpp`, `b2speed.cpp`, `chain.py`, `fpattern.py`,
  `aescount.py`.
- `run-all.sh`: build and reproduction script.
- `rx/`, `build-instr/`: instrumented source copy and its build.
- `data/`, `logs/`: raw outputs.
- `audits-txt/`: text of the 2019 audit PDFs.
- `dl-phc-argon2-git/`, `dl-argon2-spec/`: Argon2 specification versions, downloaded data only.
