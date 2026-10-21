# Agent S: statistical and structural properties of the RandomX VM state, v1 vs v2

Algorithm-level review, round 3. Track S (brief-S.md). Tree: branch `claude/modest-meitner-dkvpgd` (HEAD d3dd967), read-only.
All work in `scratchpad/agentS/` (absolute path `/tmp/claude-0/-home-user-RandomX/cfc10d96-f1d5-53d0-817a-8712f1f55302/scratchpad/agentS/`,
abbreviated `agentS/` below). Every heavy command ran under `taskset -c 0-2 nice -n 10`. Date: 2026-10-21.

## 1. Bottom line

I found no statistical or structural property of the RandomX v2 VM state that helps an attacker or a specialised
implementation. Every quantitative claim of design.md / design_v2.md that I could test holds within measurement
uncertainty, with two qualifications. First, the v1 rationale that XOR combines the F and E registers "to maximize
entropy" is false for the sign and exponent bits. Second, design_v2.md's "CFROUND will now change the rounding mode only
every 16th time it executes" is approximate: the gate lets the write through in 1/15 of executions and the mode actually
changes in 1/20.

v2 changes the state statistics in five measurable ways:

1. **The F block is now uniform (S-1).** The 64-byte F block written every iteration is strongly structured in v1:
   byte 7 of every word carries 5.83 of 8 bits, P(bit 62 = 1) = 0.105, and the byte-marginal deficit is at least 17.4
   bits per block. With the AES mix, v2 shows no bias at 49.2 million blocks, not even against the low-entropy E
   registers used as round keys.
2. **One erasure path disappears (S-2).** In v1 the XOR confines a perturbation of the end-of-iteration F/E state to
   one 8-byte Scratchpad word. An ISTORE overwrites that word before it is read in about 9% of cases. AES spreads the
   same perturbation over 2 to 8 words, so erasure falls to 0.9% for F and 0 of 1200 trials for E.
3. **Floating-point absorption inside the program grows (S-3).** An FSCAL scales a value down by up to 2^15 and a
   later FADD/FSUB rounds the difference away. In addition, E registers overflow to infinity. A flipped F input bit is
   erased within its own iteration in 0.56% of cases in v1 and 2.47% in v2; for E inputs the rates are 0.02% and 0.16%.
4. **Infinities in E become 6.5 times more frequent per iteration (S-4).** Both design.md figures are confirmed:
   1.91% of v1 programs and 6.54% of v2 programs.
5. **Rounding-mode changes become 10 times rarer (S-6).** Yet a whole hash runs in roundTiesToEven even less often
   (about 4e-4 in v1, about 6e-6 in v2), so every implementation must still support all four rounding modes.

The other questions also check out:
- **CBRANCH (S-7):** the claims hold; no CBRANCH ever jumped three times in a row, and this is proven in §4.6.
- **Write coverage (S-8):** 67.5% of the L3-only area is overwritten per hash in v1 and 69.5% in v2.
- **Compressibility (S-8):** xz -9e reduces 48 final Scratchpads to 99.975% of their size in v1 and 99.988% in v2.
  Appendix D reported 99.98% for v1.
- **Filtering strategy III (S-9):** filtering slow first programs loses hashrate in both versions once the cost of a
  new nonce is counted.
- **v1/v2 relation (S-10):** the v1 and v2 hashes of the same input are statistically independent. The work they can
  share is about 3% of a fast-mode hash and negligible in light mode.

Nothing in this track rates above Low.

## 2. Findings

