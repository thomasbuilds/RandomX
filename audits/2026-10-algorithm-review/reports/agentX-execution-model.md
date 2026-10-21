# Agent X: execution model, instruction-level parallelism and the specialised-hardware advantage (RandomX v1 vs v2)

Round 3 (algorithm-level review). Tree: `/home/user/RandomX`, branch `claude/modest-meitner-dkvpgd` (HEAD d3dd967, hashes identical
to upstream a901d5b), used read-only. All scripts and raw outputs are in
`/tmp/claude-0/-home-user-RandomX/cfc10d96-f1d5-53d0-817a-8712f1f55302/scratchpad/agentX/` (written `agentX/` below). Every
heavy command ran under `taskset -c 0-2 nice -n 10`.

Labels used throughout: **measured** = counted in traces from the real algorithm (bit-exact with the library); **model** =
computed with the stated latency, port or energy assumptions; **judgement** = my interpretation.

## 1. Bottom line

1. **The v2 efficiency claim does not measure what the security argument needs (Medium).** The "VM+AES instructions per Joule"
   column of design_v2.md (115-167 %) is exactly 1.5294 x (hash/J v2 / hash/J v1). It restates the 53 % increase of work per
   hash that v2 defines. In hash/J, the authors' own table shows v2 *less* efficient on 10 of 13 CPU configurations (-1.4 % to
   -24 %) and more efficient on 3 (Ryzen 7 1700X +0.8 %, the two Zen 5 laptops +8 % and +9 %). What matters for hardware
   resistance is how a specialised design's energy per hash changes. **Model:** with measured operation counts, a
   fixed-function design's energy per hash rises only **1.02-1.29x (central 1.11x)**, because the unchanged 16,384 random
   Dataset reads per hash dominate its energy (compute alone would rise 1.38x). The ASIC/CPU energy gap therefore moves by
   about **0.84-1.21x (central estimate, per CPU)**, not 1.15-1.67x. The Zen 5 configurations and the 1700X, which keep at
   least ~99 % of their v1 hashrate (memory-latency-bound), gain 9-21 % relative to the modelled design. Most other parts
   (Zen 2/3/4, the 9950X at 100 W, the 12900K) move by -5 % to +7 %. The two Skylake-class Intel laptops lose 14-16 %.
2. **The extra work does fill idle CPU cycles, but those idle cycles are not CPU-specific (Medium).** The idle time in v1 is
   Dataset-latency stall. **Model:** with a one-iteration prefetch, a single instance hides at most k x CP = 36 ns of DRAM
   latency at 5 GHz with CPU-class latencies, so a v1 thread on a current core waits 17-47 % of each iteration at 50-100 ns.
   v2's two-iteration prefetch hides up to 99 ns. The same arithmetic applies to any latency-bound specialised design: an
   ASIC-class instance at 1.5-2 GHz with 75-100 ns DRAM runs a v2 iteration in 0.75-1.12x the v1 time. Only throughput-bound
   designs, which hide memory latency with many instances, pay the full ~1.5x execution work. For them it is execution energy
   and execution-unit area, not the SRAM or DRAM that dominate such designs.
