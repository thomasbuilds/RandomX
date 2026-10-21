#!/bin/sh
# Distribution run: dataflow models + a few OoO configs (incl. Skylake-SP host model at 150/190 ns), 2 x 8 hashes
cd "$(dirname "$0")"
taskset -c 0-2 nice -n 10 ./bin/xsim2 --lean --hashes 8 --seed 303 --tag leanA > out/leanA.txt 2> out/leanA.err &
taskset -c 0-2 nice -n 10 ./bin/xsim2 --lean --hashes 8 --seed 404 --tag leanB > out/leanB.txt 2> out/leanB.err &
wait
