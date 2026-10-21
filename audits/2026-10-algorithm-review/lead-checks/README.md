# Lead checks

Independent re-computations of the review's key numbers, written by the lead (not by the track agents). Each script is
self-contained (Python 3, standard library only) and runs in under a second.

| script | checks | expected output |
|---|---|---|
| `lookahead_check.py` | ALG-3: the steady-state time per iteration of the main loop is max(T, (T + L)/(k + 1)) | 75 cases (T, L, k), largest difference below 1e-9 ns |
| `model_c_base.py` | ALG-1: base values of the throughput model (Model C) from its closed form | light/fast AT v1 2.24, v2 1.66 (1 GHz logic); 1.38 / 1.03 (2 GHz); 1.15 / 0.86 (3 GHz) |
| `design_v2_hashj.py` | ALG-5: "Relative work/Joule" in `doc/design_v2.md` = Hash/Joule ratio x 1.5294 | 13 configurations reproduced to rounding; v2 has lower hash/J in 10 |

Run from the repository root:

```
python3 audits/2026-10-algorithm-review/lead-checks/lookahead_check.py
python3 audits/2026-10-algorithm-review/lead-checks/model_c_base.py
python3 audits/2026-10-algorithm-review/lead-checks/design_v2_hashj.py doc/design_v2.md
```

The fourth lead check, the SuperscalarHash generator text of `doc/specs.md` chapter 6.4 against the reference
generator, is in `../spec-6.4-check/`.
