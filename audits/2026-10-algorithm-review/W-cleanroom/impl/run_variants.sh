#!/bin/bash
# Ambiguity experiments: hashes under alternative readings (V-01 layout, V-03 CBRANCH, V-04 fp memory level).
# Run from impl/.  Output: ../logs/variants.txt (plus ../logs/jumpstats.txt from jumpstats.py)
T="taskset -c 0-2 nice -n 10"
{
  echo "### V-03 cbranch=dynamic, key test key 000, all 6 inputs, v1 and v2"
  $T python3 run_hashes.py k000 both cbranch=dynamic
  echo "### V-04 fpmem_l3=1 (L3 + imm32 when src index == dst index for FADD_M/FSUB_M/FDIV_M), first input"
  $T python3 run_hashes.py k000 v1 fpmem_l3=1 --only 0
  $T python3 run_hashes.py k000 v2 fpmem_l3=1 --only 0
  echo "### V-01 layout=imm_first (imm32 in bytes 0-3, opcode/dst/src/mod in bytes 4-7), first input"
  $T python3 run_hashes.py k000 v1 layout=imm_first --only 0
} > ../logs/variants.txt 2>&1
