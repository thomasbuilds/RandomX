// Agent C2 (item 5): Dataset item statistics and Cache-line access distribution, all items of one key.
// The item function is a copy of randomx::initDatasetItem (dataset.cpp) that also records the Cache indices.
// Self-check: every 4096th item is compared with randomx::initDatasetItem.
// Usage: dsstat <key string> <threads> <outprefix> [itemLimit]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <vector>
#include <thread>
#include <algorithm>
#include <string>
#include "randomx.h"
#include "dataset.hpp"
#include "superscalar.hpp"
#include "intrin_portable.h"

using namespace randomx;
static constexpr uint32_t LINES = CacheSize / CacheLineSize; // 4194304
static constexpr uint64_t mul0 = 6364136223846793005ULL;
static constexpr uint64_t xc[8] = {0, 9298411001130361340ULL, 12065312585734608966ULL, 9306329213124626780ULL,
	5281919268842080866ULL, 10536153434571861004ULL, 3398623926847679864ULL, 9549104520008361294ULL};

struct ThreadStats {
	std::vector<uint16_t> cnt; // [step][line]
	uint64_t ones[512] = {0};
	uint64_t nbAgree[512] = {0};
	uint64_t nbHd[513] = {0};
	uint64_t repeatedLine = 0, selfcheckBad = 0, selfcheck = 0;
	uint64_t items = 0, nbPairs = 0;
};

static void item(randomx_cache* cache, uint64_t itemNumber, uint64_t (&rl)[8], uint32_t (&idx)[8]) {
	uint64_t registerValue = itemNumber;
	rl[0] = (itemNumber + 1) * mul0;
	for (int q = 1; q < 8; ++q) rl[q] = rl[0] ^ xc[q];
	for (unsigned i = 0; i < RANDOMX_CACHE_ACCESSES; ++i) {
		uint32_t line = (uint32_t)(registerValue & (LINES - 1));
		idx[i] = line;
		const uint8_t* mixBlock = cache->memory + (uint64_t)line * CacheLineSize;
		SuperscalarProgram& prog = cache->programs[i];
		executeSuperscalar(rl, prog, &cache->reciprocalCache);
		for (unsigned q = 0; q < 8; ++q) rl[q] ^= load64_native(mixBlock + 8 * q);
		registerValue = rl[prog.getAddressRegister()];
	}
}

