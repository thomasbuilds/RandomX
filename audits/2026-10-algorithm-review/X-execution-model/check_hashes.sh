#!/bin/sh
# Cross-check every HASH line of the main and lean runs against the public API
cd "$(dirname "$0")"
for pair in "mainA 101 4" "mainB 202 4" "leanA 303 8" "leanB 404 8"; do
  set -- $pair
  taskset -c 0-2 nice -n 10 ./bin/refhash $2 $3 > out/ref_$1.txt
  python3 -I - "$1" <<'PY'
import sys, re
tag = sys.argv[1]
ref = {}
for l in open(f'out/ref_{tag}.txt'):
    f = l.split(); ref[(f[1], f[2])] = f[3]
ok = bad = 0
for l in open(f'out/{tag}.txt'):
    if l.startswith('HASH '):
        f = l.split()
        if ref.get((f[2], f[3])) == f[4]: ok += 1
        else: bad += 1
print(f'{tag}: {ok} hashes match the public API, {bad} mismatch')
PY
done