| ID | Sev. | Conf. | Finding | v1 | v2 |
|----|------|-------|---------|----|----|
| S-1 | Low (v1 only, resolved by v2) | High | F block written in step 11 is non-uniform in v1. The design.md 2.5.1 rationale "To maximize entropy ... combined using the XOR operation" does not hold for the sign/exponent field. design_v2.md "AES tweak also improves data entropy" holds. | byte 7 of each word: 5.83 bits; P(bit 62=1)=0.105, bit 61 0.12, bit 60 0.21, bit 59 0.37; ≥17.4 bits of 512 missing per block; byte-pair mutual information up to 1.0 bit | all 512 bit probabilities in [0.49978, 0.50021]; total chi2 p=0.061; sign+exponent field 12.000 of 12 bits; 8 byte-pair tests incl. vs round key e3: MI equals estimator bias |
| S-2 | Info | High | A single-bit perturbation of F/E state after step 4 leaves the hash unchanged when the one Scratchpad word holding it is overwritten by ISTORE before being read. The XOR confines the perturbation to one word; AES spreads it to 2 (f_i) or 4 to 8 (e_i) words. | F_end 9.0% [7.5, 10.8]; E_end 9.3% [7.8, 11.1] | F_end 0.92% [0.51, 1.63]; E_end 0.0% [0, 0.32] |
| S-3 | Info | High | Floating-point absorption within one iteration (FSCAL down-scaling followed by FADD/FSUB rounding; E overflow to infinity) erases F/E input bits. v2 increases it (programs 1.5x longer). | F input 0.56% [0.52, 0.60] (int32 bits 0-3: 2.3-2.5%); E 0.02% | F input 2.47% [2.39, 2.56] (bits 0-3: 8.5-9.3%); E 0.16% |
| S-4 | Info | High | Infinity in E. design.md 2.5.1 claims "About 2% (6.85% for RandomX v2) of programs". Both numbers verified. Overflow including DBL_MAX saturation under directed rounding is somewhat higher. | 1.91% [1.75, 2.09] of programs; overflow 2.28% [2.10, 2.48]; 0.109% of iterations | 6.54% [6.24, 6.86]; overflow 7.66% [7.33, 8.01]; 0.704% of iterations |
| S-5 | Info | High | NaN, subnormal, underflow and divide-by-zero never occur (spec 5.3 invariant), empirically, also in v2. | 0 in 8.87e9 FP results; MXCSR IE/DE/ZE/UE never set (24,000 programs) | 0 in 1.33e10; never set (24,000 programs) |
| S-6 | Info | High | CFROUND: v2 cuts mode changes about 10x. The gate passes in 1/15, not 1/16, of executions. All four modes remain mandatory. | 0.751 changes/iteration; 12,298/hash; P(hash all-RNE) ≈ 3.9e-4 | 0.0745/iteration; 1,221/hash; P(all-RNE) ≈ 6.2e-6 |
| S-7 | Info | High | CBRANCH claims hold: jump probability ≈ 1/256 (0.3% lower, measurable); "up to twice in a row" (never 3, proof in §4.6). | taken 1/256.8; 9.32% of iterations jump | 1/256.6; 13.61% |
| S-8 | Info | High | Write coverage and compressibility of the final Scratchpad. design.md 2.8.2 "about 66%" and Appendix D "99.98%" confirmed. Residual structure in both versions: ISTORE copies of the same register value (0.7% of words duplicated) and low/high-byte bias of ISTORE-written words. | L3-only coverage 67.52% ± 0.04; xz -9e 99.975% | 69.45% ± 0.04; xz -9e 99.988% |
| S-9 | Info | Medium | Runtime: program-dependent runtime spread is small. Filtering the slowest first programs (strategy III) is unprofitable even with perfect knowledge of the runtime. | per-program cv ≤ 4.7%; reject 25%: hashrate x0.992 | ≤ 6.3%; x0.992 |
| S-10 | Info | High | v1/v2 of the same input share only the Blake2b seed, the 2 MiB fill, program 0's configuration and first 256 instructions, the start of iteration 0 and one Dataset item. Outputs are independent. | - | 20,000 pairs: bit agreement 0.50014 (z=0.62) |
| S-11 | Info | High | Other "dead" state in both versions: Scratchpad words overwritten before being read (L2 region about 25%); top bits of R registers multiplied out by even factors. | spad flip unchanged 8.7%; R bit 63: 22% | 10.1%; R bit 63: 10% |

## 3. Claims of design.md / design_v2.md checked here

| Claim (location) | Status | Evidence |
|---|---|---|
| "To maximize entropy ... floating point registers are combined using the XOR operation" (design 2.5.1, v1) | Does not hold (sign/exponent bits) | S-1: ≥17.4 bits/block missing; bit 62 of each word P(1)=0.105 |
| "AES tweak also improves data entropy (makes it more random) before it's written to the scratchpad" (design_v2 §2) | Holds | S-1: no measurable bias in 4.9e7 blocks; no pair dependence |
| "About 2% (6.85% for RandomX v2) of programs produce at least one infinity" (design 2.5.1) | Holds (v2 at the upper edge of the 95% CI) | 1.91% [1.75, 2.09]; 6.54% [6.24, 6.86] |
| Range of E "1.7E-77 to infinity" (design 2.5.1) | Holds | observed exponent range 2^-243 (v1), 2^-216 (v2) up to infinity |
| F range "around ±3.0e+14" (design 2.5.1) | Holds at iteration boundaries (v1 max 2^48 ≈ 2.8e14; v2 max 2^49 ≈ 5.6e14) | `analysis/stats_v{1,2}.txt` |
| "no operation results in NaN or a denormal number" (specs 5.3) | Holds | S-5 |
| "CFROUND will now change the rounding mode only every 16th time it executes" (design_v2 §1) | Approximately: writes 1/14.96, changes 1/20.3 of executions | §4.5 |
| CBRANCH "jump probability of 1/256" (design 2.6.2) | Holds (1/256.8, 1/256.6) | §4.6 |
| "Each CBRANCH instruction can jump up to twice in a row" (design 2.6.2) | Holds (max 2 in 12.0M jump runs; proof) | §4.6 |
| "Only about 66% of it [bottom 1792 KiB] are overwritten" (design 2.8.2) | Holds (67.5% v1; 69.5% v2) | §4.2 |
| "16 stores per program, out of which 2 stores are into L3" (design 2.8.2, per iteration) | Holds statically (v2: 24 and 3) | §4.2 |
| Appendix D: LZMA ultra "approximately 99.98%" | Holds (xz -9e 99.975% v1; 99.988% v2) | §4.2 |
| Appendix A: strategy III advantage "negligible with N = 8" | Holds; in fact negative once per-nonce cost is counted | §4.7 |
| Appendix C runtime figures (Ryzen 7 1700) | Cannot be verified on this host (different CPU, shared cores, virtualised memory) | §4.7, defer to agent T |
| design_v2 "uses the gap in the RandomX main loop ... without hurting the hashrate" | Consistent here: fast-mode v2 is 3.3% faster than v1 on this host (high-latency memory) | §4.7, indicative only |

## 4. Results by question

Sample sizes: the main statistics run is 3000 hashes per version (24,000 programs, 49,152,000 iterations, 8.87e9 (v1) and
1.33e10 (v2) classified FP results). Influence: 8,460 trials per version. Absorption: 256,000 injections per version.
Timing: 20,000 + 1,500 inputs (fast mode), 600 inputs (light mode). All intervals are 95% Wilson intervals unless stated.

### 4.1 Q1: Scratchpad blocks written per iteration

Measured over 49,152,000 R blocks and 49,152,000 F blocks per version (`rxs stats`, §5):

