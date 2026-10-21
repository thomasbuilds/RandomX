# Agent T: time-memory tradeoffs, light mode and verification time (RandomX v1 vs v2)

Branch `claude/modest-meitner-dkvpgd` (HEAD d3dd967), read-only. All work, harnesses, logs and scripts are in
`/tmp/claude-0/-home-user-RandomX/cfc10d96-f1d5-53d0-817a-8712f1f55302/scratchpad/agentT/` (below: `agentT/`).
Every timing was taken on core 3 (`taskset -c 3`, or a harness that pins itself to core 3 after a multi-threaded
Dataset initialisation), with v1 and v2 interleaved in the same process (v1,v2 then v2,v1 on alternate repetitions).

## 1. Findings

| ID | Severity | Conf. | Finding |
|---|---|---|---|
| T-1 | **High** | Medium | **v2 weakens the light-mode tradeoff margin for specialised hardware, and the main cause is the prefetch distance, not the program size.** In the throughput model (Model C, section 4.3) at the design's own assumptions (1 GHz SuperscalarHash logic, every operation 1 cycle, so an item takes 746 cycles), the light/fast AT ratio falls from **2.24 (v1) to 1.66 (v2)**, i.e. by 26%. The same 24-33% reduction appears across every parameter swept. With 2-3 GHz-class SuperscalarHash logic, v2 reaches parity or worse (1.03, 0.86) while v1 stays above 1 (1.38, 1.15). The break-even item latency (light mode loses only if an item takes longer than this) rises from 312 ns to 430 ns. Over a 14,580-point assumption grid with HBM/GDDR-class DRAM, light mode wins in 11.3% of cases for v1 and 26.2% for v2. Keeping 384 instructions but reverting to prefetch distance 1 restores 2.20; keeping prefetch distance 2 with 256 instructions gives only 1.77. design_v2.md does not discuss the effect on the light/fast balance (design.md 1.4.1, 2.9.1, 3.4). |
| T-2 | Medium | High | **The invariant in design.md 1.4.1 has zero margin by construction and depends on the CPU.** In the design's own model, AT_light/AT_fast = (256 + 3.81 MiB) × R / 2080 MiB, where R = T_light/T_fast. Break-even is at R* = 8.006. Measured R on this host is **7.48 (v1) and 7.37 (v2)**, giving AT ratios of 0.93 and 0.92 (both below 1). Appendix C gives R = 9.25 (1.155) for v1 on the reference CPU. Projected to v2 on that CPU, R lands between 7.2 and 9.5: the design's own criterion fails if v2 slows single-thread fast mode by more than about 19%. In design_v2.md's multi-threaded tables, 2 of 13 entries are at least 19% slower (i7-8650U, i7-6820HQ) and 5 more are 8-17% slower. With software AES on this host, R drops from 5.0 (v1) to 3.4 (v2). "About 8" is a property of one 2019 CPU, not of the algorithm. |
| T-3 | Medium | High | **The item-latency bottleneck is weaker than design.md 3.4 implies, and weaker than the brief's bound.** From the step order in spec 4.6.2, the address of item i is fixed right after program i−k, before item i−1 is consumed. The exact per-iteration bound is therefore **max(T_prog, (T_prog + L)/(k+1))**, not max(T_prog, L/k); this was checked by event simulation (`lookahead_sim.py`). A light-mode implementation can therefore keep 2 items in flight in v1 and 3 in v2, and its per-iteration time is about L_item/2 in v1 and L_item/3 in v2, not one 760-cycle item per iteration. |
| T-4 | Medium | Medium | **The energy claim in design.md 2.9.1 ("makes light mode very inefficient for mining") is not supported, even by the design's own numbers.** Design 3.4 itself equates the 1240 multiplications of an item with one 64-byte DRAM read. From generator op counts and stated per-operation energies, E_light/E_fast per hash is **1.37 (v1) and 1.34 (v2)** at the base values, with a range of 0.67-2.6 over DRAM energy (2-12 nJ per random 64 B read) and logic energy. Light mode costs about the same energy per hash, not "very" much more. v2 moves the ratio 2-9% towards 1, because both modes pay the extra VM energy. |
| T-5 | Low | High | **Verification time.** v2 adds only the extra VM time to light mode. On this host that is **+0.50 ms per hash (+1.6%)**: VM-only time 1.747 → 2.249 ms, paired ratio 1.31. Light mode is 93-94% item computation. Measured light-mode times are v1 30.5 ms and v2 31.1 ms (harness, batch API), and medians 32.4 and 33.1 ms (official `randomx-benchmark --verify`). Projected on the Appendix C CPU: 14.8 → 15.2 ms (+3%). v1 met the "as fast as CryptoNight" goal (design.md 1.3) with zero margin (Appendix C: "exactly matches"); v2 misses it by about 3% on that CPU (a judgement, since CryptoNight was not run here). Multi-threaded throughput falls by the same 1.6-3% (modelled; only core 3 was allowed). Direct paired light-mode medians were 1.07 in two separate runs (ranges 0.94-1.48), so the true increase is between +1.6% and about +7%; the noise does not allow a finer answer. v2's lookahead would let a verifier that computes items on 3 helper threads hash about 3x faster than the reference (modelled bound: 31 ms → 10.5 ms on this host; v1 with 2 helper threads 15.4 ms). |
| T-6 | Low | High | **CPUs without AES instructions pay much more for v2.** With software AES, fast mode on this host slows by **1.49x** from v1 to v2, against 1.03x with AES-NI. The software-to-hardware AES factor rises from 1.77x (v1) to 2.54x (v2) in fast mode. The in-loop AES (262,144 rounds per hash) costs about 2.7 ms per hash in software. Light mode is unaffected (+0-2%). JIT-less builds are also hit: the interpreter's fast mode is 1.85x slower in v2. This affects mining on AES-less RISC-V boards and ARM cores without the Crypto Extension, and on JIT-less platforms. It follows from the design ("AES must be implemented as part of the VM") but is not quantified in design_v2.md. |
| T-7 | Info | High | **Partial Dataset: no stored fraction p beats the better endpoint, for v1 or v2.** A bit-exact harness (hash XORs equal to real light mode) measured time versus p on this CPU, and it is close to linear. AT(p) is concave in the design's model (maximum about 2.6x at p = 0.5) and linear in Model C, so the optimum is p = 0 or p = 1. Packaging note: storing exactly 2 GiB of the 2080 MiB leaves 0.78% of reads to compute. |
| T-8 | Info | High | **Caveat about this host.** It is a Firecracker microVM. Random-read latency is 280 ns over 2 GiB and 165-190 ns over 256 MiB, even with 2 MiB pages. Software prefetch hides latency fully over 64 MiB, but over 2 GiB it leaves a floor of about 160-220 ns per iteration even 2-4 iterations ahead (`pfk`). Fast mode here therefore runs at about 250 ns per iteration for **both** v1 and v2. The host understates the v2 fast-mode slowdown, and its light/fast ratio is not representative of bare metal. The design conclusions above rest on the generator counts, the decomposition and the models, not on raw host ratios. |

