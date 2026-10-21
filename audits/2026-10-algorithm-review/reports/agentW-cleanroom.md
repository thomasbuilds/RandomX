# Agent W: clean-room implementation of RandomX v1 and v2 from doc/specs.md

Scope: is doc/specs.md (branch `claude/modest-meitner-dkvpgd`, HEAD d3dd967) complete and unambiguous enough to
implement RandomX v1 and v2 bit-exactly? I implemented RandomX in Python 3 from the specification (and design.md
for context), compared every stage with the reference library through its public API only, and logged every
consultation of the sources. Working directory:
`/tmp/claude-0/-home-user-RandomX/cfc10d96-f1d5-53d0-817a-8712f1f55302/scratchpad/agentW/`
(called `agentW/` below). Every number here is reproduced by `agentW/reproduce.sh`.

## 1. Verdict and findings (lead)

**Verdict.** I reached bit-exact agreement with the reference for every stage of v1 and v2: the Cache (all
256 MiB identical), 12/12 Dataset items, 69/69 light-mode hashes and 3/3 commitments. The 69 hashes are 48
fixed vectors (4 keys x 6 inputs x {v1, v2}), 20 seeded random inputs and one with a 65-byte key. But the
specification alone did not get me there:

* **Chapters 2-5 and 7 except 7.3 step 7 (the VM, the generators, the Cache, the Dataset item construction) are
  implementable without the source.** I did not consult the source for them. Two encodings are not stated in
  the text, and I had to guess both: the byte layout of an instruction word (only in an external image, Fig. 5.1,
  that this host cannot reach) and the opcode-to-instruction mapping. I also had to pick the right reading of two
  ambiguous rules (the CBRANCH jump target and the scratchpad level of floating-point memory operands). The
  natural reading was right each time: it matches all 69 reference hashes, and the alternative readings do not
  (Section 4.2).
* **Chapter 6 (SuperscalarHash generator) and step 7 of 7.3 cannot be implemented from the specification.**
  The text gives the outline but none of the random-draw protocol and few of the scheduling rules. I had to
  read `src/superscalar.cpp` and `src/blake2_generator.cpp` (consultations 3-5). I list 16 gaps (G6-01..G6-16);
  three of them are statements that are wrong. 21 separate source-only rules are each load-bearing: switching
  any one of them to its natural spec-only reading changes 8 to 16 of the 16 generated programs and changes
  Dataset item 0 for both test keys.
* **The v2 deltas are fully and correctly specified on the branch.** All 34 v2 hashes (24 fixed vectors, 10
  random inputs) matched on their first run, which came after a tooling bug of mine (Section 5) was fixed, and no
  v2-specific question needed the source. Master's v2 text (a901d5b) is ambiguous in step 10 ("AES encrypt of f0
  with e0 as key"). The branch's rewording is confirmed correct by these matches.

| id | finding | severity | confidence |
|---|---|---|---|
| W1 | Spec chapter 6 + 7.3 step 7 are not an implementable specification of SuperscalarHash: the random-draw protocol, operand-selection, stall/throw-away, termination and address-register rules exist only in `superscalar.cpp`; 3 statements contradict the reference (Table 6.3.2 order, "source selected first", "multiplied twice in a row"). Round 1's F2 (rated Info) is confirmed and extended: I demonstrate every item is consensus-relevant and add at least 7 rules F2 does not list (G6-05, G6-07, G6-12, G6-13, G6-14, the throw-away cycle rule of G6-10, and most of G6-06/G6-15). The claim that specs.md is the normative description does not hold for chapter 6. Fix: normative pseudocode, Appendix A. Same in master and branch. | Medium | High |
| W2 | The instruction word layout (opcode/dst/src/mod/imm32 byte positions) is defined only by an image on i.imgur.com (Fig. 5.1, unreachable here: proxy 403), and the opcode -> instruction mapping (contiguous ranges in table order) is never stated. Both natural guesses are right, but nothing in the text confirms them. Fix: V-01, V-02 in Section 4.3. Master and branch. | Low | High |
| W3 | CBRANCH jump target (5.4.2) reads either static (program order) or dynamic (execution order, "at the beginning of each program iteration all registers are considered unmodified"). The two readings differ whenever a CBRANCH is taken twice in a row, which happens about 3.3 times per v1 hash and 6.6 per v2 hash; the dynamic reading changes 11 of 12 standard test hashes. Reference = static. Fix: one sentence (V-03). Master and branch. | Low | High |
| W4 | Table 5.1.4 "src == dst (read) -> L3" does not say whether it applies to FADD_M/FSUB_M/FDIV_M (dst is an F/E register, src an R register). Reading it as applying when the indices are equal changes the v1 and v2 hashes tested (measured), and by estimate affects about 14 instructions per v1 hash (14/256 fp-memory opcodes x 256 instructions x 8 programs x 1/8 chance of equal indices), so essentially every hash; the reference never uses L3 for fp operands. Fix: one sentence (V-04). Master and branch. | Low | High |
| W5 | The commitment `randomx_calculate_commitment` = Blake2b-256(input \|\| hash) (in randomx.h since the v2 work) is not in the specification at all. The formula from the audit brief matches the library 3/3. Fix: add a chapter 2.1 (V-08). Master and branch. | Low (blocking for anyone who needs the commitment) | High |
| W6 | Master-only gaps fixed on the branch: v2 step 10 AES wording (V-05), step 3 register offsets (V-06), contradictory ma/mp description for v2 (V-07). The branch texts are correct (34/34 v2 matches). | Info | High |
| W7 | Implementer trap: `fprc` persists across the 8 programs and across the generator code. Any float parsing or arithmetic done by the implementation itself (not the VM) while fprc != 0 can be silently wrong; my first v1 hash failed exactly that way. The spec only implies persistence through the step numbering. Fix: one sentence (V-11). | Info | High |
| W8 | Smaller ambiguities and typos: BlakeGenerator byte order (V-09), int32 halves (V-10), keys > 60 bytes (V-12), Argon2 "output size 0" not a legal RFC 9106 tag length (V-13), typos/numbering (V-14). | Info | High |

