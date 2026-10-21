#!/bin/bash
# Third batch (agent T): repeat of the main configuration with more repetitions, and the official benchmark in fast mode.
T=${T:-$(cd "$(dirname "$0")" && pwd)}
B=${RX_BUILD:-$T/../../../build}
cd $T
while [ ! -f logs/runs2.done ]; do sleep 5; done
taskset -c 0-3 ./bin/tpart cfgs=fast,light,tiny nonces=60 lightnonces=15 reps=11 lp=1 softaes=0 > logs/tpart-hw-lp1-r2.txt 2>&1
# official tool, fast mode: the mining thread is not pinned by the tool when --threads 1, so pin the whole process to core 3
taskset -c 3 $B/randomx-benchmark --mine --jit --largePages --init 4 --nonces 1000 > logs/official-mine-v1.txt 2>&1
taskset -c 3 $B/randomx-benchmark --mine --jit --largePages --init 4 --nonces 1000 --v2 > logs/official-mine-v2.txt 2>&1
echo done > logs/runs3.done
