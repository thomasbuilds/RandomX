// Agent S: pairwise byte dependence in the F block (Q1, v2 AES mix vs v1 XOR).
// usage: rxs_pairs <v2> <n> <seed> <out.bin>
// Records 2-D histograms (256x256) for byte pairs; see PAIRS below. Byte indices: F block bytes 0..63 (word w = byte/8,
// f_i' = bytes 16i..16i+15); "E" = bytes of e_i before the mix (16i..), "Fin" = bytes of f_i before the mix.
#include <cstdio>
#include "rxs_driver.hpp"
using namespace rxs;
using randomx::NativeRegisterFile;
struct Pair { int srcA, a, srcB, b; const char* name; }; // src: 0 = F block (after mix), 1 = E before mix, 2 = F before mix, 3 = R block
static const Pair PAIRS[] = {
	{0, 7, 0, 39, "fblk[7] (f0'.lo MSB) vs fblk[39] (f2'.lo MSB): same AESENC keys"},
	{0, 0, 0, 16, "fblk[0] (f0' byte0) vs fblk[16] (f1' byte0): enc vs dec"},
	{0, 7, 1, 55, "fblk[7] (f0'.lo MSB) vs e3.lo MSB (last round key)"},
	{0, 15, 1, 63, "fblk[15] (f0'.hi MSB) vs e3.hi MSB"},
	{0, 7, 2, 7, "fblk[7] (f0'.lo MSB) vs f0.lo MSB before mix"},
	{0, 6, 1, 6, "fblk[6] vs e0.lo byte6 (first round key)"},
	{0, 0, 3, 0, "fblk[0] vs R block byte0"},
	{0, 7, 0, 15, "fblk[7] vs fblk[15] (lo/hi MSB of f0')"},
};
constexpr int NP = sizeof(PAIRS) / sizeof(PAIRS[0]);
struct PH : Hooks {
	alignas(16) uint8_t pre[2][64]; // [0]=E bytes, [1]=F bytes before mix
	std::vector<uint64_t> hist;
	PH() : hist((size_t)NP * 65536, 0) {}
	void postExec(int k, int ic, NativeRegisterFile& nreg) override {
		for (int i = 0; i < 4; ++i) {
			rx_store_vec_f128((double*)(pre[0] + 16 * i), nreg.e[i]);
			rx_store_vec_f128((double*)(pre[1] + 16 * i), nreg.f[i]);
		}
	}
	void blocks(int k, int ic, const uint64_t* rb, const uint64_t* fb, uint32_t, uint32_t) override {
		const uint8_t* src[4] = { (const uint8_t*)fb, pre[0], pre[1], (const uint8_t*)rb };
		for (int p = 0; p < NP; ++p) {
			uint8_t x = src[PAIRS[p].srcA][PAIRS[p].a], y = src[PAIRS[p].srcB][PAIRS[p].b];
			hist[(size_t)p * 65536 + x * 256 + y]++;
		}
	}
};
int main(int argc, char** argv) {
	if (argc < 5) { fprintf(stderr, "usage: rxs_pairs v2 n seed out.bin\n"); return 1; }
	bool v2 = atoi(argv[1]); int n = atoi(argv[2]); uint64_t seed = strtoull(argv[3], 0, 0);
	randomx_cache* cache = makeCache("test key 000", 12);
	Driver d(cache, v2);
	resetCounters();
	PH h; d.hooks = &h;
	for (int i = 0; i < n; ++i) {
		uint64_t input[2] = { seed, (uint64_t)i };
		uint8_t out[32];
		d.hash(input, 16, out);
		if ((i + 1) % 50 == 0) fprintf(stderr, "pairs v%d %d/%d\n", v2 ? 2 : 1, i + 1, n);
	}
	FILE* f = fopen(argv[4], "wb"); fwrite(h.hist.data(), 8, h.hist.size(), f); fclose(f);
	FILE* t = fopen((std::string(argv[4]) + ".txt").c_str(), "w");
	for (int p = 0; p < NP; ++p) fprintf(t, "%d\t%s\n", p, PAIRS[p].name);
	fclose(t);
	return 0;
}
