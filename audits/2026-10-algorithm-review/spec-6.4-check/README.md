# Lead check: doc/specs.md 6.4 against the reference SuperscalarHash generator

Finding ALG-7 of the review: the SuperscalarHash generator was not specified by `doc/specs.md`; commit 6b2a054 adds
the normative procedure as chapter 6.4. This check shows that the procedure generates exactly the reference
library's programs. Three generators print the 8 programs (address register and instruction list) for each of the
keys `sshcmp-0` ... `sshcmp-<n-1>`:

| file | generator |
|---|---|
| `refdump.cpp` | the reference library (`superscalar.cpp`, `blake2_generator.cpp`) |
| `cleanroom_dump.py` | agent W's clean-room generator, `../W-cleanroom/impl/rx_superscalar.py` |
| `appendix_dump.py` | agent W's second generator, written only from the pseudocode in Appendix A of `../reports/agentW-cleanroom.md` (`../W-cleanroom/impl/appendixA_impl.py`) |

The pseudocode block of `doc/specs.md` 6.4 is the Appendix A block without its last line. To see this, extract the
first fenced block after each heading and compare (bash, from the repository root):

````
blk() { awk -v h="$1" 'index($0,h)==1{f=1} f&&/^```/{n++; if(n==2) exit; next} f&&n==1' "$2"; }
diff <(blk '### 6.4 ' doc/specs.md) <(blk '## Appendix A.' audits/2026-10-algorithm-review/reports/agentW-cleanroom.md)
````

The only difference printed is Appendix A's last line, "The 8 programs of 7.2 are generated in order from one
BlakeGenerator seeded with K (first 60 bytes)."

## Run

Prerequisites: g++, python3 (standard library only), an unmodified build of the library in `build/`. From the
repository root:

```
mkdir -p build && (cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make randomx)
cd audits/2026-10-algorithm-review/spec-6.4-check
g++ -O2 -std=c++17 -I../../../src refdump.cpp ../../../build/librandomx.a -lpthread -o /tmp/refdump
/tmp/refdump 150 | sed -E 's/^(IADD_C|IXOR_C)[789] /\1 /' > /tmp/ref.txt
python3 -I -B cleanroom_dump.py ../W-cleanroom/impl 150 > /tmp/clean.txt
python3 -I -B appendix_dump.py ../W-cleanroom/impl 150 > /tmp/appendix.txt
cmp /tmp/ref.txt /tmp/clean.txt && cmp /tmp/ref.txt /tmp/appendix.txt && echo identical
```

The `sed` step maps the reference's size-specific instruction names (`IADD_C7`/`8`/`9`, `IXOR_C7`/`8`/`9`) to the
names used by the specification (`IADD_C`, `IXOR_C`). `-B` keeps Python from writing bytecode caches into
`../W-cleanroom/impl/`.

Result (rerun when this directory was assembled): 150 keys, 1,200 programs, 536,212 instructions; the three outputs
are identical (537,412 lines, 9,639,556 bytes, MD5 `4ce5fd4d5388bd22d0a3b2f3803bc10c`). The reference dump takes
under a second, `cleanroom_dump.py` about 11 s and `appendix_dump.py` about 5 s on the audit host.