int main(int argc, char** argv) {
	if (argc < 4) { fprintf(stderr, "usage: dsstat key threads outprefix [itemLimit]\n"); return 1; }
	std::string key = argv[1];
	int T = atoi(argv[2]);
	std::string out = argv[3];
	uint64_t N = randomx_dataset_item_count();
	if (argc > 4) N = std::min<uint64_t>(N, strtoull(argv[4], nullptr, 0));
	randomx_cache* cache = randomx_alloc_cache(RANDOMX_FLAG_ARGON2_AVX2);
	randomx_init_cache(cache, key.data(), key.size());
	std::vector<uint64_t> r0(N), r7(N);
	std::vector<uint32_t> idx1(N);
	std::vector<ThreadStats> st(T);
	std::vector<std::thread> th;
	for (int t = 0; t < T; ++t) {
		th.emplace_back([&, t]() {
			ThreadStats& s = st[t];
			s.cnt.assign((size_t)8 * LINES, 0);
			uint64_t a = N * t / T, b = N * (t + 1) / T;
			uint64_t prev[8]; bool havePrev = false;
			for (uint64_t it = a; it < b; ++it) {
				uint64_t rl[8]; uint32_t idx[8];
				item(cache, it, rl, idx);
				for (int k = 0; k < 8; ++k) {
					uint16_t& c = s.cnt[(size_t)k * LINES + idx[k]];
					if (c < 65535) c++;
				}
				for (int k = 0; k < 8; ++k) for (int j = k + 1; j < 8; ++j) if (idx[k] == idx[j]) { s.repeatedLine++; k = 8; break; }
				for (int w = 0; w < 8; ++w) for (int bit = 0; bit < 64; ++bit) s.ones[w * 64 + bit] += (rl[w] >> bit) & 1;
				if (havePrev) {
					int d = 0;
					for (int w = 0; w < 8; ++w) {
						uint64_t x = rl[w] ^ prev[w]; d += __builtin_popcountll(x);
						for (int bit = 0; bit < 64; ++bit) s.nbAgree[w * 64 + bit] += !((x >> bit) & 1);
					}
					s.nbHd[d]++; s.nbPairs++;
				}
				memcpy(prev, rl, sizeof prev); havePrev = true;
				r0[it] = rl[0]; r7[it] = rl[7]; idx1[it] = idx[1];
				if ((it & 4095) == 0) {
					alignas(64) uint8_t ref[64];
					initDatasetItem(cache, ref, it);
					s.selfcheck++;
					if (memcmp(ref, rl, 64)) s.selfcheckBad++;
				}
				s.items++;
			}
		});
	}
	for (auto& x : th) x.join();
	// merge
	std::vector<uint32_t> cnt((size_t)8 * LINES, 0);
	ThreadStats tot;
	for (auto& s : st) {
		for (size_t i = 0; i < cnt.size(); ++i) cnt[i] += s.cnt[i];
		for (int b = 0; b < 512; ++b) { tot.ones[b] += s.ones[b]; tot.nbAgree[b] += s.nbAgree[b]; }
		for (int d = 0; d <= 512; ++d) tot.nbHd[d] += s.nbHd[d];
		tot.repeatedLine += s.repeatedLine; tot.selfcheck += s.selfcheck; tot.selfcheckBad += s.selfcheckBad;
		tot.items += s.items; tot.nbPairs += s.nbPairs;
		std::vector<uint16_t>().swap(s.cnt);
	}
	FILE* f = fopen((out + ".txt").c_str(), "w");
	fprintf(f, "key '%s' items %llu threads %d\n", key.c_str(), (unsigned long long)N, T);
	fprintf(f, "self-check against randomx::initDatasetItem: %llu items, %llu mismatches\n", (unsigned long long)tot.selfcheck, (unsigned long long)tot.selfcheckBad);
	// 1. Cache-line access distribution
	auto disp = [&](int k0, int k1, const char* name) {
		double lam = 0; std::vector<uint64_t> hist(1024, 0); uint32_t mx = 0; double sum = 0, sum2 = 0;
		for (uint32_t l = 0; l < LINES; ++l) {
			uint32_t c = 0; for (int k = k0; k <= k1; ++k) c += cnt[(size_t)k * LINES + l];
			sum += c; sum2 += (double)c * c; mx = std::max(mx, c); hist[std::min<uint32_t>(c, 1023)]++;
		}
		lam = sum / LINES; double var = sum2 / LINES - lam * lam;
		// chi-square of the count histogram against Poisson(lam): bins accumulated left to right until the expectation is >= 5,
		// the remaining right tail merged into the last bin; one parameter (lam) estimated
		std::vector<double> pe(1024);
		{ double lp = -lam; for (int c = 0; c < 1024; ++c) { pe[c] = exp(lp); lp += log(lam) - log((double)c + 1); } }
		std::vector<double> E, O; double eAcc = 0, oAcc = 0;
		for (int c = 0; c < 1024; ++c) { eAcc += pe[c] * LINES; oAcc += hist[c]; if (eAcc >= 5) { E.push_back(eAcc); O.push_back(oAcc); eAcc = 0; oAcc = 0; } }
		if (!E.empty()) { E.back() += eAcc; O.back() += oAcc; }
		double chi = 0; for (size_t i = 0; i < E.size(); ++i) chi += (O[i] - E[i]) * (O[i] - E[i]) / E[i];
		int dof = (int)E.size() - 1;
		// expected maximum of LINES Poisson(lam) variables: smallest m with LINES * P(X > m) < 1
		double tail = 1; int emax = 0; for (int c = 0; c < 1024; ++c) { tail -= pe[c]; if (LINES * tail < 0.5) { emax = c; break; } }
		fprintf(f, "%-26s mean %.4f variance %.4f (dispersion %.4f) max %u (Poisson-expected max ~%d) lines never read %llu (Poisson-expected %.1f) chi2 %.1f dof %d\n",
			name, lam, var, var / lam, mx, emax, (unsigned long long)hist[0], pe[0] * LINES, chi, dof - 1);
	};
	fprintf(f, "\n[1] Cache line reads per line (%u lines)\n", LINES);
	disp(0, 0, "step 0 (index = item mod 2^22)");
	for (int k = 1; k < 8; ++k) { char nm[32]; snprintf(nm, sizeof nm, "step %d", k); disp(k, k, nm); }
	disp(1, 7, "steps 1-7 (data-dependent)");
	disp(0, 7, "all 8 steps");
	fprintf(f, "items whose 8 accesses hit some line twice: %llu (expected about %.1f for independent uniform indices 1-7)\n",
		(unsigned long long)tot.repeatedLine, (double)N * 28.0 / LINES);
	// 2. bit bias
	double chi = 0, maxdev = 0; int worst = 0;
	for (int b = 0; b < 512; ++b) {
		double p = (double)tot.ones[b] / N; double z = (tot.ones[b] - N / 2.0) / sqrt(N / 4.0); chi += z * z;
		if (fabs(p - 0.5) > maxdev) { maxdev = fabs(p - 0.5); worst = b; }
	}
	fprintf(f, "\n[2] per-bit bias over %llu items: chi2(512 dof) %.1f, max |P(1)-0.5| %.6f at bit %d (1-sigma %.6f)\n",
		(unsigned long long)N, chi, maxdev, worst, 0.5 / sqrt((double)N));
	// 3. neighbours i, i+1
	double chiA = 0, maxA = 0; for (int b = 0; b < 512; ++b) { double z = (tot.nbAgree[b] - tot.nbPairs / 2.0) / sqrt(tot.nbPairs / 4.0); chiA += z * z; maxA = std::max(maxA, fabs((double)tot.nbAgree[b] / tot.nbPairs - 0.5)); }
	double mHd = 0, vHd = 0; for (int d = 0; d <= 512; ++d) { mHd += (double)d * tot.nbHd[d]; vHd += (double)d * d * tot.nbHd[d]; }
	mHd /= tot.nbPairs; vHd = vHd / tot.nbPairs - mHd * mHd;
	fprintf(f, "\n[3] neighbouring items (i, i+1), %llu pairs: Hamming distance mean %.4f (expect 256) variance %.3f (expect 128); per-bit agreement chi2(512) %.1f, max |P(equal)-0.5| %.6f; min HD observed: ",
		(unsigned long long)tot.nbPairs, mHd, vHd, chiA, maxA);
	for (int d = 0; d <= 512; ++d) if (tot.nbHd[d]) { fprintf(f, "%d\n", d); break; }
	// 4. duplicates of r0 and r7 words
	auto dups = [&](std::vector<uint64_t>& v, const char* name) {
		std::vector<uint64_t> s(v); std::sort(s.begin(), s.end()); uint64_t d = 0; for (size_t i = 1; i < s.size(); ++i) if (s[i] == s[i - 1]) d++;
		fprintf(f, "duplicate %s words among %zu items: %llu (expected %.2e)\n", name, s.size(), (unsigned long long)d, (double)s.size() * s.size() / 2 / 18446744073709551616.0);
	};
	fprintf(f, "\n[4]\n"); dups(r0, "r0"); dups(r7, "r7");
	// 5. items sharing the first Cache line (i and i + k*2^22): equal step-1 index and r0 Hamming distance
	uint64_t pairs = 0, sameIdx1 = 0; std::vector<uint64_t> hd64(65, 0);
	for (uint64_t i = 0; i < N; ++i)
		for (uint64_t j = i + LINES; j < N; j += LINES) {
			pairs++; if (idx1[i] == idx1[j]) sameIdx1++; hd64[__builtin_popcountll(r0[i] ^ r0[j])]++;
		}
	double m64 = 0; for (int d = 0; d <= 64; ++d) m64 += (double)d * hd64[d]; m64 /= pairs;
	fprintf(f, "\n[5] item pairs sharing the step-0 Cache line (i, i + k*2^22): %llu pairs, equal step-1 Cache index %llu (expected %.1f), mean HD of r0 %.4f (expect 32)\n",
		(unsigned long long)pairs, (unsigned long long)sameIdx1, (double)pairs / LINES, m64);
	fclose(f);
	randomx_release_cache(cache);
	return 0;
}
