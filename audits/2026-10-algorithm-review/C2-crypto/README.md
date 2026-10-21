# Track C2: cryptographic and pseudo-random constructions

Report: [`../reports/agentC2-crypto-constructions.md`](../reports/agentC2-crypto-constructions.md), findings C2-F1 to
C2-F10. Section 6 of the report lists the artifacts; `run-all.sh` reruns every experiment.

## Files

| file | what it does | report section |
|---|---|---|
| `instrumentation.diff` | Inert hooks: new `src/instr_hooks.{hpp,cpp}`; hooks in `src/bytecode_machine.hpp` (memory reads, ISTORE), `src/vm_interpreted.cpp` (steps 2/3 reads, steps 9/11 writes, step-10 capture, iteration end) and `src/argon2_ref.c` (reference-index log); `CMakeLists.txt` adds `src/instr_hooks.cpp`. Hashes are unchanged; the hooks act only when a harness installs a tracker. | 2 |
| `run-all.sh` | Builds the instrumented library and all harnesses, then runs every experiment. | all |
| `h/vectors.cpp` | Reference vectors with and without a tracker; commitment against an independent Blake2b call. | header, 3.6 (C2-F4) |
| `h/keyenc.cpp` | Blake2Generator seed encoding (K and K with trailing zero bytes, 61-byte keys). | 3.1 (C2-F5) |
| `h/argon2graph.cpp` | Recomputation cost of missing Cache blocks on the real Argon2d reference graph; `noxor` counterfactual. | 3.2 (C2-F1) |
| `h/sptrack.cpp` | Scratchpad write coverage and reads of initial content; captures step-10 samples for `mixan`. | 3.3 (C2-F2) |
| `h/aesbattery.cpp` | Statistical battery for AesGenerator1R/4R output; AesHash1R lane structure. | 3.3 (C2-F9) |
| `h/mixan.cpp` | v2 F/E mix: recomputation, avalanche, key use, entropy of the written block. | 3.4 (C2-F3) |
| `h/fpattern.py` | F registers after step 10 classified as IEEE-754 doubles. | 3.4 (C2-F8) |
| `h/aescount.py` | AES rounds per hash, v1 and v2. | 3.4 |
| `h/dsstat.cpp` | All 34,078,719 Dataset items of one key: item statistics and Cache-line access distribution. | 3.5 (C2-F7) |
| `h/dsbias.cpp` | Explanation of the per-bit Dataset deviations. | 3.5 (C2-F10) |
| `h/sshconst.cpp` | Constant output bits of SuperscalarHash programs. | 3.5 (C2-F10) |
| `h/initunique.cpp` | design.md Appendix E: bits 3-53 of the initial registers are unique. | 3.5 |
| `h/chain.py` | design.md Appendix A arithmetic and feature-omission filtering. | 3.7 |
| `h/b2speed.cpp` | Indicative cost of one commitment call. | 3.6 (C2-F4) |

`instrumentation.diff` is against `src/` and `CMakeLists.txt` at d3dd967; applied to a copy of them it reproduces
agent C2's tree exactly.

## Prerequisites

x86-64 Linux with AES-NI and SSSE3 (`mixan` and `aesbattery` use `-maes -mssse3`), g++ with C++17 (gcc 13.3 was
used), cmake, python3 (standard library only). `dsstat` needs about 1.1 GB and `dsbias` about 2.3 GB of memory.
Heavy steps run under `taskset -c 0-2 nice -n 10`.

## Build and run

From the repository root:

```
cp -r audits/2026-10-algorithm-review/C2-crypto /tmp/C2
mkdir /tmp/C2/rx && cp -r src CMakeLists.txt /tmp/C2/rx/
(cd /tmp/C2/rx && patch -p1 < ../instrumentation.diff)
sh /tmp/C2/run-all.sh      # creates build-instr/, bin/, data/ and logs/ next to the script
```

`run-all.sh` builds the library in `build-instr/` (`cmake -DCMAKE_BUILD_TYPE=Release`, `make randomx`) and the
harnesses with `g++ -O2 -std=c++17`. When this directory was assembled, the harnesses rebuilt with these commands
against the audit's instrumented library were byte for byte identical to the agent's binaries, except `sptrack` and
`dsstat`, which had been linked before the last change to the instrumented library (the `argon2_ref.c` hook).

Not included: binaries, outputs (`data/`, `logs/`), and the downloaded third-party material agent C2 read for
C2-F1 (Argon2 specification versions, texts of the 2019 audits); no script reads it.
