# Track R: sourced research

Report: [`../reports/agentR-research.md`](../reports/agentR-research.md). Section 4 of the report lists these scripts;
its bibliography (section 5) gives the sources of every input.

| file | what it does | report section |
|---|---|---|
| `design_v2_tables.py` | Parses the benchmark tables of `doc/design_v2.md` and prints, per configuration, the v2/v1 ratios of hashrate and hashes per joule and the derived "Relative work/Joule" (hashes-per-joule ratio x 6,815,744 / 4,456,448). | 0 (R-1), 3 |
| `efficiency_model.py` | Arithmetic of the hardware-efficiency comparison: J/KH of the CPUs in design_v2.md (package power), of vendor-rated devices and of 2019 GPUs; device advantage over the best CPU for wall factors 1.2/1.5/2.0 and v2 retention 100/80/65.4 %; capital cost per KH/s; filtering probabilities. The inputs are copied from cited sources (URLs in the comments); nothing is measured. | 0 (R-1, R-2), 1 (Q3), 3 |

Prerequisite: python3 (standard library only). Run from the repository root:

```
python3 -I audits/2026-10-algorithm-review/R-research/design_v2_tables.py doc/design_v2.md
python3 -I audits/2026-10-algorithm-review/R-research/efficiency_model.py
```

`design_v2_tables.py` takes the path of `design_v2.md` as its argument (default `doc/design_v2.md`, relative to the
current directory). When this directory was assembled, both scripts ran in under a second and their output was
identical to the agent's saved output (13 configurations; hashes per joule lower under v2 in 10, higher in 3).

Not included: agent R's notes, the downloaded web pages and the text extracted from the 2019 audit PDFs.