Severity follows the common brief's design-level scale. None of these weakens the proof of work itself, because
the reference code is the de facto definition and is pinned by test vectors. W1 is Medium because an important
documentation claim (that specs.md specifies the algorithm) is false for one chapter, with limited practical
impact: independent implementations copy `superscalar.cpp`.

## 2. Final match status per stage

All comparisons are against `lead/build-x86/librandomx.a` through `randomx.h` only (`agentW/ref/refvals.c`,
reference values made by `agentW/ref/make_refs.sh`). "Spec only" means the stage was implemented and matched
without reading any RandomX source; "after SOURCE" means it needed a logged source consultation.

| stage | v1 | v2 | how obtained | evidence |
|---|---|---|---|---|
| 0 primitives: AES rounds, AesGenerator1R/4R, AesHash1R, BlakeGenerator, chapter-3 constants | n/a | n/a | spec only (+ FIPS-197, RFC 9106) | `python3 impl/rx_primitives.py`: AES round = FIPS-197 App. B, full AES-128 enc/dec through the single rounds, all chapter-3 keys = Hash512/Hash256 of the stated strings (fixes their byte order from the spec alone) |
| 1 Cache (Argon2d memory fill, 7.1) | MATCH (shared) | MATCH (shared) | spec only | `cmp data/cache_testkey000.bin ref/cache_testkey000.bin`: all 268,435,456 bytes identical (sha256 c52c404a...3691; words [0], [1], [16777216], [33554431] = 191e0e1d23c02186, f06bdd8741404c15, b31534dbc6e23bb4, 1f47f056d05cd99b). Argon2d also passes the RFC 9106 test vector. Other 3 keys: confirmed through the hashes. |
| 2 SuperscalarHash generation (ch. 6) | MATCH (shared) | MATCH (shared) | **only after SOURCE** (blocking) | checked indirectly through stage 3; program sizes for "test key 000": 447, 438, 449, 447, 444, 449, 452, 437, address registers 4,5,4,3,4,0,5,0 |
| 3 Dataset items (7.3) | MATCH (shared) | MATCH (shared) | spec + stage 2 (7.3 step 7 needs SOURCE) | `impl/test_items.py`: 12/12 items match (key "test key 000": items 0, 1, 2, 3, 1000, 12345, 16777216, 34078718; key "test key 001": 0, 1, 777, 34078718) |
| 4 Scratchpad fill, program generation, VM, final hash (light mode) | **24/24 MATCH** | **24/24 MATCH** | spec only (VM); guesses V-01, V-02, readings V-03, V-04 confirmed by the reference | `impl/run_hashes.py <key> both` for keys "test key 000", "test key 001", "" (empty), a 60-byte key; inputs "This is a test", "Lorem ipsum dolor sit amet", "sed do eiusmod tempor incididunt ut labore et dolore magna aliqua", empty, a 53-byte binary blob, 300 bytes of 0xab. Logs `agentW/logs/hash_{k000,k001,empty,k60}.log` |
| 4b commitment | 1/1 MATCH | 2/2 MATCH | **not in the spec**; formula from the audit brief | `rx_vm.commitment()` vs `refvals commit` |
| 4c key longer than 60 bytes (65 bytes) | 1/1 MATCH | - | randomx.h reading: Argon2d gets the whole key, the BlakeGenerator the first 60 bytes | `impl/k65_check.py` vs `ref/hash_k64.txt` (the 60-byte prefix gives a different reference hash, 4bb3032d...) |
| 4d extra coverage: 10 random inputs (lengths 0-200, seeded), key "test key 000" | 10/10 MATCH | 10/10 MATCH | spec only | `impl/random_check.py` vs `ref/hashes_random.txt`, log `logs/hash_random.log` |

The VM code generator (`impl/rx_vm.py`) was also cross-checked against a separately written plain interpreter
(`impl/rx_interp.py`): 18 programs (v1 and v2, 3 programs x 3 inputs each), identical registers and Scratchpad
(`impl/xcheck.py`).

Reference v2 vectors produced for this report (light mode, the reference library, key "test key 000"):
"This is a test" -> 22ec6b86...fb4cdb6f, "Lorem ipsum dolor sit amet" -> 9e2c772c...79012ecf, "sed do eiusmod
tempor incididunt ut labore et dolore magna aliqua" -> 4d6b063a...17796e42, empty input -> cf43f7df...6baea8e4
(full values in `agentW/ref/hashes.txt`).

## 3. Method and clean-room discipline

* Implementation: `agentW/impl/` (1,800 lines of Python 3, with spec section references throughout):
  `rx_primitives.py` (3.1-3.5), `rx_argon2.py` (7.1, numpy-vectorised Argon2d from RFC 9106),
  `rx_superscalar.py` (chapter 6, 7.2, 7.3; every rule not in the spec is tagged `[SRC]`), `rx_vm.py`
  (chapters 2, 4, 5; each program is compiled to a Python function), `rx_interp.py` (independent plain
  interpreter used only for cross-checking). No C was needed: a light-mode hash takes 13-27 s, a Cache about
  4 minutes.
