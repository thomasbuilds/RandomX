#!/bin/bash
# Second batch of timing runs (agent T). All measurements on core 3; Dataset initialisation may use cores 0-3.
T=${T:-$(cd "$(dirname "$0")" && pwd)}
B=${RX_BUILD:-$T/../../../build}
cd $T
L=logs
# 1. fast/light/tiny without large pages (4 KiB pages everywhere), hardware AES, JIT
taskset -c 0-3 ./bin/tpart cfgs=fast,light,tiny nonces=40 lightnonces=8 reps=5 lp=0 softaes=0 > $L/tpart-hw-lp0.txt 2>&1
# 2. single-hash verification (non-batch API), light mode, JIT, hardware AES, large pages and small pages
taskset -c 3 ./bin/tbench mode=light jit=1 softaes=0 lp=1 batch=0 nonces=30 reps=7 warm=3 > $L/tbench-light-verify-lp1.txt 2>&1
taskset -c 3 ./bin/tbench mode=light jit=1 softaes=0 lp=0 batch=0 nonces=20 reps=5 warm=2 > $L/tbench-light-verify-lp0.txt 2>&1
taskset -c 3 ./bin/tbench mode=light jit=1 softaes=1 lp=1 batch=0 nonces=20 reps=5 warm=2 > $L/tbench-light-verify-sw-lp1.txt 2>&1
# 3. interpreter (portable VM + interpreted SuperscalarHash), large pages
taskset -c 3 ./bin/tbench mode=light jit=0 softaes=0 lp=1 batch=1 nonces=3 reps=3 warm=1 > $L/tbench-light-interp-lp1.txt 2>&1
taskset -c 0-3 ./bin/tbench mode=fast jit=0 softaes=0 lp=1 batch=1 nonces=20 reps=3 warm=2 init=4 > $L/tbench-fast-interp-lp1.txt 2>&1
# 4. official benchmark cross-check, light mode (1000 nonces gives the reference result)
for r in 1 2 3; do
  taskset -c 3 $B/randomx-benchmark --verify --jit --largePages --nonces 300 > $L/official-verify-v1-r$r.txt 2>&1
  taskset -c 3 $B/randomx-benchmark --verify --jit --largePages --nonces 300 --v2 > $L/official-verify-v2-r$r.txt 2>&1
done
echo done > $L/runs2.done
