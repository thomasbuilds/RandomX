# RandomX algorithm-level review, October 2026: artifacts

These are the artifacts of the algorithm-level review in
[`/ALGORITHM_REVIEW_v2_2026-10.md`](../../ALGORITHM_REVIEW_v2_2026-10.md) (repository root) of RandomX at upstream
master `a901d5b` (branch base `d3dd967`; hashes unchanged). The numbers in the review and in the track reports are
reproduced by the scripts below. The heavy harnesses need an instrumented copy of the sources; the patches are
included as `instrumentation.diff` (against `src/` at `d3dd967`). They were run on a 4-vCPU Intel Xeon (Cascade Lake,
2.8 GHz, AES-NI, AVX-512) Firecracker VM with 2 MiB huge pages.

| track | directory | what it reproduces | entry point |
|---|---|---|---|
| T | [`T-tradeoffs/`](T-tradeoffs/) | Light/fast cost ratio, time-memory tradeoffs, partial Dataset, verification time, specialised-hardware cost and energy models (T-1 to T-8) | `runs1.sh` to `runs3.sh` (timings); `model.py`, `lookahead_sim.py` (models) |
| X | [`X-execution-model/`](X-execution-model/) | Trace-driven dataflow and core models of v1 and v2, energy model, design.md Appendices A and B (X-1 to X-10) | `build.sh`, `run_main.sh`, `run_lean.sh`, `analyze.py` |
| S | [`S-statistics/`](S-statistics/) | Statistical properties of the VM state | see `S-statistics/README.md` |
| C2 | [`C2-crypto/`](C2-crypto/) | Cryptographic and pseudo-random constructions: Argon2 reference graph, Scratchpad regeneration, F/E mix, Dataset statistics, commitment (C2-F1 to C2-F10) | `run-all.sh` |
| W | [`W-cleanroom/`](W-cleanroom/) | Python implementation of v1 and v2 from `doc/specs.md`, bit-exact against the library; specification gaps (W1 to W8) | `reproduce.sh` |
| R | [`R-research/`](R-research/) | Arithmetic on the tables of `doc/design_v2.md` and on published hardware figures (R-1, R-2) | `design_v2_tables.py`, `efficiency_model.py` |
| lead | [`spec-6.4-check/`](spec-6.4-check/) | `doc/specs.md` 6.4 against the reference SuperscalarHash generator: 1,200 programs, identical | `refdump.cpp`, `cleanroom_dump.py`, `appendix_dump.py` |
| lead | [`lead-checks/`](lead-checks/) | Independent recomputation of the lookahead bound, the Model C base values and the design_v2 "work/Joule" column | `lookahead_check.py`, `model_c_base.py`, `design_v2_hashj.py` |

`reports/` holds the track reports unchanged. Paths in them such as `/tmp/claude-0/.../agentT/` are the audit's
working directories: their source files are in the track directories here; raw data, logs and binaries were not kept.

Each track README gives build and run commands from the repository root. The unmodified library is built with
`mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make` (the configuration used, with gcc 13.3).
An instrumented copy is made with `cp -r src CMakeLists.txt <dir>/` and `patch -p1 < instrumentation.diff` inside
`<dir>`. Shell scripts that need the repository read `RX_ROOT` (repository root) and `RX_BUILD` (directory holding
`librandomx.a`), with defaults `../../..` and `../../../build` relative to their own directory. Copying a track
directory to a scratch location before running it keeps outputs out of the repository.
