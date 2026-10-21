#!/bin/bash
# First batch of agent T runs (documentation of the exact commands used; re-runnable).
# Build (harnesses in h/, binaries in bin/):
#   LEAD=${RX_BUILD:-$RX_ROOT/build} (read-only optimised build of the branch; RX_ROOT = repository root)
#   g++ -O2 -std=c++17 -fpermissive -I$RX_ROOT/src h/tbench.cpp $LEAD/librandomx.a -lpthread -o bin/tbench
#   g++ -O2 -std=c++17 -maes -I$RX_ROOT/src h/comp.cpp $LEAD/librandomx.a -lpthread -o bin/comp
#   g++ -O2 -std=c++17 -maes -I$RX_ROOT/src h/opcount.cpp $LEAD/librandomx.a -lpthread -o bin/opcount
#   gcc -O2 h/dramlat.c -o bin/dramlat ; gcc -O2 h/pfk.c -o bin/pfk
#   private patched copy for tpart: git -C $RX_ROOT archive HEAD | tar -x -C rx-p ; patch -p0 < patches/tpart-jit-hooks.diff
#   (cmake rx-p -DCMAKE_BUILD_TYPE=Release in build-p; make randomx)
#   g++ -O2 -std=c++17 -I rx-p/src h/tpart.cpp build-p/librandomx.a -lpthread -o bin/tpart
T=${T:-$(cd "$(dirname "$0")" && pwd)}
cd $T
taskset -c 3 ./bin/dramlat 2048 1 1 3000000            # and the other sizes / chain counts in logs/dramlat-1.txt
for K in 1 2 4 8 16; do taskset -c 3 ./bin/dramlat 2048 1 $K 1000000; done
./bin/comp core=3 reps=5 lp=1 > logs/comp-1.txt         # pins itself to core 3
taskset -c 0-2 nice -n 10 ./bin/opcount progs=20000 keys=2000 > logs/opcount.txt
taskset -c 0-3 ./bin/tpart cfgs=fast,p1.0,p0.875,p0.75,p0.5,p0.25,light,tiny nonces=100 lightnonces=30 reps=7 lp=1 softaes=0 > logs/tpart-hw-lp1.txt
taskset -c 0-3 ./bin/tpart cfgs=fast,light,tiny nonces=50 lightnonces=12 reps=5 lp=1 softaes=1 > logs/tpart-sw-lp1.txt
taskset -c 0-3 ./bin/tpart cfgs=fast,fastt0,p1.0,p1.0t0,tiny nonces=100 reps=9 lp=1 softaes=0 > logs/tpart-prefetch-t0.txt
taskset -c 3 ./bin/pfk 2048 1 100 2000000; taskset -c 3 ./bin/pfk 2048 1 40 2000000     # logs/pfk-1.txt
taskset -c 3 ./bin/pfk 2048 0 100 1000000; taskset -c 3 ./bin/pfk 64 1 100 2000000; taskset -c 3 ./bin/pfk 2048 1 70 2000000
python3 -I lookahead_sim.py > logs/lookahead-sim.txt
python3 -I model.py > logs/model-out.md
python3 -I analyze.py logs/tpart-*.txt logs/tbench-*.txt > logs/analysis.md