* Floating point: CPython `+ - * /` and `math.sqrt` follow the `fesetround` mode on x86-64. Checked by
  `impl/fpcheck.py`: 1/3 rounds down/up, sqrt(2) differs by 1 ulp between modes, and `x - x = -0` only in
  roundTowardNegative.
* Before writing any code I recorded every ambiguity found on a first reading
  (`agentW/logs/ambiguities-prereading.md`, entries A1-A13), with the candidate readings. Only chapter 6 (A10),
  together with the BlakeGenerator details it relies on (A5), required the source. Everything else was resolved
  either from the spec itself (A6: the chapter-3 constants are stated to be Hash512 outputs, which fixes their
  byte order) or by the natural reading, confirmed by the reference hashes.
* Consultations (full log in Appendix B and `agentW/logs/consultations.md`):
  * #1, permitted: the public API header `randomx.h`.
  * #2: the attempt to fetch the spec's Imgur figures, which failed with a proxy 403.
  * #3-#5, SOURCE: `blake2_generator.{cpp,hpp}`, `superscalar.cpp`, `superscalar_program.hpp` and one grep for
    three constants. The grep incidentally printed single lines of four other files; they only restate rules
    already in spec 5.2.1/5.2.6.
  * No source was read for the VM, the AES generators, Argon2 or the Dataset item construction.
  * After my chapter 6 work was complete I read round 1's finding F2 (reports/agentF) to compare.
* Process note: all heavy commands ran under `taskset -c 0-2 nice -n 10`. Twice, a short job (`xcheck.py`, about 1
  minute; `appendixA_impl.py`, about 15 s) overlapped two background jobs, i.e. 3 of my processes briefly shared
  cores 0-2. Memory: a hash process peaks at 0.32 GB (VmHWM measured on `random_check.py`), a Cache build at
  about 0.55 GB (256 MiB array plus one copy while writing; estimated), so at most about 1.1 GB with 2 jobs.
* Limitation: my training data almost certainly contains the RandomX reference sources. The clean room is
  therefore procedural, not perfect. I judged each gap on the spec text, not on whether my first guess happened to
  be right. A gap is listed whenever the text does not determine the answer, even where the natural guess turned
  out right (V-01, V-02, V-10).

## 4. Specification gaps and ambiguities

Severity column (brief-W scale): blocking / ambiguous / misleading / cosmetic. "M" = exists in master
(a901d5b:doc/specs.md), "B" = exists on the branch (d3dd967). The master/branch difference is limited to
chapters 2 and 4 (`agentW/logs/specs_master_vs_branch.diff`, 45 lines); chapter 6 is identical, so all G6 entries
are M+B.

### 4.1 Chapter 6 (SuperscalarHash) and 7.3 step 7: evidence that each rule is load-bearing

`impl/ss_sensitivity.py` regenerates the 8 programs for keys "test key 000" and "test key 001" with one
source-only rule replaced by the natural spec-only reading. It counts the changed programs (instruction list or
address register) out of 16 and checks Dataset item 0 for both keys. Output: `agentW/logs/ss_sensitivity.txt`.

| alternative (spec-only reading) | programs changed | item 0 changed | gap |
|---|---|---|---|
| Table 6.3.2 order taken literally (IADD_C before IXOR_C) | 16/16 | 2/2 | G6-03 |
| ISUB_R is its own group in the repetition rule | 16/16 | 2/2 | G6-08 |
| register draw also made when only one register qualifies | 16/16 | 2/2 | G6-02 |
| register choice with getByte() % n instead of getUInt32() % n | 16/16 | 2/2 | G6-02 |
| IMULH_R/ISMULH_R: source selected first (6.3.4 as written) | 16/16 | 2/2 | G6-05 |
| "multiplied twice in a row" applied to all 4 multiplications | 16/16 | 2/2 | G6-07 |
| IMULH/ISMULH repetition parameter = source register (no random uint32) | 16/16 | 2/2 | G6-04 |
| IROR_C count from getUInt32() | 16/16 | 2/2 | G6-01 |
| IADD_RS shift = byte & 3 | 16/16 | 2/2 | G6-01 |
| cycle counter restored after a throw-away | 16/16 | 2/2 | G6-10 |
| no stall (operand must be ready at the port cycle) | 16/16 | 2/2 | G6-10 |
| stall until an operand is ready (no 4-cycle limit) | 16/16 | 2/2 | G6-10 |
| no r5 special case for IADD_RS sources | 15/16 | 2/2 | G6-09 |
| address register by reference-CPU (not 1-cycle) latency | 11/16 | 2/2 | G6-16 |
| address-register ties to the highest index | 8/16 | 2/2 | G6-16 |
| after IMUL_RCP an odd byte selects group 3 | 16/16 | 2/2 | G6-13 |
| random decode group = getUInt32() % 4 | 16/16 | 2/2 | G6-01 |
| IMUL_RCP rule checked before the multiplication-count rule | 16/16 | 2/2 | G6-13 |
| only IMUL_R counts as a multiplication | 16/16 | 2/2 | G6-14 |
| the instruction scheduled at cycle >= 170 is dropped | 16/16 | 2/2 | G6-15 |
| IMUL_RCP's imul_rr does not wait for mov_ri | 16/16 | 2/2 | G6-06 |