Main judgement: v2 does not break RandomX's memory-hardness in practice. It does take away a substantial share of
the margin the design relied on to make light-mode (Cache + SuperscalarHash) mining uneconomic, mostly through the
prefetch distance. In the design's own CPU-ratio framing the margin was already zero by construction in v1. The
cheapest remedy, if a Dataset change is acceptable, is to raise `RANDOMX_SUPERSCALAR_LATENCY` (section 8): it
restores the margin in the model at almost no verification cost on CPUs whose light mode is bound by memory latency.
Otherwise, document the new ratio.

## 2. Measurements (measured facts)

### 2.1 Host and method
Intel Xeon (Cascade Lake, 2.8 GHz, AES-NI, AVX-512) with 4 vCPUs, in a Firecracker microVM; 1200 x 2 MiB huge pages.
Builds used: the lead's optimised build (`lead/build-x86/librandomx.a`, gcc 13, -O3) for `tbench`, `comp` and
`opcount`, plus a private patched copy for `tpart` (`agentT/rx-p`, patch in `agentT/patches/tpart-jit-hooks.diff`).
The patch adds hooks to the x86 JIT light-mode generator only, and none are active in the `fast`/`light` configurations.

Noise: other agents kept cores 0-2 busy, with load averages up to 14 during the second half of the runs. Contention for
the shared memory system leaked into core 3: the spread (max−min)/median was 12-38% for most configurations and up to
100% under peak load. Each configuration was repeated 5-11 times, interleaved. The tables give the median, the
minimum, and the median of **paired** v2/v1 ratios (v1 and v2 measured back to back), from `analyze.py`.

### 2.2 End-to-end time per hash (ms; JIT unless stated; batch API as in `randomx-benchmark`)