| | v1 R block | v1 F block | v2 R block | v2 F block |
|---|---|---|---|---|
| total chi2 over 64 byte positions (df 16,320) | 16,295, p=0.55 | 1.97e9, p≈0 | 16,046, p=0.94 | 16,600, p=0.061 |
| min per-byte chi2 p (64 tests) | 0.076 | 0 (16 of 64 below 1e-6) | 0.046 | 0.00094 (Bonferroni 0.06) |
| max per-bit \|z\| (512 bits) | 2.77 | 5,570 (102 bits with \|z\|>5) | 3.19 | 3.02 |
| byte entropy by position in word, 0→7 | 8.0000 ×8 | 8.0000 ×6, 7.9996, **5.8274** | 8.0000 ×8 | 8.0000 ×8 |
| sign+exponent (top 12 bits) entropy of F-block words | - | 9.83 of 12 | - | 12.000 (chi2 p=0.29) |

The structure of the v1 F block comes from the high bits of each word. P(bit=1) for bits 52..63, identical for all 8
words (`analysis/fbits.txt`):
`0.500 0.501 0.501 0.496 0.504 0.495 0.471 0.370 0.215 0.123 0.107 0.500`.
Cause: every F value at the end of the program has |f| ≥ 2, so exponent bit 62 is always set. E values have bit 62 set
in 89.5% of lanes. So bit 62 of f XOR e is 1 only when E < 2, and the bits below inherit the concentrated exponent
distributions. E is always positive, so the XOR passes the sign of F straight through, which happens to be fair (P=0.50).
The sum of byte-marginal deficits, 17.38 bits per 512-bit block, is a lower bound on the joint entropy deficit, because
H(block) ≤ Σ H(byte).

**v2 bias from the E round keys:** none measurable. The E registers fed to AES as keys are constrained: the sign bit is
always 0, and the top 16 bits of each lane carry 12.97 of 16 bits of entropy. Even so:
- every one of the 512 output bits lies in [0.49978, 0.50021], i.e. |bias| ≤ 2.2e-4 at N=4.9e7;
- dependence tests (`rxs_pairs`, 4,915,200 samples per pair, mutual information against its finite-sample bias) found
  nothing:

| pair (v2) | chi2 independence p | MI (bits) | bias |
|---|---|---|---|
| output MSB vs the MSB of e3 (last round key) | 0.90 | 0.00265 | 0.00277 |
| output MSB vs F input MSB | 0.62 | 0.00021 | 0.00026 |
| f0' vs f2' MSB (same AESENC keys) | 0.99 | 0.00944 | 0.00954 |
| f0' vs f1' byte 0 (enc vs dec) | 0.52 | 0.00956 | 0.00954 |

The same pairs in v1 give MI from 0.48 to 1.01 bits (`analysis/pairs_v1.txt`). The mechanism is expected: the last
operation of AESENC/AESDEC is `Y ^ e3`, and Y, the output of 3.5+ rounds over a 16-byte state that holds at least 50
bits of entropy, is indistinguishable from uniform in marginal and pairwise tests.

**Judgement (S-1):** The v1 structure is real but carries no attack value. The biased bits are still state-dependent and
must be computed and stored exactly. Feeding them back does no harm either:
- XORing a biased block into uniform R registers yields uniform R registers (the R block measures uniform);
- loading it as F/E operands only changes operand magnitude distributions.

Severity Low only because a stated design rationale (2.5.1) is false for v1. v2 fixes it.

### 4.2 Q2: Final Scratchpad, write coverage, compressibility

**Write coverage per hash** (bitmap of 8-byte words written at least once; 3000 hashes):

| region | v1 measured | v2 measured | analytic v1 / v2 |
|---|---|---|---|
| L1 (16 KiB) | 100.000% | 100.000% | - |
| L2 only (240 KiB) | 94.07% (sd 1.94) | 97.62% (sd 0.97) | - |
| L3 only (1792 KiB) | **67.519% ± 0.037** (sd 1.03, min 64.3, max 71.9) | **69.453% ± 0.041** (sd 1.16) | 67.535% / 69.502% |
| whole 2 MiB | 70.88% | 72.99% | - |

The analytic model is `1 − e^(−32768/32768) · e^(−S·16384/262144)`. It assumes 2 block writes per iteration at uniform
64-byte addresses and S = 2 (v1) or 3 (v2) L3 ISTOREs per iteration at uniform 8-byte addresses. design.md 2.8.2 says
"about 66%"; v1 measures 67.5% and v2 adds 2 percentage points through its 50% more ISTOREs.

**Who last wrote each word of the final Scratchpad:**

| | fill | R block | F block | ISTORE |
|---|---|---|---|---|
| v1 | 29.1% | 28.2% | 28.2% | 14.4% |
| v2 | 27.0% | 27.15% | 27.15% | 18.7% |

**Byte-level entropy by position within the word** (order-0, all 3000 Scratchpads):
- v1: byte 0 7.99917, bytes 1-5 8.00000, byte 6 7.99997, byte 7 **7.78860**; overall 7.99555 bits/byte.
  The last-write classes show where this comes from: F-block words have byte 7 at 5.83 bits; ISTORE words have byte 0
  at 7.966 and byte 7 at 7.990.
- v2: byte 0 7.99853, byte 7 7.99958, all other bytes 8.00000; overall 7.999956 bits/byte. The only residual structure
  is in ISTORE-written words (byte 0 7.964, byte 7 7.989). Mid-program register values are not uniform: products have
  extra trailing zeros, and IMULH/ISMULH high halves are biased.

**Compressibility** (48 final Scratchpads per version, 96 MiB; `harness/compress.sh`):

