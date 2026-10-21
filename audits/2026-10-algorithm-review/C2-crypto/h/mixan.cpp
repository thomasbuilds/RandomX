// Agent C2: analysis of the step-10 F/E mix samples captured by sptrack (item 4).
// Record = Fpre[64] | E[64] | Fpost[64] (registers f0..f3 / e0..e3, 16 bytes each, little-endian byte image).
// Usage: mixan <v1|v2> <file.mix> [avalancheSamples]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <vector>
#include <string>
#include <random>
#include <map>
#include "intrin_portable.h"
#include "soft_aes.h"

struct Rec { uint8_t fpre[64], e[64], fpost[64]; };

static void mixV2(const uint8_t* fpre, const uint8_t* e, uint8_t* out) {
	rx_vec_i128 f[4], k[4];
	for (int i = 0; i < 4; ++i) { f[i] = rx_load_vec_i128((const rx_vec_i128*)(fpre + 16 * i)); k[i] = rx_load_vec_i128((const rx_vec_i128*)(e + 16 * i)); }
	for (int i = 0; i < 4; ++i) {
		f[0] = aesenc<true>(f[0], k[i]);
		f[1] = aesdec<true>(f[1], k[i]);
		f[2] = aesenc<true>(f[2], k[i]);
		f[3] = aesdec<true>(f[3], k[i]);
	}
	for (int i = 0; i < 4; ++i) rx_store_vec_i128((rx_vec_i128*)(out + 16 * i), f[i]);
}

static int hd(const uint8_t* a, const uint8_t* b, int n) { int s = 0; for (int i = 0; i < n; ++i) s += __builtin_popcount(a[i] ^ b[i]); return s; }
static double h2(double p) { return (p <= 0 || p >= 1) ? 0.0 : -(p * log2(p) + (1 - p) * log2(1 - p)); }