| configuration (log) | v1 median [min] | v2 median [min] | paired v2/v1 | R = light/fast v1 / v2 |
|---|---|---|---|---|
| fast, HW AES, 2 MiB pages (`tpart-hw-lp1.txt`, 7 reps) | 4.076 [3.833] = 245 H/s | 4.218 [3.974] = 237 H/s | 1.029 (0.92-1.43) | |
| light, HW AES, 2 MiB pages (same run) | 30.47 [25.30] | 31.10 [28.68] | 1.070 (0.96-1.15) | **7.48 / 7.37** |
| VM only ("tiny": items from a 4 KiB L1 buffer) (same run) | 1.747 [1.653] | 2.249 [2.143] | **1.310** (1.13-1.55) | |
| fast, HW AES, 2 MiB pages, repeat (`tpart-prefetch-t0.txt`, 9 reps, higher load) | 4.193 [3.484] | 4.316 [4.217] | 1.075 | |
| fast / light / tiny, HW AES, repeat (`tpart-hw-lp1-r2.txt`, 11 reps) | see section 2.8 | | | |
| fast, HW AES, 4 KiB pages (`tpart-hw-lp0.txt`; very noisy, spreads 82% / 74%) | 8.74 [6.30] | 6.97 [6.35] | 0.85 (0.71-1.35) | |
| light, HW AES, 4 KiB pages (same run) | 47.7 [37.6] | 45.8 [42.7] | 1.03 | 5.46 / 6.56 |
| fast, **soft AES**, 2 MiB pages (`tpart-sw-lp1.txt`) | 7.21 [5.71] | 10.71 [8.97] | **1.487** (1.19-1.66) | |
| light, soft AES (same run) | 36.2 [32.0] | 36.9 [34.8] | 1.00 | 5.02 / 3.44 |
| VM only, soft AES (same run) | 4.03 [3.31] | 7.19 [6.71] | **1.92** (1.53-2.21) | |
| light, single-hash API (verification), HW AES, 2 MiB (`tbench-light-verify-lp1.txt`) | 32.3 [30.3] | 31.5 [30.1] | 0.95 (0.82-1.28) | |
| same, 4 KiB pages (`tbench-light-verify-lp0.txt`) | 41.3 [37.0] | 42.6 [40.2] | 1.03 | |
| same, soft AES (`tbench-light-verify-sw-lp1.txt`) | 33.0 [25.8] | 33.4 [27.3] | 1.01 | |
| **interpreter** fast, HW AES, 2 MiB (`tbench-fast-interp-lp1.txt`) | 36.4 [35.1] | 67.3 [63.9] | 1.85 | |
| **interpreter** light (`tbench-light-interp-lp1.txt`) | 274.1 [261.3] | 287.8 [281.1] | 1.10 | 7.53 / 4.27 |
| official `randomx-benchmark --verify --jit --largePages --nonces 300` (3+3 runs) | 32.43 (29.6-35.3) | 33.07 (32.1-36.9) | 1.12 (0.91-1.14) | |
| official `taskset -c 3 randomx-benchmark --mine --jit --largePages --init 4 --nonces 1000 [--v2]` (1+1 run) | 246.6 H/s = 4.055 | 237.9 H/s = 4.203 | 1.036 | |

All hash XORs in the light, fast and partial configurations agree with each other per version. The official tool's
results for 300 nonces are `ebafb270...` (v1) and `a9d8284e...` (v2) in every run. Its 1000-nonce fast-mode runs
print the reference results `10b649a3...` (v1) and `b85d79e0...` (v2). Without large pages, fast mode
here is about 1.7-2.1x slower (medians; minima 1.6x), which confirms the brief's TLB remark; the design analysis uses
the 2 MiB numbers.

### 2.3 Light-mode decomposition and per-iteration time (HW AES, 2 MiB pages; `logs/model-out.md` section 1)

| | v1 | v2 |
|---|---|---|
| fill + hash + 8 x (program generation + JIT + Blake2b), per hash (batch) | 116 us (0.4%) | 131 us (0.4%) |
| VM execution (tiny − overhead) | 1.631 ms (5.4%) | 2.118 ms (6.8%) |
| Dataset items (light − tiny) | 28.72 ms (94.3%) | 28.85 ms (92.8%) |
| item cost in light-mode context | 1753 ns | 1761 ns |
| **VM time per iteration** | **99.5 ns** (279 cycles; 0.92 VM instr/cycle) | **129.2 ns** (362 cycles; 1.06 VM instr/cycle) |
| random DRAM read latency, 2 GiB (`dramlat`) | 280 ns | 280 ns |
| fast-mode stall per iteration (fast − tiny) | 142 ns | 120 ns |

So v2 does 1.5x the instructions (plus 16 AES rounds per iteration) in 1.30x the time. That supports design_v2's
point that the extra work fills idle issue slots. The design.md 2.9 claim that one iteration is about one DRAM latency
(50-100 ns) cannot be tested on this host, where DRAM latency is 2-3x that of bare metal. On the Appendix C CPU it
held for v1 (90 ns per iteration). For v2 the corresponding condition is T_prog ≥ L/2 (section 4.2).

### 2.4 Item function and fixed costs (`comp`, core 3, 5 reps; `logs/comp-1.txt`)
- JIT item function, warm (Cache lines in L1, pure SuperscalarHash): **467 ns per item = 58.4 ns = 164 cycles per program**, which matches the 170-cycle target. Cold random items: **1264 ns**. Sequential blocks (the Dataset-initialisation pattern): 1344 ns per item per thread. The C++-interpreted SuperscalarHash takes 12.1-12.7 us per item.
- AesGenerator1R fill of 2 MiB: 88 us (HW) / 890 us (SW). AesHash1R: 86 / 879 us. Fused hashAndFill: 87 / 1726 us. AesGenerator4R over 3200 B: 0.245 / 5.19 us. Blake2b-512 of 256 B: 0.51 us. **JIT compile of one program: v1 2.89 us, v2 4.81 us** (+66%).
- Light mode on this host is bound by Cache-read latency: per access, max(58 ns of SuperscalarHash, about 175-220 ns of Cache read).