### 4.2 VM ambiguities: evidence

* V-01 (instruction layout) and V-02 (opcode map): the natural readings give 69/69 matches. The alternative layout
  "imm32 first" gives a different hash (`logs/variants.txt`).
* V-03 (CBRANCH): the dynamic reading changes 11 of the 12 standard hashes for "test key 000" (v1 5/6, v2 6/6;
  `logs/variants.txt`). `impl/jumpstats.py` (30 hashes per version, stand-in Dataset, the statistic does not
  depend on Dataset contents) counts taken jumps per hash and "taken twice in a row" events: v1 1569 jumps and 3.27
  twice-in-a-row per hash, at least one in 21/30 hashes; v2 2360 jumps and 6.57, at least one in 28/30 hashes. Only
  the twice-in-a-row case separates the two readings. After a jump back from CBRANCH j to its target, instructions
  target..j-1 contain neither a CBRANCH nor a write to dst_j. So the dynamic reading makes j its own last
  modifier, which suppresses the second jump, and every other jump target is identical under both readings.
* V-04 (fp memory level): reading Table 5.1.4's L3 rule as applying to FADD_M/FSUB_M/FDIV_M when the dst and src
  indices are equal changes the hash for v1 and v2 ("This is a test", `logs/variants.txt`; measured). Modelled
  estimate: 14/256 opcodes are fp memory instructions, so a v1 hash has about 14 x 8 = 112 of them, of which 1/8
  (about 14) have equal indices. The chance that a hash is unaffected is about (7/8)^112 = 3e-7 (assumes uniformly
  random dst/src fields, which holds because program bytes come from AesGenerator4R).

### 4.3 Gap table with proposed replacement text