int main(int argc, char** argv) {
	if (argc < 3) { fprintf(stderr, "usage: mixan v1|v2 file.mix [avalancheSamples]\n"); return 1; }
	bool v2 = std::string(argv[1]) == "v2";
	FILE* f = fopen(argv[2], "rb");
	if (!f) { perror("open"); return 1; }
	std::vector<Rec> recs; Rec r;
	while (fread(&r, sizeof r, 1, f) == 1) recs.push_back(r);
	fclose(f);
	size_t n = recs.size();
	int avs = argc > 3 ? atoi(argv[3]) : 2000;
	printf("version %s samples %zu\n", v2 ? "v2" : "v1", n);

	// 1. verify the mix
	size_t bad = 0;
	for (auto& x : recs) {
		uint8_t o[64];
		if (v2) mixV2(x.fpre, x.e, o); else for (int i = 0; i < 64; ++i) o[i] = x.fpre[i] ^ x.e[i];
		if (memcmp(o, x.fpost, 64)) bad++;
	}
	printf("[1] recomputed step-10 output mismatches: %zu of %zu\n", bad, n);

	// 2. bit statistics of E (round keys) and of the written F block
	std::vector<uint64_t> onesE(512, 0), onesF(512, 0), onesFpre(512, 0);
	std::map<int, uint64_t> expHist; uint64_t infHalves = 0, halves = 0;
	for (auto& x : recs) {
		for (int b = 0; b < 512; ++b) {
			onesE[b] += (x.e[b / 8] >> (b % 8)) & 1;
			onesF[b] += (x.fpost[b / 8] >> (b % 8)) & 1;
			onesFpre[b] += (x.fpre[b / 8] >> (b % 8)) & 1;
		}
		for (int h = 0; h < 8; ++h) {
			uint64_t v; memcpy(&v, x.e + 8 * h, 8);
			int ex = (int)((v >> 52) & 0x7FF);
			expHist[ex]++; halves++;
			if (ex == 0x7FF) infHalves++;
		}
	}
	auto report = [&](const char* name, std::vector<uint64_t>& ones) {
		double hsum = 0, maxdev = 0; int fixed = 0; int worst = -1;
		double chi = 0;
		for (int b = 0; b < 512; ++b) {
			double p = (double)ones[b] / n;
			hsum += h2(p);
			if (ones[b] == 0 || ones[b] == n) fixed++;
			if (fabs(p - 0.5) > maxdev) { maxdev = fabs(p - 0.5); worst = b; }
			double z = (ones[b] - n / 2.0) / sqrt(n / 4.0); chi += z * z;
		}
		printf("    %-34s sum of per-bit entropies %.1f / 512, constant bits %d, max |P(1)-0.5| %.4f (bit %d), chi2(512 dof) %.1f\n", name, hsum, fixed, maxdev, worst, chi);
	};
	printf("[2] per-bit statistics (n = %zu; 1-sigma of P(1) = %.4f)\n", n, 0.5 / sqrt((double)n));
	report("E registers at step 10 (keys)", onesE);
	report("F registers before step 10", onesFpre);
	report(v2 ? "F block written (AES mix)" : "F block written (f xor e)", onesF);
	double hexp = 0; for (auto& kv : expHist) { double p = (double)kv.second / halves; hexp -= p * log2(p); }
	printf("    E exponent field: %zu distinct values, Shannon entropy %.2f bits of 11; +inf halves %.5f%%\n", expHist.size(), hexp, 100.0 * infHalves / halves);
	// E: per-register entropy upper bound (sum of per-bit entropies within each 128-bit register)
	for (int k = 0; k < 4; ++k) {
		double hs = 0; for (int b = 128 * k; b < 128 * k + 128; ++b) hs += h2((double)onesE[b] / n);
		printf("    e%d: sum of per-bit entropies %.1f / 128\n", k, hs);
	}

	if (!v2) return 0;

	// 3. avalanche of the v2 mix for real (F, E) states
	std::mt19937_64 rng(12345);
	size_t m = std::min<size_t>(avs, n);
	// F input bits: flip bit b of f_j, measure HD in f_j' (other lanes must stay equal)
	double sumF = 0; double minF = 1e9, maxF = 0; uint64_t crossLane = 0; uint64_t cnt = 0;
	std::vector<double> perBitF(512, 0);
	// SAC matrix for lane 0 (f0 input bit -> f0' output bit)
	std::vector<uint32_t> sac0(128 * 128, 0);
	// E bits: flip bit b of e_k, HD per lane
	std::vector<double> perBitE(512, 0); std::vector<double> perKeyLaneHD(16, 0);
	std::vector<uint32_t> eAffectsByte(4 * 16, 0); // for e2: which output bytes of f0' change
	for (size_t s = 0; s < m; ++s) {
		const Rec& x = recs[(s * 7919) % n];
		uint8_t base[64]; mixV2(x.fpre, x.e, base);
		for (int b = 0; b < 512; ++b) {
			uint8_t fp[64]; memcpy(fp, x.fpre, 64); fp[b / 8] ^= 1 << (b % 8);
			uint8_t o[64]; mixV2(fp, x.e, o);
			int lane = b / 128;
			int d = hd(o + 16 * lane, base + 16 * lane, 16);
			perBitF[b] += d;
			for (int l = 0; l < 4; ++l) if (l != lane && memcmp(o + 16 * l, base + 16 * l, 16)) crossLane++;
			if (lane == 0) for (int ob = 0; ob < 128; ++ob) if (((o[ob / 8] ^ base[ob / 8]) >> (ob % 8)) & 1) sac0[(b % 128) * 128 + ob]++;
			cnt++;
		}
		for (int b = 0; b < 512; ++b) {
			uint8_t ep[64]; memcpy(ep, x.e, 64); ep[b / 8] ^= 1 << (b % 8);
			uint8_t o[64]; mixV2(x.fpre, ep, o);
			int key = b / 128; double tot = 0;
			for (int l = 0; l < 4; ++l) { int d = hd(o + 16 * l, base + 16 * l, 16); perKeyLaneHD[key * 4 + l] += d; tot += d; }
			perBitE[b] += tot;
			if (key == 2) for (int by = 0; by < 16; ++by) if (o[by] != base[by]) eAffectsByte[2 * 16 + by]++;
		}
	}
	for (int b = 0; b < 512; ++b) { perBitF[b] /= m; perBitE[b] /= m; }
	for (int b = 0; b < 512; ++b) { sumF += perBitF[b]; minF = std::min(minF, perBitF[b]); maxF = std::max(maxF, perBitF[b]); }
	printf("[3] avalanche of the v2 mix over %zu real step-10 states\n", m);
	printf("    flip 1 bit of f_j: mean HD of f_j' %.2f / 128 (min over bit positions %.2f, max %.2f); changes in other lanes: %llu\n", sumF / 512, minF, maxF, (unsigned long long)crossLane);
	double sacMax = 0; for (auto c : sac0) sacMax = std::max(sacMax, fabs((double)c / m - 0.5));
	printf("    strict avalanche (lane 0, 128x128 bit pairs): max |P(flip)-0.5| %.4f (1-sigma %.4f)\n", sacMax, 0.5 / sqrt((double)m));
	for (int k = 0; k < 4; ++k) {
		printf("    flip 1 bit of e%d: mean HD per lane f0' %.2f f1' %.2f f2' %.2f f3' %.2f (bits of 128)\n", k,
			perKeyLaneHD[k * 4 + 0] / (128.0 * m), perKeyLaneHD[k * 4 + 1] / (128.0 * m), perKeyLaneHD[k * 4 + 2] / (128.0 * m), perKeyLaneHD[k * 4 + 3] / (128.0 * m));
	}
	{
		// e2 single bit: how many output bytes of f0' change on average
		double tot = 0; for (int by = 0; by < 16; ++by) tot += eAffectsByte[32 + by];
		printf("    flip 1 bit of e2: mean number of changed bytes over the 4 lanes %.2f of 64\n", tot / (128.0 * m));
	}

	// 4. e3 enters linearly and identically in all lanes: f0'^f2' and f1'^f3' do not depend on e3
	size_t depends = 0;
	for (size_t s = 0; s < std::min<size_t>(n, 10000); ++s) {
		const Rec& x = recs[s];
		uint8_t ep[64]; memcpy(ep, x.e, 64);
		for (int i = 48; i < 64; ++i) ep[i] = (uint8_t)rng();
		uint8_t o1[64], o2[64]; mixV2(x.fpre, x.e, o1); mixV2(x.fpre, ep, o2);
		for (int i = 0; i < 16; ++i) {
			if ((o1[i] ^ o1[32 + i]) != (o2[i] ^ o2[32 + i])) { depends++; break; }
			if ((o1[16 + i] ^ o1[48 + i]) != (o2[16 + i] ^ o2[48 + i])) { depends++; break; }
		}
	}
	printf("[4] samples where f0'^f2' or f1'^f3' changes when e3 is replaced by random bytes: %zu of %zu\n", depends, std::min<size_t>(n, 10000));

	// 5. equal lanes
	size_t eqPre = 0, eqPost = 0;
	for (auto& x : recs) {
		if (!memcmp(x.fpre, x.fpre + 32, 16) || !memcmp(x.fpre + 16, x.fpre + 48, 16)) eqPre++;
		if (!memcmp(x.fpost, x.fpost + 32, 16) || !memcmp(x.fpost + 16, x.fpost + 48, 16)) eqPost++;
	}
	printf("[5] samples with f0==f2 or f1==f3: before mix %zu, after mix %zu (of %zu)\n", eqPre, eqPost, n);
	return 0;
}
