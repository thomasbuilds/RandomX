#!/bin/bash
# Agent W (clean-room RandomX from doc/specs.md) - reproduces every number in reports/agentW-cleanroom.md.
# Heavy steps run on cores 0-2 only (core 3 is reserved).  Approximate wall times on the audit host in brackets.
set -e
W=${W:-$(cd "$(dirname "$0")" && pwd)}
RX_ROOT=${RX_ROOT:-$W/../../..}; RX_BUILD=${RX_BUILD:-$RX_ROOT/build}
T="taskset -c 0-2 nice -n 10"
cd $W

# 0. Reference values through the public API only (ref/refvals.c, linked against the lead's librandomx.a)
gcc -O2 -o ref/refvals ref/refvals.c -I$RX_ROOT/src $RX_BUILD/librandomx.a -lstdc++ -lpthread -lm
# hashes: see the loop in this file's history (ref/hashes.txt: 4 keys x v1/v2 x 6 inputs); items; cache dump:
#   (cd ref && $T ./refvals cache "test key 000" cache_testkey000.bin)
#   (cd ref && $T ./refvals items "test key 000" 0 1 2 3 1000 12345 0x1000000 34078718 34078719 > items_testkey000.txt)
#   (cd ref && ./refvals commit <hex input> <hex hash>)

cd impl
# 1. self tests: AES (FIPS-197), chapter-3 constants vs Hash512 statements, Argon2d (RFC 9106 vector), 7.3 constants
python3 rx_primitives.py && python3 rx_argon2.py selftest && python3 rx_superscalar.py && python3 fpcheck.py
# 2. Cache (Argon2d, spec 7.1) for the four keys [~4 min each, 2 in parallel]
$T python3 rx_argon2.py cache "test key 000" ../data/cache_testkey000.bin
$T python3 rx_argon2.py cache "test key 001" ../data/cache_testkey001.bin
$T python3 rx_argon2.py cache "" ../data/cache_empty.bin
$T python3 rx_argon2.py cache "RandomX example key with exactly sixty bytes of length ....." ../data/cache_k60.bin
cmp ../data/cache_testkey000.bin ../ref/cache_testkey000.bin && echo "Cache identical"
# 3. SuperscalarHash + Dataset items vs reference [~10 s]
$T python3 test_items.py
# 4. generated VM code vs independent interpreter [~1 min]
$T python3 xcheck.py
# 5. light-mode hashes v1+v2, 4 keys x 6 inputs [~4 min per key]
for k in k000 k001 empty k60; do $T python3 run_hashes.py $k both > ../logs/hash_$k.log; done
# 6. commitment (formula Blake2b-256(input||hash) taken from the audit brief; not in the spec)
python3 -c "import rx_vm; print(rx_vm.commitment(b'This is a test', bytes.fromhex('639183aae1bf4c9a35884cb46b09cad9175f04efd7684e7262a0ac1c2f0b4e3f')).hex())"
# 7. ambiguity experiments
$T python3 ss_sensitivity.py > ../logs/ss_sensitivity.txt     # chapter 6 alternative readings [~1 min]
./run_variants.sh                                              # V-01, V-03, V-04 hashes [~6 min]
$T python3 jumpstats.py 30 > ../logs/jumpstats.txt             # CBRANCH twice-in-a-row frequency [~5 min]
$T python3 ss_stats.py 500 > ../logs/ss_stats.txt              # design.md 3.4 SuperscalarHash statistics [~40 s]
# 7b. extra coverage and checks
#   reference side (from ref/): random_inputs.txt -> hashes_random.txt; a 65-byte key -> hash_k64.txt
#     (cd ../ref && python3 -c "import random; r=random.Random(20261021); print(' '.join(bytes(r.randrange(256) for _ in range(r.randrange(0,201))).hex() for _ in range(10)))" > random_inputs.txt
#      && for V in v1 v2; do ./refvals hashhex "test key 000" $V $(cat random_inputs.txt); done > hashes_random.txt)
#     (cd ../ref && K="RandomX example key that is sixty-four bytes long, past the limit"; ./refvals hash "$K" v1 "This is a test" > hash_k64.txt; ./refvals hash "${K:0:60}" v1 "This is a test" >> hash_k64.txt)
$T python3 random_check.py > ../logs/hash_random.log                  # 20 hashes [~6 min]
$T python3 rx_argon2.py cache "RandomX example key that is sixty-four bytes long, past the limit" ../data/cache_k65.bin
$T python3 k65_check.py > ../logs/hash_k65.log
$T python3 appendixA_impl.py                                          # Appendix A completeness check [~20 s]
# 8. master vs branch specification
git -C $RX_ROOT show a901d5b:doc/specs.md > ../logs/specs_master_a901d5b.md
diff -u ../logs/specs_master_a901d5b.md $RX_ROOT/doc/specs.md > ../logs/specs_master_vs_branch.diff || true