| id | section | problem | severity | M/B | proposed replacement text |
|---|---|---|---|---|---|
| G6-01 | 6.3, 3.5 | The random-draw protocol (which BlakeGenerator call, width, mapping, order) is absent for every decision. | blocking | M+B | Replace 6.3 by the normative pseudocode of Appendix A, which fixes every draw. |
| G6-02 | 6.3.4 | Register choice rule. | blocking | M+B | "A register is chosen from the qualifying registers, listed in increasing index order: none -> failure; one -> that register, no random number is drawn; otherwise the element at index `getUInt32() mod n`." |
| G6-03 | Table 6.3.2 | Row order is the draw order but the 7/8/9-byte row lists IADD_C first; the reference maps 0 -> IXOR_C. | misleading | M+B | Rewrite each row as an indexed list: "3 (not last): [ISUB_R, IXOR_R][getByte() & 1]; 3 (last): [ISUB_R, IXOR_R, IMULH_R, ISMULH_R][getByte() & 3]; 4 (group 4, not last): IMUL_R; 4: [IROR_C, IADD_RS][getByte() & 1]; 7, 8, 9: [IXOR_C, IADD_C][getByte() & 1]; 10: IMUL_RCP." |
| G6-04 | 6.3.4 | IMULH_R/ISMULH_R draw a 32-bit value used only by the repetition rule. | blocking | M+B | "IMULH_R and ISMULH_R draw `par = getUInt32()` (interpreted as a signed 32-bit value) immediately after being selected; this value is their 'source' for the repetition rule." |
| G6-05 | 6.3.4 | "The source operand (if needed) is selected first" is false for IMULH_R/ISMULH_R. | misleading | M+B | "Operands are selected when the macro-op that uses them is issued: for IMULH_R/ISMULH_R the destination with the first mov_rr and the source with mul_r/imul_r (which may belong to the next decode group); for all other instructions with a source, the source first." |
| G6-06 | 6.1, 6.2 | Macro-op roles and the IMUL_RCP internal dependency. | blocking | M+B | Table per instruction: (srcOp, dstOp, resultOp) = (0, 0, 0) for one-macro-op instructions with a source, (-, 0, 0) for IROR_C/IADD_C/IXOR_C, (1, 0, 1) for IMULH_R/ISMULH_R, (-, 1, 1) for IMUL_RCP; "imul_rr of IMUL_RCP cannot be scheduled before the cycle at which mov_ri's result is ready." |
| G6-07 | 6.3.4 | "cannot be multiplied twice in a row" really means IMUL_R after IMUL_R only. | misleading | M+B | "If the instruction is IMUL_R, the destination must not be a register whose last operation was IMUL_R, unless allowChainedMul is true." |
| G6-08 | 6.3.4 | Operation groups of the repetition rule not defined. | blocking | M+B | "Each instruction has a group and a parameter: ISUB_R and IADD_RS: group IADD_RS; IXOR_R: IXOR_R; IMUL_R: IMUL_R (parameter = source register); IROR_C, IADD_C (any size), IXOR_C (any size), IMUL_RCP: own group, parameter -1; IMULH_R, ISMULH_R: own group, parameter as in G6-04. A register qualifies as destination only if its last (group, parameter) differs from the instruction's." |
| G6-09 | 6.3.4 | IADD_RS source special case. | blocking | M+B | "For IADD_RS, if exactly two registers are ready and one of them is r5, r5 is the source and no random number is drawn." |
| G6-10 | 6.3.4 | Stall / throw-away mechanics, including the persistent cycle advance. | blocking | M+B | "If no register qualifies, the selection is retried with the cycle increased by 1, at most 4 attempts in total; each failed attempt also advances the generator's current cycle. If a later attempt succeeds, the current cycle is restored when the macro-op is issued; if all 4 fail, the instruction is discarded (not added to the program), the cycle advance is kept, and a new instruction is selected for the same slot. After 256 consecutive discards the rest of the decode group is abandoned." |
| G6-11 | 6.3.4 | allowChainedMul scope. | ambiguous | M+B | "allowChainedMul is true while the previous attempt to fill the current slot was discarded (G6-10); it becomes false after any successful operand selection." |
| G6-12 | 6.3.3 | Port search details (start cycle, horizon, 2-uop rule, failure). | blocking | M+B | "Starting at the current cycle, the first cycle c < LATENCY+4 is taken at which a permitted port is free, testing P5, P0, P1 in this order; a macro-op with two micro-ops needs P1 and P5 free in the same cycle; mov_rr uses no port. If no cycle is found, generation ends." |
| G6-13 | 6.3.1 | Bullets are a priority list; meaning of "currently processed instruction"; IMUL_RCP draw. | ambiguous | M+B | "Apply the first matching rule. The current instruction is the one whose macro-ops were being issued when the previous group ended (none at the start). After IMUL_RCP: group 0 if getByte() is odd, else group 3; otherwise group getByte() & 3." |
| G6-14 | 6.3.1 | What counts as a multiplication. | ambiguous | M+B | "mulCount counts completed IMUL_R, IMULH_R, ISMULH_R and IMUL_RCP instructions; the current decoding cycle starts at 0 (so the first group is always group 4)." |
| G6-15 | 6.3 | Termination details. | ambiguous | M+B | "When a macro-op is scheduled at a cycle >= LATENCY, the current decode group is completed (an instruction that completes in it is kept, an unfinished one is dropped) and generation stops. Generation also stops after LATENCY decode groups, at 3*LATENCY+2 instructions, or when the port search fails." |
| G6-16 | 7.3 step 7 | "register that has the longest dependency chain" undefined (model, ties). | blocking | M+B | "The address register of a SuperscalarHash program: set lat[r] = 0; for each instruction in order, lat[dst] = max(lat[dst] + 1, lat[src] + 1 if it has a source register different from dst, else 0); the address register is the lowest-numbered register with the largest lat." |
| V-01 | 5.1, Fig. 5.1 | Instruction layout only in an external image. | ambiguous (blocking without the figure) | M+B | "An instruction is 8 bytes: byte 0 opcode, byte 1 dst, byte 2 src, byte 3 mod, bytes 4-7 imm32 (little-endian)." (keep the figure as an illustration) |
| V-02 | 5.1.1 | Opcode -> instruction map not stated. | ambiguous | M+B | "Opcodes are assigned in contiguous ranges in the order of Tables 5.2.1, 5.3.1, 5.4.1, 5.5.1: IADD_RS 0-15, IADD_M 16-22, ISUB_R 23-38, ISUB_M 39-45, IMUL_R 46-61, IMUL_M 62-65, IMULH_R 66-69, IMULH_M 70, ISMULH_R 71-74, ISMULH_M 75, IMUL_RCP 76-83, INEG_R 84-85, IXOR_R 86-100, IXOR_M 101-105, IROR_R 106-113, IROL_R 114-115, ISWAP_R 116-119, FSWAP_R 120-123, FADD_R 124-139, FADD_M 140-144, FSUB_R 145-160, FSUB_M 161-165, FSCAL_R 166-171, FMUL_R 172-203, FDIV_M 204-207, FSQRT_R 208-213, CBRANCH 214-238, CFROUND 239, ISTORE 240-255." |
| V-03 | 5.4.2 | Static vs dynamic jump target. | ambiguous | M+B | "The jump target of every CBRANCH is fixed when the program is loaded, by scanning the program in order with the modification rules below; it does not depend on which instructions were executed. A CBRANCH can therefore jump twice in a row." (and replace "At the beginning of each program iteration, all registers are considered to be unmodified" by "When the program is loaded, all registers are considered unmodified") |
| V-04 | 5.1.4 | L3 rule vs fp memory operands. | ambiguous | M+B | "The `src == dst` case applies only to the integer memory instructions (IADD_M, ISUB_M, IMUL_M, IMULH_M, ISMULH_M, IXOR_M); FADD_M, FSUB_M and FDIV_M always use L1 or L2 according to mod.mem." |
| V-05 | 4.6.2 step 10 | Master: "AES encrypt of f0 with e0 as key" (full AES? one round? byte order?). | ambiguous | M only | Branch text (already correct). |
| V-06 | 4.6.2 step 3 | Master: offsets of f0-f3/e0-e3 in the 64-byte block not given. | ambiguous | M only | Branch text (already correct). |
| V-07 | 4.3 | Master: "ma ... next Dataset read and mp ... next prefetch", contradictory in v2 (mp = ma). | misleading | M only | Branch text (already correct). |
| V-08 | 2 | Commitment not specified. | blocking (for the commitment) | M+B | "2.1 Commitment. `C = Hash256(H \|\| R)`, where H is the input of chapter 2 and R its 32-byte result." |
| V-09 | 3.5.2 | 4-byte output byte order and leftover bytes. | ambiguous | M+B | "getUInt32 returns the next 4 unused bytes as a little-endian integer; if fewer than 4 (getByte: 1) unused bytes remain, they are discarded and S = Hash512(S) first." |
| V-10 | 4.3.1 | Which int32 goes to which half. | cosmetic | M+B | "bits 0-31 -> lower half, bits 32-63 -> upper half". |
| V-11 | 2 step 6 | fprc persistence implicit. | cosmetic | M+B | "fprc is set to 0 only once per hash; each program starts with the value left by the previous one." |
| V-12 | 7, randomx.h | Keys > 60 bytes "implementation-defined"; the reference uses the whole key for Argon2 and the first 60 bytes for the BlakeGenerator (confirmed: 65-byte key, stage 4c). | cosmetic | M+B | Either forbid keys > 60 bytes or state: "Argon2d uses the whole key; the BlakeGenerator of 7.2 uses its first 60 bytes." |
| V-13 | 7.1 | "output size 0" is not a legal Argon2 tag length (RFC 9106: T >= 4), so stock Argon2 libraries cannot be used; the literal reading (LE32(0) in H0, no finalizer) is correct. | cosmetic | M+B | "T = 0 is used only inside H0; implementations must use the Argon2 memory-fill procedure directly." |
| V-14 | 1.1, 4.5, 5.2.9, 5.4.2, 6.3.2, 6.3.4, 5.1.4 | SuperscalarHash "see chapter 7" (is 6); no 4.5.1; two 5.2.9; "inifinite"; Table 6.3.2 title repeats 6.3.1; "is it selected"; "memory write mod.cond is 14 or 15" (missing "and"). | cosmetic | M+B | Fix. |

