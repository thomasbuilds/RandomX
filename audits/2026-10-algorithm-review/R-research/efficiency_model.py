#!/usr/bin/env python3
"""Agent R: reproducible arithmetic for the hardware-efficiency comparisons in agentR-research.md.

Every input below is copied from a cited source (URL in the comment). Nothing is measured here.
Run:  python3 -I efficiency_model.py
"""

# ---------------------------------------------------------------- inputs (sourced)
# CPU Hash/Joule from doc/design_v2.md (RandomX repo, branch HEAD d3dd967); power is CPU package power
# read with HWInfo64 or a fixed PPT/TDP estimate (https://gist.github.com/SChernykh/6058ecf01c929883b9d19c7eeadc8809),
# NOT wall power.  (name, v1 H/J, v2 H/J, v1 H/s, v2 H/s)
CPUS = [
    ("Ryzen 9 9950X @285W", 95.38, 94.01, 27186.1, 26791.7),
    ("Ryzen 9 9950X @100W", 199.122, 173.462, 19912.2, 17346.2),
    ("Ryzen AI 9 HX 370 @28W", 235.61, 254.35, 6597.15, 7121.69),
    ("Ryzen 9 7945HX @62W", 260.1, 246.9, 16126, 15308),
    ("Ryzen 9 5950X @~124W", 117.03, 105.78, 14745.9, 12905.3),
    ("Ryzen 9 3950X @131W", 114.88, 105.87, 15049.34, 13868.64),
    ("Core i9-12900K @125W", 69.16, 66.49, 8644.47, 8310.78),
    ("Core i7-8650U @15W", 122.08, 94.33, 1831.15, 1415),
]

# Specialised RandomX (v1) devices: vendor ratings at the wall, not independently measured.
#  X5:  212 KH/s, 1350 W  (Viperatech/EIN Presswire 2023-08-29 press release; asicminervalue listing)
#  X9:  1 MH/s, 2472 W, USD 5,600 (monero-project/monero#10270, 2025-12-30, quoting Bitmain listing)
#  R1X: 1.2 MH/s, 2055 W, ~USD 4,499-4,950 (pineconematches.com / minercompare.com vendor pages)
DEVICES = [
    ("Bitmain Antminer X5 (2023)", 212e3, 1350.0, None),
    ("Bitmain Antminer X9 (announced 2025-12)", 1000e3, 2472.0, 5600.0),
    ("Pinecone/INIBOX R1X (vendor)", 1200e3, 2055.0, 4499.0),
]

# GPUs, RandomX 1.1.0 (2019), board power as reported in SChernykh's READMEs
# https://raw.githubusercontent.com/SChernykh/RandomX_CUDA/master/README.md
# https://raw.githubusercontent.com/SChernykh/RandomX_OpenCL/master/README.md
GPUS = [
    ("Titan V", 2199, 125), ("Tesla V100", 2524, 177), ("RTX 2080 Ti", 1641, 242),
    ("GTX 1080 Ti", 1136, 190), ("Radeon VII", 1500, 150), ("Vega 64 (low clock)", 845, 115),
]

# v2 operation count per hash (PR #317 description): v1 4,456,448 ops, v2 6,815,744 ops
OPS_V1, OPS_V2 = 4194304 + 262144, 6291456 + 524288

# 9950X launch price USD 649 (AMD official, reported by thefpsreview.com 2024-08-07)
CPU_9950X_PRICE = 649.0

# ---------------------------------------------------------------- assumptions (stated, varied)
WALL_FACTORS = (1.2, 1.5, 2.0)    # CPU package -> wall power multiplier (PSU, RAM, board); assumption
ASIC_V2_FACTORS = (1.0, 0.80, OPS_V1 / OPS_V2)  # fraction of v1 hashrate an ASIC keeps under v2; 0.654 = purely
                                                 # compute-bound (sech1's "1 MH/s -> ~650 KH/s", meta#1330)


def jkh(hs, watts):
    return watts / (hs / 1000.0)