### 2.5 Memory system (`dramlat`, `pfk`; `logs/dramlat-1.txt`, `logs/pfk-1.txt`)
- Dependent random 64 B reads with 2 MiB pages: 1 MiB 20 ns, 8-64 MiB 141-158 ns, 256 MiB 163-188 ns, 2 GiB 278-286 ns. With 4 KiB pages: 256 MiB 245 ns, 2 GiB 390 ns.
- Memory-level parallelism over 2 GiB: 2/4/8/16 independent chains give 136/65/42/37 ns per load (27 M loads/s per core).
- Prefetch k iterations ahead with about 110-140 ns of dependent compute per step: over 64 MiB the latency is fully hidden for k ≥ 1 (128-136 ns per step). Over 2 GiB the floor is 160-220 ns for k = 1-4. `prefetcht0` and `prefetchnta` behave the same in RandomX (`tpart-prefetch-t0.txt`). This is a property of this VM's large-footprint random access, not of RandomX.

### 2.6 Software vs hardware AES (medians, 2 MiB pages)

| ratio SW/HW | v1 | v2 |
|---|---|---|
| fast mode | 1.77 | **2.54** |
| VM only (tiny) | 2.30 | 3.20 |
| light mode | 1.19 | 1.19 |
| single-hash verification | 1.02 | 1.06 |

### 2.7 Partial Dataset (bit-exact harness; `tpart-hw-lp1.txt`)
Items with index below a threshold are read from memory (prefetched 1 or 2 iterations ahead, like fast mode); the
rest are computed from the Cache by the light-mode item function. For every p, the hash XOR equals the real
light/fast XOR (for example p = 0.1 and p = 0: `b96dcc98...` v1, `3b554d06...` v2, the same as real light mode).
Medians in ms per hash:

| p stored | 1 | 0.875 | 0.75 | 0.5 | 0.25 | 0 (light) |
|---|---|---|---|---|---|---|
| v1 | 4.01 (fast 4.08) | 8.20 | 11.07 | 16.95 | 23.01 | 30.47 |
| v2 | 4.56 (fast 4.22) | 8.42 | 11.02 | 17.97 | 23.70 | 31.10 |

Time falls roughly linearly in (1−p), the stored fraction; computed items cost 1.75 us each in both versions.

### 2.8 Repeat of the main configuration (11 reps; `logs/tpart-hw-lp1-r2.txt`)
The repeat was taken under heavier load (load average 9-14). Medians [min]: fast v1 3.886 [3.511], v2 4.120 [3.824]
(paired 1.073); light v1 28.49 [25.55], v2 32.25 [26.14] (paired 1.069, range 0.94-1.48); VM only v1 1.770 [1.676],
v2 2.292 [2.125] (paired 1.262). R = 7.33 (v1) and 7.83 (v2) from medians, or 7.28 and 6.84 from minima. This
confirms run 1: on this host R is about 7.3-7.8 for both versions, and the v1/v2 difference in R cannot be resolved
here. The VM-only ratio is stable at 1.26-1.31.

## 3. Operation counts from the generators (`bin/opcount progs=20000 keys=2000`; `logs/opcount.txt`)

Per program iteration (20,000 random programs, AesGenerator4R output decoded with the opcode table):

| per iteration | v1 (256) | v2 (384) |
|---|---|---|
| 64-bit multiplications (incl. effective IMUL_RCP) | 37.98 | 56.99 |
| integer ALU ops (add/sub/xor/rot/neg/CBRANCH) | 103.1 | 154.6 |
| FP add/sub lanes / mul lanes / div lanes / sqrt lanes | 83.9 / 64.0 / 8.0 / 11.9 | 125.9 / 96.0 / 12.0 / 17.9 |
| Scratchpad reads L1 / L2 / L3 (8 B) + 2 x 64 B | 27.0 / 9.0 / 3.1 | 40.4 / 13.4 / 4.7 |
| Scratchpad stores L1 / L2 / L3 (8 B) + 2 x 64 B | 10.5 / 3.5 / 2.0 | 15.8 / 5.2 / 3.0 |
| AES rounds in the loop | 0 | 16 |
| CBRANCH re-execution (instructions; jump p = 1/256) | 0.61 | 0.92 |
| Dataset reads | 1 | 1 |

Per-type counts match size × frequency/256 within sampling error, for example FMUL_R 31.99 / 48.01, CBRANCH
24.97 / 37.48, ISTORE 16.00 / 24.00, CFROUND 1.01 / 1.50 (in v2 only about 1/16 of CFROUNDs write `fprc`). The full
29-type table is in `logs/opcount.txt`.