Readings that the text did determine and that were correct: the E-register conversion (4.3.2: "set" is
unambiguous for the fraction bits because an int32 converted to double always has its 22 low fraction bits zero,
so setting and OR-ing agree), the A-register construction, ma/mx/mt/mp and the v2 two-iteration prefetch,
CFROUND v2, IMUL_RCP's reciprocal, the cimm construction, the AES round definitions and the chapter-3 byte orders
(self-verifying through the Hash512 statements). These are positive results for the spec.

## 5. Implementer trap found the hard way (W7)

My first v1 hash did not match. The generated code was correct (it agreed with the independent interpreter). The
cause: I embedded the A-register values as decimal float literals, which CPython parses with code that is
sensitive to the current rounding mode. Programs 2-8 are compiled while `fprc` holds the value left by the
previous program, so their A registers could be off by one ulp. Passing the exact doubles fixed it, and all 69
hashes then matched. The specification is not wrong here, but a one-line warning in chapter 2 step 6 (V-11) would
help: fprc persists across programs, so host-side floating-point work done during program generation inherits it.

## 6. v1 and v2 side by side

| item | v1 | v2 |
|---|---|---|
| program size 256 / 384 (1.2, 4.6.2 step 4) | spec complete; matched | spec complete; matched |
| CFROUND (5.4.1) | spec complete; matched | conditional rule complete; matched |
| prefetch / mp alias (4.3, 4.6.2 steps 5-8) | branch complete (master's 4.3 sentence confusing); matched | branch complete; master's 4.3 sentence contradictory (V-07); matched |
| step 10 F/E mixing | XOR: complete | branch: complete (single rounds, LE images, order); master ambiguous (V-05); matched |
| hashes matched (light mode) | 24/24 fixed + 10/10 random + 1/1 (65-byte key) | 24/24 fixed + 10/10 random |
| CBRANCH static/dynamic exposure (V-03) | twice-in-a-row 3.27 per hash | 6.57 per hash (larger programs) |
| shared parts (Cache, SuperscalarHash, Dataset) | identical for v1 and v2 (randomx.h, spec 1.2); gaps G6 apply to both | same |

## 7. Design claims checked along the way

| claim | status | evidence |
|---|---|---|
| design.md 3.4: average SuperscalarHash has 450 instructions, 155 multiplications, longest dependency chain 95 | holds approximately: 447.4 +- 9.4 instructions, 155.8 +- 2.4 multiplications, chain 92.8 +- 4.3 (sd; standard error 0.07, so "95" is about 2% high) | `impl/ss_stats.py 500`: 4000 programs, `logs/ss_stats.txt` |
| design.md 2.6.2: "Each CBRANCH instruction can jump up to twice in a row" | holds under the static reading (the reference's): after a jump bits b..b+7 of dst are 0, so a second jump needs bits b..b+7 of cimm = 255 plus a carry into bit b, and a third needs a second carry, i.e. D + 2C >= 2^(b+1), where D < 2^b is the low b bits of dst and C < 2^(b-1) those of cimm (bit b-1 of cimm is 0): impossible. Twice-in-a-row does occur (3.3 per v1 hash). Under the dynamic reading the claim would be false (never twice). | `logs/jumpstats.txt`, argument above |
| specs.md is the normative description | does not hold for chapter 6 / 7.3 step 7 (W1); holds for the rest given V-01..V-04 | sections 2-4 |
| design_v2.md performance and efficiency tables | cannot be verified by this track (a Python implementation; no hashrate or power measurement) | - |
| design_v2.md / randomx.h: Cache and Dataset identical for v1 and v2 | holds for what was tested: one Cache and one set of SuperscalarHash programs per key served both versions, 24/24 matches each | Section 2 |

Additional observation (Info): the address register is far from uniform (r0 20.8 %, r5 7.0 % of 4000 programs)
because ties go to the lowest index and r5 is never an IADD_RS destination. This does not bias the Cache index,
which is the 64-bit value of the register, not its number.

## 8. Reproduction

`agentW/reproduce.sh` lists every command. Key ones (all heavy commands under `taskset -c 0-2 nice -n 10`, run from
`agentW/impl/`):

* reference values: `agentW/ref/make_refs.sh` (public API only, `ref/refvals.c`)
* self tests: `python3 rx_primitives.py; python3 rx_argon2.py selftest; python3 rx_superscalar.py; python3 fpcheck.py`
* Cache: `python3 rx_argon2.py cache "test key 000" ../data/cache_testkey000.bin` then `cmp` with `ref/cache_testkey000.bin`
* items: `python3 test_items.py`; VM cross-check: `python3 xcheck.py`
* hashes: `python3 run_hashes.py {k000|k001|empty|k60} both` -> `logs/hash_*.log`
* ambiguity experiments: `python3 ss_sensitivity.py`, `./run_variants.sh`, `python3 jumpstats.py 30`, `python3 ss_stats.py 500`
* extra coverage: `python3 random_check.py` (20 random-input hashes), `python3 k65_check.py` (65-byte key, needs
  `rx_argon2.py cache` for that key first), `python3 appendixA_impl.py` (Appendix A completeness, 1624 programs)
* master vs branch: `git -C /home/user/RandomX show a901d5b:doc/specs.md` and `diff` -> `logs/specs_master_vs_branch.diff`

## Appendix A. Proposed normative text for spec 6.3 (replaces the prose of 6.3.1-6.3.4)

Derived from `impl/rx_superscalar.py`, which reproduces the reference (12/12 Dataset items, 69/69 hashes).
Completeness check of this text: `impl/appendixA_impl.py` is a second generator written only from the pseudocode
below. It produces the same instructions and address register as `rx_superscalar.py` for 1624/1624 programs
(203 keys x 8). Writing it exposed one subtlety that is now in the text: the test for starting a new instruction
must be `m >= |cur|`, because after an abandoned decode group cur = NONE while m may be > 0.
`getByte`/`getUInt32` are the BlakeGenerator calls of 3.5 (V-09). LATENCY = RANDOMX_SUPERSCALAR_LATENCY.

```
Macro-ops: Table 6.2.1.  Instruction -> macro-ops, (srcOp, dstOp, resultOp):
  ISUB_R sub_rr (0,0,0)   IXOR_R xor_rr (0,0,0)   IADD_RS lea_sib (0,0,0)   IMUL_R imul_rr (0,0,0)
  IROR_C ror_ri (-,0,0)   IADD_C add_ri (-,0,0)   IXOR_C xor_ri (-,0,0)
  IMULH_R mov_rr,mul_r,mov_rr (1,0,1)   ISMULH_R mov_rr,imul_r,mov_rr (1,0,1)
  IMUL_RCP mov_ri,imul_rr (-,1,1); its imul_rr may not start before mov_ri's result (depCycle)
Per register: ready = 0, lastGroup = none, lastPar = -1.   portBusy[0 .. LATENCY+3][P0,P1,P5] = free.
cur = NONE (no macro-ops); m = 0; cycle = 0; depCycle = 0; done = false; program = []; mulCount = 0; throwAway = 0

for decodeCycle = 0 .. LATENCY-1, while not done and |program| < 3*LATENCY+2:
    group = 5 if cur is IMULH_R/ISMULH_R
            else 4 if mulCount <= decodeCycle
            else (0 if getByte() is odd else 3) if cur is IMUL_RCP
            else getByte() & 3                                  # groups: Table 6.3.1
    j = 0
    while j < number of slots of group:
        top = cycle
        if m >= |cur|:                                          # start a new instruction in slot j
                                                                # (>=: after an abandoned group cur = NONE, m may be > 0)
            if done or |program| >= 3*LATENCY+2: break
            cur = select(slot size, group, last = (j is the last slot)); m = 0     # see below
        op = macro-op m of cur
        sc = schedule(op, cycle, commit = false); if sc < 0: done = true; break
        if m == cur.srcOp:
            repeat up to 4 times: if selectSource(cur, sc) succeeds: stop; else sc += 1, cycle += 1
            if all 4 failed:
                if throwAway < 256: throwAway += 1; m = |cur|; continue      # discard, same slot j
                cur = NONE; break                                            # abandon this group
        if m == cur.dstOp:
            same as above with selectDestination(cur, sc, allowChainedMul = throwAway > 0)
        throwAway = 0
        sc = schedule(op, sc, commit = true); if sc < 0: done = true; break
        depCycle = sc + op.latency
        if m == cur.resultOp: ready[cur.dst] = depCycle; lastGroup[cur.dst] = cur.group; lastPar[cur.dst] = cur.par
        j += 1; m += 1
        if sc >= LATENCY: done = true
        cycle = top
        if m == |cur|: append cur to program (src := dst if it has no source)
                       if cur is IMUL_R, IMULH_R, ISMULH_R or IMUL_RCP: mulCount += 1
    cycle += 1

select(size, group, last):
    3, last:  [ISUB_R, IXOR_R, IMULH_R, ISMULH_R][getByte() & 3]      3: [ISUB_R, IXOR_R][getByte() & 1]
    4, group 4 and not last: IMUL_R                                    4: [IROR_C, IADD_RS][getByte() & 1]
    7, 8, 9: [IXOR_C, IADD_C][getByte() & 1]                           10: IMUL_RCP
  then, in the same order:
    ISUB_R: group IADD_RS, par = source      IXOR_R: group IXOR_R, par = source      IMUL_R: group IMUL_R, par = source
    IADD_RS: mod = getByte() (shift = bits 2-3); group IADD_RS, par = source
    IROR_C: repeat imm32 = getByte() & 63 until imm32 != 0; par = -1
    IADD_C, IXOR_C: imm32 = getUInt32(); par = -1 (one group per mnemonic for all sizes)
    IMULH_R, ISMULH_R: par = getUInt32() as signed 32-bit; dst may equal src
    IMUL_RCP: repeat imm32 = getUInt32() until imm32 is neither 0 nor a power of 2; par = -1

schedule(op, c, commit):
    if op is IMUL_RCP's imul_rr: c = max(c, depCycle)
    mov_rr: return c (no port)
    one micro-op: first c' in [c, LATENCY+3] with a free permitted port, ports tested P5, P0, P1; mark it if commit
    mul_r / imul_r: first c' in [c, LATENCY+3] where P1 and P5 are both free; mark both if commit
    none found: return -1

choose(list): none -> fail; one -> it (no draw); else list[getUInt32() mod |list|]   (lists in register order)
selectSource(cur, c): R = {r : ready[r] <= c}
    if cur is IADD_RS and |R| = 2 and r5 in R: src = r5, par = 5, no draw
    else src = choose(R); if par is "source": par = src
selectDestination(cur, c, allowChainedMul): R = {r : ready[r] <= c
        and (cur is IMULH_R/ISMULH_R or r != src)
        and (allowChainedMul or cur is not IMUL_R or lastGroup[r] != IMUL_R)
        and (lastGroup[r] != cur.group or lastPar[r] != cur.par)
        and not (cur is IADD_RS and r = r5)}
    dst = choose(R)

Address register (7.3 step 7): lat[0..7] = 0; for each instruction in order:
    lat[dst] = max(lat[dst] + 1, lat[src] + 1 if src != dst else 0); address register = lowest r with maximal lat[r].
The 8 programs of 7.2 are generated in order from one BlakeGenerator seeded with K (first 60 bytes).
```

## Appendix B. Consultation log (verbatim copy of agentW/logs/consultations.md)

| # | time (UTC) | what was read | kind | why / what it answered |
|---|---|---|---|---|
| 0 | 20:44 | `ls src/` (file names only, no content) | permitted (listing) | to locate randomx.h for linking |
| 1 | ~20:44 | `src/randomx.h` (public API header, full) | permitted | needed to link. Spec-relevant content: keySize note "only the first 60 bytes of a longer key are used by the SuperscalarHash generator" (spec 7: "implementation-defined"); commitment only described as "from a RandomX hash and its input" (no formula). |
| 2 | ~20:45 | attempted download of the spec's figures (i.imgur.com/FtkWRwe.png = Fig 5.1 instruction encoding, Enk42b8 = Fig 4.1, 86h5SbW = Fig 7.1) | spec figures | FAILED: proxy returned 403 for i.imgur.com. Fig 5.1 (the only definition of the byte layout of an instruction word) is not available to this implementer. No further network probing (denied by policy). |
| 3 | ~20:53 | SOURCE: `src/blake2_generator.cpp`, `src/blake2_generator.hpp` | SOURCE (blocked) | Blocked on A10a/A5 (spec 3.5 + 6): which generator calls exist and their byte order / refill rule. Answers recorded as gaps G6-01..G6-16 and V-09 (report section 4.3). |
| 4 | ~20:53 | SOURCE: `src/superscalar.cpp` (generator: decoder buffers, macro-op table, instruction selection, port scheduling, operand selection, address register), `src/superscalar_program.hpp` | SOURCE (blocked) | Blocked on A10a-j (spec chapter 6 does not define the random draws, the scheduling search, operand-selection retry/drop rules, termination, or the address register). Chapter 6 cannot be implemented bit-exactly without it; every rule taken from the source is listed as G6-01..G6-16 (report section 4.3) and tagged [SRC] in impl/rx_superscalar.py. |
| 5 | ~20:55 | SOURCE: `grep SuperscalarMaxSize\|RegisterNeedsDisplacement\|isZeroOrPowerOf2` over src/*.{hpp,h,cpp}; `src/superscalar_program.hpp` | SOURCE (blocked) | Answered: SuperscalarMaxSize = 3*RANDOMX_SUPERSCALAR_LATENCY+2 (spec 6.3 states the same limit); r5 = "RegisterNeedsDisplacement". Incidental exposure: the grep also printed single lines of bytecode_machine.cpp (IADD_RS r5 test, IMUL_RCP power-of-2 test), instruction.cpp, assembly_generator_x86.cpp and jit_compiler_a64.cpp; they only restate rules already in spec 5.2.1/5.2.6. |
| 6 | ~21:05 | round-1 reports: `reports/agentF-superscalar-argon2.md` finding F2 (lines 18-60) and a grep of `reports/agentK-spec-coverage.md` for "superscalar/chapter 6/F2" | other agents' reports (not source) | Read AFTER my chapter-6 implementation matched the reference (items 12/12) and after the first VM hashes matched, to compare my gap list with F2. The agentK grep incidentally showed K-S1 (v2 step 10 wording, with a code reference); no effect on my implementation, which was already complete. |
| 7 | ~21:13 | `git -C /home/user/RandomX show a901d5b:doc/specs.md` | master specification (permitted, brief step 5) | master vs branch comparison, logs/specs_master_vs_branch.diff |

No other file under src/ (in particular no VM, JIT, bytecode, dataset, AES, Argon2 or test source) was read.
The VM (chapters 2, 4, 5), the generators (chapter 3), the Cache (7.1) and the Dataset item construction (7.3
except step 7) were implemented from the specification alone.
