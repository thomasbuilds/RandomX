# RandomX algorithm-level review: the v1 design and the v2 changes

Repository: thomasbuilds/RandomX, branch `claude/modest-meitner-dkvpgd`.
Reviewed algorithm: master `a901d5b` (= upstream tevador/RandomX v2.0.1 plus the fix commits #340-#363), as
defined by `doc/specs.md`, `doc/design.md` and `doc/design_v2.md`. The branch changes no hash.
Date: 2026-10-21. Companion report: [AUDIT_v2_2026-10.md](AUDIT_v2_2026-10.md) (implementation audit, rounds 1
and 2). Reproduction scripts and the full track reports: [audits/2026-10-algorithm-review/](audits/2026-10-algorithm-review/).

**Scope.** The whole algorithm as it stands on master: the v1 base design and the four v2 changes (conditional
CFROUND, AES mixing of the F and E registers, 384-instruction programs, Dataset prefetch two iterations ahead), plus
the commitment function added with v2. Implementation correctness is covered by the companion report and is not
repeated here. Monero's integration (fork height, seed epochs, use of the commitment for difficulty) was deferred by
the maintainer and is not covered.

**Method.** Six independent tracks, followed by lead verification of the key results:

| track | subject | main method |
|---|---|---|
| T | time-memory tradeoffs, light mode, verification time | timing harnesses on a pinned core (v1 and v2 interleaved), bit-exact partial-Dataset harness, analytic and throughput cost models for specialised hardware, event simulation of the loop |
| X | execution model, instruction-level parallelism, hardware advantage | traces of 192 programs per version (hashes bit-exact with the library), trace-driven core models for Skylake, Zen 2-5 and ASIC-class designs, energy model |
| S | statistical properties of the VM state | instrumented interpreter, 3,000 hashes per version (24,000 programs, 49 million iterations), entropy, FP exception, rounding-mode, branch and coverage statistics |
| C2 | cryptographic constructions | every primitive and construction checked against its role; Argon2 reference graph of the Cache; AES statistical battery; Dataset bias scan over all 34 million items |
| W | clean-room implementation from the specification | 2,000 lines of Python written from `doc/specs.md` only, with every source consultation logged; bit-exact against the library |
| R | sourced research | the four 2019 audits and the authors' answers, GitHub and Monero discussions of v2, published hardware claims |

Lead verification: the efficiency arithmetic of design_v2.md recomputed for all 13 published configurations; the
exact lookahead bound re-derived and checked with an independent event simulation (75 cases, exact match); the base
values of the specialised-hardware throughput model recomputed by hand (2.24 and 1.66); the normative SuperscalarHash
generator text added to the specification checked against the reference generator (1,200 programs, 536,212
instructions, identical). Labels used below: **measured** (counted or timed, with the command in the track report),
**model** (computed with stated assumptions and a sensitivity range), **judgement**.

**Measurement host.** 4-vCPU Intel Xeon (Cascade Lake, 2.8 GHz, AES-NI, AVX-512) Firecracker VM, 2 MiB huge pages.
Random DRAM latency is 280 ns over 2 GiB, far above bare metal, so absolute fast/light ratios from this host are not
representative; the design conclusions rest on operation counts, the models and the published benchmark tables.

## 1. Bottom line

1. **Nothing breaks RandomX's security goals in v1 or v2.** Every cryptographic component is sound for its role. The v2
   AES mix is strictly stronger than the v1 XOR: the F-register lines written to the Scratchpad are statistically uniform
   in v2 and measurably biased in v1. Program filtering, feature omission and ILP exploitation are no easier in v2.
2. **One High finding (model, medium confidence): the v2 prefetch distance weakens light-mode tradeoff resistance for
   specialised hardware.** Because the address of the Dataset item used in iteration i is known two programs earlier, a
   light-mode design (on-chip Cache plus SuperscalarHash engines, no 2 GiB Dataset) can keep three items in flight
   instead of two. In a throughput cost model at the design's own assumptions, the light/fast area-time ratio falls
   from 2.24 (v1) to 1.66 (v2), and by 24-33 % under every assumption swept. With SuperscalarHash logic of 2-3 GHz
   class, v2 light mode reaches parity or wins (1.03, 0.86) where v1 kept a margin (1.38, 1.15). If light mode wins, a
   miner needs no Dataset DRAM at all. This is a maintainer decision before activation (section 7). The cheapest remedy,
   if a Dataset change is acceptable, is `RANDOMX_SUPERSCALAR_LATENCY` 170 -> 255 or 340, which restores the margin
   (2.44 or 3.21) at almost no verification cost on CPUs, whose light mode waits on Cache reads.
3. **The v2 efficiency claim is definitional.** The "VM+AES instructions per Joule" column of design_v2.md is the
   hash-per-Joule ratio multiplied by 1.53, the factor by which v2 increases the work per hash. In hashes per Joule, the
   authors' own tables show v2 less efficient in 10 of 13 configurations. Modelled against specialised hardware, the
   ASIC/CPU energy gap moves by 0.84-1.21x depending on the CPU (central estimates): roughly neutral, not 1.15-1.67x
   in favour of CPUs. The idle time that v2 fills on CPUs is Dataset latency, which latency-bound specialised designs
   share. v2 is not shown to improve hardware resistance; it does not reduce it in fast mode either.
4. **The design invariants were thinner than stated, already in v1.** The light/fast ratio of about 8 behind the
   2080 MiB Dataset (design 1.4.1) is a zero-margin construction that depends on the CPU (7.48 for v1 and 7.37 for v2 on
   this host; break-even 8.006). The verification target "as fast as CryptoNight" was met with zero margin by v1 and is
   missed by about 3 % in v2 (projected). The premise "one loop iteration takes about one DRAM latency" no longer held
   for v1 on 4-5 GHz cores; v2 restores it.
5. **The specification was not sufficient for an independent implementation; it is now.** The clean-room
   implementation matched the library only after reading `superscalar.cpp`: chapter 6 left 16 generator rules
   unspecified and contradicted the reference in three places, and the commitment, the instruction byte layout, the
   opcode map, the CBRANCH target rule and the floating-point L3 rule were missing or ambiguous. All are fixed in
   `doc/specs.md` on the branch (commit `6b2a054`), including a normative generator procedure verified identical to the
   reference.
6. **Context.** Vendor claims for RandomX machines (Antminer X9, December 2025: 1 MH/s at 2,472 W; Pinecone R1X:
   1.2 MH/s at 2,055 W) imply 1.9-4.5x the energy efficiency of the best CPU in design_v2.md, assuming a CPU
   package-to-wall factor of 1.2-2.0. No independent measurement exists. The v2 design received no external review before this one.

No consensus parameter was changed by this review. Documentation fixes are in commit `6b2a054`.

## 2. Design invariants and their status

| # | Invariant or claim (source) | v1 | v2 | Findings |
|---|---|---|---|---|
| I1 | Light mode is not viable for mining: light-mode AT product >= fast-mode AT product, fast/light time ratio about 8 (design 1.4.1, 2.9.1, 3.4) | Holds only under assumptions: zero margin by construction in the design's CPU framing (R* = 8.006; App. C 9.25 yes, this host 7.48 no); in the throughput model 2.24 at the design's 1 GHz assumption, 1.15 with 3 GHz-class SuperscalarHash logic, lost once an item takes less than 312 ns | **Weakened**: 24-33 % less margin in every model (1.66 at the design's assumptions); parity with 2 GHz-class logic (1.03), lost with 3 GHz-class logic (0.86) or below 430 ns per item | ALG-1, ALG-2, ALG-3, ALG-4 |
| I2 | Verification at least as fast as CryptoNight (design 1.3) | Met with zero margin (App. C: 14.8 ms, "exactly matches") | Missed by about 3 % on the App. C CPU (projected; +1.6 % measured here) | ALG-9 |
| I3 | One iteration takes about one DRAM latency; Dataset reads fully prefetched (design 2.2, 2.9) | No longer true on 4-5 GHz cores: one-iteration prefetch hides only about 36 ns at 5 GHz | Restored: hides up to about 99 ns | ALG-11, ALG-3 |
| I4 | Specialised hardware gains little over CPUs (design 1, 2); v2 improves CPU efficiency relative to it (design_v2 5) | Contested by unverified vendor claims (1.9-4.5x) | Not demonstrated: the efficiency metric is definitional; modelled ASIC/CPU gap change 0.84-1.21x | ALG-5, ALG-6, ALG-8 |
| I5 | Program filtering and feature omission unprofitable with 8 chained programs (design 1.2, App. A) | Holds | Holds, stronger (probability that a program has no CFROUND 0.367 -> 0.222; filtering gain at most 0.5 %) | ALG-29, section 6.3 |
| I6 | Scratchpad content and VM state well mixed (design 2.5.1, 2.8, App. D) | Holds for integer data; F-register lines written to the Scratchpad are biased (at least 17 bits per 64-byte line short of uniform) | Holds; F lines uniform | ALG-16, ALG-27, section 6.4 |
| I7 | Pseudo-random generators and hashes adequate for their role (design 3, App. F) | Holds | Holds | ALG-12, ALG-13, section 6.5 |
| I8 | `doc/specs.md` is sufficient for an independent bit-exact implementation | Did not hold (chapter 6, commitment, several ambiguities) | Same; fixed by this review | ALG-7, ALG-14 |

## 3. Work per hash

Computed from the instruction frequencies and the generators (measured counts agree).

| quantity | v1 | v2 |
|---|---|---|
| VM instructions per hash (8 programs x 2048 iterations) | 4,194,304 | 6,291,456 |
| Scratchpad reads by instructions / ISTOREs per iteration | 39 / 16 | 58.5 / 24 |
| integer multiplications per iteration | 38 | 57 |
| floating point instructions per iteration (of which FDIV_M, FSQRT_R) | 94 (4, 6) | 141 (6, 9) |
| CBRANCH / CFROUND per iteration | 25 / 1 | 37.5 / 1.5 (writes `fprc` in about 1/15 of executions) |
| bytes read / written per iteration (instructions, two L3 lines, Dataset item) | 504 / 256 | 660 / 320 |
| AES rounds per hash (Scratchpad fill, fingerprint, program generation, main loop) | 266,504 | 530,696 |
| Dataset reads per hash | 16,384 (1 MiB) | 16,384 (1 MiB) |
| light mode: Cache reads / SuperscalarHash instructions per hash | 131,072 / 58.6 M | 131,072 / 58.6 M |
| SuperscalarHash per Dataset item: instructions, multiplications, critical path (1 cycle per operation) | 3,578 / 1,247 / 746 | same (shared Dataset) |
| probability that a program contains no CFROUND | 0.367 | 0.222 |
| VM+AES "instructions" per hash as counted by design_v2.md | 4,456,448 | 6,815,744 (x1.53) |

## 4. Findings

Severity follows the scale used for all three rounds of this audit, applied to design properties: Critical (breaks a
core security goal of the PoW in practice), High (materially weakens hardware or tradeoff resistance or verification,
or violates a stated design invariant), Medium (weakens a property with limited practical impact, or an important
design claim is unsupported or no longer true), Low, Info. The source column names the track; its report in
`audits/2026-10-algorithm-review/reports/` holds the evidence.

### 4.1 High and Medium

| ID | Severity (confidence) | Finding | Source | Status |
|---|---|---|---|---|
| ALG-1 | **High** (medium; model) | **v2's prefetch distance weakens light-mode tradeoff resistance for specialised hardware.** The exact loop recurrence (ALG-3) lets a light-mode design keep k+1 items in flight: 2 in v1, 3 in v2. In the throughput model (Model C: memory shared by many instances; cost per hash = area x time of every resource it holds: the VM instance plus either the DRAM reads (fast mode) or the item engines and a share of the on-chip Cache (light mode); design assumptions: 1 GHz SuperscalarHash logic, 1 cycle per operation, 746 ns per item) the light/fast AT ratio falls from **2.24 to 1.66**. Every single-parameter sweep gives a v2/v1 factor of 0.67-0.76. With 2 GHz logic and 2-cycle multiplication, v2 gives 1.03 (v1 1.38); with 3 GHz logic, 0.86 (v1 1.15). The break-even item latency rises from 312 ns to 430 ns. Over a grid of 14,580 assumption sets with HBM/GDDR-class DRAM, light mode wins in 11.3 % (v1) and 26.2 % (v2) of cases. The program size is not the cause: 384 instructions with one-iteration prefetch gives 2.20, and 256 instructions with two-iteration prefetch gives 1.77. design_v2.md does not discuss the effect. | T (lead recomputed the base case) | Maintainer decision (section 7, options A-F) |
| ALG-2 | Medium (high) | **The "fast/light ratio about 8" invariant has zero margin by construction and depends on the CPU.** In the design's own model, AT_light/AT_fast = (256 MiB + 3.81 MiB for one SuperscalarHash core) x R / 2080 MiB, so the 2080 MiB size puts break-even at R* = 8.006. Measured R on this host: 7.48 (v1), 7.37 (v2). Appendix C's CPU gives 9.25 for v1; projected for v2 it fails the criterion if single-thread fast mode is more than about 19 % slower in v2, which design_v2's multi-threaded tables show for 2 of 13 configurations (5 more are 8-17 % slower). On faster CPUs v2 can raise R. With software AES, R falls from 5.0 to 3.4. | T | Document (recommendation 3) |
| ALG-3 | Medium (high; verified) | **The item-latency bottleneck of light mode is weaker than design 3.4 implies.** From the step order of spec 4.6.2, the address of item i is fixed when program i-k ends. The steady-state period per iteration is exactly **max(T, (T + L)/(k + 1))** (T: program time, L: item latency; k = 1 in v1, 2 in v2). Light mode with L >> T therefore needs about L/2 per iteration in v1 and L/3 in v2, not one item latency; fast mode hides DRAM completely if k x T >= L. The reference light-mode implementation computes items synchronously and does not use this. | T (lead: independent event simulation) | Document; mechanism of ALG-1 |
| ALG-4 | Medium (medium) | **"The energy required to calculate SuperscalarHash makes light mode very inefficient" (design 2.9.1) is not supported by the design's own figures.** With design 3.4's equivalence of one item's 1,240 multiplications to one 64-byte DRAM read and stated per-operation energies, E_light/E_fast per hash is 1.37 (v1) and 1.34 (v2) at base values, 0.67-2.6 over the ranges. Light mode costs about the same energy per hash, not much more. | T | Document |
| ALG-5 | Medium (high; verified) | **The v2 efficiency claim does not measure hardware resistance.** design_v2.md's "VM+AES/Joule" and "Relative work/Joule" columns equal (v2 hash/J / v1 hash/J) x 1.5294 (6,815,744 / 4,456,448 operations per hash); all 13 published values are reproduced. In hash/J, v2 is lower in 10 of 13 configurations (75.6-98.6 % of v1) and higher in 3 (100.8-109.2 %). The power figures are CPU package power from monitoring software or nominal limits, not wall power. Modelled: a fixed-function design's energy per hash rises only 1.02-1.29x (central 1.11x) in v2, because the unchanged 16,384 random Dataset reads dominate its energy; the ASIC/CPU energy gap therefore changes by 0.84-1.21x per CPU (CPUs that keep about 99 % or more of their v1 hashrate, i.e. the 9950X at 285 W, the two Zen 5 laptops and the 1700X, gain 9-21 %; most others, i.e. Zen 2/3/4, the 9950X at 100 W and the 12900K, move by -5 to +7 %; the two Skylake laptops lose 14-16 %). | R, X (lead recomputed the table) | design_v2.md 5 now explains the metric (`6b2a054`); recommendation 2 |
| ALG-6 | Medium (medium; model + judgement) | **The idle CPU time that v2 fills is not CPU-specific.** In v1 a thread on a current 5 GHz core waits 17-47 % of each iteration for the Dataset (50-100 ns DRAM). v2's longer program and prefetch distance fill that wait. A latency-bound specialised design has the same wait: an ASIC-class instance at 1.5-2 GHz with 75-100 ns DRAM runs a v2 iteration in 0.75-1.12x of its v1 time. Only throughput-bound designs (which hide latency with many instances) pay the full ~1.5x execution work, and for them execution energy and area are a minor part of the cost. | X | Judgement; recommendation 2 |
| ALG-7 | Medium (high; verified) | **Specification chapter 6 did not specify SuperscalarHash.** The random-draw protocol, operand selection, stall and throw-away rules, termination and the address register existed only in `superscalar.cpp`; Table 6.3.2's order, "source selected first" and "multiplied twice in a row" contradicted it. Each of 21 source-only rules is load-bearing (changing any one alters 8-16 of 16 programs). | W (lead verified the fix) | **Fixed**: normative chapter 6.4 and corrected 6.3 (`6b2a054`), identical to the reference on 1,200 programs |
| ALG-8 | Medium; High if verified (low; vendor claims) | **Published hardware claims contradict the premise that specialised hardware gains little.** Antminer X5 (August 2023): 212 kH/s at 1,350 W. Antminer X9 (announced December 2025): 1 MH/s at 2,472 W, about 4x lower capital cost per kH/s than a 9950X chip. Pinecone R1X: 1.2 MH/s at 2,055 W. Against the best CPU in design_v2.md (3.84 J/kH, package power) with a package-to-wall factor of 1.2-2.0, that is 1.9-3.1x (X9) and 2.7-4.5x (R1X) in v1 energy terms. Vendors describe RISC-V cores with DRAM, i.e. fast-mode designs, for which ALG-5 and ALG-6 predict little change in v2. No teardown, independent measurement or network-share data was found. | R | Recommendation 6 |

### 4.2 Low

| ID | Severity (confidence) | Finding | Source | Status |
|---|---|---|---|---|
| ALG-9 | Low (high) | **Verification time.** v2 adds only the extra VM time to light mode, which is 93-94 % item computation: +0.50 ms per hash (+1.6 %) on this host (VM-only time 1.75 -> 2.25 ms, ratio 1.31); direct light-mode medians gave 1.07, so the increase is between +1.6 % and about +7 %. Projected on Appendix C's CPU: 14.8 -> 15.2 ms (+3 %), i.e. the CryptoNight target, met with zero margin in v1, is missed by about 3 % (CryptoNight was not run). Interpreter light mode +5 %. Conversely, v2's lookahead would let a verifier computing items on 3 helper threads finish a hash about 3x faster than the reference (model: 31 -> 10.5 ms on this host). | T | Document; optional verifier improvement (recommendation 7) |
| ALG-10 | Low (high) | **CPUs without AES instructions pay much more for v2.** Fast mode with software AES is 1.49x slower in v2 than in v1 (1.03x with AES-NI); the software/hardware AES factor rises from 1.77x to 2.54x; the in-loop AES costs about 2.7 ms per hash in software. The interpreter's fast mode is 1.85x slower in v2. Light mode is unaffected. Affects AES-less RISC-V and ARM cores and JIT-less platforms; follows from the design goal "AES must be implemented as part of the VM" but is not quantified in design_v2.md. | T | Document |
| ALG-11 | Low (high) | **design 2.2/2.9 "one iteration takes about one DRAM latency, Dataset reads fully prefetched" fails for v1 on fast cores.** With a one-iteration prefetch a single instance hides at most k x critical path = about 36 ns at 5 GHz; v2 hides up to about 99 ns. The condition L <= k x T should be stated. design_v2's "memory-bound in v1, compute-bound in v2" is supported, where "compute-bound" means bound by the dependency chain (one v2 thread uses 3.0 of 8 dispatch slots per cycle on a Zen 5 model). | X, T | Document |
| ALG-12 | Low (high measurement; medium provenance) | **design 2.9.1 cited "halving the memory increases computational cost 3423 times" for 3-pass Argon2d; the figure is not in any version of the Argon2 version 0x13 documentation** (1-pass Argon2d: 1.5x at half memory; t-pass: time-area reduced by 1.33). The conclusion holds more strongly for RandomX's random access to the final array: on the real Cache graph, dropping 1/64 of the final blocks makes each missing block cost 430,357 compressions on average (55 % of a full Cache initialisation), because version 0x13 XORs each block into the previous pass's block. Without that XOR the median miss would cost 1-3 compressions. | C2 | **Fixed** in design.md 2.9.1 (`6b2a054`) |
| ALG-13 | Low (high) | **The initial Scratchpad content can be regenerated on demand.** Each AesGenerator1R lane is a fixed invertible AES round per block, so an implementation can store only the words the VM has written plus generator checkpoints. Measured: 43.8 % (v1) and 46.4 % (v2) of words are written on average over a hash; with checkpoints every 16 blocks, memory averages about 52-54 % of 2 MiB and peaks at 78-81 %, for 0.6-0.7 M extra AES rounds per hash. A bounded constant-factor tradeoff that only pays with memory pooled across instances; v2 reduces it slightly. | C2 | Document |
| ALG-14 | Low (high) | **Further specification gaps, all consensus-relevant.** The commitment `Blake2b-256(input \|\| hash)` (consensus-critical in Monero v17) was not specified; the instruction byte layout existed only in an external image and the opcode map was not stated; CBRANCH targets read either static or dynamic (the dynamic reading changes 11 of 12 test hashes; a jump is taken twice in a row 3.3 times per v1 hash and 6.6 per v2 hash); the "src == dst -> L3" rule did not say it excludes FADD_M/FSUB_M/FDIV_M (the other reading changes essentially every hash); BlakeGenerator byte order, `fprc` persistence across programs, int32 halves, keys over 60 bytes and Argon2's output size 0 were implicit. | W, C2, R | **Fixed** in specs.md (`6b2a054`) |
| ALG-15 | Low (high; process) | **The v2 design had no external review.** Asked in MRL whether v2 needed an audit like v1, the only reply was "changes are small and evolutionary". The prefetch change, the one with the largest effect (ALG-1), was committed on 2026-01-23 and merged on 2026-02-17 with no public rationale beyond "complements the program size increase" and benchmark gains. | R | Recommendation 5 |
| ALG-16 | Low (high; v1 only) | **In v1 the F-register lines written to the Scratchpad (step 11) are biased, so design 2.5.1's rationale "to maximize entropy ... combined using the XOR operation" does not hold for the sign and exponent bits.** Every F value at the end of a program has \|f\| >= 2 and 89.5 % of E lanes are >= 2, so bit 62 of `f XOR e` is 1 with probability 0.105 and byte 7 of each word carries 5.83 of 8 bits: at least 17.4 bits of each 512-bit line are missing. v2's AES mix removes the bias completely: all 512 bit probabilities lie in [0.49978, 0.50021] over 49 million lines, with no dependence on the round keys. The biased bits still depend on the state and must be computed, so the bias gives no shortcut. | S, C2 | Resolved by v2; recommendation 3 |

### 4.3 Info

| ID | Finding | Source |
|---|---|---|
| ALG-17 | v2's AES mix uses the E registers unevenly: e0 and e1 are fully diffused, e2 reaches one column per lane and e3 acts as final whitening, identical in all four lanes (it cancels in the XOR of two lanes); f0/f2 and f1/f3 go through the same keyed permutation. Avalanche is ideal and the output shows no dependence on any round key. Not a weakness; a future revision could rotate the key order per lane. | C2, S |
| ALG-18 | Floating-point and AES work, including the 16 v2 AES rounds per iteration, is never on the loop-carried critical path: zeroing their latencies changes the recurrence by at most 0.01 cycle per iteration in all 384 traced programs. The AES mix costs CPUs no latency ("without hurting the hashrate" holds) and costs a specialised design about 0.4 % of its energy and one small AES unit. A lazy-AES scheme could skip at most 1.4 % of main-loop rounds. | X |
| ALG-19 | v2 exposes no disproportionate ILP: the critical path grows 1.38x with CPU latencies (1.46-1.48x with 1-cycle or ASIC latencies) for 1.50x the instructions; available parallelism rises 1-8 %. A window of 320 or more entries runs within 1-2 % of the unbounded bound in both versions. | X |
| ALG-20 | CFROUND: v1's cost on CPUs is 0.1-27 % depending on how the core handles `LDMXCSR`; v2's is at most 1.3 %, plus 0.108 branch mispredictions per iteration from the gate. design_v2's "only every 16th time" is approximate: the gate passes in 6.68 % of executions (about 1/15; register values have extra trailing zeros) and the mode actually changes in about 1/20 of executions (0.0745 changes per iteration against 0.751 in v1). All four modes stay mandatory: directed modes carry 71 % of FP operations, and a whole hash stays in round-to-nearest with probability about 6e-6 in v2 (4e-4 in v1). | X, S |
| ALG-21 | SuperscalarHash programs often have constant output bits (74 % of programs). The last program's constant bits pass into every Dataset item XORed only with a Cache bit, which shows up as a 6.1-sigma bias of one bit position over the full Dataset of one key. This quantifies the bias noted in the 2019 Trail of Bits audit; it is not exploitable. | C2 |
| ALG-22 | BlakeGenerator does not encode the key length: K and K followed by zero bytes give the same SuperscalarHash programs (the Argon2 Cache differs). Hash512(H) and Hash512(register file) have no domain separation, and the commitment of a 224-byte input is a Hash256 of 256 bytes like the final hash. AesGenerator and AesHash1R lanes never mix (Blake2b supplies the cross-lane diffusion). All harmless for RandomX's use. | C2 |
| ALG-23 | Partial Dataset storage has no interior optimum in v1 or v2 (the cost is concave or linear in the stored fraction), so the attacker's choice is all or nothing. A device holding exactly 2 GiB of the 2080 MiB computes 0.78 % of reads. | T |
| ALG-24 | design_v2.md 3 says the 9950X is "only 20-25 % faster" than the 3950X in RandomX; its own tables give 1.32x (100 W) and 1.81x (285 W). | R |
| ALG-25 | GPUs fall further behind in v2 (AES through tables, 50 % more FP64 work). Outsourced hashrate (Qubic, 2025, with 6- and 18-block reorganisations on Monero) is a real-world issue that no PoW-level change addresses; it is noted, not analysed. | X, R |
| ALG-26 | The commitment is binding and injective but is not a proof of work on its own: anyone can compute it for a claimed result, so a verifier must also check the RandomX result. As a pre-filter it only proves Blake2b work (about one GPU-minute per block at a difficulty of 300 G). The batch API returns the result of the previous input, so callers must pair results with their inputs. The Monero side (difficulty from the commitment, statements in its BLOCK_HASHING.md) belongs to the deferred integration review. | C2, R |
| ALG-27 | State that never influences the hash exists in both versions and cannot be predicted without executing the program: a Scratchpad word overwritten by an ISTORE before any read (a random Scratchpad bit flip leaves the hash unchanged in 8.7 % of cases in v1, 10.1 % in v2), top bits of R registers shifted out by even multipliers, and F input bits absorbed by FSCAL scaling followed by FADD/FSUB rounding (0.56 % of F input bit flips in v1, 2.47 % in v2). v1's XOR additionally confines any F or E perturbation to one Scratchpad word, erased before use in about 9 % of cases; v2's AES spreads it over 2-8 words (0.9 % for F, 0 of 1,200 for E). When the hash changes, it changes completely (Hamming distance 128 +- 8). Net effect of v2 positive. | S |
| ALG-28 | Floating-point claims hold: programs with at least one infinite E value 1.91 % (v1) and 6.54 % (v2) [95 % CI 6.24-6.86] against design 2.5.1's 2 % and 6.85 %; no NaN, subnormal, underflow or division by zero in 2.2e10 classified results (MXCSR flags checked); F stays below about 2^49 at iteration boundaries; E ranges from 2^-243 to infinity. | S |
| ALG-29 | Control-flow and coverage claims hold: CBRANCH is taken with probability 1/256.8 (v1) and 1/256.6 (v2); it never jumps three times in a row (0 in 12 million jump runs, and a proof); 67.5 % (v1) and 69.5 % (v2) of the L3-only Scratchpad area is overwritten per hash (design: about 66 %); the final Scratchpad compresses to 99.975 % and 99.988 % of its size (Appendix D: 99.98 %). Filtering slow first programs (Appendix A strategy III) loses hashrate in both versions once the cost of a new nonce is counted: the program-dependent runtime spread is at most 4.7 % (v1) and 6.3 % (v2). | S |
| ALG-30 | The v1 and v2 hashes of the same input are statistically independent (20,000 pairs, bit agreement 0.50014). Computing both shares only the seed hash, the Scratchpad fill, program 0's configuration and one Dataset item: about 3 % of a fast-mode hash, so mining both versions during a transition gives no meaningful economy. | S |

## 5. The v2 changes one by one

### 5.1 Conditional CFROUND

*Change:* CFROUND writes `fprc` only if bits 2-5 of `src >>> imm32` are zero. *Rationale (design_v2 1):* mode changes
cost up to 10 % of hashrate on Ryzen, an easy advantage for specialised hardware.

*Findings:* the rationale holds and the cost is now negligible (ALG-20). Rounding-mode support stays mandatory:
directed modes still carry 71 % of floating-point operations, and a design implementing only round-to-nearest computes
essentially no correct v2 hash (a hash stays entirely in round-to-nearest with probability about 6e-6, against 4e-4 in
v1). The gate passes in about
1/15 of executions and changes the mode in about 1/20, so mode switches are 10 times rarer than in v1. It adds a
data-dependent branch that costs CPUs 0.108 mispredictions per iteration.

*Verdict:* sound; achieves its goal.

### 5.2 AES mixing of F and E (step 10)

*Change:* each F register goes through four single AES rounds keyed by e0-e3 (AESENC for f0 and f2, AESDEC for f1
and f3) instead of `f ^= e`. *Rationale (design_v2 2):* double the AES work without hurting the hashrate, force AES
into the VM, and write better-mixed data to the Scratchpad.

*Findings:* all three goals are met. The written lines become uniform (ALG-16), and a perturbation of the F or E
state can no longer be confined to one Scratchpad word (ALG-27); the rounds are off the critical path and cost CPUs
nothing (ALG-18). Their cost to specialised hardware is also small: about 0.4 % of a fixed-function
design's energy and one small AES unit per VM, so the change does little for hardware resistance. The key schedule is
uneven (e3 acts as whitening) without practical consequence (ALG-17). CPUs without AES instructions pay about 1.5x in fast mode
(ALG-10).

*Verdict:* sound and an improvement over v1 for the Scratchpad's statistics; small effect on hardware economics.

### 5.3 Program size 256 -> 384

*Change:* `RANDOMX_PROGRAM_SIZE` 384. *Rationale (design_v2 3):* modern cores finish a 256-instruction iteration well
before a DRAM read returns; more work keeps them busy.

*Findings:* work per hash rises 1.5x (section 3) with no disproportionate ILP (ALG-19); filtering and feature omission
become less profitable (section 6.3, ALG-29). Verification time rises 1.6-7 % on this host, because light mode is dominated by
item computation (ALG-9). The light-mode margin is almost unaffected by this change alone (2.24 -> 2.20 in Model C).
For hardware economics, see ALG-5 and ALG-6: a fixed-function design's energy per hash rises 1.02-1.29x because the
Dataset reads dominate.

*Verdict:* sound; it raises the work per hash and, together with 5.4, restores the "about one DRAM latency per
iteration" balance on current CPUs (ALG-11).

### 5.4 Dataset prefetch two iterations ahead

*Change:* `mp` = `ma` in v2 (`mx` in v1), so the item read in iteration i+2 is addressed at the end of program i.
*Rationale (design_v2 4):* complements the program size increase.

*Findings:* the CPU rationale holds: fast mode hides DRAM latency whenever 2T >= L (ALG-3, ALG-11). The same lookahead
is available to every implementation, and it is the change with the largest security effect: a light-mode design can
overlap three item computations instead of two, which removes about a quarter of the light-mode margin (ALG-1). It also
helps latency-bound specialised fast-mode designs as much as CPUs (ALG-6), and would let a pipelined verifier run
about 3x faster (ALG-9).

*Verdict:* achieves its CPU goal but weakens light-mode tradeoff resistance; the trade-off was not analysed when the
change was made (ALG-15). Options in section 7.

### 5.5 Commitment

`randomx_calculate_commitment` = Blake2b-256(input || result), identical for v1 and v2. Sound as a binding commitment
and a cheap pre-filter; not a proof of work by itself (ALG-26). It is now specified in specs.md 2.1.

## 6. Whole-design review (v1 base)

### 6.1 Memory-hardness

- **Dataset (2080 MiB).** No partial-storage strategy helps (ALG-23). The 2080 MiB figure carries no margin in the
  design's own CPU-ratio framing (ALG-2); its real protection against light-mode mining is the cost of SuperscalarHash
  logic, quantified in ALG-1.
- **Cache (256 MiB, Argon2d, 3 passes).** A hard floor for light-mode miners and verifiers: recomputing any missing
  block of the final array costs about half a Cache initialisation (ALG-12). The design's area-time argument rests on
  this floor and it holds.
- **Scratchpad (2 MiB).** The L3-only part is overwritten as the design states (67.5 % per hash in v1, 69.5 % in v2;
  ALG-29), and the final content is incompressible. The regenerable initial content gives a bounded constant-factor
  tradeoff (ALG-13).

### 6.2 Light mode and verification

Light mode is the lever of the area-time argument, and the review found it weaker than the documents state on three
counts: its time per iteration is (T + L)/(k + 1), not L (ALG-3); its energy per hash is about that of fast mode, not
much higher (ALG-4); and the decisive comparison for specialised hardware depends on how fast SuperscalarHash logic
can be (ALG-1). v1 kept a margin of 2.24 at the design's 1 GHz assumption and still 1.15 with 3 GHz-class logic; v2
is at parity with 2 GHz-class logic and loses with 3 GHz-class logic. The break-even item latency moves from 312 ns to
430 ns. Verification cost is essentially unchanged by v2 on CPUs with AES (ALG-9).

### 6.3 Execution and specialised hardware

The trace-driven model reproduces the measured v2/v1 time ratios on six platforms (from 0.96-0.98 modelled against 1.01
measured on Zen 5 to 1.28 against 1.29-1.32 on Skylake laptops), which supports its use for the hardware comparison.
Its conclusions: the loop-carried dependency chain (integer operations plus the two L3 reads) bounds performance in
both versions; FP and AES are off the critical path (ALG-18); ILP grows less than the instruction count (ALG-19); the
program-filtering attack of design Appendix A stays unprofitable (at most 0.5 % gain in v2, negative once the fill cost
is counted; the probability that a program lacks a given instruction of weight 1/256 falls from 0.367 to 0.222). The
efficiency question is in ALG-5, ALG-6 and ALG-8.

### 6.4 Statistical properties

An instrumented interpreter (bit-exact with the library) recorded 3,000 hashes per version: 24,000 programs, 49 million
iterations and 2.2e10 classified floating-point results. Nothing in the VM state helps an attacker or a specialised
implementation.

- **Scratchpad writes.** The integer lines are uniform in both versions. v1's F lines are biased (ALG-16); v2's are
  uniform. Order-0 entropy of the final Scratchpad: 7.9956 bits per byte (v1) and 7.99996 (v2); the only residual
  structure in v2 is in ISTORE-written words (products have extra trailing zeros) and in repeated words written by
  ISTOREs of the same register (0.7 % of words), both harmless because AesHash1R absorbs every word.
- **Diffusion.** A change of the input, the Scratchpad or the registers either leaves the hash unchanged (overwritten
  or absorbed state, ALG-27) or changes it completely; there is no partial diffusion.
- **Floating point.** The design's figures hold (ALG-28); the spec invariant that no NaN or denormal occurs holds
  empirically, so FTZ/DAZ settings cannot matter.
- **Rounding modes and branches.** ALG-20 and ALG-29; the CBRANCH "at most twice in a row" property is proven.
- **Runtime.** The program-dependent spread is small (coefficient of variation at most 4.7 % in v1 and 6.3 % in v2 per
  program), so neither filtering (ALG-29) nor worst-case verification time (p99.9 at most 1.04x the mean, model) is a
  concern.
- **v1 and v2 together.** Independent outputs, about 3 % shared work (ALG-30).

### 6.5 Cryptographic constructions

All components were checked against their role and are sound: Blake2b (seed, register-file and final hashes,
BlakeGenerator), AesGenerator1R (Scratchpad fill: invertible per lane, adequate for its purpose), AesGenerator4R
(program generation), AesHash1R (fingerprint: per-lane avalanche, with Blake2b supplying cross-lane diffusion), Argon2d
version 0x13 (Cache), SuperscalarHash (Dataset diffusion; constant output bits harmless, ALG-21) and the commitment.
The arithmetic of design Appendix A (chaining) reproduces exactly. Details in the C2 report.

### 6.6 Specification and determinism

A second, independent implementation written from the specification in Python reproduces the library bit for bit: the
256 MiB Cache, 12 of 12 Dataset items, 69 of 69 light-mode hashes for v1 and v2 (four keys including the empty key and
a 60-byte key, inputs from 0 to 300 bytes) and 3 of 3 commitments. Only chapter 6 required reading the source; the
other gaps were resolved by the natural reading and confirmed by the reference (ALG-7, ALG-14). Two determinism points
are worth stating for implementers and are now in the specification: `fprc` persists across the 8 programs of a hash,
so host-side floating-point code run between programs inherits the rounding mode; and CBRANCH targets are fixed when
the program is loaded. Denormals never occur, so FTZ/DAZ settings do not matter (companion report, D-7).

## 7. Recommendations

No consensus parameter was changed. Items 1-5 should be settled before v2 is activated on Monero.

1. **Decide on the light-mode margin (ALG-1).** Options, with their effect in the throughput model (v2 light/fast AT
   ratio; v1 today 2.24, v2 1.66):

   | option | effect | CPU verification cost | other costs |
   |---|---|---|---|
   | A. `RANDOMX_SUPERSCALAR_LATENCY` 170 -> 255 / 340 | 2.44 / 3.21 | about 0 on CPUs whose light mode waits on Cache reads (this host: 58 ns of SuperscalarHash per 220 ns Cache read; a 5 GHz desktop: 34 vs 75 ns); up to +50 % / +100 % on slow-clocked boards | v1 and v2 no longer share the Dataset: miners rebuild once at the fork; light verifiers reuse the Argon2 Cache and need a second set of 8 SuperscalarHash programs; Dataset initialisation x1.5-2 per epoch |
   | B. `RANDOMX_CACHE_ACCESSES` 8 -> 10 / 12 / 16 | 2.05 / 2.44 / 3.21 | x1.23 / x1.46 / x1.92 (each access is a DRAM round trip) | same Dataset change; moves verification further from the CryptoNight target |
   | C. Prefetch distance back to 1 (keep 384 instructions) | 2.20 | none | gives back v2's main CPU benefit (x1.10-1.27 per iteration on a 5 GHz desktop) |
   | D. 256 instructions with prefetch distance 2 | 1.77 | -0.5 ms | gives back the extra work per hash for little gain |
   | E. Fewer program iterations | 1.66 | proportional | does not help: items and VM work scale together |
   | F. Accept and document | 1.66 | none | update design.md and design_v2.md (item 3) |

   Judgement: if a Dataset change is acceptable before release, A (255 or 340) is the cheapest way to recover and
   exceed v1's margin. Sharing the Dataset between v1 and v2 is a transition convenience, not a security property.
   Otherwise choose F and state the margin. C trades away v2's CPU benefit; E does not help. Because ALG-1 is a model
   result, an independent hardware-cost estimate before the decision is worthwhile (item 5).
2. **Restate the v2 efficiency claim (ALG-5, ALG-6, ALG-24).** Lead with hashes per Joule, state the power basis
   (done in `6b2a054`), describe the effect on specialised hardware as roughly neutral unless measured otherwise, and
   correct "20-25 % faster".
3. **Update the design rationale (ALG-2, ALG-3, ALG-4, ALG-9, ALG-10, ALG-11, ALG-16, ALG-20).** In design.md 1.4.1,
   2.2, 2.5.1, 2.9 and 3.4 and design_v2.md: the exact lookahead bound and its consequence for light mode, the v2 light/fast ratio and the
   dependence of the invariant on SuperscalarHash logic speed, the zero-margin construction of 2080 MiB, the energy
   comparison, the condition L <= k x T, the v2 verification cost, the cost on CPUs without AES instructions, the v1
   F-line bias, and the measured CFROUND gate rate (1/15 passes, 1/20 mode changes).
4. **Keep the specification changes and pin them with vectors (ALG-7, ALG-14).** Add test vectors for SuperscalarHash
   program generation (instruction lists and address registers for a few keys, e.g. those used by the spec check in
   the artifacts) and for the commitment, so that chapter 6.4 and chapter 2.1 are checked mechanically.
5. **Get an external review of the v2 design (ALG-15)**, focused on ALG-1 and on the hardware economics (ALG-5,
   ALG-6), ideally by one of the 2019 auditors or a hardware designer.
6. **Watch the hardware landscape (ALG-8).** An independent measurement of an X9 or R1X (wall power, hashrate, v1 and
   v2) would settle whether I4 still holds.
7. **Optional, not for this release.** Rotate the E key order per lane in the AES mix if the algorithm is revised again
   (ALG-17). Use the lookahead in the reference light-mode implementation to compute items for later iterations on helper
   threads (ALG-9); this needs no consensus change.

## 8. Evidence and artifacts

In [audits/2026-10-algorithm-review/](audits/2026-10-algorithm-review/):
- `reports/`: the track reports (T, X, S, C2, W, R) with methods, raw numbers, commands and sources.
- Track directories with the scripts, harness sources and instrumentation patches that produce every number above
  (heavy harnesses need an instrumented copy of `src/`; patches included). The README lists the entry point for each
  result.
- `lead-checks/`: the lead's independent re-computations (lookahead bound, Model C base values, design_v2 table
  arithmetic), each under a second to run; `spec-6.4-check/`: the reference generator against the normative
  pseudocode of specs.md 6.4 (1,200 programs, 536,212 instructions, identical).

Raw data, logs and binaries were not kept in the repository.
