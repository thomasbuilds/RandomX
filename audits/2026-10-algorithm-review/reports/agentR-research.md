# Agent R: sourced research on RandomX's design security, the hardware landscape and the v2 proposal

Round 3 (algorithm-level review), track R. Date of research: 2026-10-21. Repository: /home/user/RandomX, branch
`claude/modest-meitner-dkvpgd` (HEAD d3dd967), read only.
Working files: /tmp/claude-0/-home-user-RandomX/cfc10d96-f1d5-53d0-817a-8712f1f55302/scratchpad/agentR/
(notes.md, efficiency_model.py and .out, design_v2_tables.py and .out, audits/*.txt, web/).

Method and access limits. I read the four 2019 audit PDFs in the repository (`audits/`, converted with `pdftotext`),
the RandomX authors' written responses to the audits, GitHub pull requests, issues, release pages and Monero meeting
logs (WebFetch of github.com pages; raw files with `curl` from raw.githubusercontent.com), and used WebSearch for
everything else. The proxy or the fetch tool could not reach ostif.org, reddit.com, bitcointalk.org, monerologs,
news sites, arxiv.org or blog.trailofbits.com. Claims taken only from WebSearch result text (page not opened) are
marked "(search)" in the bibliography. The GitHub API and MCP were limited to the fork, so upstream pull requests were
read as public web pages; the inline review comments of tevador/RandomX#317 were not retrievable ("Failed to load
comments").

---

## 0. Findings for the lead (severity per the common brief; confidence in brackets)

**R-1 Medium (confidence medium): v2's headline efficiency claim does not show improved hardware resistance.**
design_v2.md reports "massive efficiency improvements (in terms of VM+AES instructions per Joule)" [10]. That column
is, by construction, (v2 hashes per joule / v1 hashes per joule) x 1.5294, where 1.5294 = 6,815,744 / 4,456,448
operations per hash [28]; `design_v2_tables.py` reproduces all 13 published values. In plain hashes per joule, v2 is
lower in 10 of the 13 published configurations (75.6% to 98.6% of v1) and higher in 3 (100.8% to 109.2%). The "work"
metric equals the CPU's gain relative to an adversary whose energy per hash grows with the operation count (a purely
compute-bound design). That is the assumption behind sech1's forecast for Bitmain's X9 ("should drop from 1 MH/s to
~650 KH/s on v2") [34]. Nobody has shown it holds for real specialised hardware, which vendors describe as RISC-V cores
with DRAM [21][23]. My judgement: such hardware may be latency-bound like a CPU, and then v2 barely moves the
CPU/ASIC ratio.

**R-2 Medium, High if the claims are verified (confidence low, because the inputs are vendor claims).**
Vendors now claim better efficiency than CPUs and much lower capital cost per unit of hashrate:
- Antminer X5 (August 2023): 212 KH/s at 1,350 W, 6.37 J/KH [20][22].
- Antminer X9 (announced December 2025): 1 MH/s at 2,472 W, 2.47 J/KH, USD 5,600 [23].
- Pinecone R1X: 1.2 MH/s at 2,055 W, 1.71 J/KH [25].

For comparison, the best CPU in design_v2.md uses 3.84 J/KH measured at the CPU package, not at the wall [10][29],
and a Ryzen 9 9950X chip alone costs USD 23.87 per KH/s [26]. My modelled estimate, assuming a CPU
package-to-wall power factor of 1.2 to 2.0:

| Device | Energy advantage over the best CPU, v1 | Same, under v2 if the device is compute-bound |
|---|---|---|
| X9 | 1.9x to 3.1x | 1.25x to 2.1x |
| R1X | 2.7x to 4.5x | 1.8x to 3.0x |

Capital cost per KH/s is about 4x lower for the X9 than for the 9950X chip alone. I found no independent measurement,
teardown or network-share data for any of these devices. The X9 was reportedly discontinued in May 2026; that report
is unverified [24].

**R-3 Medium if the measurements confirm it (confidence medium that no public data exist).** No public measurement
exists of v2's light-mode verification time or of its fast/light ratio. Per hash, v2 still reads 16,384 Dataset items,
the same as v1 [11], but runs 52.9% more VM and AES operations [28]. That shifts the cost balance toward light mode,
and design.md 1.4.1 derives the 2080 MiB Dataset from a ratio of 8 [9]. The only published v2 verification number
predates the 384-instruction and prefetch changes: soft-AES light verification went from 15.28 to 17.12 ms (+12%) on a
Ryzen 3700X [27]. Agent T's measurements should settle this.

**R-4 Low, process (confidence high): no external review of the v2 design.** Asked in MRL on 2026-01-21 whether v2
needed an audit like v1, the only reply was "Changes are small and evolutionary" [34]. The prefetch change was
committed on 2026-01-23 and merged on 2026-02-17 [64][28], with no public rationale beyond "complements the program
size increase" [10] and benchmark gains [29].

**R-5 Low (confidence high): the commitment is consensus-critical in Monero v17 but is not specified.**
- `doc/specs.md` does not define the commitment, `Blake2b-256(input || hash)` (src/randomx.cpp:458-467), although
  Monero v17 computes difficulty from it [49][45][47].
- Its cheap pre-check only proves Blake2b work: about one minute on a high-end GPU per block at ~300G difficulty
  (tevador, 2022) [48].
- Forging a chain costs time linear in its length (SChernykh, 2026-10-16) [45]. Monero's BLOCK_HASHING.md wrongly says
  "exponentially harder" [47].

**R-6 Info (confidence high): the 2019 audit recommendations are implemented or explicitly accepted (Q1 and the
table in section 2).** Two remain open in spirit:
- No complete or machine-readable specification. A 2026 request says SuperscalarHash program generation cannot be
  reimplemented from specs.md alone; the maintainer replied that documenting every rule would "10x the docs" [55].
  The commitment is also missing from specs.md.
- X41's static-scheduling (VLIW) observation [2], which v2 does not address.

**R-7 Info (confidence high): design_v2.md's power basis and one internal inconsistency.**
- Its power figures are CPU package power (HWInfo64) or nominal PPT/TDP values, not wall power [29].
- Its claim that the 9950X is "only 20-25% faster" than the 3950X in RandomX contradicts its own tables, which show
  1.32x at 100 W and 1.81x at 285 W [10].

**R-8 Info: correction to the brief.** The Antminer X5 was announced in August 2023 [20][21]. Bitmain's 2025
announcement was the X9 [23].

---

## 1. Summary by question

### Q1. The 2019 audits: algorithm-level findings and their status

**Scope.** Four OSTIF-coordinated audits ran between May and August 2019 [8][7]:

| Auditor | Scope | Effort |
|---|---|---|
| Trail of Bits (ToB) | v1.0.2 | two person-weeks [1] |
| X41 D-SEC | commit e4b2270 | 30 days [2] |
| Kudelski | commit 3daceac | 6 person-days [3] |
| Quarkslab | v1.0.4 and later; also re-checked the earlier reports and the authors' replies | 32 days [4] |

**Fixed:**
- ToB-ARW-001 found that single AES rounds give too little diffusion and recommended at least two [1]. Program
  generation moved to 4 rounds (AesGenerator4R, release v1.0.3) [5][6].
- AesGenerator4R used the same keys for columns 0/2 and 1/3 (X41 RNDX-PT-19-105, Kudelski O-05). Fixed in v1.0.4
  with keys 0-3 and 4-7 [6][11].
- X41 found that 99.61% of branches are not taken, so no branch predictor is needed. CBRANCH frequency was raised:
  16/256 in the v1.0.2 frequency table [1], 25/256 today [11], and release v1.1.0 lists "Increase the frequency of
  CBRANCH" [6]. A branch predictor still cannot be used meaningfully (design.md 2.6.1) [9][5].
- ToB-ARW-003 found the configuration parameters brittle. Answered by the "Unsafe configurations" compile-time checks
  [12].
- Quarkslab's Argon2 bounds (M1, L1, L2), datasetOffset (M2), key length (M3) and alignment (L3) points were added to
  specs.md, configuration.md and static_asserts [11][12].
- Kudelski O-06: the "entropy" wording was corrected [9].

**Accepted with a rationale:**
- Single-round AesGenerator1R and AesHash1R stay, because more rounds add overhead and the Scratchpad is overwritten
  anyway [5][9]. Quarkslab agreed [4].
- AesHash1R and the generators are invertible and not cryptographic (X41-103/104, Kudelski O-01) [2][3][9].
- SuperscalarHash output bias (ToB appendix D): tevador calls full avalanche "an impossible task" [1][5].
- X41-101: static VLIW preprocessing yields 3.41x parallelism, so a "relatively simple, albeit large, VLIW CPU" and
  a 28 nm ASIC are plausible [2]. Reply: more dependency would hurt CPUs [5].
- Kudelski O-03: RandomX is outsourceable; non-outsourceability was not a goal [3].

**Not done:**
- Quarkslab N1 (configurable personalisation strings) and N2 (explicit warnings against reusing the AES functions)
  [4].
- Quarkslab's suggestion to derive four AesHash1R extra keys from Hash512 (master still uses two keys from Hash256)
  [4][11].
- ToB and X41's machine-readable specification [1][2].

### Q2. Independent analyses of hardware resistance and tradeoffs

**No peer-reviewed analysis.** I found none. The quantitative work is on GitHub:
- **Issue #11** (December 2018, a pre-release design that the author marks outdated): an HBM ASIC estimated at about
  9x a CPU's efficiency; SChernykh estimated 2-3x [13].
- **Issue #31** (2019): a single-chip light-mode ASIC estimated at 15 KH/s and 95 W, about 3x a CPU, as an upper
  bound. Linzhi replied that such power figures "can easily be off by >10x". Outcome: SuperscalarHash and doubled
  branch frequency [14].
- **X41's VLIW model** (see Q1) [2].

**Tradeoffs.**
- Scratchpad compression saves at most about 5% and is negated by random L3 writes (issue #65) [15].
- Light mode is "deliberately slow and energy burning" (issue #276) [16]. A configuration check guards the
  Cache/Dataset ratio [12].

**Program filtering.** The only quantitative analysis is the authors' own: chaining 8 programs leaves a "drop 25% of
programs" ASIC at 0.44x [9]. Quarkslab found no significant optimisation "even with approximations" [4]. By my
arithmetic, hardware lacking CFROUND could use only 3.3x10^-4 of 8-program chains in v1 and 6.0x10^-6 in v2.

**Key grinding (gap).** The README requires that the key not be miner-selectable, yet in Monero the key is a mined
block's hash [8]. I found no public analysis of key-block grinding.

### Q3. Specialised hardware and GPUs

**Specialised devices.**

| Device | Announced | Claimed performance | Notes |
|---|---|---|---|
| Antminer X5 | Pre-orders 2023-08-29 | 212 KH/s, 1,350 W, 6.37 J/KH [20][22] | Marketed as a RISC-V CPU design [21] |
| Antminer X9 | 2025-12 | 1 MH/s, 2,472 W [23] | "Custom RISC-V cores" [23]; reportedly discontinued May 2026 (unverified) [24] |
| R1X | — | 1.2 MH/s, 2,055 W [25] | Vendor claims only |

No teardown or independent measurement was found; CPU comparison in R-2.

**GPUs (RandomX 1.1.0, 2019, board power).** Titan V reached 2,199 H/s at 125 W, Tesla V100 2,524 H/s at 177 W and
Radeon VII 1,500 H/s at 150 W [18][19]. That is 6-18 H/J against 33-260 H/J for the CPUs in design_v2.md, at the
package [10]. I found no reliable modern GPU figures.

**Network share and outsourced hashrate.** No public data show how much of the network hashrate comes from
specialised hardware. Outsourced CPU hashrate did matter (Kudelski O-03): Qubic's incentivised pool claimed 51% in
August 2025, while other estimates put it at about 25-36%. A 6-block reorg on 2025-08-12 was attributed to Qubic, and
an 18-block reorg on 2025-09-14 was linked to it [57][58][59].

### Q4. The v2 proposal: rationale, concerns, resolution, adoption

**History.**
- 2023-09-08: tevador opens PR #274 with the conditional CFROUND and the AES mix [27]. In 2025 it was proposed for
  a "small" hard fork [30] but was unfinished (RISC-V code missing) [31].
- 2026-01-26: SChernykh opens #317, which adds the 384-instruction program (committed 2026-01-12) and the prefetch
  change (2026-01-23; "now part of the main design" by 2026-01-28 [35]). Merged 2026-02-17 [28][64].
- Releases: RandomX v2.0 on 2026-03-25 [6] (announced by @monero on 2026-03-30 [56]) and v2.0.1 on 2026-05-10 [6];
  XMRig v6.26.0 on 2026-03-28 [51].

**Rationale given for each change:**
1. CFROUND: v1 changes the rounding mode so often that it costs "up to 10%" of hashrate on Ryzen [10]. Measured
   +8.4% on Zen 1 and +4.9% to +7.9% on other AMD CPUs, about 0 on Intel (2023) [27].
2. AES mix: uses the time the CPU spends idle "waiting for scratchpad data", doubles the AES work "without hurting the
   hashrate" and puts AES inside the VM [27][10][34].
3. 384 instructions: DRAM latency has stayed at roughly 50-55 ns while cores sped up (an unsourced figure) [10]; "fix
   the bottlenecks in RandomX on modern CPUs" [34].
4. Prefetch: "complements the program size increase" [10]; it "increased a lot" hashrate and efficiency [29].

**Concerns raised and how they were resolved:**
- Soft-AES verification cost: +10-11% (tevador) [27]. Accepted. This matters for nodes without AES instructions;
  for example, the Raspberry Pi 4 reportedly lacks the ARMv8 crypto extensions [61].
- Several configurations lose 12-24% hashrate (Core i7-6820HQ -24%, Core i7-8650U -23%, Ryzen 7 3700X -15%) [10].
  Presented as an efficiency gain.
- Audit question: answered "small and evolutionary" [34].
- 5 of 10 v2 runs crashed on one community Ryzen 9 3950X. Unexplained [29].
- armv7: passes CI [27].
- Light-mode ratio and verification time: no public discussion found.
- Platform bugs found after release: invalid hashes "1 out of ~268 million" on ARM and RISC-V (also affecting v1)
  [6]; big-endian VSX (fixed in #345); RISC-V vector soft AES at VLEN 2048 or more (open; same as round-1 C-F6)
  [53][54][63].

**Adoption.**
- Monero's v17 PR (#10038) has implemented v2 support since February 2026 [38] but is still open. On 2026-09-16 sech1
  said it "hasn't received approvals yet because it does some controversial changes on top of RandomX v2" [42]. On
  2026-10-16 he requested changes, and the hard-fork-table entry is still a TODO [45].
- The same code, keeping the transaction count, was merged into the FCMP++ staging branch on 2026-09-23 and is in
  beta v3 [46][43][44]. P2Pool support was in progress in August 2026 [41] and its v5 branch was ready for testing in
  September 2026 [44].
- No mainnet height is set; the hard fork table still ends at v16 [52].
- Developers discussed a gap of at least 6 months after the first HF-ready binary and a code freeze of 2-3 months
  [40][39].
- jeffro256 (2025-11-26): the commitment is "the most important"; he is "neutral" on the VM changes [32].

### Q5. The commitment

**Origin.** tevador proposed it in November 2022 as a DoS mitigation [48]:
- Light verification takes about 15 ms on high-end machines and up to 1 s on a Raspberry Pi Zero.
- A Blake2b pre-check costs about 0.3 µs.

His 2023 Monero proposal [50]:
- calls the change "completely unrelated to ASIC resistance";
- lists light nodes and remote-node wallets as other uses;
- estimates about 8 MB per year of extra chain data.

**Implementation and use.** The commitment was merged in PR #265 on 2023-09-08 [49]. Monero's v17 integration makes
the commitment the consensus PoW hash (difficulty is checked against it) [49][46][53]. Storing the intermediate hash,
serving it over P2P and pre-verifying it before full verification are still TODO items in the master PR [45][47].
sech1 noted that the new data belongs in the block itself, not in the block template or hashing blob [37].

XMRig supports the commitment in Stratum; pools do not yet (July 2026) [40][62].

**Caveats.** Forging is linear Blake2b work, about one GPU-minute per block [48][45]. The commitment is not in
specs.md.

### Q6. Third-party evaluations of v2
- **Community benchmarks:** about 20 CPUs, 114-167% "work/J", package power [29].
- **Independent implementation:** DataHoarder's go-randomx supports v2 [33][36].
- **Automated (LLM) reviews of Monero's integration:** three low-severity platform issues, plus API and comment
  discrepancies [52][53][54].
- **A 2026 Springer review** discusses v2 conceptually; I saw only its abstract [60].

No independent hardware evaluation and no published v2 verification timings were found.

---

## 2. Publicly raised concerns about the design

| # | Concern | First raised (source) | Status in master (or elsewhere) |
|---|---|---|---|
| 1 | Single AES round gives too little diffusion | ToB-ARW-001, May 2019 [1] | Fixed for program generation (AesGenerator4R, v1.0.3) [6][11]. Accepted for Scratchpad fill and AesHash1R [5][9] |
| 2 | AesGenerator4R reuses keys across columns ("ECB-like") | Kudelski O-05; X41-105 [3][2] | Fixed in v1.0.4: keys 0-3 and 4-7 (specs 3.3) [6][11] |
| 3 | AesHash1R and the AES generators are invertible, not cryptographic | X41-103/104; Kudelski O-01 [2][3] | Accepted; rationale in design.md 3.1-3.3 [9] |
| 4 | Warn against reusing these functions elsewhere | Quarkslab N2 [4] | Partly: rationale documented, no explicit warning [9] |
| 5 | AesHash1R extra keys: derive 4 from Hash512 instead of 2 from Hash256 | Quarkslab 4.3.1/5.3.3 [4] | Not adopted (specs 3.4) [11] |
| 6 | SuperscalarHash output bias and tuned initial registers | ToB appendix D [1] | Accepted; authors' rationale [5] |
| 7 | Brittle, security-relevant configuration parameters | ToB-ARW-003 [1] | Fixed: "Unsafe configurations" compile-time checks (src/common.hpp:99-105) [12] |
| 8 | Insufficient testing; no machine-readable specification | ToB-ARW-002; X41-110 [1][2] | Tests added (v1.0.4) [6]. No machine-readable spec; SuperscalarHash generator not specified in full (#336) [55] |
| 9 | Branch predictor unused (99.61% of branches not taken) | X41-100 [2] | Accepted (design.md 2.6.1); CBRANCH 16 to 25/256 in v1.1.0 [9][6] |
| 10 | Static VLIW preprocessing replaces out-of-order logic (3.41x ILP) | X41-101 [2] | Open in spirit; v2 does not change it |
| 11 | No need for MMU, TLB or coherence | X41-102 [2] | Accepted (not useful in a PoW) [5] |
| 12 | Outsourceability | Kudelski O-03 [3] | Not a goal. Qubic 2025 reorgs [57][58] |
| 13 | Why both AES encryption and decryption are used | Kudelski O-04 [3] | Undocumented; v2 keeps the enc/dec alternation [11] |
| 14 | "Operations that preserve entropy" wording | Kudelski O-06 [3] | Fixed ("high output entropy") [9] |
| 15 | Argon2 salt of at least 8 bytes; lanes and memory bounds | Quarkslab M1, L1, L2 [4] | Fixed (configuration.md; common.hpp:41-45, 85) [12] |
| 16 | datasetOffset rule; mx/ma alignment; keys over 60 bytes | Quarkslab M2, L3, M3 [4] | Fixed in specs.md (lines 428, 335, 895) [11] |
| 17 | Make personalisation strings configurable | Quarkslab N1 [4] | Not adopted |
| 18 | RWX JIT pages | X41-107 [2] | Optional W^X (`--secure`, v1.1.0) [6] |
| 19 | Single-chip or light-mode ASIC estimates | #11, #31 (2018-19) [13][14] | Led to SuperscalarHash and more branches [14]; no newer public analysis |
| 20 | Soft-AES verification cost of the AES mix (+10-11%) | tevador, PR #274, 2023-09-11 [27] | Accepted |
| 21 | Older and low-power CPUs lose up to 24% hashrate under v2 | design_v2.md tables [10] | Accepted (framed as work per joule) |
| 22 | Does v2 need an external audit? | rucknium, MRL 2026-01-21 [34] | Answered "small and evolutionary"; no audit found |
| 23 | v2 crashes on one Ryzen 9 3950X (5 of 10 runs) | Fountain5405, gist, 2026-01-26 [29] | Unexplained |
| 24 | Light/fast ratio and verification time under the final v2; prefetch's effect on light-mode or ASIC designs | (no public source found) | Open; for agent T |
| 25 | Wrong v2 hashes on big-endian PPC64 with VSX | xmrack F1 [53][54] | Fixed (#345, c866c15) |
| 26 | RISC-V vector soft AES wrong at VLEN of 2048 or more | xmrack F2/F3 [53]; round-1 C-F6 [63] | Open; such hardware does not exist yet [54] |
| 27 | `RANDOMX_FLAG_V2` silently ignored by `randomx_alloc_cache` | xmrack #668 [52] | Documented in randomx.h:52 |
| 28 | Comment `;# modify "mx"` on a line that modifies `ma` | xmrack #668 [52] | Still present: src/asm/program_read_dataset_sshash_init_v2.inc:13 (comment only) |
| 29 | Forging intermediate PoW: "exponentially harder" claim; grinding cost is linear | SChernykh, #10038, 2026-10-16 [45] | Open in Monero's docs [47] |
| 30 | Commitment missing from specs.md | This review (grep of doc/) | Open |
| 31 | Removing the transaction count from the hashing blob (Monero side) | #10038; xmrack #668 [45][52] | Count re-added 2026-10-14 [45] |
| 32 | `MONERO_RANDOMX_UMASK` could silently create a v1 VM | selsta, 2026-08-20 [45] | Fixed (V2 bit stripped, with a warning) [52] |
| 33 | X9 under v2: ~650 KH/s, or "bricked" if its AES is a separate circuit | sech1, MRL 2026-01-21 [34] | Unverified forecast |

## 3. Design claims checked against sources

| Claim | Verdict |
|---|---|
| design.md 1.4.1: fast/light ratio of 8, so area x time is constant | v1 measured 9.25 on one thread (design.md App. C) [9]. The README's multi-thread table gives 3.4-6.6x [8]. The pre-release design of February 2019 (4 GiB Dataset) was about 20x (2.2 vs 50 ms) [17]. **Not re-validated for v2** (R-3) |
| design.md: GPUs are disadvantaged | Holds on 2019 data: 6-18 vs 33-260 H/J [18][19][10] |
| README: FPGAs are much less efficient | Consistent with X41 (FPGA uneconomic) [2]. No FPGA miners found |
| design.md: only 1 AES round needed for the Scratchpad | Accepted by all four audits [1][4][5] |
| design_v2 1: CFROUND costs up to 10% on Ryzen | Consistent (+4.9% to +8.4% AMD; ~0 Intel) [27] |
| design_v2 2: AES mix does not hurt hashrate | Holds with hardware AES (1004.2 vs 1003.8 H/s) [27]. Soft-AES verification +12% [27] |
| design_v2 3: 9950X only 20-25% faster than 3950X; DRAM latency 50-55 ns | **Contradicted by its own tables (1.32-1.81x)**. Latency figure unsourced [10] |
| design_v2 5: "massive efficiency improvements" | Arithmetic reproduced exactly. Hashes per joule fall in 10 of 13 configurations. Power is package power, not wall power (R-1, R-7) [10][29] |

## 4. Reproducibility

- `python3 -I .../agentR/design_v2_tables.py /home/user/RandomX/doc/design_v2.md` produces `design_v2_tables.out`
  (13 configurations; hashes per joule down in 10).
- `python3 -I .../agentR/efficiency_model.py` produces `efficiency_model.out` (J/KH, the modelled advantage with the
  wall factors 1.2/1.5/2.0 and v2 retention 100/80/65.4%, capital cost, filtering probabilities).
- Audit text: `pdftotext -layout /home/user/RandomX/audits/Report-*.pdf` into `agentR/audits/`.
- Authors' responses: `curl https://raw.githubusercontent.com/hyc/RandomxAudits/master/Comment-{TrailOfBits,X41,Quarkslab}.md`,
  saved in `agentR/web/audit-comments/`.
- X post dates were decoded from the snowflake IDs: `(id >> 22) + 1288834974657` ms.

## 5. Bibliography (accessed 2026-10-21)

1. Trail of Bits, RandomX Security Assessment, 2019-05-28. https://github.com/tevador/RandomX/blob/master/audits/Report-TrailOfBits.pdf
2. X41 D-SEC, RandomX Audit, 2019-07-10. https://github.com/tevador/RandomX/blob/master/audits/Report-X41.pdf ; https://x41-dsec.de/static/reports/X41-RandomX-Audit-2019-Final-Report-Public.pdf
3. Kudelski Security, RandomX Security Audit, 2019-07-02. https://github.com/tevador/RandomX/blob/master/audits/Report-Kudelski.pdf
4. Quarkslab, RandomX Security Audit, 2019-07-30. https://github.com/tevador/RandomX/blob/master/audits/Report-Quarkslab.pdf ; https://blog.quarkslab.com/security-audit-of-monero-randomx.html
5. RandomX authors' responses to the audits. https://github.com/hyc/RandomxAudits (Comment-TrailOfBits.md, Comment-X41.md, Comment-Quarkslab.md)
6. RandomX releases. https://github.com/tevador/RandomX/releases?page=2 (v1.0.3, v1.0.4, v1.1.0) ; https://github.com/tevador/RandomX/releases (v2.0, v2.0.1)
7. OSTIF audit summary (search; blocked). https://ostif.org/four-audits-of-randomx-for-monero-and-arweave-have-been-completed-results/
8. RandomX README. https://github.com/tevador/RandomX/blob/master/README.md
9. doc/design.md. https://github.com/tevador/RandomX/blob/master/doc/design.md
10. doc/design_v2.md. https://github.com/tevador/RandomX/blob/master/doc/design_v2.md
11. doc/specs.md (branch HEAD). https://github.com/tevador/RandomX/blob/master/doc/specs.md
12. doc/configuration.md and src/common.hpp. https://github.com/tevador/RandomX/blob/master/doc/configuration.md
13. tevador/RandomX#11, Possible ASIC design. https://github.com/tevador/RandomX/issues/11
14. tevador/RandomX#31, Single-chip ASIC design. https://github.com/tevador/RandomX/issues/31
15. tevador/RandomX#65, Scratchpad time-memory tradeoff. https://github.com/tevador/RandomX/issues/65
16. tevador/RandomX#276, Why fast mode? https://github.com/tevador/RandomX/issues/276
17. tevador/RandomX#19, RAM requirement. https://github.com/tevador/RandomX/issues/19
18. RandomX_CUDA README. https://github.com/SChernykh/RandomX_CUDA
19. RandomX_OpenCL README. https://github.com/SChernykh/RandomX_OpenCL
20. Viperatech pre-orders for Antminer X5, EIN Presswire, 2023-08-29 (search). https://www.einpresswire.com/article/652579512/viperatech-launches-preorders-for-bitmain-antminer-x5-monero-miner-paving-the-way-for-a-new-era-in-crypto-mining
21. Hiveon post on X5 RISC-V, 2023-09-07 (search). https://x.com/hiveonofficial/status/1699832059617849603
22. ASIC Miner Value, Antminer X5 (search). https://www.asicminervalue.com/miners/bitmain/antminer-x5
23. monero-project/monero#10270, Antminer X9, 2025-12-30. https://github.com/monero-project/monero/issues/10270
24. OneMiners, "Antminer X9: Dead or Delayed?" (search; reseller blog). https://oneminers.com/blogs/news/whatever-happened-to-the-antminer-x9-bitmain-monero-miner
25. Pinecone R1X vendor pages (search). https://pineconematches.com/product/pinecone-inibox-matches-r1x-xmr-edition/ ; https://minercompare.com/miner/pinecone-matches-inibox-r1x-xmr-edition-12mh
26. TheFPSReview, Ryzen 9 9950X at USD 649, 2024-08-07 (search). https://www.thefpsreview.com/2024/08/07/amd-confirms-pricing-for-ryzen-9000-series-processors-including-ryzen-9-9950x-for-649/
27. tevador/RandomX#274, RandomX v2 VM changes (2023-09-08 to 2026-01-27). https://github.com/tevador/RandomX/pull/274
28. tevador/RandomX#317, RandomX v2 (merged 2026-02-17). https://github.com/tevador/RandomX/pull/317
29. SChernykh, "RandomX v2 testing" gist (2026-01). https://gist.github.com/SChernykh/6058ecf01c929883b9d19c7eeadc8809
30. Monero Research Lab, 2025-03-05. https://github.com/monero-project/meta/issues/1167
31. MRL, 2025-04-30. https://github.com/monero-project/meta/issues/1197
32. MRL, 2025-11-26. https://github.com/monero-project/meta/issues/1303
33. MRL, 2025-12-10. https://github.com/monero-project/meta/issues/1310
34. MRL, 2026-01-21. https://github.com/monero-project/meta/issues/1330
35. MRL, 2026-01-28. https://github.com/monero-project/meta/issues/1333
36. Monero Community Workgroup, 2026-01-31. https://github.com/monero-project/meta/issues/1334
37. MRL, 2026-02-18. https://github.com/monero-project/meta/issues/1343
38. Tech meeting #158, 2026-02-23. https://github.com/monero-project/meta/issues/1344
39. MRL, 2026-07-22. https://github.com/monero-project/meta/issues/1426
40. Tech meeting #179, 2026-07-27. https://github.com/monero-project/meta/issues/1430
41. MRL, 2026-08-12. https://github.com/monero-project/meta/issues/1440
42. MRL, 2026-09-16. https://github.com/monero-project/meta/issues/1458
43. Tech meeting #186, 2026-09-21. https://github.com/monero-project/meta/issues/1459
44. MRL, 2026-09-23. https://github.com/monero-project/meta/issues/1464
45. monero-project/monero#10038, RandomX v2 and commitments in v17. https://github.com/monero-project/monero/pull/10038
46. seraphis-migration/monero#469, merged 2026-09-23. https://github.com/seraphis-migration/monero/pull/469
47. jeffro256, docs/BLOCK_HASHING.md. https://raw.githubusercontent.com/jeffro256/monero/randomx_commitment/docs/BLOCK_HASHING.md
48. tevador/RandomX#258, DoS mitigations (2022-11-06). https://github.com/tevador/RandomX/issues/258
49. tevador/RandomX#265, commitments (merged 2023-09-08). https://github.com/tevador/RandomX/pull/265
50. monero-project/monero#8827, improving PoW verification (2023-04-22). https://github.com/monero-project/monero/issues/8827
51. XMRig releases (v6.26.0, 2026-03-28). https://github.com/xmrig/xmrig/releases
52. xmrack/monero-review#668 (2026-09-19). https://github.com/xmrack/monero-review/issues/668
53. xmrack/monero-review#1146 (2026-10-14). https://github.com/xmrack/monero-review/issues/1146
54. tevador/RandomX#352, incorrect hashes on rare hardware. https://github.com/tevador/RandomX/issues/352
55. tevador/RandomX#336, SuperscalarHash generator not specified. https://github.com/tevador/RandomX/issues/336
56. @monero, "RandomX v2.0 has been released!", 2026-03-30 (search). https://x.com/monero/status/2038689736307482760
57. CoinDesk on Qubic, 2025-08-12 (search). https://www.coindesk.com/business/2025/08/12/monero-s-51-attack-problem-inside-qubic-s-controversial-network-takeover
58. Rucknium, Monero's 18-block reorg, 2025-09-26 (search). https://rucknium.me/posts/monero-18-block-reorg/
59. "Inside Qubic's Selfish Mining Campaign on Monero", arXiv 2512.01437 (search). https://arxiv.org/pdf/2512.01437
60. Charukesh J. and Devidas S., Discover Computing 29:461, 2026-07-24 (search; abstract only). https://link.springer.com/article/10.1007/s10791-026-10269-9
61. CNX Software, Raspberry Pi 4 lacks the ARMv8 crypto extensions, 2020-01-29 (search). https://www.cnx-software.com/2020/01/29/raspberry-pi-4-benchmarked-with-32-bit-and-64-bit-debian-os/
62. xmrig/xmrig#3775, commitment field in Stratum submit (cross-referenced from [49]). https://github.com/xmrig/xmrig/pull/3775
63. Rounds 1-2 audit report, C-F6. /home/user/RandomX/AUDIT_v2_2026-10.md
64. Local git history, `git log upstream/pr317` (1115ec9 2023-09-07; 4821dbf 2026-01-12; 4d3c4f2 2026-01-23). /home/user/RandomX
