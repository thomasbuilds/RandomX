# Track S: statistical properties of the VM state

Report: `../reports/agentS-statistics.md`. Findings used in the review: ALG-16, ALG-17, ALG-20, ALG-27 to ALG-30.

## Files

| file | purpose |
|---|---|
| `instrumentation.diff` | counters added to `src/bytecode_machine.{hpp,cpp}` (FP results by class and rounding mode, CFROUND executions/writes/changes, CBRANCH runs, MXCSR flags, write coverage, last writer). Hashes are unchanged. |
| `harness/rxs_driver.hpp` | re-implementation of the loop of `vm_interpreted.cpp` (spec 4.6.2) with hooks after steps 3, 4 and 9-11; Dataset items from the real Cache (light mode) |
| `harness/rxs.cpp`, `harness/rxs_infl.cpp` | `rxs verify` (test vectors and 20 random inputs against the library), `rxs stats` (sections 4.1, 4.2, 4.4-4.6 of the report), `rxs infl` (4.3), `rxs diverge` (4.8) |
| `harness/rxs_absorb.cpp`, `rxs_pairs.cpp`, `rxs_debug.cpp` | FP absorption (4.3), byte-pair dependence (4.1), instruction-level trace of one absorbed flip |
| `harness/rxtime.cpp`, `speed.cpp` | runtime distribution (4.7); built against an unmodified library |
| `harness/ftz_check.c` | checks that an FTZ flush sets the MXCSR UE flag (4.4) |
| `harness/an_*.py`, `analytic.py` | analysis of the outputs (numpy, pandas) |
| `harness/compress.sh` | compressibility of dumped final Scratchpads (4.2); needs `xz` |

## Build and run

From the repository root, on x86-64 with AES-NI (the harness uses `-maes`):

```
mkdir -p /tmp/rxs && cp -r src CMakeLists.txt /tmp/rxs/
cd /tmp/rxs && patch -p1 < <repo>/audits/2026-10-algorithm-review/S-statistics/instrumentation.diff
mkdir build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make randomx && cd ..
H=<repo>/audits/2026-10-algorithm-review/S-statistics/harness
g++ -O2 -std=c++17 -maes -I src $H/rxs.cpp $H/rxs_infl.cpp build/librandomx.a -lpthread -o rxs
for t in rxs_pairs rxs_absorb rxs_debug; do g++ -O2 -std=c++17 -maes -I src $H/$t.cpp build/librandomx.a -lpthread -o $t; done
# rxtime: against an unmodified build of the library
g++ -O2 -std=c++17 -maes -I <repo>/src $H/rxtime.cpp <unmodified build>/librandomx.a -lpthread -o rxtime
./rxs verify
```

Experiments (all statistics use the key "test key 000"; the report lists the outputs they produce):

```
mkdir -p data
./rxs stats 0 3000 1 data/s_v1 48 ; ./rxs stats 1 3000 1 data/s_v2 48
./rxs infl 0 60 141 11 data/infl_v1.csv ; ./rxs infl 1 60 141 11 data/infl_v2.csv
./rxs_absorb 0 2000 3 data/absorb_v1.csv ; ./rxs_absorb 1 2000 3 data/absorb_v2.csv
./rxs_pairs 0 300 21 data/pairs_v1.bin ; ./rxs_pairs 1 300 21 data/pairs_v2.bin
./rxs diverge 200 5
./rxs_debug 9 1 2034 0 4
./rxtime fast 20000 2 7 data/time_fast.csv 1 2 ; ./rxtime fast 1500 7 8 data/time_fast7.csv 1 2 ; ./rxtime light 600 3 7 data/time_light.csv
$H/compress.sh data/s_v1 ; $H/compress.sh data/s_v2
python3 -I $H/an_stats.py data/s_v1 data/s_v2
python3 -I $H/an_fbits.py data/s_v1 data/s_v2
python3 -I $H/an_infl.py data/infl_v1.csv data/infl_v2.csv
python3 -I $H/an_absorb.py data/absorb_v1.csv data/absorb_v2.csv
python3 -I $H/an_pairs.py data/pairs_v1.bin data/pairs_v2.bin
python3 -I $H/an_time.py data/time_fast7.csv "fast mode"
python3 -I $H/an_intrinsic.py data/time_fast7.csv
python3 -I $H/analytic.py
```

All runs together wrote about 220 MB of data, including 96 Scratchpad dumps. The report's numbers come from these runs
on a 4-vCPU Intel Xeon (Cascade Lake, 2.8 GHz) VM; timing results (`rxtime`) depend on the host and were taken on
shared cores.
