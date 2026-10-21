# Specification gaps found by the clean-room implementation (agent W)

(Working notes. The final table, with proposed replacement text, is section 4.3 of reports/agentW-cleanroom.md.)

Spec = doc/specs.md on branch d3dd967; "master" = a901d5b:doc/specs.md. Severity scale from brief-W:
blocking (impossible bit-exactly without the source), ambiguous (two plausible readings give different results),
misleading (the text says something the reference does not do), cosmetic.
"Evidence" names the experiment that shows the gap is load-bearing (logs/ss_sensitivity.txt, logs/variants.txt).
Where: B = branch, M = master.

## Chapter 6 (SuperscalarHash generator) - unchanged between master and branch; all entries are in M and B

| id | section | problem | severity | evidence |
|---|---|---|---|---|
| G6-01 | 6.3.1-6.3.4, 3.5 | The random-draw protocol is not specified at all: which BlakeGenerator call (1 or 4 bytes) each decision uses, how the value maps to the choice, and in which order the calls happen (decode group, instruction per slot, IADD_RS `mod`, IROR_C count, IADD_C/IXOR_C imm32, IMUL_RCP imm32, register choices). | blocking | draw_byte, group_u32, iror_u32, shift_byte: 16/16 programs change |
| G6-02 | 6.3.4 | Register choice: uniform `index = getUInt32() % n` over the qualifying registers in increasing index order; **no draw when n = 1**; failure when n = 0. | blocking | draw_single, draw_byte: 16/16 |
| G6-03 | Table 6.3.2 | Lists "IADD_C, IXOR_C" for 7/8/9-byte slots, but the draw maps 0 -> IXOR_C, 1 -> IADD_C (similarly the order of the other rows is the draw order, which the table does not say). | misleading | slot7_spec_order: 16/16 |
| G6-04 | 6.3.4 | IMULH_R / ISMULH_R draw a random 32-bit "group parameter" (getUInt32) used only by the repetition rule; consumes 4 generator bytes. | blocking | imulh_par_src: 16/16 |
| G6-05 | 6.3.4 | "The source operand (if needed) is selected first" is false for IMULH_R / ISMULH_R: the destination is chosen at their first macro-op (mov_rr), the source at the second (mul_r), possibly in the next decode group; dst may equal src. | misleading | imulh_src_first: 16/16 |
| G6-06 | 6.1, 6.2 | Which macro-op of a multi-macro-op instruction chooses dst, reads src and produces the result (IMULH: dst at mov_rr #1, src + result at mul_r; IMUL_RCP: dst + result at imul_rr), and that IMUL_RCP's imul_rr cannot start before mov_ri's result. | blocking | rcp_no_dep: 16/16 |
| G6-07 | 6.3.4 | "register cannot be multiplied twice in a row" is only IMUL_R after IMUL_R on the destination (IMULH_R, ISMULH_R, IMUL_RCP are not covered). | misleading | chain_all_mul: 16/16 |
| G6-08 | 6.3.4 | Repetition rule uses operation groups: ISUB_R is in IADD_RS's group; IADD_C (and IXOR_C) of all three sizes form one group; the compared parameter is the source register (ISUB_R, IXOR_R, IADD_RS, IMUL_R), -1 (immediates) or the random value of G6-04. | blocking | isub_own_group: 16/16 |
| G6-09 | 6.3.4 | Source rule special case: for IADD_RS, if exactly two registers are ready and one is r5, r5 is the source (no draw). | blocking | no_r5_rule: 15/16 |
| G6-10 | 6.3.4 | Stall/throw-away mechanics: if no register qualifies, retry at the next cycle up to 4 times, then throw the instruction away and choose a new one for the same slot (up to 256 consecutive times, then abandon the decode group); the cycle advance of the failed attempts is kept when the instruction is thrown away (undone when an attempt succeeds). | blocking | lookahead1, lookahead_inf, restore_cycle: 16/16 |
| G6-11 | 6.3.4 | `allowChainedMul` is exactly "the previous attempt in this slot was thrown away"; reset after the next successful operand selection. Text: "set to true if an attempt ... failed". | ambiguous | (part of G6-10) |
| G6-12 | 6.3.3 | Port search: starts at the current decode cycle (or the producer's result cycle for IMUL_RCP's imul_rr), scans forward to cycle 173 (170+4), per-cycle port order P5,P0,P1 restricted to the allowed ports; 2-uop macro-ops need both ports free in the same cycle; mov_rr takes no port; failure ends generation. | blocking | (all programs) |
| G6-13 | 6.3.1 | Decode-group bullets are a priority list (IMULH/ISMULH -> 5, then mulCount <= cycle -> 4, then IMUL_RCP -> 0/3, else random); "currently processed instruction" = the instruction whose macro-ops were being issued when the previous group ended (NOP at start); after IMUL_RCP an odd byte selects group 0, even group 3. | ambiguous | rcp_rule_first, rcp_group_rev: 16/16 |
| G6-14 | 6.3.1 | "multiplications" = IMUL_R, IMULH_R, ISMULH_R, IMUL_RCP, counted when the instruction completes; compared with the 0-based decode cycle (so group 0 is always 4-4-4-4). | ambiguous | mulcount_imul_only: 16/16 |
| G6-15 | 6.3 | Termination: a macro-op scheduled at cycle >= 170 sets the stop flag, but the current decode group is finished; an instruction completed in it is kept, an unfinished one is dropped. Also stops on port-search failure and after 170 decode groups (not listed). | ambiguous | drop_last: 16/16 |
| G6-16 | 7.3 step 7 (+6) | "register that has the longest dependency chain" is not defined: it is the register with the largest latency in a 1-cycle-per-instruction model (dst <- max(dst, src)+1, src ignored when src = dst or no src), ties to the lowest index. | blocking | addr_cpu_latency 11/16, addr_tie_high 8/16 |

All 21 alternative readings change Dataset item 0 for both test keys (logs/ss_sensitivity.txt).

## VM and other chapters

| id | section | problem | severity | where | evidence |
|---|---|---|---|---|---|
| V-01 | 5.1, Fig 5.1 | Byte layout of an instruction word is only in an external image (i.imgur.com, unreachable from this host). Text names fields and widths but not positions. Natural reading (fields in section order, one byte each, imm32 little-endian in bytes 4-7) matches the reference. | ambiguous (blocking without the figure and without test vectors) | M, B | all hashes match with this reading; layout=imm_first differs |
| V-02 | 5.1.1, Tables 5.2.1-5.5.1 | Opcode -> instruction mapping not stated; contiguous ranges in table order (IADD_RS 0-15, ..., ISTORE 240-255) matches. | ambiguous | M, B | all hashes match |
| V-03 | 5.4.2 | CBRANCH "last modified": static (program order) vs dynamic (execution order, reset each iteration). Differ only when a CBRANCH is taken twice in a row. Static matches; design.md 2.6.2 implies static. | ambiguous | M, B | logs/jumpstats.txt, logs/variants.txt |
| V-04 | 5.1.4, Table 5.1.4 | "src == dst (read) -> L3": does it apply to FADD_M/FSUB_M/FDIV_M (dst is F/E, src is R)? Reference: never. | ambiguous | M, B | logs/variants.txt |
| V-05 | 4.6.2 step 10 (v2) | Master: "AES encrypt of f0 with e0 as key" (full AES or one round? state byte order?). Fixed in branch. | ambiguous | M only | - |
| V-06 | 4.6.2 step 3 | Master does not say which 8-byte words load f0-f3 and e0-e3. Fixed in branch. | ambiguous | M only | - |
| V-07 | 4.3 | Master: "ma contains the address of the next Dataset read and mp contains the address of the next Dataset prefetch ... always aligned" - contradictory for v2 (mp = ma). Fixed in branch. | misleading | M only | - |
| V-08 | (none) | `randomx_calculate_commitment` (Blake2b-256(input \|\| hash)) is not in the spec. | blocking (for the commitment) | M, B | matches when taken from the brief |
| V-09 | 3.5.2 | Byte order of the 4-byte output (little-endian) and discarding of 1-3 leftover bytes not stated. | ambiguous (moot while ch. 6 is blocking) | M, B | consulted source |
| V-10 | 4.3.1 | Which int32 goes to which half (low 32 bits -> low half). | cosmetic (natural reading) | M, B | matches |
| V-11 | 2 | `fprc` persists across the 8 programs (only implied by the step numbering). | cosmetic | M, B | (implementer trap, see report) |
| V-12 | 7, randomx.h | Keys > 60 bytes "implementation-defined": the reference uses all bytes for Argon2 and the first 60 for the BlakeGenerator. | cosmetic | M, B | header text |
| V-13 | 7.1 | "output size 0" with Argon2: RFC 9106 requires T >= 4, so standard Argon2 libraries cannot be used; the literal reading (LE32(0) in H0, finalizer skipped) is correct. | cosmetic | M, B | Cache byte-identical |
| V-14 | various | 1.1 SuperscalarHash "see chapter 7" (is 6); no 4.5.1; two 5.2.9; "inifinite"; Table 6.3.2 title duplicates 6.3.1; "is it selected"; "memory write mod.cond" (missing "and"). | cosmetic | M, B | - |
