// Agent C2 (item 5): design.md Appendix E claim - bits 3-53 of the 8 initial SuperscalarHash registers are unique
// over all registers of all Dataset items 0..34078718 (same check as src/tests/superscalar-init.cpp, but bucketed by
// the top bits of the reduced value so that memory stays at ~140 MB per pass instead of a 272M-entry hash set).
#include <cstdio>
#include <cstdint>
#include <vector>
#include <algorithm>
static constexpr uint64_t mul0 = 6364136223846793005ULL;
static constexpr uint64_t xc[8] = {0, 9298411001130361340ULL, 12065312585734608966ULL, 9306329213124626780ULL,
	5281919268842080866ULL, 10536153434571861004ULL, 3398623926847679864ULL, 9549104520008361294ULL};
int main(int argc, char** argv) {
	const uint64_t N = 34078719, mask = 0x3FFFFFFFFFFFF8ULL; // bits 3-53
	const int B = 16; // buckets by bits 50-53 of the reduced value
	uint64_t dups = 0, total = 0;
	// variant without the design's adjustments of the r1 and r3 constants (2^33+700 and 2^14), to show they matter
	for (int variant = 0; variant < 2; ++variant) {
		uint64_t c[8]; for (int q = 0; q < 8; ++q) c[q] = xc[q];
		if (variant == 1) { c[1] -= (1ULL << 33) + 700; c[3] -= (1ULL << 14); }
		dups = 0; total = 0;
		for (int b = 0; b < B; ++b) {
			std::vector<uint64_t> v; v.reserve(N * 8 / B + N / 4);
			for (uint64_t it = 0; it < N; ++it) {
				uint64_t r0 = (it + 1) * mul0;
				for (int q = 0; q < 8; ++q) { uint64_t x = (r0 ^ c[q]) & mask; if ((int)(x >> 50) == b) v.push_back(x); }
			}
			std::sort(v.begin(), v.end());
			for (size_t i = 1; i < v.size(); ++i) if (v[i] == v[i - 1]) dups++;
			total += v.size();
		}
		printf("%s: %llu register values (8 per item, %llu items), duplicate values of bits 3-53: %llu\n",
			variant == 0 ? "RandomX constants" : "constants without the 2^33+700 / 2^14 adjustments",
			(unsigned long long)total, (unsigned long long)N, (unsigned long long)dups);
	}
	return 0;
}
