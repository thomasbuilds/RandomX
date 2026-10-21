#!/bin/bash
# Reference values from librandomx.a (public API only).  Run from ref/.
R="taskset -c 0-2 nice -n 10 ./refvals"
LONG=$(python3 -c "print('ab'*150)")
K60="RandomX example key with exactly sixty bytes of length ....."
for K in "test key 000" "test key 001" "" "$K60"; do
  for V in v1 v2; do
    $R hash "$K" $V "This is a test" "Lorem ipsum dolor sit amet" "sed do eiusmod tempor incididunt ut labore et dolore magna aliqua" -
    $R hashhex "$K" $V 0c0c8f9a8eb6052b4d6a0d1a4a7a3e3a00000000a3a5e1d4c1c8d5a6fa1a43a7c4a0bf5f6a28bc52a1e8f1ab64b7a9f28e8d9cf101 $LONG
  done
done > hashes.txt
$R cache "test key 000" cache_testkey000.bin
$R items "test key 000" 0 1 2 3 1000 12345 0x1000000 34078718 34078719 > items_testkey000.txt
$R items "test key 001" 0 1 777 34078718 > items_testkey001.txt
{ echo "commit input=5468697320697320612074657374 hash=v1(test key 000) -> $(./refvals commit 5468697320697320612074657374 639183aae1bf4c9a35884cb46b09cad9175f04efd7684e7262a0ac1c2f0b4e3f)"
  echo "commit input=5468697320697320612074657374 hash=v2(test key 000) -> $(./refvals commit 5468697320697320612074657374 22ec6b861b3eb23686b2efbad69513c967ecfce80983df66c9c5b4fbfb4cdb6f)"
  echo "commit input=(empty) hash=v2(test key 000) -> $(./refvals commit '' cf43f7dfbabedac8df9b52f2a5ea5a2dc36adceaf08975db8f409d5f6baea8e4)"; } > commit.txt