Per hash: v1 4.19 M VM instructions (0.62 M multiplications); v2 6.29 M (0.93 M) plus 262,144 in-loop AES rounds;
both modes add 262,144 AES rounds for fill and hash.

SuperscalarHash (2000 keys x 8 programs): **447.3 instructions** per program (sd 9.6; design 450), **155.8
multiplications** (sd 2.4; design 155), generator ASIC latency 92.8 (design 95), reference-CPU latency 172.8 cycles,
IPC 3.10. Per item: **3578 instructions, 1247 multiplications** (design 1240). The dataflow critical path of a whole
item (8 programs plus 8 dependent Cache reads, unlimited parallelism) is **746 unit delays** (design 760), and 890 /
1093 / 1339 delays when a multiplication costs 2 / 3 / 4 ALU delays. An item becomes memory-bound when a Cache read
takes longer than about 93 delays: it is then 8 x (L_c + 1), e.g. 809 at L_c = 100 and 1608 at L_c = 200. A
light-mode hash therefore costs 16,384 items = 58.6 M SuperscalarHash instructions (20.4 M multiplications) plus
131,072 random Cache reads, on top of the same VM work as fast mode.

## 4. Models (modelled estimates; `model.py` prints every table in `logs/model-out.md`)

### 4.1 The design's own model (1.4.1)
AT_light/AT_fast = (256 MiB + one 0.2 mm² SuperscalarHash core = 3.81 MiB of DRAM at 0.149 Gb/mm²) × R / 2080 MiB.
The 2080 MiB size was chosen so that break-even falls at R* = **8.006**, leaving no margin.

| CPU / version | R | AT ratio |
|---|---|---|
| Ryzen 7 1700, v1 (Appendix C) | 9.25 | 1.155 |
| this host v1 / v2 (measured) | 7.48 / 7.37 | 0.934 / 0.921 |
| this host, soft AES v1 / v2 | 5.02 / 3.44 | 0.63 / 0.43 |
| Ryzen 7 1700, v2: light +0.44 ms (VM +30%), fast unchanged / +10% / +20% / +30% | 9.53 / 8.66 / 7.94 / 7.33 | 1.19 / 1.08 / 0.99 / 0.92 |

The v2 projection fails the criterion once single-thread fast mode is about 19% slower in v2 than in v1. design_v2's
multi-threaded tables give v2/v1 time factors from 0.92 (Zen 5 laptops) to 1.32 (i7-6820HQ). On fast CPUs, v2 can
**raise** R, because k = 2 lets fast mode go below one DRAM latency per iteration. The ratio moves in opposite
directions on different CPUs, so the CPU-ratio argument cannot settle the question either way.

### 4.2 Exact lookahead bound (T-3)
Write S_i for the start of program i, T for the program time and L for the item latency (a DRAM read in fast mode,
an item computation in light mode). Program i+1 can start only after program i and after item i has been XORed in
(step 7), and the address of item i is fixed when program i−k ends (step 5). That gives
S_{i+k+1} = max(S_{i+k} + T, S_i + T + L), with period **max(T, (T+L)/(k+1))**. `lookahead_sim.py` reproduces this
on 20 cases. Consequences:
- Fast mode hides DRAM fully when T ≥ L (v1) or T ≥ L/2 (v2). This is design_v2's rationale and it holds.
- In light mode with L_item ≫ T, the period is L_item/2 in v1 and L_item/3 in v2: **v2 tolerates 1.5x the item latency**, using up to 3 items in flight per hash instance. For the Ryzen 1700 light case (item 806 ns) the bound falls from 448 to 308 ns per iteration (×0.69); for an ASIC with 760 ns items, from 405 to 278 ns. The reference light-mode implementation does not exploit any of this: it computes each item synchronously.
- The brief's max(T, L/k) overstates the v1 bound by up to 2x and the v1-to-v2 improvement (×0.5 instead of ×0.68).

### 4.3 Specialised hardware
Common assumptions (A), with base values and swept ranges:
- VM: T_prog(v1) = 50 ns [30-80]; v2 = 1.5 × [1.3]. VM instance area 1.15 mm² (2 MiB 7 nm SRAM about 0.67 + logic 0.5) [0.8-2.0].
- DRAM: 80 ns [50-100]. DRAM area-time per random 64 B read c_dram = 2.7e-7 mm²·s (GDDR6-class) [1.3e-7 HBM; 1e-6 and 2.5e-6 for DDR DIMMs, where 4-8 dies take part in every read].
- SuperscalarHash logic: S1-S5 in the table below. Item engine 0.2 mm² per item in flight (design 3.4) [0.05-0.4], or an unrolled pipeline at 1.1e-8 mm²·s per item.
- Cache: on-chip SRAM, latency 10 ns [5-20], area 82 mm² at 7 nm [64 at 5 nm, 228 at 16 nm], amortised over 300 mm² of VM and engine logic [100-600].

