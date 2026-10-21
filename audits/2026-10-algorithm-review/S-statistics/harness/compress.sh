#!/bin/bash
# Q2: compressibility of final Scratchpads (dumped by `rxs stats ... dumpSpads`).
# usage: compress.sh <prefix> (e.g. data/s_v1) ; writes <prefix>_xz.txt
set -e
P=$1
OUT=${P}_xz.txt
CAT=${P}_spads.cat
cat ${P}_spad*.bin > $CAT
N=$(stat -c %s $CAT)
echo "files: $(ls ${P}_spad*.bin | wc -l), bytes $N" > $OUT
# 1. design.md Appendix D equivalent: LZMA, maximum preset (xz -9e, defaults lc=3 lp=0 pb=2)
C1=$(xz -9e -T1 -c $CAT | wc -c)
echo "xz -9e: $C1 bytes, ratio $(python3 -c "print(f'{$C1/$N*100:.4f}%')")" >> $OUT
# 2. position-aware literal model (8-byte words: lp=3 pb=3, no previous-byte context)
C2=$(xz -T1 --lzma2=preset=9e,lc=0,lp=3,pb=3 -c $CAT | wc -c)
echo "xz --lzma2=preset=9e,lc=0,lp=3,pb=3: $C2 bytes, ratio $(python3 -c "print(f'{$C2/$N*100:.4f}%')")" >> $OUT
# 3. per-file xz -9e (each 2 MiB scratchpad alone), mean ratio
python3 - "$P" >> $OUT <<'EOF'
import sys, glob, subprocess
fs = sorted(glob.glob(sys.argv[1] + "_spad*.bin"))[:8]
r = []
for f in fs:
    d = open(f, "rb").read()
    c = subprocess.run(["xz", "-9e", "-T1", "-c", f], capture_output=True).stdout
    r.append(len(c) / len(d))
print(f"per-file xz -9e on {len(fs)} files: mean ratio {sum(r)/len(r)*100:.4f}% (min {min(r)*100:.4f}%, max {max(r)*100:.4f}%)")
EOF
rm -f $CAT
cat $OUT