def main():
    print(f"v2/v1 ops per hash = {OPS_V2}/{OPS_V1} = {OPS_V2 / OPS_V1:.4f}; compute-bound v2 hashrate factor = {OPS_V1 / OPS_V2:.4f}")
    print("\nCPU efficiency (design_v2.md, package power): J/KH = 1000 / (H/J)")
    for name, hj1, hj2, _, _ in CPUS:
        print(f"  {name:26s} v1 {1000 / hj1:6.2f} J/KH   v2 {1000 / hj2:6.2f} J/KH   (v2/v1 hashes per joule {hj2 / hj1:6.1%})")
    best_v1 = max(c[1] for c in CPUS)
    best_v2 = max(c[2] for c in CPUS)
    print(f"  best CPU in table: v1 {1000 / best_v1:.2f} J/KH, v2 {1000 / best_v2:.2f} J/KH (package power)")

    print("\nSpecialised devices (vendor wall ratings, RandomX v1):")
    for name, hs, w, price in DEVICES:
        extra = f", USD {price / (hs / 1000):.2f} per KH/s" if price else ""
        print(f"  {name:40s} {jkh(hs, w):5.2f} J/KH{extra}")

    print("\nGPU (2019, board power): ")
    for name, hs, w in GPUS:
        print(f"  {name:22s} {hs / w:6.1f} H/J = {jkh(hs, w):6.1f} J/KH")

    print("\nDevice advantage over best CPU (CPU J/KH at the wall / device J/KH), sensitivity:")
    print("  rows: device; columns: CPU wall factor x ASIC v2 retention")
    for name, hs, w, _ in DEVICES:
        dev_v1 = jkh(hs, w)
        for wf in WALL_FACTORS:
            cpu_v1 = 1000 / best_v1 * wf
            line = f"  {name[:30]:30s} wall x{wf:.1f}: v1 {cpu_v1 / dev_v1:5.2f}x"
            for rf in ASIC_V2_FACTORS:
                cpu_v2 = 1000 / best_v2 * wf
                dev_v2 = dev_v1 / rf
                line += f" | v2 (keeps {rf:5.1%}) {cpu_v2 / dev_v2:5.2f}x"
            print(line)

    print("\ndesign_v2.md 'Relative work/Joule' reproduced as (v2 H/J / v1 H/J) x (ops_v2/ops_v1):")
    for name, hj1, hj2, _, _ in CPUS:
        print(f"  {name:26s} {hj2 / hj1 * OPS_V2 / OPS_V1:7.2%}")

    print("\ndesign_v2.md claim '9950X only 20-25% faster than 3950X in RandomX' vs its own v1 tables:")
    h9950_285, h9950_100, h3950 = CPUS[0][3], CPUS[1][3], CPUS[5][3]
    print(f"  9950X@285W / 3950X@131W = {h9950_285 / h3950:.3f};  9950X@100W / 3950X@131W = {h9950_100 / h3950:.3f}")

    print("\nProgram filtering by unsupported instruction (design.md App. A Strategy II, Q = share of usable programs):")
    print("  frequency f/256 from specs.md Table 5.x; program length L = 256 (v1) / 384 (v2); chain of 8 programs")
    for name, f in (("CFROUND", 1), ("IMULH_M", 1), ("FDIV_M", 4)):
        for ver, L in (("v1", 256), ("v2", 384)):
            q = (1 - f / 256) ** L
            print(f"  {name:8s} {ver}: Q = {q:.4f}, whole chain usable Q^8 = {q ** 8:.3e}")

    print("\nCapex per hashrate (vendor prices; CPU chip only, no board/RAM/PSU):")
    hs_9950 = CPUS[0][3]
    print(f"  Ryzen 9 9950X chip only: USD {CPU_9950X_PRICE / (hs_9950 / 1000):.2f} per KH/s (v1, 285 W PBO config, {hs_9950:.0f} H/s)")
    for name, hs, w, price in DEVICES:
        if price:
            print(f"  {name:40s} USD {price / (hs / 1000):.2f} per KH/s (v1); "
                  f"USD {price / (hs * OPS_V1 / OPS_V2 / 1000):.2f} per KH/s if compute-bound under v2")


if __name__ == "__main__":
    main()
