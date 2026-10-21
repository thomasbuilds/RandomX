# Ambiguities recorded on first reading of doc/specs.md (branch d3dd967), BEFORE writing code or consulting src/

Written 20:50 UTC. Each entry: spec section, exact question, candidate interpretations.
Resolution status is filled in later in gaps.md (this file is the frozen pre-reading record).

A1. 5.1 / Fig 5.1 (image only, not reachable from this host: proxy 403 on i.imgur.com).
    Q: byte layout of the 64-bit instruction word (which bits are opcode, dst, src, mod, imm32)?
    Text gives only field names and implied widths: opcode 256 values (8 bits), dst/src "bits 0-2 used",
    mod 8 bits (Table 5.1.3), imm32 32 bits. Candidates: (a) bytes in section order
    opcode|dst|src|mod|imm32(LE) = bytes 0,1,2,3,4-7; (b) imm32 first then opcode..; (c) packed bit fields.
    Nothing in the text selects one.

A2. 5.1.1: Q: mapping of the 256 opcode values to the 29 instructions. Text gives only frequencies (counts) in
    Tables 5.2.1, 5.3.1, 5.4.1, 5.5.1. Candidates: (a) contiguous ranges in the order the instructions are listed
    in the tables 5.2.1 -> 5.3.1 -> 5.4.1 -> 5.5.1; (b) order of Table 5.1.1 groups (integer, fp, control, store)
    = same as (a); (c) any other order / interleaving. Not stated.

A3. 5.4.2 CBRANCH: "The target of the jump is the instruction following the instruction when register dst was last
    modified" + "At the beginning of each program iteration, all registers are considered to be unmodified."
    Q: is "last modified" determined statically (program order, once per program) or dynamically (execution
    order, reset each loop iteration)? They differ after a taken backward jump (dynamic: the CBRANCH itself
    becomes the last modifier, so a second jump in a row is impossible; static: target is fixed).
    design.md 2.6.2 ("Each CBRANCH instruction can jump up to twice in a row") supports static.

A4. 5.1.4 / Table 5.1.4: "it's a memory read and dst and src encode the same register" -> L3.
    Q: does this apply to FADD_M/FSUB_M/FDIV_M, where dst is an F/E register and src an R register with the same
    index? Candidates: (a) never for fp (different registers); (b) when dst field == src field (index compare).

A5. 3.5.2 BlakeGenerator: Q: byte order of the 4-byte output (little-endian assumed); whether leftover bytes are
    discarded when fewer than 4 remain (assumed yes: "not enough unused bytes left" -> rehash, then take 4).

A6. 3.2-3.4: Q: byte order of the hex key/state strings and of the 16-byte AES state (FIPS-197 column-major
    with byte i = s[i%4][i/4], strings in memory order). Checkable through "keys were generated as Hash512(..)".

A7. 4.3.1: Q: which 32-bit half goes to which fp half (low 32 bits -> low half assumed).

A8. 2 / 4.5: Q: the 128 configuration bytes precede the program bytes in the gen4 output (assumed, "split into two
    parts" lists configuration first).

A9. 7.1: Argon2 "output size 0": Q: is T=0 really hashed into H0 (as LE32(0))? Taken literally: yes.

A10. Chapter 6 (SuperscalarHash generator) - questions that the text does not answer:
  a. Random draws: which BlakeGenerator calls (1-byte or 4-byte) are made, in which order, and how each maps to
     a choice: decode group (random group 0-3), instruction choice within a slot (2 or 4 candidates), imm32 of
     IROR_C / IADD_C / IXOR_C / IMUL_RCP, mod.shift of IADD_RS, register choice among the available registers.
  b. How "imm32 % 64 != 0" (IROR_C) and "imm32 != 0, != 2^N" (IMUL_RCP) are enforced (rejection loop? remap?).
  c. Decode group selection: priority between the four bullet rules; meaning of "currently processed
     instruction" at a group boundary; what counts as a "multiplication" for the group-4 rule; "current
     decoding cycle" origin (0 or 1) and whether the comparison happens before or after counting.
  d. Instructions spanning groups: may the macro-ops of one instruction (IMULH_R: mov_rr,mul_r,mov_rr;
     IMUL_RCP: mov_ri,imul_rr) be split across decode groups? (6.3.1 bullets 1 and 3 imply yes but never say so.)
  e. Port scheduling: from which cycle does the search start; port order for P01 / P05 / P015 (only
     "P5 -> P0 -> P1" given); how a 2-uop macro-op (mul_r: P1+P5) is placed; is there a lookahead limit.
  f. Dependency vs port timing: is scheduleCycle max(port-free cycle, operand-ready cycle)? Which macro-op of
     a multi-macro-op instruction reads src / dst, and when dst becomes ready (latency of which macro-op).
  g. Operand selection: "available at scheduleCycle" vs "ready at the required cycle"; what happens when no
     register qualifies (retry next cycle? drop instruction? fill the slot with what?); exact role and scope of
     allowChainedMul; meaning of "last instruction applied to the register or its source must be different"
     (what is compared for IMULH_R / ISMULH_R, which have no meaningful "source group"?).
  h. Termination: is the instruction that reaches cycle >= RANDOMX_SUPERSCALAR_LATENCY kept or dropped; is the
     limit 3*170+2 = 512 counted in instructions or macro-ops; is the partially filled last decode group kept.
  i. 7.3 step 7 "the register that has the longest dependency chain": in which latency model (reference CPU
     cycles, or 1 cycle per instruction), and how ties are broken. Chapter 6 never defines it.
  j. Whether IMUL_RCP's reciprocal is computed with the same rule as 5.2.6 (assumed yes).

A11. 2 (steps): fprc is set once (step 6) outside the 7-10 loop, so it is NOT reset between programs. Stated by
     step numbering only; noted as a place where a reader could easily go wrong, not an ambiguity.

A12. Commitment (randomx_calculate_commitment in randomx.h): not defined in specs.md at all.

A13. Cosmetic: 1.1 says SuperscalarHash is "see chapter 7" (it is chapter 6); 4.5 has no 4.5.1 subsection;
     two subsections are numbered 5.2.9; "inifinite" typo in 5.4.2; Table 6.3.2 title duplicates Table 6.3.1's.
