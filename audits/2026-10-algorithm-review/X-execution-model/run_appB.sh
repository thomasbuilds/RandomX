#!/bin/sh
# Reproduce design.md Appendix B with the repository's perf-simulation.cpp (v1 = 256, v2 = 384 instructions)
cd "$(dirname "$0")"
N=${1:-2000}
for V in 256 384; do
  i=0
  for cfg in "1 1 0 0" "1 1 0 1" "2 1 0 0" "2 1 0 1" "2 1 1 0" "2 1 1 1" "4 2 0 0" "4 2 0 1" "4 2 1 0" "4 2 1 1"; do
    set -- $cfg
    i=$((i+1))
    args="--executionPorts $1 --memoryPorts $2 --nonces $N"
    [ "$3" = 1 ] && args="$args --reorder"
    [ "$4" = 1 ] && args="$args --speculate"
    r=$(taskset -c 0-2 nice -n 10 ./bin/perfsim$V $args | sed 's/Execution took //; s/ cycles per program//')
    echo "size=$V design=$i exu=$1 mem=$2 ooo=$3 spec=$4 cycles=$r"
  done
done