| | xz -9e (lc=3, lp=0, pb=2) | xz lzma2 9e, lc=0, lp=3, pb=3 | per file, xz -9e, mean (min) of 8 | position-aware order-0 entropy bound |
|---|---|---|---|---|
| v1 | **99.9752%** | 99.9669% | 99.983% (99.810%) | 99.668% |
| v2 | **99.9879%** | 99.9870% | 99.937% (99.493%) | 99.997% |

Appendix D's "approximately 99.98%" (7-Zip LZMA2 ultra, v1) is reproduced. LZMA captures only a small part of v1's
order-0 per-position redundancy; the right-hand column is the ideal for a coder that models byte position within the word.

Individual Scratchpads can be compressed further (down to 99.49% for a v2 file) because of duplicated 8-byte words:
- per Scratchpad, 1018 (v1) and 935 (v2) words on average repeat an earlier value, with a maximum of 4217;
- 0.77% (v1) and 0.70% (v2) of all words are involved in a duplicate;
- 60% of these duplicates sit in L1/L2, which are 12.5% of the space and the region where ISTOREs land
  (`analysis/repeated_words.txt`).

They are ISTORE copies of the same unmodified register to several addresses. Expected and harmless: the AesHash1R
fingerprint absorbs every word bijectively, and an implementation cannot know in advance which words will repeat.

### 4.3 Q3: Influence and diffusion

**Method (`rxs infl`).** For each of 60 inputs per version, a reference run stores the full state at every program
boundary: the 2 MiB Scratchpad, the program seed, the rounding mode and the register file. Each trial restores program
k, injects one bit flip and runs forward. The perturbed state is compared bit-for-bit with the reference at the end of
each program:
- full register file, rounding mode and Scratchpad all identical: "unchanged". This verdict is exact.
- register file differs: "changed". This is certain up to Blake2b collisions, because the register file seeds the next
  program or the final hash.
- only the Scratchpad differs: the run continues to later programs, ending with the final hash if needed.

One trial in 40 always runs to the final hash, to sample the Hamming distance.

Injection classes:
- **input:** flip of the 16-byte input.
- **spad:** flip of a random bit of the Scratchpad before program k.
- **R_start:** flip of an R register after step 3 of iteration j.
- **F_start / E_start:** flip of a bit of the 8-byte source word (two int32) of an F/E register, with the conversion
  redone. This is a pure register-input flip inside the reachable state space.
- **R_end / F_end / E_end:** flip of any bit of an R/F/E register after step 4, i.e. just before the dataset XOR and the
  F/E mix.

k is uniform in 0..7 and j uniform in 0..2047.

**Results** (8,460 trials per version, 1,200 per class except input):

