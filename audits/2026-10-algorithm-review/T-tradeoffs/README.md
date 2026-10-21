# Track T: time-memory tradeoffs, light mode and verification time

Report: [`../reports/agentT-tradeoffs.md`](../reports/agentT-tradeoffs.md), findings T-1 to T-8. Section 10 of the
report lists the original commands; the paths there are the audit's working directory, whose source files are
collected here.

## Files

| file | what it does | report section |
|---|---|---|
| `h/tpart.cpp` | Interleaved v1/v2 timing in one process: `fast`, `light`, partial Dataset (`p<fraction>`), VM only (`tiny`); a `t0` suffix selects `prefetcht0`. Needs the instrumented library. | 2.2, 2.3, 2.6-2.8, 5 (T-1, T-2, T-5 to T-7) |
| `h/tbench.cpp` | Interleaved v1/v2 timing through the public API: single-hash verification, interpreter. | 2.2, 7 (T-5, T-6) |
| `h/comp.cpp` | Component timings: Dataset item function, AES fill and hash, AesGenerator4R, Blake2b, JIT compile. | 2.4 |
| `h/opcount.cpp` | Operation counts from the library's generators (programs and SuperscalarHash), item critical paths. | 3 |
| `h/dramlat.c`, `h/pfk.c` | Random-read latency and memory-level parallelism; software prefetch k steps ahead. | 2.5 (T-8) |
| `analyze.py` | Medians, minima and paired v2/v1 ratios of the timing logs. | 2.2, 2.6-2.8 |
| `model.py` | Every model table: the design's model (design.md 1.4.1), Models B and C, energy, partial Dataset, options. The measured inputs are written into the script. | 4, 5, 6, 8 (T-1, T-2, T-4) |
| `lookahead_sim.py` | Event simulation of the main-loop dependencies against max(T, (T+L)/(k+1)). | 4.2 (T-3) |
| `alt_check.py` | eDRAM, 16 nm SRAM and DRAM-density alternatives; loads `model.py` from the current directory. | 4.3 |
| `runs1.sh`, `runs2.sh`, `runs3.sh` | The measurement commands as run; the build lines are in the header of `runs1.sh`. `runs3.sh` waits for `logs/runs2.done`. | 10 |
| `instrumentation.diff` | Hooks in the x86 JIT light-mode generator (`src/jit_compiler_x86.cpp`, `src/jit_compiler_x86_static.S`) used by `tpart`; not active in the `fast` and `light` configurations. `CMakeLists.txt` is unchanged. | 2.1 |
| `instrumentation-t0.diff` | Agent T's second tree (`rx-t0`, binary `tpart-t0`): the same hooks with `prefetcht0` written into the Dataset prefetch templates and without the runtime switch. No command in `runs1-3.sh` uses it, and the packaged `h/tpart.cpp` (which refers to `tpart_prefetch_t0`) links only against the `instrumentation.diff` build. The reported prefetcht0 comparison (`tpart-prefetch-t0.txt`) used that runtime switch (`cfgs=fast,fastt0,p1.0,p1.0t0,tiny`). | 2.5 |
| `patches/` | Agent T's original patches, kept for the `patch -p0` line of `runs1.sh` (run from this directory): `tpart-jit-hooks.diff` is the same change as `instrumentation.diff`; `t0-static-variant.diff` turns `rx-p` into `rx-t0`. | |

The diffs are against `src/` at d3dd967. Applied to a copy of `src/`, each reproduces agent T's tree exactly.

## Prerequisites

- x86-64 Linux with at least 4 cores: timings run on core 3 (`taskset -c 3`, or the harness pins itself after a
  Dataset initialisation on cores 0-3).
- g++/gcc with C++17 (gcc 13.3 was used), cmake, python3 (standard library only).
- AES-NI for the hardware-AES configurations (`softaes=1` selects software AES).
- 2 MiB huge pages for `lp=1` and `--largePages`; the audit host had 1200 (`sysctl -w vm.nr_hugepages=1200`). Fast
  mode holds the 2080 MiB Dataset and the 256 MiB Cache.

## Build

From the repository root:

```
R=$(pwd)
mkdir -p build && (cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make)   # unmodified library + randomx-benchmark
cp -r audits/2026-10-algorithm-review/T-tradeoffs /tmp/T && cd /tmp/T && mkdir -p bin logs
g++ -O2 -std=c++17 -fpermissive -I$R/src h/tbench.cpp $R/build/librandomx.a -lpthread -o bin/tbench
g++ -O2 -std=c++17 -maes -I$R/src h/comp.cpp $R/build/librandomx.a -lpthread -o bin/comp
g++ -O2 -std=c++17 -maes -I$R/src h/opcount.cpp $R/build/librandomx.a -lpthread -o bin/opcount
gcc -O2 h/dramlat.c -o bin/dramlat; gcc -O2 h/pfk.c -o bin/pfk
mkdir rx-p && cp -r $R/src $R/CMakeLists.txt rx-p/ && (cd rx-p && patch -p1 < ../instrumentation.diff)
mkdir build-p && (cd build-p && cmake ../rx-p -DCMAKE_BUILD_TYPE=Release && make randomx)
g++ -O2 -std=c++17 -I rx-p/src h/tpart.cpp build-p/librandomx.a -lpthread -o bin/tpart
```

When this directory was assembled, these commands (gcc 13.3, linked against the audit's library builds) rebuilt
`tbench`, `comp`, `opcount`, `dramlat`, `pfk` and `tpart` byte for byte identical to the binaries behind the report.

## Run

```
bash runs1.sh                      # dramlat, comp, opcount, tpart (HW AES, SW AES, prefetcht0), pfk, models, analysis
RX_BUILD=$R/build bash runs2.sh    # 4 KiB pages, single-hash verification, interpreter, randomx-benchmark --verify
RX_BUILD=$R/build bash runs3.sh    # main configuration again (11 reps), randomx-benchmark --mine
python3 -I analyze.py logs/tpart-hw-lp1.txt
```

The scripts write to their own directory (`T` overrides it). `RX_BUILD` (the build with `randomx-benchmark`)
defaults to `../../../build`, the repository's `build/` when the scripts are run in place.

The models need no measurements. From this directory: `python3 -I model.py`, `python3 -I lookahead_sim.py`,
`python3 -I alt_check.py`.

Not included: binaries, build directories, the instrumented trees (replaced by the diffs) and the raw logs cited in
the report.
