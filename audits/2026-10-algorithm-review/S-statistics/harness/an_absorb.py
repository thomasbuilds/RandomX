"""P(perturbation of an F/E input bit is absorbed within the iteration), by bit of the 8-byte source word."""
import sys, os, pandas as pd
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from an_common import fmt_ci
for fn in sys.argv[1:]:
    df = pd.read_csv(fn)
    v = int(df.version.iloc[0])
    print(f"== v{v}: {fn}: {df.input.nunique()} inputs, {len(df)} injections")
    for c in ("F", "E"):
        s = df[df.cls == c]
        print(f"  {c}: overall absorbed {fmt_ci(int(s.absorbed.sum()), len(s))}")
        groups = ((0, 4), (4, 8), (8, 16), (16, 24), (24, 31), (31, 32), (32, 36), (36, 40), (40, 48), (48, 56), (56, 63), (63, 64))
        print("    by source-word bit group: " + "; ".join(f"{lo}-{hi-1}: {s[(s.bit>=lo)&(s.bit<hi)].absorbed.mean()*100:.2f}%" for lo, hi in groups))