Alternatives (`alt_check.py`, Model C v1 / v2): Cache in eDRAM (about 3x SRAM density, 30 mm²) gives 1.94 / 1.43;
Cache in 16 nm SRAM (228 mm²) gives 3.10 / 2.29. Model B with modern DRAM density (0.3 Gb/mm²) gives 0.91 / 0.55.

Model B is the design's framing: one instance, area (memory + engines + VM) × hash time, Cache and Dataset at DRAM
density with DRAM latency on Cache reads. Model C is throughput-optimal: the memory is shared by many instances, and
the cost per hash is VM area × hash time + 16,384 × (c_dram, or item-engine area × item latency) + Cache amortisation.

| SuperscalarHash logic | item latency | v1 B | v1 C | v2 B | v2 C | C, unrolled item pipeline v1 / v2 |
|---|---|---|---|---|---|---|
| S1: 1 GHz, every op 1 cycle (design 3.4) | 746 ns | 0.83 | **2.24** | 0.50 | **1.66** | 1.73 / 1.16 |
| S2: 1 GHz, multiply 3 cycles | 1093 ns | 1.19 | 3.24 | 0.71 | 2.38 | 2.47 / 1.64 |
| S3: 2 GHz, multiply 2 cycles | 445 ns | 0.72 | 1.38 | 0.44 | **1.03** | 1.09 / 0.75 |
| S4: 3 GHz, multiply 3 cycles | 364 ns | 0.72 | 1.15 | 0.44 | **0.86** | 0.92 / 0.64 |
| S5: dataflow, ALU 0.2 ns, multiply 0.8 ns | 268 ns | 0.72 | 0.87 | 0.44 | 0.66 | 0.72 / 0.51 |

(Ratios are light/fast AT, so a value below 1 means light mode is cheaper. The item-latency column is for the on-chip
Cache of Model C; Model B reads its Cache at DRAM latency, so its items take at least 8 x 81 = 648 ns.) Each
parameter swept on its own leaves
the v2/v1 factor between 0.67 and 0.76. Break-even item latency in Model C: **v1 312 ns, v2 430 ns** (unrolled
pipeline: 402 / 626 ns); light mode loses only while the achievable item latency stays above it. Full grid:
- HBM/GDDR-class DRAM, 14,580 combinations: light wins in 11.3% of cases for v1 and 26.2% for v2 (14.9% v2-only).
- All DRAM classes, 29,160 combinations: 49.8% / 60.4%. Commodity DIMMs make fast mode expensive per random read, but a fast-mode ASIC would not use them.
- Model B: 80.0% / 93.3%. In the design's single-instance framing with realistic ASIC times, light mode already won in v1.

Judgement: v1's specialised-hardware margin depended on SuperscalarHash being slow (about 1 GHz-equivalent per
operation). v2 needs it to be about 1.4x slower again for the same margin.

### 4.4 Energy per hash (A: per-operation energies in pJ, low/base/high)
The energies are: ALU 0.05/0.1/0.2; 64-bit multiply 1.5/3/6; FP add 0.5/1/2; FP multiply 1.2/2.5/5; FP divide and
square root 6/12/24; 8 B scratchpad access at L1/L2/L3 1.5/5/15 (base); 64 B L3 access 60; per-instruction overhead
1; AES round 4; SuperscalarHash per-op overhead 0.3; on-chip 64 B Cache read 300 [100-1000]; random 64 B DRAM read
5 nJ [2-12].

| | VM pJ/iteration | E_VM per hash | E_item | E_fast | E_light | light/fast |
|---|---|---|---|---|---|---|
| v1 base | 1299 | 26 uJ | 7.45 nJ | 108 uJ | 148 uJ | **1.37** |
| v2 base | 1893 | 36 uJ | 7.45 nJ | 118 uJ | 158 uJ | **1.34** |
| v1 / v2, low energies | | | | | | 0.69 / 0.70 |
| v1 / v2, high energies | | | | | | 2.60 / 2.38 |
| v1 / v2, DRAM 2 / 12 nJ (base ops) | | | | | | 2.51 / 0.67, 2.30 / 0.68 |

## 5. Partial Dataset (task 3)
The measured CPU times are in 2.7. AT(p)/AT(1), where the p < 1 cases also carry the Cache:

| p | 1 | 0.875 | 0.75 | 0.5 | 0.25 | 0 |
|---|---|---|---|---|---|---|
| design model with host times, v1 / v2 | 1 / 1 | 2.01 / 2.00 | 2.38 / 2.29 | 2.60 / 2.66 | 2.12 / 2.11 | 0.93 / 0.92 |
| Model C (base), v1 / v2 | 1 / 1 | 1.21 / 1.14 | 1.36 / 1.21 | 1.65 / 1.36 | 1.95 / 1.51 | 2.24 / 1.66 |