3. **Floating-point work, including the 16 v2 AES rounds, is never on the loop-carried critical path (Info, measured for every
   program).** Setting all FP and AES latencies to zero changes the recurrence by at most 0.01 cycles per iteration in all
   192 + 192 programs. The critical path per iteration is the integer chain plus the two L3 scratchpad reads. So the AES mix
   costs CPUs no latency (design_v2's "without hurting the hashrate" is supported). It also costs a specialised design
   almost nothing: about 0.4 % of its energy per hash and one small AES unit.
4. **v2 does not expose disproportionate ILP (Info).** **Model:** the critical path grows 1.38x with CPU latencies and
   1.46-1.48x with 1-cycle or ASIC latencies, for 1.50x the instructions. Available parallelism rises only 1-8 %. A
   Zen-4-class core with a window of 320 or more entries runs within 1-2 % of the unbounded-window bound in both versions. A
   224-entry window costs 5.4 % (v1) and 6.8 % (v2). The hypothesised "opposite effect" is not material.
5. **"Memory-bound in v1, compute-bound in v2" (design_v2 §3-4) is supported by the model.** Zen 5 model, one thread,
   5.2 GHz: v1 takes 292 cycles per iteration at 75 ns against 188 with memory hidden (memory-bound). v2 takes 259 against
   258 (latency hidden). With 2-way SMT, v1 core time per thread-iteration grows 108 -> 152 -> 184 cycles at 0, 75 and
   100 ns, while v2 stays at 146-148. Here "compute-bound" means bound by the dependency chain, not by execution resources:
   one thread uses 3.0 of 8 dispatch slots per cycle, two threads 5.3.
6. **Model validation.** The trace-driven core model reproduces the measured v2/v1 time ratios:

   | platform | model | measured |
   |---|---|---|
   | this host, Dataset reads removed (Skylake-SP model) | 1.39 | 1.29-1.41 (agent T) |
   | this host, full Dataset, model at 150 ns | 0.99 | 1.00-1.10 (agent T) |
   | Zen 2 with SMT | 1.21 | 1.08-1.17 (3950X, 3700X) |
   | Zen 4 with SMT at 75 ns | 1.05 | 1.05 (7945HX) |
   | Zen 5 with SMT at 75 ns | 0.96-0.98 | 1.01 (9950X at 285 W) |
   | Skylake-class, one thread | 1.28 | 1.29-1.32 (8650U, 6820HQ) |
7. **Dead work and lazy AES (coordinator addendum): nothing worth skipping (Info, measured).** Of 786,432 F-block stores, 8
   are dead at 64-byte granularity. Structurally, steps 9 and 11 only write the two lines just read in steps 2 and 3, so only
   ISTOREs can kill a block. 55.5 % of F blocks are first read only by the final AesHash1R: their AES can be deferred, not
   skipped. At 16-byte granularity, ISTOREs overwrite 0.95 % (v1) and 1.40 % (v2) of F registers before any read. A lazy
   scheme could therefore skip at most 1.4 % of the main-loop AES rounds (0.22 of 16 per iteration). That costs ~55 % extra
   storage for pending (f, e) state and a lookup on every scratchpad read. 26 % of ISTOREs are dead (overwritten before
   read). No other per-iteration work is skippable.
8. **Program filtering (Appendix A) stays unprofitable in v2 (Info).** **Model:** take an oracle that rejects the slowest 25 % of
   first programs. With N = 8 chaining the gain is 0.2-0.7 % in v1 and 0.4-0.5 % in v2. It turns negative once each rejected
   candidate pays for a fresh scratchpad fill. In memory-bound CPU regimes v2's program-to-program spread (CV 8.9 %) is larger
   than v1's (3.9 %), because v1's Dataset stall masks program differences. The gain is still negligible.

## 2. Findings

| # | Severity | Confidence | Finding |
|---|---|---|---|
| X-1 | Medium | High (arithmetic), medium (model magnitude) | design_v2.md §5 efficiency metric is definitional; the modelled ASIC/CPU energy-gap change is 0.84-1.21x (central), not 1.15-1.67x |
| X-2 | Medium | Medium-high | The "fills idle cycles" rationale applies equally to latency-bound specialised designs; only throughput-bound designs pay the extra work, mainly as execution energy and area |
| X-3 | Low | High (model), medium (absolute ns) | design.md §2.2/§2.9 ("one iteration ~ one DRAM latency", "fully prefetched") no longer hold for v1 on current cores; v2 restores them up to ~99 ns at 5 GHz |
| X-4 | Info | High | All FP and AES work is off the loop-carried critical path in v1 and v2 |
| X-5 | Info | High | v2 exposes only 1-8 % more ILP; finite OoO windows cost CPUs about the same in v1 and v2 (224 entries: +5.4 % / +6.8 %) |
| X-6 | Info | Medium | CFROUND: modelled v1 LDMXCSR cost 0.1-27 % depending on how the core handles MXCSR; v2 <= 1.3 %, at the price of 0.11 extra mispredictions per iteration |
| X-7 | Info | High | design.md Appendix B: speculative rows reproduce exactly, non-speculative rows are 4-15 % higher with the current tool; v2 scales all ten designs by 1.47-1.50x |
| X-8 | Info | High | Appendix A's conclusion holds for v2 (strategy III <= 0.5 %, negative once the fill cost is counted) |
| X-9 | Info | High | Lazy evaluation of the v2 AES mix can skip at most 1.4 % of main-loop AES rounds; not worth its storage |
| X-10 | Info | Medium | GPUs: v2 adds ~50 % FP64 work and 262,144 table-driven AES rounds per hash to the main loop; GPUs fall further behind |

### X-1 (Medium): the v2 efficiency claim is definitional; the relevant gap changes by ~0.84-1.21x

**Measured / arithmetic.** Per hash, v1 executes 4,194,304 VM instructions and v2 6,291,456. design_v2's "VM+AES" adds the
262,144 fill/fingerprint AES rounds and, for v2, 262,144 main-loop rounds: 4,456,448 vs 6,815,744, ratio 1.5294. The table's
VM+AES/s divided by H/s gives exactly these numbers (9950X: 4.4563e6 and 6.8159e6). Every "Relative work/Joule" entry equals
1.5294 x (hash/J v2 / hash/J v1); for example the 9950X at 285 W gives 1.5294 x 94.01 / 95.38 = 1.507. The metric therefore
grows with any increase of work per hash, whether or not CPUs gain anything relative to other hardware. In hash/J the
authors' table gives v2/v1 = 0.986, 0.871, 1.080, 1.092, 0.949, 0.915, 0.904, 0.922, 0.854, 1.008, 0.961, 0.773 and 0.756 for
the 13 configurations, 10 of them below 1.

**Model (task 5, `agentX/energy.py`).** Measured operation counts per iteration (table in §4) are multiplied by per-operation
energies for a 7 nm-class fixed-function design (assumptions in §8.1, each with low/central/high values). Central result per
hash: v1 114 uJ, v2 126 uJ (ratio 1.108). Of this, DRAM is 82 uJ in both: 16,384 random 64-byte reads at 5 nJ. The
27-point sensitivity grid (DRAM 2/5/15 nJ per line x per-instruction overhead 1/3/10 pJ x operation energies low/central/high)
gives 1.020-1.291. Excluding DRAM entirely gives 1.384, not 1.50, because the 4 block accesses per iteration and the
fill/fingerprint do not change. The gap change is the ASIC ratio divided by the CPU ratio, with the CPU ratio taken from the
authors' measurements:

| CPU (design_v2.md) | CPU energy/hash v2/v1 | claimed "work/J" | gap change, ASIC low (1.020) | central (1.108) | high (1.291) | compute-only (1.384) |
|---|---|---|---|---|---|---|
| Ryzen 9 9950X @285W (Zen 5) | 1.015 | 1.507 | 1.005 | 1.092 | 1.272 | 1.364 |
| Ryzen 9 9950X @100W (Zen 5) | 1.148 | 1.332 | 0.888 | 0.965 | 1.124 | 1.206 |
| Ryzen AI 9 HX 370 @28W (Zen 5) | 0.926 | 1.651 | 1.101 | 1.196 | 1.393 | 1.494 |
| Ryzen AI 9 365 @28W (Zen 5) | 0.916 | 1.670 | 1.114 | 1.210 | 1.409 | 1.512 |
| Ryzen 9 7945HX @62W (Zen 4) | 1.053 | 1.452 | 0.968 | 1.052 | 1.225 | 1.314 |
| Ryzen 5 8600G @45W (Zen 4) | 1.093 | 1.399 | 0.933 | 1.014 | 1.181 | 1.266 |
| Ryzen 9 5950X (Zen 3) | 1.106 | 1.382 | 0.922 | 1.002 | 1.167 | 1.251 |
| Ryzen 9 3950X (Zen 2) | 1.085 | 1.409 | 0.940 | 1.021 | 1.190 | 1.276 |
| Ryzen 7 3700X (Zen 2) | 1.172 | 1.305 | 0.870 | 0.946 | 1.102 | 1.182 |
| Ryzen 7 1700X (Zen 1) | 0.992 | 1.541 | 1.028 | 1.117 | 1.301 | 1.395 |
| Core i9-12900K | 1.040 | 1.470 | 0.980 | 1.065 | 1.241 | 1.331 |
| Core i7-8650U @15W | 1.294 | 1.182 | 0.788 | 0.856 | 0.997 | 1.070 |
| Core i7-6820HQ @45W | 1.323 | 1.156 | 0.771 | 0.838 | 0.976 | 1.046 |

A gap change above 1 means CPUs gain relative to the specialised design. Even the compute-only column, which assumes the
specialised design spends nothing on DRAM, stays below the claimed figures. That is because the specialised design's own work
per Joule also rises: by 1.53 / 1.11 = 1.38x centrally.

**Judgement.** v2 is roughly neutral for the energy gap: it helps memory-latency-bound CPUs a little and hurts power-limited
and older CPUs a little. The claim in design_v2.md §5 should be restated in hash/J. Any claim of improved hardware resistance
should rest on an explicit specialised-hardware model like this one. For the gap change on a 9950X to reach the claimed 1.5,
the specialised design's energy would have to be dominated by VM execution with negligible DRAM energy. That contradicts the
design's own premise (design.md §2.9: Dataset reads are the natural parallelisation limit).

### X-2 (Medium): the idle time v2 fills is Dataset latency, which latency-bound specialised designs had too

**Model (task 4, `agentX/memory.py`).** The loop-carried recurrence of one VM instance is `T_iter = max(CP, (L_mem + CP)/(k+1))`,
with k = 1 (v1) or 2 (v2). CP is the critical path per iteration with the Dataset line available (dataflow model, mean over
192 programs). The formula matches the explicit-latency dataflow simulation within 3 % (§7). The largest Dataset latency one
instance hides is k x CP:

| instance | CP v1 (cycles) | CP v2 | L_max v1 | L_max v2 |
|---|---|---|---|---|
| CPU-class latencies at 5 GHz | 179.0 | 246.7 | 36 ns | 99 ns |
| CPU-class at 3 GHz | 179.0 | 246.7 | 60 ns | 164 ns |
| ASIC-class latencies at 1 GHz | 68.5 | 101.0 | 68 ns | 202 ns |
| ASIC-class at 2 GHz | 68.5 | 101.0 | 34 ns | 101 ns |

Time per iteration v2/v1 for one instance, at 50, 75 and 100 ns:

| instance | 50 ns | 75 ns | 100 ns |
|---|---|---|---|
| CPU-class at 5 GHz | 1.15 | 0.89 | 0.73 |
| ASIC-class at 1 GHz | 1.48 | 1.41 | 1.20 |
| ASIC-class at 1.5 GHz | 1.41 | 1.12 | 0.92 |
| ASIC-class at 2 GHz | 1.20 | 0.92 | 0.75 |

So "specialised hardware had no idle time to fill" is true only for designs that hide Dataset latency by running many
instances per execution pipeline. For those, v2 adds ~1.38x execution energy and ~1.44x execution-unit occupancy per hash.
The per-instance 2 MiB SRAM, the DRAM channels and the DRAM energy, which dominate such a design, do not change (X-1). For an
SRAM-area-dominated latency-bound instance, the area-time per hash changes by:

| regime | area-time v2/v1 |
|---|---|
| memory latency hidden | 1.48 |
| 1 GHz, 75 ns | 1.37 |
| 1 GHz, 100 ns | 1.17 |
| 2 GHz, 100 ns | 0.76 |

### X-3 (Low): design.md §2.2 and §2.9 iteration-time claims

design.md §2.2 says: "256 instructions is short enough so that high-performance CPUs can execute one iteration in similar time
it takes to fetch data from DRAM". §2.9 says: "All Dataset accesses ... are fully prefetched. The time to execute one program
iteration ... is about the same as typical DRAM access latency (50-100 ns)".

**Model.** With CPU-class latencies, a v1 iteration's critical path is 179 cycles: 36 ns at 5 GHz, 60 ns at 3 GHz. The 3 GHz
figure matches the 2019 Ryzen 7 1700 on which the design was measured. On current 5 GHz cores, v1 is therefore not fully
prefetched at any DRAM latency above ~36 ns (single thread). In the Zen 5 core model, a v1 iteration grows from 188 cycles
(memory hidden) to 292 cycles at 75 ns. v2 restores the claim up to ~99 ns at 5 GHz. design.md should state the condition
`L_mem <= k x CP` and carry v2's numbers (k = 2, 384 instructions).

### X-4 (Info): FP and AES are off the loop-carried critical path

**Structural reason.** F and E registers are reloaded from the scratchpad at every iteration (step 3). No instruction moves an
FP value into an integer register. FP results reach integers only through the F block stored in step 11, and the median F
block is first read 4,096-8,191 iterations later; only 0.13-0.18 % are read within one iteration. CFROUND is the only
integer-to-FP edge.

**Measured on all 192 + 192 programs.** Setting the latency of AES, or of every FP class plus AES, to zero leaves the
recurrence unchanged: the largest difference is 0.01 cycles per iteration, under both the Zen-4 and the ASIC latency models.
The FP part only trails the integer chain. At CPU latencies the mean lag is 19 cycles (v1) and 27 cycles (v2); zeroing the AES
latency cuts the v2 lag to 14.

**Consequences.**
- design_v2 §2's "without hurting the hashrate" is supported.
- A specialised design can implement FP, divide, square root and AES as slow, shared or deeply pipelined units without losing
  per-instance speed.
- The FP complexity of design.md §2.5 buys area and energy, not latency.

### X-5 (Info): ILP and window size

**Model.** Dataflow with unbounded resources; CP is the mean loop-carried recurrence per iteration in cycles over 192 programs,
with its 95 % CI:

| latency model | v1 CP | v2 CP | v2/v1 | v1 VM instr/cycle | v2 VM instr/cycle |
|---|---|---|---|---|---|
| (a) design.md App. B, all ops 1 cycle | 57.5 +/- 0.9 | 84.0 +/- 1.0 | 1.460 | 4.51 | 4.61 |
| (b) Zen-4 latencies, L3 47 cycles | 179.0 +/- 3.2 | 246.7 +/- 3.3 | 1.378 | 1.46 | 1.57 |
| (c) optimistic ASIC (1/2-cycle int, 2-cycle FP, 6-cycle div/sqrt, SRAM 1-2) | 68.5 +/- 1.1 | 101.0 +/- 1.2 | 1.475 | 3.80 | 3.84 |
| (c') ASIC with slow FP (3-cycle add/mul, 12-cycle div/sqrt) | 68.5 | 101.0 | 1.475 | 3.80 | 3.84 |

**Model, out-of-order core with Zen-4 ports and latencies, memory hidden** (main run, 64 programs; cycles per iteration):

| ROB entries | v1 | v2 |
|---|---|---|
| 224 | 197.5 | 267.3 |
| 320 | 189.3 | 253.7 |
| 448, 512, 4096 | 187.4 | 250.4 |

The infinite-window result is 2.2 % (v1) and 2.4 % (v2) above the dataflow bound, from port contention.

**Judgement.** v2 does not reward wide windows or dataflow machines more than v1 does. The CPU's handicap against a dataflow
design is per-operation latency: 179 vs 69 cycles per v1 iteration (2.6x) and 247 vs 101 in v2 (2.4x). Part of that is the
fixed L3-scratchpad latency on the critical path: 47-55 cycles per iteration on a CPU, 2 on SRAM. This fixed part is a smaller
share in v2, so CPUs gain ~7 % relative per-instance speed in cycles.

### X-6 (Info): CFROUND

**Measured, per iteration.**

| | v1 | v2 |
|---|---|---|
| CFROUND executed | 1.09 | 1.64 |
| fprc written (v2: gate passed) | 1.09 | 0.108 |
| fprc value actually changed | 0.81 | 0.081 |
| mispredictions (taken CBRANCH + CFROUND gate) | 0.100 | 0.149 + 0.108 = 0.257 |

The x86 JIT implements the v2 gate as `test/jnz` around `ldmxcsr`, so every gate opening is a misprediction.

**Model, cost of LDMXCSR** (Zen-4 model, one thread, memory hidden):

| how the core handles LDMXCSR | v1 | v2 |
|---|---|---|
| stalls dispatch for 10 cycles | +0.1 % | +0.4 % |
| drains older FP operations | +27 % | +1.3 % |

**Judgement.** design_v2's "up to 10 % of hashrate on Ryzen" lies inside these bounds. It cannot be verified without Zen
hardware. A specialised design renames or predicates the rounding mode and never paid this cost, so the change removes a CPU
handicap rather than adding an ASIC cost.

### X-7 (Info): Appendix B reproduction (task 3)

**Measured** with `agentX/run_appB.sh`: perf-simulation.cpp from the branch, 2,000 programs per design, v1 built with
`-DRANDOMX_PROGRAM_SIZE=256`.

| design | config | App. B (v1) | reproduced v1 | v2 (384) | v2/v1 |
|---|---|---|---|---|---|
| 1 | 1 EXU + 1 MEM, in-order, non-spec | 293 | 311.4 | 466.1 | 1.50 |
| 2 | 1+1, in-order, spec | 262 | 262.2 | 392.3 | 1.50 |
| 3 | 2+1, in-order, non-spec | 197 | 216.6 | 324.0 | 1.50 |
| 4 | 2+1, in-order, spec | 161 | 161.0 | 240.4 | 1.49 |
| 5 | 2+1, OoO, non-spec | 144 | 149.9 | 223.5 | 1.49 |
| 6 | 2+1, OoO, spec | 122 | 122.4 | 182.4 | 1.49 |
| 7 | 4+2, in-order, non-spec | 135 | 154.7 | 231.1 | 1.49 |
| 8 | 4+2, in-order, spec | 99 | 98.2 | 146.2 | 1.49 |
| 9 | 4+2, OoO, non-spec | 89 | 97.5 | 144.7 | 1.48 |
| 10 | 4+2, OoO, spec | 64 | 63.7 | 93.8 | 1.47 |

The speculative rows match. The non-speculative rows are 4-15 % higher. The audit's round-2 fix (51807a3) only changed the
misprediction loops of the speculative paths. The published non-speculative rows therefore probably predate a later change,
such as the final CBRANCH frequency of 25/256 (not verified).

The tool simulates one pass over the program with random branch outcomes, no loop overhead and no memory dependencies.
**Model:** the trace-driven version, with real dependencies, real branch outcomes and the end-of-iteration work, gives 57.5
(v1) and 84.0 (v2) cycles per iteration with speculation, and 93.7 and 140.7 without (latency model (a)).

### X-8 (Info): Appendix A and the runtime distribution (task 6)

**Arithmetic.** Appendix A reproduces exactly. For strategy II with Q = 0.75, programs per result are 1, 2.33, 6.48 and 26.97
for N = 1, 2, 4 and 8; the speeds are 1.50, 1.29, 0.93 and 0.45. This does not depend on program length, so it is unchanged in
v2.

**Model, strategy III** with an oracle that rejects the slowest 25 % of first programs; 192 programs per version; "fill cost"
charges each candidate 0.2 of a mean program for a fresh scratchpad fill:

| model | v1 CV | v2 CV | v1 speed, N=8 | v2 speed, N=8 | with fill cost (v1 / v2) |
|---|---|---|---|---|---|
| dataflow, CPU latencies, memory hidden | 12.6 % | 9.6 % | 1.0069 | 1.0053 | 0.999 / 0.997 |
| dataflow, CPU latencies, 5 GHz, 75 ns | 4.1 % | 9.5 % | 1.0023 | 1.0053 | 0.994 / 0.997 |
| dataflow, ASIC latencies, memory hidden | 11.4 % | 8.5 % | 1.0065 | 1.0044 | 0.998 / 0.996 |
| dataflow, ASIC latencies, 1 GHz, 100 ns | 4.8 % | 8.5 % | 1.0027 | 1.0044 | 0.995 / 0.996 |
| OoO Zen 5, 5.2 GHz, 75 ns | 3.9 % | 8.9 % | 1.0021 | 1.0050 | 0.994 / 0.997 |
| OoO Zen 5 SMT, 75 ns (per thread; 168 programs) | 3.5 % | 7.4 % | 1.0019 | 1.0041 | 0.994 / 0.996 |

No simple program feature explains the runtime: |r| <= 0.31 for the counts of IMULH, FDIV, FSQRT, CBRANCH and CFROUND and for
taken branches.

**Measured, branches.** Taken CBRANCHes are 0.100 (v1) and 0.149 (v2) per iteration. That matches 25.3 and 38.3 executions per
iteration x 1/256. Each taken branch re-executes 6.2 VM instructions, i.e. 0.24 % of the work in both versions.

### X-9 (Info): dead work and lazy evaluation (coordinator addendum)

**Measured.** Every 8-byte scratchpad word was tracked over 24 whole hashes per version (8 programs each). The final AesHash1R
counts as a read of every word.

| written object | v1 dead | v2 dead |
|---|---|---|
| F block (step 11, 64 B) | 2 of 393,216 | 6 of 393,216 |
| F register image (16 B quarter of the F block) | 0.948 % | 1.399 % |
| integer block (step 9, 64 B) | 9 of 393,216 | 16 of 393,216 |
| ISTORE (8 B) | 25.7 % | 26.2 % |

First reader of each written object in v2 (%):

| written object | next block read | L1 operand | L2 operand | L3 operand | final AesHash1R |
|---|---|---|---|---|---|
| F block | 32.5 | 0.76 | 11.2 | 0.04 | 55.5 |
| integer block | 32.3 | 0.77 | 11.2 | 0.05 | 55.7 |
| ISTORE | 5.7 | 46.4 | 13.2 | 0.002 | 8.4 |

**Why F blocks are (almost) never dead.** The only 64-byte writes, steps 9 and 11, go to the two lines read in steps 2 and 3 of
the same iteration. A block can therefore only be overwritten after it has been read. The dead F blocks were covered by
ISTOREs in the L1/L2 region. The dead integer blocks are the `spAddr0 == spAddr1` case, where the F block overwrites them in
the same iteration (expected 393,216 x 2^-15 = 12). The median distance from an F-block store to its first read is
4,096-8,191 iterations, and 55.5 % are first read by the final fingerprint.

**Answers to the addendum.**
1. Dead fractions are as in the first table above.
2. A lazy scheme keyed on 64-byte lines skips 0.002 % of the main-loop AES rounds. Keyed on 16-byte F registers it skips
   1.40 %: 0.22 of the 16 rounds per iteration, about 3,670 of 530,696 AES rounds per hash. It also skips the roughly one
   F-register FP operation per iteration that feeds those registers.
3. The only other skippable per-iteration work is the 26 % of ISTOREs that are overwritten before being read (one store
   each).

**Cost of the lazy scheme.**
- It must keep the unmixed f and all four e registers (80-128 bytes) for every pending line.
- Most lines stay pending until the final fingerprint, so the pending state grows to ~9,100 lines by the end of a hash. That is
  ~1.1 MiB, +55 % storage.
- Every scratchpad read must check a pending map: a 32,768-bit tag array plus indirection.

The saving is ~1.4 % of 0.5 uJ (the main-loop AES energy per hash in the central model), against +55 % SRAM. Not worthwhile;
the AES mix is effectively mandatory work.

### X-10 (Info): GPUs (brief)

**Arithmetic.** Per hash, v2 adds 262,144 AES rounds to the main loop: 530,696 in total against 266,504. GPUs have no AES
instructions. A T-table round costs ~16 shared-memory lookups and ~20 integer operations, so the main-loop AES alone adds
roughly 0.5 k GPU instructions per iteration. That comes on top of 1.5x the FP64 work (134 vs 90 FP operations per iteration,
twice that in FP64 lanes), which runs at 1/32-1/64 of the FP32 rate on consumer GPUs.

**Judgement, no measurement.** NVIDIA GPUs have per-instruction rounding modes (e.g. `__dadd_rd`), so v1's CFROUND was never a
GPU problem, and v2's change neither helps nor hurts. The structural GPU handicaps are unchanged:
- one divergent program per thread;
- a 2 MiB scratchpad per hash that does not fit on-chip (about 36 scratchpads fit in a 72 MB L2);
- the per-iteration integer chain through two random 2 MiB reads.

v2 widens the CPU-GPU gap modestly.

## 3. Method and validation

**Trace harness** (`agentX/src/builder.hpp`, `xsim.cpp`). This is a copy of `InterpretedVm::execute` that uses the library's own
components: `BytecodeMachine::compileProgram/executeInstruction`, `fillAes1Rx4/fillAes4Rx4/hashAes1Rx4`, Blake2b, and the
light-mode Dataset item function (JIT cache, real key). For every executed instruction, including those re-executed after a
taken CBRANCH, it emits a micro-operation sequence that mirrors the x86-64 JIT code:
- address `lea` + `and`, load, operation;
- `mul` as 4 slots;
- FDIV_M with `andps`/`orps`;
- CBRANCH as `add` + fused `test/jz`;
- CFROUND as `rol` (plus `test/jnz` in v2), then `and/or/mov/ldmxcsr` when the gate opens;
- ISTORE.

It also emits the loop body of `program_loop_load`, `program_read_dataset[_v2]`, `randomx_prefetch_scratchpad` and
`program_loop_store[_hard_aes]`. Dependencies are explicit producer indices: integer, F and E registers, fprc, true
read-after-write dependencies through the scratchpad (from the actual addresses), the spAddr chain, ma/mx, and the Dataset
prefetch that fetched each line.

**Validation.** All 48 hash computations of the main and lean runs (24 inputs x v1/v2) equal `randomx_calculate_hash` from a
light-mode JIT VM (`check_hashes.sh`: "24 + 24 match, 0 mismatch"); so do the 4 validation hashes (`out/validate.txt`). Excluding
loop overhead, the harness counts 1.79 x86 slots per VM instruction in both versions, matching design.md §2.1.1's 1.8.

**Samples.** v1 and v2 use the same inputs, which give different programs.
- Main run: 8 hashes (inputs (101, 0..3) and (202, 0..3)), 64 programs per version, every analysis (`out/mainA.txt`,
  `out/mainB.txt`).
- Lean run: 16 further hashes (seeds 303 and 404), 128 programs per version; dataflow models plus nine core configurations
  (`out/leanA.txt`, `out/leanB.txt`).
- Tables marked "192 programs" combine both runs. The two runs agree within sampling error: the v2/v1 critical-path ratio is
  1.33 in the main run and 1.40 in the lean run (CPU latencies), 1.38 combined.

**Latency models** (cycles):

| class | (a) App. B | (b) Zen-4 class | (c) optimistic ASIC |
|---|---|---|---|
| int add/sub/xor/rot/neg/swap | 1 | 1 (3-operand lea 2) | 1 |
| address lea + and | 0 (in memory stage) | 1 + 1 | 1 |
| 64x64 multiply / high half | 1 / 1 | 3 / 4 | 2 / 2 |
| load L1 / L2 / L3 scratchpad | 1 | 4 / 14 / 47 (Zen 5: 55; Skylake-SP: 5 / 14 / 70) | 1 / 1 / 2 |
| FP load + cvtdq2pd, extra | 0 | +6 | 0 |
| FP add-sub / mul / div / sqrt | 1 | 3 / 3 / 13 / 20 | 2 / 2 / 6 / 6 (c': 3 / 3 / 12 / 12) |
| FP logic / AES round | 1 / 1 | 1 / 4 | 0 / 1 |
| store-to-load forwarding | 1 | 7 | 1 |
| branch mispredict redirect | 2 | 13 (Skylake 16, Golden Cove 17) | 2 |
| Dataset line after arrival | 1 | 4 | 1 |

The (b) values are approximate published figures (Agner Fog's instruction tables, uops.info, design.md §2.8.1's cache table),
quoted from memory rather than re-measured. The conclusions depend mainly on the L3 latency: the recurrence moves by about 1
cycle per cycle of L3 latency, in both versions.

**Core models** (`agentX/src/analyze.hpp`).
- ROB slots, dispatch width (shared under SMT), in-order retire.
- Port counts:

  | core | ALU | multipliers | loads | stores | FP | other |
  |---|---|---|---|---|---|---|
  | Zen 4 | 4 | 1 | 3 | 2 | 2 FADD, 2 FMUL/AES | 1 non-pipelined divider occupied 5/9 cycles |
  | Zen 5 | 6 | 3 | 4 | | | ROB 448, dispatch 8 |
  | Zen 2 | | | | | | ROB 224 |
  | Skylake-SP | | | 2 | 1 | | ROB 224, 4-wide |
  | Golden Cove | | | | | | ROB 512, 6-wide |

- Oldest-first issue through resource calendars.
- A branch mispredict stalls dispatch until the branch resolves, plus the penalty.
- A Dataset line arrives at prefetch issue + L_mem.
- LDMXCSR modes: renamed; 10-cycle dispatch stall (the default); FP drain.

Not modelled: scheduler and register-file limits, cache misses of L1/L2-level scratchpad words, and cache/DRAM contention
between cores. The models therefore run 20-30 % faster than real cores in absolute terms. For example, the Zen 5 SMT model takes
146-148 core cycles per thread-iteration for v2, against ~182-190 implied by the 9950X measurement. They are used for v1-vs-v2
ratios, which they reproduce (§1 item 6 and §7).

## 4. Task 1: measured work per iteration (dynamic, 192 programs per version)

| quantity per iteration | v1 | v2 | v2/v1 |
|---|---|---|---|
| VM instructions executed (incl. re-executed) | 256.62 | 384.93 | 1.500 |
| simple integer ops (add/sub/xor/neg/rot/swap/branch/store/cfround) | 123.99 | 185.74 | 1.498 |
| 64-bit multiplies (IMUL_R/M, IMUL_RCP) / high multiplies | 28.59 / 10.08 | 43.28 / 15.51 | 1.51 / 1.54 |
| FP add-sub / mul / div / sqrt | 42.38 / 31.96 / 3.92 / 6.13 | 63.05 / 47.87 / 5.87 / 9.08 | 1.49 / 1.50 / 1.50 / 1.48 |
| scratchpad operand reads L1 / L2 / L3 | 27.48 / 9.13 / 3.05 | 41.16 / 13.54 / 4.53 | 1.50 / 1.48 / 1.49 |
| ISTORE L1 / L2 / L3 | 10.81 / 3.40 / 1.94 | 15.97 / 5.22 / 2.90 | 1.48 / 1.54 / 1.49 |
| 64-byte scratchpad block reads + writes | 2 + 2 | 2 + 2 | 1 |
| Dataset reads | 1 | 1 | 1 |
| main-loop AES rounds | 0 | 16 | - |
| taken CBRANCH / re-executed VM instructions | 0.100 / 0.625 | 0.149 / 0.928 | 1.48 / 1.48 |
| CFROUND executed / fprc written / fprc changed | 1.09 / 1.09 / 0.81 | 1.64 / 0.108 / 0.081 | |
| micro-operations / x86 slots | 516.3 / 536.3 | 743.8 / 778.0 | 1.44 / 1.45 |
| loads with a true RAW dependency on an earlier store of the same program (of which from the previous or same iteration) | 28.3 (0.23) | 43.7 (0.51) | |

AES rounds per hash: 266,504 (v1) and 530,696 (v2). That is fill 131,072 + fingerprint 131,080 + program generation
4,352 / 6,400 + main loop 0 / 262,144.

## 5. Task 2: dependency analysis

**Model, per-iteration critical paths** (dataflow, unbounded resources, mean over 192 programs). "iso" is one iteration in
isolation with all inputs ready at t = 0. "rec" is the loop-carried recurrence, including the next iteration's address
computation and L3 reads.

| model | v1 rec | v1 iso CP | v1 iso FP path | v2 rec | v2 iso CP | v2 iso FP path | v2 iso CP without AES latency |
|---|---|---|---|---|---|---|---|
| (a) App. B | 57.5 | 59.7 | 56.7 | 84.0 | 86.5 | 84.5 | - |
| (b) Zen-4 class | 179.0 | 203.6 | 202.1 | 246.7 | 274.7 | 273.7 | 264.1 |
| (c) ASIC | 68.5 | 73.5 | 71.3 | 101.0 | 106.0 | 104.3 | 104.3 |

- **AES chain.** The end-of-iteration AES chain (4 rounds deep, 4 independent chains) lengthens the isolated FP path by
  11-13 cycles at CPU latencies and 2-3 cycles at ASIC latencies. But it ends in a store whose line is read thousands of
  iterations later, so it is not on the next iteration's critical path (X-4). The v1 XOR is likewise off the path.
- **Fixed part of the recurrence.** At the start it is spMix -> mask -> address -> L3 block read -> XOR; at the end it is the
  Dataset XOR. That is 52 cycles at CPU latencies and 7 at ASIC latencies. The program part then scales ~1.5x from v1 to v2 at
  both latency sets (127 -> 195 and 62 -> 94 cycles), about the instruction ratio. The fixed part dilutes the totals to 1.38x
  and 1.48x.
- **Speculation, 1-cycle model.** Without speculation the recurrence is 93.7 (v1) and 140.7 (v2) cycles, so speculation is worth
  1.63x and 1.67x. design.md §2.6.2's claim holds, slightly more so in v2.
- **Non-renamed rounding mode** (LDMXCSR waits for older FP operations). It does not change the recurrence in the dataflow model.
  It only lengthens the FP lag, which matters on a CPU through the window (X-6).

## 6. Task 3: resource-constrained simulation

Appendix B is covered in X-7.

**Model, finite-window cores** (main run, 64 programs; cycles per iteration, mean +/- 95 % CI; Dataset latency in ns at the
model clock):

| core model | v1 | v2 | v2/v1 | Dataset wait per load, v1 / v2 |
|---|---|---|---|---|
| Zen-4 ports, renamed MXCSR, memory hidden, ROB 224 | 197.5 +/- 4.2 | 267.3 +/- 6.0 | 1.354 | 0 / 0 |
| ... ROB 320 | 189.3 +/- 4.5 | 253.7 +/- 5.4 | 1.341 | 0 / 0 |
| ... ROB 448 / 512 / 4096 | 187.4 +/- 4.8 | 250.4 +/- 5.4 | 1.336 | 0 / 0 |
| Zen 4 (10-cycle LDMXCSR stall), 5 GHz, 75 ns | 285.9 +/- 2.5 | 254.8 +/- 5.4 | 0.891 | 145 / 0.6 |
| Zen 4, 5 GHz, 100 ns | 348.3 | 262.9 | 0.755 | 208 / 36 |
| Zen 4, 3.5 GHz, 75 ns | 229.7 | 254.7 | 1.109 | 88 / 0 |
| Zen 5, 5.2 GHz, memory hidden | 192.1 +/- 5.0 | 256.3 +/- 5.5 | 1.334 | 0 / 0 |
| Zen 5, 5.2 GHz, 75 ns | 294.5 +/- 2.7 | 256.5 +/- 5.4 | 0.871 | 194 / 6 |
| Zen 5, 5.2 GHz, 100 ns | 359.4 | 268.2 | 0.746 | 259 / 72 |
| Zen 2, 4.2 GHz, 70 ns | 243.0 | 250.7 | 1.032 | 83 / 0 |
| Golden Cove, 4.9 GHz, 80 ns | 332.7 | 282.6 | 0.849 | 147 / 4 |
| Skylake-SP (this host), 2.8 GHz, memory hidden | 238.8 +/- 4.9 | 321.4 +/- 6.6 | 1.346 | 0 / 0 |
| Skylake-SP, 2.8 GHz, 90 ns | 251.7 | 321.4 | 1.277 | 48 / 0 |

With 2-way SMT (core cycles per thread-iteration):

| core model | v1 | v2 | v2/v1 |
|---|---|---|---|
| Zen 5 (ROB 224 per thread), memory hidden | 108.2 +/- 2.4 | 146.4 +/- 2.9 | 1.353 |
| Zen 5, 75 ns | 151.9 +/- 1.2 | 146.4 +/- 2.9 | 0.963 |
| Zen 5, 100 ns | 184.1 | 148.3 | 0.806 |
| Zen 4 (ROB 160 per thread), 5 GHz, 75 ns | 149.6 | 157.7 | 1.054 |
| Zen 4, 3.5 GHz, 75 ns | 123.8 | 157.7 | 1.274 |
| Zen 2 (ROB 112 per thread), 4.2 GHz, 70 ns | 129.4 | 156.7 | 1.211 |

**Model, lean run** (128 further programs). It reproduces these within ~4 %:

| config | v1 | v2 | v2/v1 |
|---|---|---|---|
| Zen 4, ROB 320, memory hidden | 181.9 | 258.0 | |
| Zen 5, 75 ns | 291.3 | 259.6 | |
| Zen 5 SMT, 75 ns | 150.0 | 148.9 | 0.992 |
| Skylake-SP model at 150 ns | 329.3 | 326.3 | 0.991 |
| Skylake-SP model at 190 ns | 385.2 | 326.4 | 0.847 |

## 7. Task 4: memory overlap and the design_v2 claims

**Model, recurrence validation.** Dataflow with explicit Dataset latency against `T = max(CP, (L + CP)/(k+1))`, 192 programs,
cycles per iteration:

| case | L | v1 sim | v1 formula | v2 sim | v2 formula |
|---|---|---|---|---|---|
| CPU latencies, 5 GHz, 50 / 75 / 100 ns | 250 / 375 / 500 | 218.5 / 280.8 / 343.2 | 214.5 / 277.0 / 339.5 | 246.7 / 246.8 / 257.7 | 246.7 / 246.7 / 248.9 |
| ASIC latencies, 1 GHz, 50 / 75 / 100 ns | 50 / 75 / 100 | 68.6 / 74.0 / 86.0 | 68.5 / 71.7 / 84.2 | 101.0 / 101.0 / 101.0 | 101.0 / 101.0 / 101.0 |
| ASIC latencies, 2 GHz, 50 / 100 ns | 100 / 200 | 86.0 / 136.0 | 84.2 / 134.2 | 101.0 / 103.8 | 101.0 / 101.0 |

**Model, Dataset-stall share of a single instance** (formula; CP = 179.0 / 246.7 cycles with CPU-class latencies, 68.5 / 101.0
with ASIC-class latencies; "stall" is the share of the iteration spent waiting for the Dataset line).

CPU-class latencies, one thread:

| clock GHz | DRAM ns | v1 ns/iter | v1 stall % | v2 ns/iter | v2 stall % | v2/v1 time per hash |
|---|---|---|---|---|---|---|
| 3.0 | 50 | 59.7 | 0 | 82.2 | 0 | 1.378 |
| 3.0 | 75 | 67.3 | 11 | 82.2 | 0 | 1.221 |
| 3.0 | 100 | 79.8 | 25 | 82.2 | 0 | 1.030 |
| 4.0 | 50 | 47.4 | 6 | 61.7 | 0 | 1.302 |
| 4.0 | 75 | 59.9 | 25 | 61.7 | 0 | 1.030 |
| 4.0 | 100 | 72.4 | 38 | 61.7 | 0 | 0.852 |
| 5.0 | 50 | 42.9 | 17 | 49.3 | 0 | 1.150 |
| 5.0 | 75 | 55.4 | 35 | 49.3 | 0 | 0.891 |
| 5.0 | 100 | 67.9 | 47 | 49.8 | 1 | 0.733 |
| 5.5 | 50 | 41.3 | 21 | 44.9 | 0 | 1.087 |
| 5.5 | 75 | 53.8 | 39 | 44.9 | 0 | 0.834 |
| 5.5 | 100 | 66.3 | 51 | 48.3 | 7 | 0.729 |

ASIC-class latencies, one instance:

| clock GHz | DRAM ns | v1 ns/iter | v1 stall % | v2 ns/iter | v2 stall % | v2/v1 |
|---|---|---|---|---|---|---|
| 1.0 | 50 | 68.5 | 0 | 101.0 | 0 | 1.475 |
| 1.0 | 75 | 71.7 | 5 | 101.0 | 0 | 1.408 |
| 1.0 | 100 | 84.2 | 19 | 101.0 | 0 | 1.199 |
| 1.5 | 50 | 47.8 | 5 | 67.3 | 0 | 1.408 |
| 1.5 | 75 | 60.3 | 24 | 67.3 | 0 | 1.116 |
| 1.5 | 100 | 72.8 | 37 | 67.3 | 0 | 0.924 |
| 2.0 | 50 | 42.1 | 19 | 50.5 | 0 | 1.199 |
| 2.0 | 75 | 54.6 | 37 | 50.5 | 0 | 0.924 |
| 2.0 | 100 | 67.1 | 49 | 50.5 | 0 | 0.752 |

**design_v2 §3** says Zen 5 is "only 20-25 % faster in RandomX because it is bottlenecked by RAM latency" and needs "more work
to keep themselves busy while waiting for data"; §4 adds the two-ahead prefetch. Both are **supported by the model, with two
qualifications**:
1. "Busy" means a longer dependency chain, not saturated execution resources. A single v2 thread dispatches 3.0 x86 slots per
   cycle on the 8-wide Zen 5 model; with SMT, 5.3.
2. The same change helps latency-bound specialised designs (X-2).

**Model against measurement** (time per hash v2/v1; measured = 1 / hashrate ratio):

| platform | model | measured |
|---|---|---|
| this host, Dataset reads removed (agent T's "tiny": items from an L1-resident buffer) | Skylake-SP model, memory hidden: 1.35 (64 programs) / 1.39 (192) | 1.29-1.41 (agent T, `agentT/logs/tpart-*.txt`, several noisy runs on the shared host) |
| this host, full Dataset, large pages (host DRAM latency 140-190 ns from agent T's `dramlat`) | Skylake-SP model at 150 / 190 ns: 0.991 / 0.847 | 1.00-1.10 (agent T, fast and p1.0 runs) |
| Ryzen 9 9950X @285 W (Zen 5, SMT, DDR5) | Zen 5 SMT at 75 ns: 0.96-0.99; at 100 ns: 0.81 | 1.015 |
| Ryzen 9 7945HX (Zen 4, SMT) | Zen 4 SMT at 5 GHz, 75 ns: 1.054 | 1.054 |
| Ryzen 9 3950X / 3700X (Zen 2, SMT) | Zen 2 SMT at 70 ns: 1.211 | 1.085 / 1.172 |
| Ryzen 9 9950X @100 W (lower clock) | Zen 4 SMT at 3.5 GHz, 75 ns: 1.274 | 1.148 |
| Core i7-8650U / i7-6820HQ (Skylake class) | Skylake-SP at 90 ns, one thread: 1.277 | 1.294 / 1.323 |

**Judgement.** The model captures the pattern that decides the debate. High clock plus high DRAM latency makes v2 nearly free,
or even faster, per hash on CPUs. Low clock or narrow cores make it cost 15-35 %.

## 8. Task 5: relative advantage

### 8.1 Energy model assumptions

Model for a 7 nm-class fixed-function design. Values are low / central / high, in pJ:

| item | low | central | high |
|---|---|---|---|
| 64-bit integer op | 0.03 | 0.05 | 0.1 |
| 64x64 multiply | 1 | 2 | 4 |
| 64x64 multiply, high half | 1.5 | 3 | 6 |
| 2-lane FP64 add | 1 | 2 | 4 |
| 2-lane FP64 multiply | 2.5 | 5 | 10 |
| 2-lane FP64 divide or square root | 10 | 20 | 40 |
| FP logic | 0.05 | 0.1 | 0.2 |
| AES round | 1 | 2 | 4 |
| 8-byte SRAM access, 16 KiB array | 0.5 | 1 | 2 |
| 8-byte SRAM access, 256 KiB array | 2 | 4 | 8 |
| 8-byte SRAM access, 2 MiB array | 5 | 10 | 20 |
| 64-byte access to the 2 MiB array | 20 | 40 | 80 |
| per-VM-instruction overhead (fetch from the 3 KiB program buffer, decode, register file, operand network, pipeline and clock) | 1 | 3 | 10 |
| one random 64-byte DRAM read (activate, burst, I/O, controller) | 2,000 | 5,000 | 15,000 |

**Basis.** Horowitz, "Computing's energy problem" (ISSCC 2014), 45 nm figures: 32-bit add 0.1 pJ, 32-bit FP multiply 3.7 pJ,
64-bit read from 8 KiB / 1 MiB SRAM 10 / 100 pJ, DRAM 1.3-2.6 nJ per 64 bits. Logic and SRAM are scaled ~5-10x to 7 nm. DRAM
uses DDR4/DDR5 random-access energy of 10-30 pJ/bit including activation. These are order-of-magnitude assumptions. The
conclusion depends mainly on the DRAM-to-compute ratio, which the grid spans from compute-dominated (1.29) to DRAM-dominated
(1.02).

### 8.2 Results

**Model, central breakdown per hash** (uJ):

| part | v1 | v2 |
|---|---|---|
| VM operations | 8.9 | 13.3 |
| per-instruction overhead | 12.6 | 18.9 |
| scratchpad words | 2.3 | 3.4 |
| scratchpad blocks | 2.6 | 2.6 |
| main-loop AES | 0 | 0.5 |
| fill, fingerprint and generation | 5.8 | 5.8 |
| DRAM | 81.9 | 81.9 |
| total | 114.1 | 126.4 (ratio 1.108) |

Gap changes per CPU are in X-1, and area-time for an SRAM-dominated latency-bound instance in X-2.

The AES tweak alone costs the modelled design 0.5 uJ per hash (0.4 %) and one 128-bit AES round unit. It forces the AES unit
into the VM datapath, but does not make the design materially "harder" (design_v2 §2, second bullet).

**Judgement, absolute advantage.** With these assumptions a specialised design spends ~0.11-0.13 mJ per hash, against
3.8-30 mJ of measured package energy per hash on the CPUs above (both versions): a factor of 30-250, dominated on the
specialised side by DRAM. Real designs would carry more control, leakage and PHY overhead, so this is an upper bound on the
advantage. The v2/v1 ratio, which is the subject here, is insensitive to that overhead (grid in X-1).

## 9. Task 6 summary

| quantity | v1 | v2 |
|---|---|---|
| taken CBRANCH per iteration | 0.100 | 0.149 |
| repeated work per taken branch | 6.2 VM instructions (0.24 %) | 6.2 VM instructions (0.24 %) |
| per-program runtime CV, latency-bound (memory hidden) | 11-13 % | 8-10 % |
| per-program runtime CV, memory-bound regimes | 3.5-4.8 % | 7.4-9.5 % |

Strategy III gains at most 0.7 % with N = 8 and is negative with a realistic fill cost (X-8). Selective execution is therefore
slightly *more* profitable in v2 than in v1 where v1 was memory-bound, slightly less where it was not, and negligible in every
case. Strategy II (hardware that rejects a fraction of programs) is unchanged by program length. No natural program class is
cheap to exclude in v2. For example, programs without CFROUND are 22 % of v2 programs (37 % in v1), and the rounding mode is
inherited across the chain, so a "no rounding-mode" design would need 8 consecutive such programs.

## 10. Claims of design.md and design_v2.md in this track

| claim | status | evidence |
|---|---|---|
| design §2.1.1: 1.8 x86 instructions per VM instruction | holds (1.79 in v1 and v2) | §3 |
| design §2.2/§2.9: one iteration ~ one DRAM latency; Dataset fully prefetched | no longer holds for v1 on 4-5 GHz cores; holds again in v2 up to ~99 ns at 5 GHz | X-3, §7 |
| design §2.6.2: CBRANCH taken 1/256, cheap, speculation pays | holds (0.100 / 0.149 taken per iteration; speculation worth 1.63x / 1.67x) | §4, §5 |
| design §2.7 / App. B: superscalar, OoO and speculation all help | holds; App. B speculative rows reproduce, non-speculative rows +4-15 % | X-7 |
| design §2.8.3: 39 reads and 16 writes per iteration | holds for v1 (39.7 / 16.2); v2 59.2 / 24.1 | §4 |
| design §2.8.1 (v2 note): AES mix "keeps the CPU busy at all times" | partly: the AES fits in the shadow of the critical path, but a single thread still uses ~3 of 8 dispatch slots | X-4, §7 |
| design App. A: filtering unprofitable with N = 8 | holds for v2 | X-8 |
| design_v2 §1: CFROUND costs up to 10 % on Ryzen | cannot be verified (model bounds 0.1-27 %); v2 cost <= 1.3 % | X-6 |
| design_v2 §2: AES doubles AES work without hurting hashrate | holds (530,696 vs 266,504 rounds; AES off the critical path) | X-4 |
| design_v2 §2: AES makes specialised hardware harder | marginal: +0.4 % energy, one small unit | §8.2 |
| design_v2 §3-4: Zen 5 memory-bound in v1, needs more work; two-ahead prefetch complements | holds in the model, one thread and SMT; the same holds for latency-bound specialised designs | §7, X-2 |
| design_v2 §5: "only a slight hashrate reduction" | 0.77-1.09x measured by the authors; the model reproduces the dependence on clock and DRAM latency | §7 |
| design_v2 §5: "massive efficiency improvements" (VM+AES/J 1.15-1.67x) | definitional; no evidence of a relative gain over specialised hardware (modelled gap change 0.84-1.21x) | X-1 |

## 11. Reproduction

```
cd /tmp/claude-0/-home-user-RandomX/cfc10d96-f1d5-53d0-817a-8712f1f55302/scratchpad/agentX
./build.sh                                                     # bin/xsim (links ../lead/build-x86/librandomx.a)
taskset -c 0-2 nice -n 10 ./bin/xsim --hashes 2 --validate --no-ooo --tag val > out/validate.txt   # bit-exact check
./run_main.sh                                                  # out/mainA.txt, out/mainB.txt (~12 min each)
OUT=xsim2 ./build.sh; ./run_lean.sh                            # out/leanA.txt, out/leanB.txt (--lean, ~8 min each)
./check_hashes.sh                                              # every traced hash vs the public API (bin/refhash)
./run_appB.sh 2000 > out/appB.txt                              # Appendix B, v1 and v2 (bin/perfsim256, bin/perfsim384)
python3 -I analyze.py out/mainA.txt out/mainB.txt              # dataflow, OoO, branches, distributions, liveness (64 programs)
python3 -I analyze.py out/mainA.txt out/mainB.txt out/leanA.txt out/leanB.txt   # combined (192 programs)
python3 -I energy.py   out/mainA.txt out/mainB.txt out/leanA.txt out/leanB.txt  # task 5 energy and area-time model
python3 -I memory.py   out/mainA.txt out/mainB.txt out/leanA.txt out/leanB.txt  # task 4 recurrence and validation
python3 -I filtering.py out/mainA.txt out/mainB.txt out/leanA.txt out/leanB.txt # task 6 Appendix A
python3 -I dist.py     out/mainA.txt out/mainB.txt out/leanA.txt out/leanB.txt  # addendum: first-read distances
```

**Sources.**
- `src/trace.hpp`: micro-operation format.
- `src/builder.hpp`: instrumented VM and liveness tracking.
- `src/analyze.hpp`: latency models, dataflow, out-of-order and SMT core model.
- `src/xsim.cpp`: driver and configuration list.
- `src/refhash.cpp`: public-API reference hashes.

**Outputs used in this report.**
- `out/tables_main.md` (64 programs) and `out/all_tables.md` (192 programs)
- `out/energy_all.md`, `out/memory_all.md`, `out/filtering_all.md`, `out/dist_all.md`
- `out/lean_tables.md`
- `out/appB.txt`, `out/validate.txt`
- `out/ref_*.txt`
