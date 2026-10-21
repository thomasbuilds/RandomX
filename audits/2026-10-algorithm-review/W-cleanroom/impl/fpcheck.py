"""Check that CPython float +,-,*,/ and math.sqrt honour the dynamic rounding mode set by libm fesetround."""
import ctypes, math, struct
libm = ctypes.CDLL("libm.so.6")
FE = {"nearest": 0x000, "down": 0x400, "up": 0x800, "zero": 0xc00}   # x86-64 glibc values
bits = lambda x: struct.unpack("<Q", struct.pack("<d", x))[0]
a, b = 1.0, 3.0
res = {}
for name, mode in FE.items():
    assert libm.fesetround(mode) == 0 and libm.fegetround() == mode
    x, y = float(a), float(b)          # avoid constant folding: values come from variables
    res[name] = (bits(x / y), bits(x + 1e-17), bits(-(x + 1e-17)), bits(y * 0.1), bits(math.sqrt(2.0 * x)),
                 bits(-x - 1e-17), bits(x - x))
libm.fesetround(0)
for k, v in res.items():
    print(k.ljust(8), " ".join("%016x" % t for t in v))
n, d, u, z = (res[k] for k in ("nearest", "down", "up", "zero"))
assert d[0] == z[0] and u[0] == d[0] + 1, "division"
assert u[1] == n[1] + 1 and d[1] == n[1], "addition"
assert u[4] == d[4] + 1, "sqrt"
assert d[6] == 0x8000000000000000 and n[6] == 0, "x - x = -0 in roundTowardNegative"
print("OK: +,-,*,/, sqrt and signed zero follow fesetround")
