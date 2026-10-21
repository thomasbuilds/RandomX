#!/bin/sh
# Agent C2: reproduce every number in reports/agentC2-crypto-constructions.md.
# Private instrumented copy of the branch sources: rx/ (= the repository's src + CMakeLists.txt at d3dd967) with
#   src/instr_hooks.{hpp,cpp} (new), hooks in src/bytecode_machine.hpp (memory reads, ISTORE), src/vm_interpreted.cpp
#   (steps 2/3 reads, steps 9/11 writes, step-10 capture, iteration end) and src/argon2_ref.c (reference-index log).
#   The hooks are inert unless a harness installs a tracker; hashes are unchanged (sptrack/mixan recompute the mix).
set -e
D=$(cd "$(dirname "$0")" && pwd)
RUN="taskset -c 0-2 nice -n 10"
mkdir -p "$D/build-instr" "$D/bin" "$D/data" "$D/logs"
(cd "$D/build-instr" && cmake -DCMAKE_BUILD_TYPE=Release ../rx >/dev/null && $RUN make -j3 randomx >/dev/null)
CXX="g++ -O2 -std=c++17 -I$D/rx/src"
LIB="$D/build-instr/librandomx.a -lpthread"
$CXX -o "$D/bin/sptrack" "$D/h/sptrack.cpp" $LIB
$CXX -maes -mssse3 -o "$D/bin/mixan" "$D/h/mixan.cpp" $LIB
$CXX -o "$D/bin/dsstat" "$D/h/dsstat.cpp" $LIB
$CXX -o "$D/bin/argon2graph" "$D/h/argon2graph.cpp" $LIB
$CXX -o "$D/bin/keyenc" "$D/h/keyenc.cpp" $LIB
$CXX -maes -mssse3 -o "$D/bin/aesbattery" "$D/h/aesbattery.cpp" $LIB
$CXX -o "$D/bin/vectors" "$D/h/vectors.cpp" $LIB
$CXX -o "$D/bin/b2speed" "$D/h/b2speed.cpp" $LIB
g++ -O2 -std=c++17 -o "$D/bin/initunique" "$D/h/initunique.cpp"
cd "$D/data"
# item 3 + 4: Scratchpad write coverage / initial-content reads, step-10 samples (light mode, interpreter, hard AES)
$RUN "$D/bin/sptrack" v1 60 7 "$D/data/sp-v1" 16
$RUN "$D/bin/sptrack" v2 60 7 "$D/data/sp-v2" 16
$RUN "$D/bin/mixan" v2 "$D/data/sp-v2.mix" 1500 > "$D/logs/mixan-v2.txt"
$RUN "$D/bin/mixan" v1 "$D/data/sp-v1.mix" > "$D/logs/mixan-v1.txt"
python3 -I "$D/h/fpattern.py" "$D/data/sp-v1.mix" "$D/data/sp-v2.mix" > "$D/logs/fpattern.txt"
# item 3: statistical battery and AesHash1R lane structure
$RUN "$D/bin/aesbattery" 64 2097152 > "$D/logs/aesbattery.txt"
# item 2: Argon2 reference graph of the RandomX Cache, partial-storage recomputation cost
$RUN "$D/bin/argon2graph" "test key 000" 200 > "$D/logs/argon2graph.txt"
# item 1: Blake2Generator seed encoding
$RUN "$D/bin/keyenc" > "$D/logs/keyenc.txt"
# item 5: all 34,078,719 Dataset items of key "test key 000" (3 threads, ~1.1 GB RSS)
$RUN "$D/bin/dsstat" "test key 000" 3 "$D/data/ds-full"
# item 7: Appendix A arithmetic and feature-omission filtering
python3 -I "$D/h/chain.py" > "$D/logs/chain.txt"
$RUN "$D/bin/vectors" > "$D/logs/vectors.txt"
$RUN "$D/bin/b2speed" > "$D/logs/b2speed.txt"  # indicative only (shared, loaded host)
$RUN "$D/bin/initunique" > "$D/logs/initunique.txt"
$RUN "$D/bin/argon2graph" "test key 000" 200 noxor > "$D/logs/argon2graph-noxor.txt"
# item 5: explanation of the per-bit Dataset deviations (full Dataset through the library's JIT initialiser, ~2.3 GB RSS)
$CXX -o "$D/bin/dsbias" "$D/h/dsbias.cpp" $LIB
$RUN "$D/bin/dsbias" "test key 000" 3000000 > "$D/logs/dsbias.txt"
# item 5: constant output bits of SuperscalarHash programs (500 keys x 8 programs, 1024 random inputs each)
$CXX -o "$D/bin/sshconst" "$D/h/sshconst.cpp" $LIB
$RUN "$D/bin/sshconst" 500 1024 > "$D/logs/sshconst.txt"
python3 -I "$D/h/aescount.py" > "$D/logs/aescount.txt"