There is no interior minimum for either version. The (memory × time) product is concave in p, and the throughput cost
is linear in p. The attacker's choice is all or nothing, so the relevant comparison is p = 0 against p = 1, which is
T-1/T-2. Each program uses a 2 GiB window at a 0-32 MiB offset, so a device holding exactly 2 GiB computes 0.78% of
reads on average (at most 1.56%). On this CPU that costs about 5% of hashrate. It could matter only at a memory
packaging step; it is not a weakness in a linear cost model.

## 6. Prefetch distance: where k = 2 matters (task 4)
Per-iteration lower bounds in ns, exact formula (the brief's formula in brackets):

| case | v1 | v2 | v2/v1 |
|---|---|---|---|
| this host, fast (T 99.5/129, DRAM 280) | 190 [280] | 136 [140] | 0.72 |
| this host, light, reference implementation (synchronous items) | 1853 (T + item) | 1890 | 1.02 |
| this host, light, lookahead-exploiting (item 1760) | 930 [1760] | 630 [880] | 0.68 |
| 5 GHz desktop, fast (T 45/58.5, DRAM 70) | 58 [70] | 58 [58] | 1.00 |
| 5 GHz desktop, fast, DRAM 90 | 68 | 58.5 | 0.87 |
| ASIC fast (T 50/75, DRAM 50 / 100) | 50 / 75 | 75 / 75 | 1.50 / 1.00 |
| ASIC light, item 364 / 760 / 1093 ns | 207 / 405 / 572 | 146 / 278 / 389 | 0.71 / 0.69 / 0.68 |

Where k = 2 matters:
- Latency-limited light-mode designs (ASIC or FPGA with an on-chip Cache).
- Verifiers that pipeline item computation across threads.
- Fast mode on CPUs whose VM iteration is shorter than DRAM latency (high-clock cores with few threads, where L > T).

Where it matters little or not at all:
- Fast-mode ASICs, where T_prog is about L_dram and v2 even costs 1.5x per iteration because the program is longer.
- The reference light-mode verifier, which never uses the lookahead.

Massively parallel light-mode designs sit in between. Their cost is throughput, not single-hash latency, but each
in-flight hash holds a 2 MiB scratchpad for its whole duration. That occupancy is the dominant light-mode cost term in
Model C, so k = 2 still matters there, and this is why T-1 holds in the throughput model.

## 7. Verification (task 5)
- Single hash (reference light mode, JIT, HW AES, 2 MiB pages): v2 is +0.50 ms (+1.6%) on this host. Direct light-mode pairs: 1.07 (batch), 0.95 (single-hash API) and 1.12 (official tool); these are consistent within noise. Software AES: +0-6%. Interpreter: +5% (288 against 274 ms).
- Against CryptoNight (design.md 1.3): Appendix C says v1's 14.8 ms "exactly matches" CryptoNight on the Ryzen 7 1700. With the measured +30% VM time per iteration, v2 on that CPU is about 15.2 ms (+3%). In judgement, the goal is missed by a few percent rather than met with margin, and the margin had been zero in v1. CryptoNight was not run here, so the comparison cannot be verified directly.
- Multi-threaded verification throughput (modelled): light mode is bound by Cache-read latency per thread (8 dependent reads per item), and per-core random-read capacity is 27 M/s (`dramlat`, 16 chains) against 4.3 M/s needed by one light thread. Throughput therefore scales with threads, and v2 lowers it by the same amount as single-hash latency (+1.6-3% modelled; paired light-mode medians up to +7% measured).
- Opportunity: a verifier that computes items for iterations i+1..i+k on helper threads is bounded by (T + L_item)/(k+1) per iteration. That is about 15 ms (v1, 2 threads) and 10 ms (v2, 3 threads) on this host, against 31 ms today. v2's lookahead thus makes single-hash verification latency potentially better than v1's. This is not implemented in the library.

## 8. Options to restore the invariant (task 6; recommendations only)

| option | effect in Model C (v2 light/fast AT; v1 today 2.24) | CPU verification cost (model) | other costs |
|---|---|---|---|
| A. `RANDOMX_SUPERSCALAR_LATENCY` 170 → 255 / 340 | 2.44 / 3.21 | **≈ 0** on memory-bound CPUs: this host (58 ns SuperscalarHash vs 220 ns per Cache access) and a 5 GHz desktop (34 vs 75 ns) | Dataset changes, so v1 and v2 no longer share it; miners rebuild once at the fork, and light verifiers reuse the Argon2 Cache and need only a second set of 8 SuperscalarHash programs. Dataset init compute ×1.5-2 per epoch. Slow-clocked CPUs (SuperscalarHash program ≈ DRAM latency, e.g. 1.5-2.4 GHz boards) see up to +50% (255) or +100% (340) verification time. JIT buffers are already parameterised (round 2, O-4). |
| B. `RANDOMX_CACHE_ACCESSES` 8 → 10 / 12 / 16 | 2.05 / 2.44 / 3.21 | ×1.23 / ×1.46 / ×1.92 (every access is a DRAM round trip) | Same Dataset change. Dataset init ×1.25-2. Moves verification further from the CryptoNight goal. |
| C. Prefetch distance back to 1 (keep 384 instructions) | 2.20 | none | Gives back design_v2's CPU benefit: on a 5 GHz desktop the per-iteration bound rises ×1.10 (DRAM 70 ns) to ×1.27 (DRAM 90 ns). |
| D. 256 instructions with prefetch distance 2 | 1.77 | −0.5 ms | Gives back the "more work per hash" goal; little gain. |
| E. Fewer program iterations | unchanged (1.66) | proportional | Item count and VM work scale together, so the ratio does not move. **Does not help.** |
| F. Accept and document | 1.66 | none | Update design.md 1.4.1/2.9.1/3.4 and design_v2.md: give the v2 light/fast ratio, the exact lookahead bound, and the fact that the invariant depends on SuperscalarHash logic speed. |

Recommendation (judgement): if a Dataset change is acceptable before release, option A (255 or 340) is the
cheapest way to recover and exceed v1's margin. Sharing the Dataset between v1 and v2 is a convenience for the
transition, not a security property; losing it costs each miner one Dataset rebuild at the fork. Otherwise, use F and
state the margin honestly; C trades away v2's main CPU benefit.

## 9. Design claims: status after v2

| claim | v1 | v2 |
|---|---|---|
| 1.3 at least as fast to verify as CryptoNight | met with about 0 margin (App. C) | about 3% slower on the reference CPU (projected); not directly verifiable here |
| 1.4.1 light-mode AT ≥ fast-mode AT with ratio about 8 | holds only on CPUs with R ≥ 8.006 (App. C 9.25: yes; this host 7.48: no) | weaker: −24 to −33% in specialised-hardware models; on CPUs R moves either way (7.37 here) |
| 1.4.1 / 3.4 SuperscalarHash area 0.2 mm², 760-cycle item, 1240 multiplications | counts confirmed: 746 delays, 1247 multiplications, 447 instructions | unchanged (same Dataset) |
| 2.2 / 2.9 one iteration ≈ one DRAM latency, Dataset reads fully prefetched | holds on the App. C CPU (90 ns); not testable on this VM (DRAM 280 ns) | condition becomes T ≥ L/2 (exact bound); VM iteration 1.30x v1 here |
| 2.9 about 1500 H/s per bank group (25 M random reads/s ÷ 16,384) | arithmetic correct | unchanged (same reads per hash) |
| 2.9.1 / 3.4 light mode bottlenecked by SuperscalarHash; energy makes it very inefficient | latency: weaker than stated (L/2, T-3); energy: not supported (T-4) | latency: L/3; energy ratio 1.34 |
| 3.4 170-cycle target ≈ DRAM latency at 2-4 GHz | 164 cycles measured per program (58 ns at 2.8 GHz) | unchanged |
| App. C overhead 7.5% of fast-mode time | this host 116 us = 2.8% of 4.08 ms (2.9 us JIT per program) | 131 us (4.8 us JIT per program) |
| design_v2 §3-4 longer programs and 2-ahead prefetch keep fast CPUs busy | consistent with the exact bound and with VM 1.30x for 1.5x work | not measurable as a hashrate gain on this VM (T-8) |
| design_v2 §2 AES mix doubles AES per hash without hurting hashrate | true with AES-NI (262,144 + 262,144 rounds) | false without AES instructions: fast mode 1.49x slower (T-6) |

## 10. Reproduction
- Harness sources: `agentT/h/` (`tbench.cpp`, `tpart.cpp`, `comp.cpp`, `opcount.cpp`, `dramlat.c`, `pfk.c`); binaries in `agentT/bin/`. Private JIT hooks: `agentT/patches/tpart-jit-hooks.diff` against `src/`, applied in `agentT/rx-p` and built in `agentT/build-p`.
- Exact commands: `agentT/runs1.sh` (first batch, including build lines), `agentT/runs2.sh`, `agentT/runs3.sh`. Raw outputs are in `agentT/logs/`.
- Analysis: `python3 -I agentT/analyze.py agentT/logs/<log>` (medians and paired ratios), `python3 -I agentT/model.py` (every model table, `logs/model-out.md`), `python3 -I agentT/lookahead_sim.py` (exact bound, `logs/lookahead-sim.txt`), `python3 -I agentT/alt_check.py` (eDRAM, 16 nm and DRAM-density alternatives, `logs/alt-check.txt`). Run the Python scripts from `agentT/`.
- Main timing command: `taskset -c 0-3 ./bin/tpart cfgs=fast,p1.0,p0.875,p0.75,p0.5,p0.25,light,tiny nonces=100 lightnonces=30 reps=7 lp=1 softaes=0`. Dataset init uses 4 threads, then the process pins itself to core 3.
