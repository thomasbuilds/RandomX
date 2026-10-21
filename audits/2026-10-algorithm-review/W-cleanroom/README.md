# Track W: clean-room implementation of RandomX v1 and v2 from doc/specs.md

Report: [`../reports/agentW-cleanroom.md`](../reports/agentW-cleanroom.md), findings W1 to W8. Section 8 of the report
lists the commands. Its Appendix A pseudocode, without the last line, is the block now in `doc/specs.md` 6.4. The
Python implementation is compared with the reference library through its public API only (`ref/refvals.c`).

## Files

| file | what it does | report section |
|---|---|---|
| `impl/rx_primitives.py` | AES rounds, AesGenerator1R/4R, AesHash1R, BlakeGenerator, chapter-3 constants (spec 3.1-3.5); self-test when run. | 2 (stage 0) |
| `impl/rx_argon2.py` | Argon2d Cache (spec 7.1, numpy): `selftest` (RFC 9106 vector), `cache <key> <out.bin>`. | 2 (stage 1) |
| `impl/rx_superscalar.py` | SuperscalarHash generator and Dataset items (spec 6, 7.2, 7.3); rules taken from the source are tagged `[SRC]`. | 2 (stages 2-3), 4.1 |
| `impl/rx_vm.py` | The VM (spec 2, 4, 5), each program compiled to a Python function; commitment. | 2 (stage 4) |
| `impl/rx_interp.py` | Independent plain interpreter, used only by `xcheck.py`. | 2 |
| `impl/test_items.py` | 12 Dataset items against `ref/items_*.txt`. | 2 (stage 3) |
| `impl/xcheck.py` | `rx_vm.py` against `rx_interp.py` on 18 programs. | 2 |
| `impl/run_hashes.py` | Light-mode hashes for 4 keys x 6 inputs x v1/v2 against `ref/hashes.txt`; options `cbranch=dynamic`, `fpmem_l3=1`, `layout=imm_first` for the alternative readings. | 2 (stage 4), 4.2 |
| `impl/random_check.py` | 10 random inputs x v1/v2 against `ref/hashes_random.txt`. | 2 (stage 4d) |
| `impl/k65_check.py` | 65-byte key against `ref/hash_k64.txt`. | 2 (stage 4c) |
| `impl/fpcheck.py` | CPython arithmetic follows the `fesetround` mode. | 3 |
| `impl/ss_sensitivity.py` | Each source-only SuperscalarHash rule replaced by its spec-only reading. | 4.1 |
| `impl/run_variants.sh` | Hashes under the alternative readings V-01, V-03, V-04. | 4.2 |
| `impl/jumpstats.py` | Frequency of CBRANCH taken twice in a row. | 4.2, 7 |
| `impl/ss_stats.py` | SuperscalarHash statistics (design.md 3.4). | 7 |
| `impl/appendixA_impl.py` | Second generator written only from the Appendix A pseudocode, compared with `rx_superscalar.py`. | Appendix A |
| `ref/refvals.c` | Reference values through `randomx.h` only: Cache dump, Dataset items, hashes, commitment. | 2 |
| `ref/make_refs.sh` | Writes `ref/hashes.txt`, `ref/items_testkey000.txt`, `ref/items_testkey001.txt`, `ref/commit.txt` and `ref/cache_testkey000.bin`. | 2 |
| `reproduce.sh` | Every command, in order. | 8 |

The Python scripts are run from `impl/` and read `../ref/` and `../data/`.

## Prerequisites

Linux x86-64 with glibc (`rx_vm.py` and `fpcheck.py` set the rounding mode through `fesetround` in `libm.so.6`, with
the x86-64 constants), python3 with numpy, gcc, and an unmodified build of the library. Disk: about 1.5 GiB for five
256 MiB Caches and the reference Cache dump. On the audit host a Cache took about 4 minutes and a light-mode hash 13-27 s.

## Build and run

From the repository root:

```
R=$(pwd)
mkdir -p build && (cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make randomx)   # unmodified library
cp -r audits/2026-10-algorithm-review/W-cleanroom /tmp/W && cd /tmp/W && mkdir -p data logs
gcc -O2 -o ref/refvals ref/refvals.c -I$R/src $R/build/librandomx.a -lstdc++ -lpthread -lm
(cd ref && bash make_refs.sh)
```

Then run the two reference commands written as comments in section 7b of `reproduce.sh` (they write
`ref/random_inputs.txt`, `ref/hashes_random.txt` and `ref/hash_k64.txt`), and finally:

```
RX_ROOT=$R bash reproduce.sh
```

`reproduce.sh` defaults to `RX_ROOT=../../..` (relative to its own directory) and `RX_BUILD=$RX_ROOT/build`. Its step 8
compares master's `doc/specs.md` (a901d5b) with the working tree's. The report's diff was taken against d3dd967; this
branch's `doc/specs.md` also carries the review's specification fixes (6b2a054), so compare with
`git -C $R show d3dd967:doc/specs.md` to reproduce it.

When this directory was assembled, the command above rebuilt `ref/refvals` byte for byte identical to the agent's,
and `../spec-6.4-check/` ran `rx_superscalar.py` and `appendixA_impl.py` from this directory.

The clean-room notes cited by the report are in `notes/`: `ambiguities-prereading.md` (every ambiguity recorded on a
first reading of the specification, before any code was written) and `gaps.md` (the final gap list). The consultation
log is reproduced in Appendix B of the report.

Not included: reference values and Caches (`ref/*.txt`, `*.bin`, `data/`), binaries and the other logs.
