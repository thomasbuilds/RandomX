# Errata after the fact-check of 2026-10-22

The track reports in `reports/` are kept verbatim. On 2026-10-22 five independent verifiers re-checked the final
report and ALGORITHM_REVIEW_v2_2026-10.md against these reports and re-ran most of the evidence from this folder.
The following statements in the track reports were found wrong or imprecise; ALGORITHM_REVIEW_v2_2026-10.md and
AUDIT_v2_2026-10.md were corrected accordingly.

| report | statement | correction | how it was checked |
|---|---|---|---|
| agentT-tradeoffs.md (T-1, 4.3, 9) | "24-33 %" reduction / v2/v1 factor "between 0.67 and 0.76" across every parameter swept | 0.67-0.79 when one parameter is varied at a time (21-33 % lower); 0.54-0.91 over the full 29,160-point grid (9-46 %) | `model.py` section 3.1 prints 0.79 (Tprog1 30 ns), 0.78 (Ae 0.4), 0.77 (Avm 0.8); re-run byte-identical |
| agentT-tradeoffs.md (2.2, T-2) | R = 7.48 (v1) / 7.37 (v2) | one run; the repeat (2.8) gave 7.33 / 7.83, so about 7.3-7.8 for both versions; 8.6-9.9 on the newer host | T's own logs; re-measured on 2026-10-22 on a 2.1 GHz Xeon (the VM changed hardware) |
| agentT-tradeoffs.md (8) | option A: verification cost "about 0"; "Dataset init compute x1.5-2" | model values; measured on the newer host: +0 % (255), +7 % (340); Dataset initialisation wall time x1.02 / x1.15 (compute x1.5-2) | variant builds with only `configuration.h` changed, 7 rotated repetitions |
| agentT-tradeoffs.md (8) | option B x1.23 / x1.46 / x1.92 | the model prints 1.93 (test host) / 1.91 (desktop); measured x1.20 / x1.43 / x1.88 | as above |
| agentT-tradeoffs.md (T-6) | interpreter fast mode 1.85x slower in v2 | 3 runs on the test host with AES-NI; 1.41-1.48x on the newer host | `tbench`, 7 and 11 repetitions |
| agentS-statistics.md (S-2) | v2 erasure of a changed F register at the end of an iteration 0.92 % | 1.56 % [1.34, 1.81] pooled over 10,800 trials (S 11/1200, re-runs 27/1200 and 130/8400); E stays 0 | `rxs infl` with seeds 12 and 13 |
| agentS-statistics.md (4.8) | computing both hashes costs "about 97%" of the sum | about 98.4 % (107 us shared of 3.456 + 3.341 ms) | arithmetic |
| agentS-statistics.md (4.4) | per-iteration infinite-E interval 0.109 % [0.108, 0.110] | the interval ignores clustering by program; an independent sample gave 0.123 % (program-level figures unaffected) | `rxs stats`, new inputs |
| agentR-research.md (R-2) | vendors describe RISC-V cores "with DRAM"; X9 "discontinued in May 2026" | no vendor text found mentions DRAM; the X9 withdrawal rests on one reseller report (unverified) | Monero issue #10270 (Bitmain listing); vendor pages blocked by the proxy |
| agentR-research.md / review ALG-25 | Qubic "6- and 18-block reorganisations" | Qubic claimed a 6-block reorganisation and mined selfishly with reorganisations of up to 9 blocks; the 18-block reorganisation of September 2025 was not attributed | Monero issues #10064, research-lab #136 |
| agentR-research.md / review ALG-26 | "Monero's BLOCK_HASHING.md" | the file exists only on the PR #10038 branch (jeffro256/randomx_commitment) | raw.githubusercontent.com |
| review (all) | the commitment "added with v2" | in RandomX since 1.2.0 (2023, PR #265); Monero adopts it together with v2 | `git tag --contains 1c603a2` |
| agentX-execution-model.md | "core models for Zen 2-5" | Zen 2, 4 and 5, Skylake-SP and Golden Cove (no Zen 3) | `src/xsim.cpp` |

Re-runs that confirmed the reports (all from this folder): T's and X's models and traces (byte-identical outputs), the
lead checks, `spec-6.4-check` (1,200 programs), W's clean-room Python (Cache byte-identical, 6/6 hashes, 8/8 items,
3/3 commitments), S's statistics on 3,000 new hashes per version, C2's Argon2 graph (433,544 and 430,654 compressions
per missing block for two keys) and the full-Dataset bit statistics with independent code (bit 450, z = -6.07).

Commit messages on the branch are not rewritten. Three of them carry statements that the reports now correct:
`2c1f5ec` says E-4 "leaked the 2 MiB mapping of every released scratchpad/cache" (only sizes that are not a multiple
of the huge page size leaked, and then the whole mapping; the default sizes with 2 MiB pages did not); `51807a3`
says "at most 275 of 512 literals observed" (over 3,000 keys; the maximum over 20,000 keys was 290); `4230131` says
the prefetch reduces "the light-mode tradeoff margin ... by about a quarter" (the light/fast cost ratio falls by
about a quarter, 2.24 to 1.66; its excess over 1 falls by almost half, 1.24 to 0.66). `2c1f5ec` also says B-1 is
"live on macOS/FreeBSD arm64": XNU leaves a 128-byte red zone below SP, so macOS is not affected; FreeBSD, NetBSD
and OpenBSD are (AUDIT_v2_2026-10.md section 8).
