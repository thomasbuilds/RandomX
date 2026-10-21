#!/bin/sh
# Main experiment: full model set, 2 processes x 4 hashes (v1 and v2 on the same inputs)
cd "$(dirname "$0")"
taskset -c 0-2 nice -n 10 ./bin/xsim --hashes 4 --seed 101 --tag mainA > out/mainA.txt 2> out/mainA.err &
taskset -c 0-2 nice -n 10 ./bin/xsim --hashes 4 --seed 202 --tag mainB > out/mainB.txt 2> out/mainB.err &
wait