| class | v1 P(hash unchanged) | v2 P(hash unchanged) | mechanism |
|---|---|---|---|
| input | 0/60 | 0/60 | Blake2b |
| spad | 8.67% [7.20, 10.39] | 10.08% [8.50, 11.92] | word overwritten by ISTORE before any read (100% of 84/115 such cases unchanged; 3.8% / 1.1% of "read first" cases; 0% of words never touched) |
| R_start | 1.50% [0.95, 2.36] | 0.92% [0.51, 1.63] | top bits shifted out by multiplication by an even factor |
| F_start | 7.83% [6.44, 9.49] | 3.58% [2.67, 4.79] | v1: single-word erasure (flat across bits); v2: mostly FP absorption (low bits, see below) |
| E_start | 8.42% [6.98, 10.12] | 0.08% [0.01, 0.47] | v1: single-word erasure; v2: AES spreads e over all 4 outputs |
| R_end | 0.17% [0.05, 0.61] | 0.08% [0.01, 0.47] | - |
| F_end | 9.00% [7.51, 10.75] | 0.92% [0.51, 1.63] | v1: one word; v2: 2 words (16 bytes of f_i') |
| E_end | 9.33% [7.81, 11.11] | 0.00% [0, 0.32] | v1: one word; v2: 4 to 8 words |

Hamming distance of the final hash when changed: mean 126.5 to 128.4, sd 7.6 to 8.5 for every class and version (5.9
for the 60-trial v2 input class), matching Binomial(256, 1/2) (mean 128, sd 8). There is no partial diffusion: the output either does not change at all
or changes completely.

**Bits with reduced or no influence** (per-bit tables in `analysis/infl_v{1,2}.txt`; per-bit n ≈ 20, so bit groups are
quoted):
- **R registers:** top bits. R_start bit 63 is erased in 22% [11, 41] (v1) and 9.5% [2.6, 29] (v2) of trials; bits 56-62
  in 7.8% and 3.4%; bits 8-55 essentially never (≤ 0.34%). Multiplication by an even register or immediate (IMUL_R,
  IMUL_M, IMUL_RCP) shifts the top bit out. The low byte has a small rate in v2 (2.3%), through IMULH.
- **F/E state, v1 only, all bits (S-2):** about 9% erasure, flat across bit positions. The XOR maps any F or E bit to
  exactly one bit of one 8-byte F-block word, and that word is overwritten by an ISTORE before being read with about 9%
  probability. Block writes never erase unread data: every block written in steps 9/11 was read in steps 2/3 of the same
  iteration. v2 removes this path.
- **F inputs, both versions, low bits (S-3).** Measured directly with `rxs_absorb`: 2000 inputs × 128 injections; the
  register after step 4 is compared with the reference. Absorption rate by bit group of each int32:

| int32 bits | v1 F | v2 F |
|---|---|---|
| 0-3 | 2.3-2.5% | 8.5-9.3% |
| 4-7 | 1.1-1.2% | 5.1-5.5% |
| 8-15 | 0.34-0.44% | 2.1-2.2% |
| 16-23 | ≤ 0.07% | 0.5-0.6% |
| ≥ 24 | 0 | ≤ 0.14% |
| overall | 0.56% [0.52, 0.60] | 2.47% [2.39, 2.56] |

  Instruction-level traces (`rxs_debug`) show how a flip of int32 bit b (δ = 2^b) disappears:
  1. FSCAL multiplies the value by 2^(−1..−15). FSCAL is exact, so this only shrinks δ along with the value.
  2. FADD/FSUB with an A register (up to 2^32) or a memory operand restores the magnitude while δ stays small.
  3. After one or two such cycles δ falls below half an ulp and is rounded away. Traced example (input 9, k=1,
     j=2034): δ = 16 at 2^30 became 1 ulp at 2^31, then vanished.

  This is a property of FSCAL plus rounding, present in v1. v2 increases it about 4.4x because each F register sees
  1.5x more FSCAL/FADD cycles per iteration.
- **E inputs:** absorbed 0.02% (v1) and 0.16% (v2), flat across bits. This is saturation: once E overflows to infinity,
  or to DBL_MAX under round-down/toward-zero, the difference is gone. It tracks the 6.5x higher infinity rate of v2.
- **Scratchpad state (S-11):** a single-bit flip anywhere in the Scratchpad leaves the hash unchanged in 8.7% (v1) and
  10.1% (v2) of cases:

| region | v1 | v2 |
|---|---|---|
| L3-only | 6.3% | 8.0% |
| L2-only | 25.3% | 22.6% |
| L1 | n=11 | n=11 |

  These are words that an ISTORE overwrites before any read. v2's extra ISTOREs make this slightly more likely.

**Judgement.** Every hash-chain design has state that is overwritten before use. What matters for a PoW is that none of
this can be predicted without executing the program, and none of it lets an implementation skip work: whether a word or
bit is "dead" depends on runtime addresses and values. v2 removes the XOR-specific single-word erasure (good) but
slightly increases FP absorption and infinity saturation. The net effect on the influence of F/E state is positive:
- E_end erasure goes from 9.3% to 0%;
- F_end goes from 9.0% to 0.9%;
- F_start goes from 7.8% to 3.6%.

### 4.4 Q4: Floating-point values

Values are taken at the end of each iteration, after step 4 and before the mix; 3.93e8 lanes per class and version.

| | v1 | v2 |
|---|---|---|
| F magnitude (finite non-zero exponent range) | almost all 2^1..2^48; \|f\| < 1 in 902 of 3.9e8 lanes (down to 2^−24), plus −2^−1008 (FSCAL of an exact zero) | almost all 2^1..2^49; \|f\| < 1 in 1007 lanes (down to 2^−15) |
| F exactly zero at iteration end | 2 lanes (1 program) | 0 |
| FP results equal to zero (cancellations) | 220 / 8.87e9 | 283 / 1.33e10 |
| E exponent range | 2^−243..2^1023, and +inf | 2^−216..2^1023, and +inf |
| E < 1 (bins ≤ 1e−20..1) | 10.3% | 1.9% |
| E lanes infinite | 1.60e−4 | 1.02e−3 |
| iterations with ≥1 infinite E lane | 0.109% [0.108, 0.110] | 0.704% [0.701, 0.706] |
| programs with ≥1 infinite E (any iteration) | **1.91% [1.75, 2.09]** | **6.54% [6.24, 6.86]** |
| programs with an overflow (MXCSR OE: infinity or DBL_MAX saturation) | 2.28% [2.10, 2.48] | 7.66% [7.33, 8.01] |
| NaN results / MXCSR IE | 0 / 0 programs | 0 / 0 programs |
| subnormal results / MXCSR UE / DE | 0 / 0 / 0 | 0 / 0 / 0 |
| divide-by-zero (ZE) | 0 programs | 0 programs |

E decade histogram (|x|, left edge, bins of 1e20), v2:

| bin | 1e−80 | 1e−60 | 1e−40 | 1e−20 | 1e+0 | 1e+20 | 1e+40 | 1e+60 | 1e+80 | 1e+100 | 1e+120 | 1e+140 | ≥1e+160 | inf |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| share | 0.00% | 0.05% | 0.27% | 1.62% | 18.6% | 25.1% | 19.0% | 12.4% | 8.6% | 5.2% | 3.4% | 2.0% | 3.7% | 0.10% |

The v1 histogram is in `analysis/stats_v1.txt`; it is shifted toward smaller values because v1 iterations have fewer FMUL.

Notes on the method and the invariants:
- **No-subnormal check.** With RandomX's MXCSR (FTZ and DAZ set), a subnormal result would be flushed to zero. I
  therefore rely on the sticky UE flag, captured before every fprc write and after every iteration. I verified on this
  CPU that an FTZ flush sets UE: `harness/ftz_check.c` prints `MXCSR flags 0x30 (UE=1 PE=1)`. UE was never set in
  48,000 programs, so no operation produced a subnormal.
- **The 6.85% claim.** design.md's v2 figure lies between the count of infinities at iteration end (6.54%) and the
  overflow count (7.66%); it is consistent with the former at the edge of the 95% interval.
- **Infinite E lanes in v2.** An infinite E lane is a constant AES key lane, 0x7FF0000000000000. The F block stays
  uniform (§4.1), so this has no measurable effect.

### 4.5 Q5: CFROUND and rounding modes

| | v1 | v2 |
|---|---|---|
| static CFROUND per program (mean); programs with none | 1.002; 36.65% [36.04, 37.26] (Binomial: 36.72%) | 1.506; 22.27% [21.75, 22.80] (22.25%) |
| executions per iteration; per hash | 1.0041; 16,451 | 1.5099; 24,738 |
| fprc writes per iteration; per hash (write fraction) | 1.0041; 16,451 (1) | 0.1009; 1,654 (0.06685 = 1/14.96) |
| mode changes per iteration; per hash | 0.7506; 12,298 | **0.0745; 1,221** |
| mean iterations between changes | 1.33 | 13.4 |
| FP-op residency RNE / down / up / zero | 31.34 / 22.81 / 22.71 / 23.13% | 28.72 / 23.72 / 23.82 / 23.74% |
| P(program leaves mode 0 unchanged, starting in 0) | 0.375 [0.366, 0.385] | 0.223 [0.215, 0.232] |
| P(whole hash in roundTiesToEven): model (above)^8 | **3.9e-4** [3.2e-4, 4.8e-4] | **6.2e-6** [4.5e-6, 8.4e-6] |
| observed all-RNE hashes | 2/3000 (6.7e-4 [1.8e-4, 2.4e-3]) | 0/3000 |

The model is exact because the 8 programs are generated from independent Blake2b outputs. The analytic residency
estimate, 1/4 plus the share of programs before the first CFROUND, gives 30.4% and 27.7%.

**Gate pass rate.** The v2 gate passes in 6.68% of executions, not 6.25% (z = 155). Bits 2-5 of `rotr(src, imm)` are not
uniform for mid-program register values. Products have extra trailing zeros (the same cause as the ISTORE byte-0 bias in
§4.2), so the rotated window is zero slightly more often. The effect is harmless and only shortens the mean run between
changes from the 14.2 iterations expected at 1/16 to the 13.4 observed.

**What the v2 gate changes for implementations that must support dynamic rounding.**
1. **All four modes stay mandatory.** Directed modes still carry 71% of FP operations. A hash avoids them with
   probability about 6e-6 in v2, versus about 4e-4 in v1, because longer programs make a program without CFROUND rarer
   (22% versus 37%). An RNE-only device is therefore even less viable in v2, and since the mode depends on runtime
   register values, it cannot predict which hashes it could handle.
2. **What changes is switching frequency.** It drops from 0.75 to 0.075 per iteration: one change per ~340 dynamic
   instructions in v1 versus ~5,200 in v2. The mode is piecewise constant over about 13 iterations, so a "mode
   unchanged" prediction at each CFROUND is right 95% of the time. Which implementations benefit:
   - **CPUs:** writes to the control register (x86 MXCSR, ARM FPCR) are expensive or serialising, so CPUs benefit most.
     This is the intended effect (design_v2 §1).
   - **CUDA GPUs:** they have per-instruction rounding intrinsics and see no switching cost in either version.
   - **GPU kernels specialised per mode, or with a per-op mode dispatch** (e.g. OpenCL targets without per-instruction
     directed rounding): they gain from rarer changes, but must still implement all modes.
   - **ASICs:** a custom FPU can carry the 2-bit mode with every operation at negligible cost, so the gate gives them
     nothing.
3. **Net effect.** The gate narrows a CPU-specific penalty without creating any shortcut. Agents X and T cover the
   performance side.

### 4.6 Q6: CBRANCH

| | v1 | v2 |
|---|---|---|
| CBRANCH executions | 1.237e9 | 1.849e9 |
| taken per execution | 0.003894 [0.003891, 0.003898] = 1/256.8 (z = −6.8 vs 1/256) | 0.003897 = 1/256.6 (z = −6.3) |
| iterations with ≥1 jump | 9.317% (analytic 9.321%) | 13.613% (13.651%) |
| jumps per iteration: 0 / 1 / 2 / 3 / 4 / 5 / 6 | 90.68 / 8.85 / 0.449 / 0.0158% / 187 / 2 / 0 | 86.39 / 12.62 / 0.946 / 0.0479% / 910 / 18 / 1 |
| re-executed instructions per taken jump: mean, median, p99, max | 6.20, 4, 27, 76 | 6.26, 4, 27, 84 |
| re-executed instructions per iteration: mean, p99, p99.9, max | 0.608, 14, 28, 104 | 0.917, 17, 31, 130 |
| dynamic instruction overhead | +0.24% | +0.24% |
| consecutive-jump runs of one instance: length 1 / 2 / ≥3 | 4,796,128 / 9,961 / **0** | 7,177,373 / 14,117 / **0** |
| fraction of runs with a double jump (model 1/512) | 1/482 | 1/509 |

**"Each CBRANCH instruction can jump up to twice in a row" holds, and it holds by construction.** The block re-executed
after a jump contains no modification of dst and no other CBRANCH, because a CBRANCH counts as modifying every register.

Write b for the condition offset, F for the 8 condition bits of cimm (bits b..b+7) and c for the low part of cimm, which
satisfies c < 2^(b−1) because bit b−1 is cleared.
1. After a jump, bits b..b+7 of dst are 0.
2. A second jump needs F + carry ≡ 0 (mod 256). Bit b of cimm is forced to 1, so F is odd, which forces F = 0xFF
   (probability 2^−7 over the random imm32 bits) together with a carry out of the low b bits.
3. A carry out of the second addition requires L + c ≥ 2^b, where L is the low part after the first jump.
4. After a second carry, the low part becomes L + c − 2^b < c.
5. A third carry would need (L + c − 2^b) + c ≥ 2^b, impossible since 2c < 2^b.

So no CBRANCH can jump three times in a row. The same argument gives P(double | jump) = 2^−7 · 1/4 = 1/512, which v2
matches. The small v1 excess (1/482, z = 5.9) and the 0.3% deficit of the taken rate are consistent with mid-program
registers whose low bits are slightly non-uniform (as in §4.5); neither affects any claim.

### 4.7 Q7: Runtime distribution and filtering strategy III

**Host and caveat.** Xeon 2.8 GHz VM, JIT plus AES-NI, one thread on cores 0-2 that are shared with other agents. Fast
mode uses 2 MiB pages. The raw spread is dominated by slow host-level drift: per-input v1 and v2 times, measured back to
back, correlate at 0.55 to 0.85, which program content cannot explain since the two programs are independent. I
therefore quote:
1. minima over repeated runs;
2. the dispersion of the 8 program times *within* each hash, which cancels drift common to the hash and gives an upper
   bound on the program-dependent spread.

Clean timing belongs to agent T (core 3).

| (min over reps) | v1 | v2 |
|---|---|---|
| fast, `rxtime fast 1500 7`: hash mean | 3.456 ms (p99 4.51, max/mean 1.55) | 3.341 ms (p99 4.81, max/mean 1.73) |
| fast: program mean | 391 µs | 378 µs |
| fast: fill / final AesHash+Blake2b | 107 / 90 µs | 103 / 84 µs |
| fast: within-hash per-program cv (program-dependent, upper bound) | **4.65%** | **6.34%** |
| fast: program 0 slower than programs 1-7 by | 3.4% | 5.4% |
| light, `rxtime light 600 3`: hash mean | 34.35 ms (max/mean 1.42, host noise) | 35.25 ms (+2.6%; max/mean 1.54) |
| light: modelled intrinsic hash cv; p99.9/mean | ≤ 1.4%; ≤ 1.04 | ≤ 1.3%; ≤ 1.04 |

Appendix C's absolute numbers (Ryzen 7 1700: 1.6 ms fast, 14.8 ms light) cannot be compared on this host. Its
qualitative points do hold here:
- overhead outside VM execution: fill plus final hash are about 5.7% of a fast hash (Appendix C: 5.6% plus generation);
- verification cost is dominated by SuperscalarHash (light mode is about 10x fast mode here, for both versions).

On this host fast-mode v2 is 3.3% faster than v1, which is consistent with high memory latency: about 190 ns per v1
iteration, hidden better by the two-ahead prefetch. Indicative only.

**Worst case for verification.** Light-mode time is about 16,384 Dataset items (a fixed cost) plus the VM. The
program-dependent spread of a whole light hash is at most about 1.4% (cv), giving p99.9/mean ≤ 1.04 for both versions.

This is a model, not a measurement. It assumes the 8 program times of a hash are independent and roughly normal. It
takes the light-mode within-hash program cv (4.07% v1, 3.85% v2) as an upper bound, since that cv still contains host
noise. Doubling the cv would give p99.9/mean ≤ 1.09.

v2 adds about 3% to the mean light-mode cost on this host. The light/fast ratio is agent T's question.

**Strategy III** (`harness/an_intrinsic.py`, fast, within-hash distribution). Model: the miner knows each first
program's runtime perfectly (an oracle, the best case) and rejects the slowest fraction r. Every rejected candidate costs
a new nonce: Blake2b, the 2 MiB fill and program generation, measured at 3.2% of a hash. design.md assumes these are
free; both views are shown.

| r | v1 first-program speedup | v1 hashrate (rejected nonces free) | v2 first-program speedup | v2 hashrate (free) |
|---|---|---|---|---|
| 10% | x1.009 | x0.998 (x1.001) | x1.013 | x0.998 (x1.002) |
| 25% | x1.020 | **x0.992** (x1.002) | x1.027 | **x0.992** (x1.003) |
| 50% | x1.035 | x0.972 (x1.004) | x1.048 | x0.974 (x1.006) |

The optimal r is 0 for both versions. The noisier 20,000-input run gives at most x1.0002 with the within-hash
distribution, and x1.0025 even on raw times, which noise inflates.

Appendix A assumed a 5% first-program boost at r = 25%; the measured program-dependent spread gives 2.0% (v1) and 2.7%
(v2). The conclusion "negligible with N = 8" holds, and in fact the strategy loses. v2's somewhat wider program-time
spread does not change this: with N = 8 the attainable advantage stays below +0.3% even if nonces were free.

### 4.8 Q8: Relation between v1 and v2

**Shared state** (`rxs2 diverge 200 5`, 200 inputs). For the same input and key, the following were identical in 200 of
200 cases:
- the 2 MiB fill;
- program 0's 128-byte configuration plus its first 256 instructions;
- ma, mx and datasetOffset;
- the R registers at the start of iteration 0.

The R registers after the program of iteration 0 were identical in 0 of 200 cases, and so were the R registers at the
start of iteration 1 and the Scratchpad after program 0.

The complete list of work a dual (v1+v2) computation can share is therefore:
- Hash512(H) and the AesGenerator1R fill: 107 µs, 3.1% of a fast-mode hash, under 0.5% of a light-mode hash;
- the AesGenerator4R output for program 0 (the library generates 3200 bytes for both versions);
- the integer part of the first 256 instructions of the very first iteration, about 4e-5 of the VM work. The FP part is
  also shared unless a CFROUND among those 256 instructions writes in v1 but is gated in v2;
- one Dataset item: iteration 0 of program 0 reads item `ma` in both versions. In v2, iteration 1 reads item `mx`, which
  is also fixed by the configuration; v1 reads `mx ^ R` there, so it is not shared.

The Cache and Dataset are identical by design. Nothing else is shared: computing both hashes costs about 97% of the sum
of their costs in fast mode and essentially the full sum in light mode.

**Independence of the outputs** (20,000 pairs from the fast run; `analysis/time_fast.txt`):

| test | result |
|---|---|
| bit agreement | 0.50014 (z = 0.62) |
| per-position agreement | max \|z\| 2.74 over 256 positions |
| leading 64-bit words | correlation 0.0000 (±0.0071) |
| joint "both below target" counts, T = 2^−2 / 2^−4 / 2^−6 | 1243 / 75 / 7, expected 1250 / 78.1 / 4.9 |
| XOR byte chi-square | p = 0.014 |

The XOR byte p-value is one of seven tests in this table and is not significant after correction. The 1,500- and
600-pair runs give p = 0.96 and 0.996.

Therefore a nonce that is valid for one version is no more likely to be valid for the other, and during the transition
mining both versions gives no economy beyond the 3% fill.

## 5. Method and reproducibility

**Harness.**
- **Private instrumented copy.** `agentS/rxtree/src` is a copy of src/; only `bytecode_machine.{hpp,cpp}` changed (diff
  in `agentS/instrumentation.diff`). The added counters cover: FP operations by rounding mode; CFROUND executions,
  writes and changes; per-lane classification of every FP result; CBRANCH taken, re-executed and consecutive jumps;
  MXCSR status flags captured before each fprc write; a memory watch on one word; and write-coverage and last-writer
  maps. Built with `cmake ../rxtree -DCMAKE_BUILD_TYPE=Release && make randomx` in `agentS/build-instr`.
- **Driver.** `agentS/harness/rxs_driver.hpp` re-implements the loop of `vm_interpreted.cpp` (spec 4.6.2), with hooks
  after step 3, after step 4 and after steps 9-11. Dataset items come from the real Cache (key "test key 000") through
  the JIT SuperscalarHash (light mode).
- **Bit-exact check.** `./rxs verify` reproduces both test vectors: v1 `639183aa…4e3f` and v2 `22ec6b86…db6f` for
  "This is a test". It also matches the library's own JIT light VM on 20 of 20 random inputs per version.
- **Inputs.** 16-byte strings (seed, index), all under key "test key 000".

**Commands.** Run in `agentS/`, all under `taskset -c 0-2 nice -n 10`.

Build:
```
g++ -O2 -std=c++17 -maes -I rxtree/src harness/rxs.cpp harness/rxs_infl.cpp build-instr/librandomx.a -lpthread -o rxs2
g++ -O2 -std=c++17 -maes -I rxtree/src harness/{rxs_pairs,rxs_absorb,rxs_debug}.cpp build-instr/librandomx.a -lpthread -o <name>
g++ -O2 -std=c++17 -maes -I /home/user/RandomX/src harness/rxtime.cpp ../lead/build-x86/librandomx.a -lpthread -o rxtime
```

Experiments:
```
./rxs2 verify
./rxs stats 0 3000 1 data/s_v1 48 ; ./rxs stats 1 3000 1 data/s_v2 48
./rxs2 infl 0 60 141 11 data/infl_v1.csv ; ./rxs2 infl 1 60 141 11 data/infl_v2.csv
./rxs_absorb 0 2000 3 data/absorb_v1.csv ; ./rxs_absorb 1 2000 3 data/absorb_v2.csv
./rxs_pairs 0 300 21 data/pairs_v1.bin ; ./rxs_pairs 1 300 21 data/pairs_v2.bin
./rxs2 diverge 200 5
./rxs_debug 9 1 2034 0 4
./rxtime fast 20000 2 7 data/time_fast.csv 1 2 ; ./rxtime fast 1500 7 8 data/time_fast7.csv 1 2 ; ./rxtime light 600 3 7 data/time_light.csv
harness/compress.sh data/s_v1 ; harness/compress.sh data/s_v2
```

`./rxs_debug 9 1 2034 0 4` is the instruction-level trace of one absorbed flip.

`rxs` and `rxs2` are the same source except that rxs2 runs 1 in 40 influence trials to the final hash instead of 1 in 16;
`stats` is identical in both.

Analysis (Python 3, numpy and pandas; my own chi-square tail, no scipy on the host):
```
python3 -I harness/an_stats.py data/s_v1 data/s_v2
python3 -I harness/an_fbits.py data/s_v1 data/s_v2
python3 -I harness/an_infl.py data/infl_v1.csv data/infl_v2.csv
python3 -I harness/an_absorb.py data/absorb_v1.csv data/absorb_v2.csv
python3 -I harness/an_pairs.py data/pairs_v1.bin data/pairs_v2.bin
python3 -I harness/an_time.py <csv>
python3 -I harness/an_intrinsic.py <csv>
python3 -I harness/analytic.py
```

All outputs are in `agentS/analysis/*.txt`; raw data is in `agentS/data/` (223 MB, including 96 Scratchpad dumps).

**Limitations.**
- **One key.** Statistics use the key "test key 000". The Dataset enters the VM state only through the R XOR and the
  addresses, so the VM-state statistics should not depend on the key, but this was not tested with other keys.
- **Per-bit influence resolution.** About 20 trials per bit and class, so bit groups are quoted. The absorption run
  (2,000 per bit) gives precise per-bit numbers for F/E inputs.
- **Timing.** Measured on shared, virtualised cores; the runtime figures are upper bounds on program-dependent spread
  and indicative absolute values.
- **Fast mode only for timing.** The statistical experiments ran in light mode. The algorithm is identical in fast mode,
  so this does not change the results.
