#!/usr/bin/env python3
"""Extra alternatives for the report: eDRAM Cache area and modern DRAM density (uses model.py definitions)."""
import io, contextlib, runpy
with contextlib.redirect_stdout(io.StringIO()):
    g = runpy.run_path("model.py")
BASE, model_C, model_B = g["BASE"], g["model_C"], g["model_B"]
for lab, upd in (("Cache in eDRAM (~3x SRAM density, 30 mm^2)", dict(Acache=30.0)),
                 ("Cache in 16 nm SRAM (228 mm^2)", dict(Acache=228.0)),
                 ("modern DRAM density 0.3 Gb/mm^2 (Model B)", dict(dram_density=0.3))):
    p = dict(BASE); p.update(upd)
    print(f"{lab}: Model C v1 {model_C(p,1):.2f} v2 {model_C(p,2):.2f}; Model B v1 {model_B(p,1)[0]:.2f} v2 {model_B(p,2)[0]:.2f}")
