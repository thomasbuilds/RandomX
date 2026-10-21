#!/usr/bin/env python3
"""Lead check of ALG-1: base values of agent T's Model C (light/fast area-time ratio per hash), by hand.

Assumptions (T's report, section 4.3): N = 16,384 iterations; program time T = 50 ns (v1), 75 ns (v2); DRAM latency
80 ns; VM instance 1.15 mm^2; DRAM area-time per random 64-byte read 2.7e-7 mm^2*s; item engine 0.2 mm^2 per item in
flight; 256 MiB on-chip Cache of 82 mm^2 shared by 300 mm^2 of logic. Period per iteration max(T, (T + L)/(k + 1)).
Fast:  AT = Avm * N * period(T, Ldram, k) + N * cdram
Light: AT = (Avm * N * period(T, Litem, k) + N * Ae * Litem) * (1 + Acache / Alogic)
Usage: python3 model_c_base.py
"""
N, Avm, cdram, Ae, Acache, Alogic, Ldram = 16384, 1.15, 2.7e-7, 0.2, 82.0, 300.0, 80e-9

def period(T, L, k):
    return max(T, (T + L) / (k + 1))

def ratio(v, Litem):
    k, T = (1, 50e-9) if v == 1 else (2, 75e-9)
    fast = Avm * N * period(T, Ldram, k) + N * cdram
    light = (Avm * N * period(T, Litem, k) + N * Ae * Litem) * (1 + Acache / Alogic)
    return light / fast

for name, L in (("S1: 1 GHz, 1 cycle per operation (design 3.4)", 746e-9), ("S3: 2 GHz, multiply 2 cycles", 445e-9),
                ("S4: 3 GHz, multiply 3 cycles", 364e-9)):
    print(f"{name}: item {L * 1e9:.0f} ns, light/fast AT v1 {ratio(1, L):.2f}, v2 {ratio(2, L):.2f}")
