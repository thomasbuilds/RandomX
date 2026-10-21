# Track X: execution model, instruction-level parallelism and the specialised-hardware advantage

Report: [`../reports/agentX-execution-model.md`](../reports/agentX-execution-model.md), findings X-1 to X-10.
Section 11 of the report lists the original commands.

No instrumented copy of the sources is needed: the harness is new code that includes the library's internal headers
(`-I src`) and links an unmodified build.

## Files

| file | what it does | report section |
|---|---|---|
| `src/builder.hpp` | Copy of `InterpretedVm::execute` built from the library's own components. Emits a dependency-annotated micro-op trace that mirrors the x86-64 JIT code; collects dynamic statistics and Scratchpad liveness. | 3, 4, X-9 |
| `src/trace.hpp` | Micro-op trace format. | 3 |
| `src/analyze.hpp` | Latency models, dataflow critical paths, out-of-order and SMT core models. | 3, 5, 6 |
| `src/xsim.cpp` | Driver and list of model configurations; `--lean` selects the lean run's list. | 3 |
| `src/refhash.cpp` | Reference hashes through the public API (light-mode JIT VM). | 3 (validation) |
| `build.sh` | Builds `bin/xsim`; `OUT=xsim2` builds `bin/xsim2` (used by `run_lean.sh`). | |
| `run_main.sh` | Main run, 2 processes x 4 hashes (seeds 101, 202): `out/mainA.txt`, `out/mainB.txt`, about 12 min each on the audit host. | 4-7 |
| `run_lean.sh` | Lean run, `bin/xsim2 --lean`, 2 x 8 hashes (seeds 303, 404): `out/leanA.txt`, `out/leanB.txt`, about 8 min each. | 6 (lean table), tables marked "192 programs" |
| `check_hashes.sh` | Every traced hash against `bin/refhash`. | 3 |
| `run_appB.sh` | design.md Appendix B with the repository's `src/tests/perf-simulation.cpp`, v1 (256) and v2 (384 instructions). | X-7 |
| `analyze.py` | Dataflow, work per iteration, out-of-order models, branches and CFROUND, runtime distribution, liveness. | 4, 5, 6, 9, X-4 to X-6, X-9 |
| `energy.py` | Operation counts, energy per hash of a specialised design, sensitivity grid, gap change per CPU, area-time. | 8, X-1, X-2 |
| `memory.py` | Dataset-latency recurrence, its validation against the dataflow simulation, stall shares. | 7, X-2, X-3 |
| `filtering.py` | design.md Appendix A check, strategy III, correlation of runtime with program features. | 9, X-8 |
| `dist.py` | Distance between a Scratchpad write and its first read. | X-4, X-9 |

`energy.py`, `memory.py` and `filtering.py` import `analyze.py` from the current directory, so run them from this
directory.

## Prerequisites

x86-64 Linux with AES-NI and SSSE3 (`-maes -mssse3`), g++ with C++17 (gcc 13.3 was used), cmake, python3 (standard
library only). The run scripts use cores 0-2 (`taskset -c 0-2 nice -n 10`).

## Build and run

From the repository root:

```
R=$(pwd)
mkdir -p build && (cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make randomx)   # unmodified library
cp -r audits/2026-10-algorithm-review/X-execution-model /tmp/X && cd /tmp/X && mkdir -p out
RX_ROOT=$R sh build.sh && RX_ROOT=$R OUT=xsim2 sh build.sh
g++ -O2 -std=c++17 -I$R/src src/refhash.cpp $R/build/librandomx.a -lpthread -o bin/refhash
g++ -O2 -std=c++17 $R/src/tests/perf-simulation.cpp $R/build/librandomx.a -lpthread -o bin/perfsim384
g++ -O2 -std=c++17 -DRANDOMX_PROGRAM_SIZE=256 $R/src/tests/perf-simulation.cpp $R/build/librandomx.a -lpthread -o bin/perfsim256
taskset -c 0-2 nice -n 10 ./bin/xsim --hashes 2 --validate --no-ooo --tag val > out/validate.txt   # bit-exact check
sh run_main.sh
sh run_lean.sh
sh check_hashes.sh
sh run_appB.sh 2000 > out/appB.txt
python3 -I analyze.py out/mainA.txt out/mainB.txt > out/tables_main.md                             # 64 programs per version
python3 -I analyze.py out/mainA.txt out/mainB.txt out/leanA.txt out/leanB.txt > out/all_tables.md   # 192 programs
python3 -I analyze.py out/leanA.txt out/leanB.txt > out/lean_tables.md                             # 128 programs
for s in energy memory filtering dist; do
  python3 -I $s.py out/mainA.txt out/mainB.txt out/leanA.txt out/leanB.txt > out/${s}_all.md
done
```

`build.sh` defaults to `RX_ROOT=../../..` and `RX_BUILD=$RX_ROOT/build`, which fit when it is run in place.

Agent X did not record the build commands of `refhash`, `perfsim256` and `perfsim384`. When this directory was
assembled, the commands above (gcc 13.3, the audit's library build) gave binaries byte for byte identical to the
agent's, and `build.sh` gave `bin/xsim2` identically. The main run's `bin/xsim` was built from an earlier state of
`src/xsim.cpp` whose `--lean` list had only `z4r320` and `z5m75`; `run_main.sh` does not use `--lean`, and with that
list restored the packaged source gives the main-run binary byte for byte.

Not included: binaries, outputs (`out/`), and `lean_patch.py`, an editing script that is already applied to
`src/xsim.cpp`.
