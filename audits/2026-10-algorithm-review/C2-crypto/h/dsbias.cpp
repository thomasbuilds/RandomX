// Agent C2 (item 5): explain per-bit deviations of Dataset items.
// item = SSH7(state) XOR C[idx7]. If SSH7's output bit b has bias eps_b and the Cache bit b (over the lines selected
// at step 7) has bias delta_b, then P(item bit = 1) = 0.5 - 2 eps_b delta_b (independent case).
// 1) full Dataset via the library (JIT Dataset init, 3 threads): per-bit item frequencies d_b (all 34,078,719 items)
// 2) Cache per-bit frequencies over all 2^22 lines (unweighted) and weighted by step-7 use (from a sample)
// 3) sample of items with the replicated item function: SSH7 output bit frequencies before the final XOR (eps_b)
// Usage: dsbias <key> <sampleItems>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <vector>
#include <thread>
#include <string>
#include <algorithm>
#include "randomx.h"
#include "dataset.hpp"
#include "superscalar.hpp"
#include "intrin_portable.h"

using namespace randomx;
static constexpr uint32_t LINES = CacheSize / CacheLineSize;
static constexpr uint64_t mul0 = 6364136223846793005ULL;
static constexpr uint64_t xc[8] = {0, 9298411001130361340ULL, 12065312585734608966ULL, 9306329213124626780ULL,
	5281919268842080866ULL, 10536153434571861004ULL, 3398623926847679864ULL, 9549104520008361294ULL};

int main(int argc, char** argv) {
	std::string key = argc > 1 ? argv[1] : "test key 000";
	uint64_t S = argc > 2 ? strtoull(argv[2], nullptr, 0) : 2000000;
	const int T = 3;
	randomx_cache* cache = randomx_alloc_cache(RANDOMX_FLAG_JIT | RANDOMX_FLAG_ARGON2_AVX2);
	randomx_init_cache(cache, key.data(), key.size());
	const uint64_t N = randomx_dataset_item_count();
	// 1) full Dataset through the library's own (JIT) initialiser
	randomx_dataset* ds = randomx_alloc_dataset(RANDOMX_FLAG_DEFAULT);
	if (!ds) { fprintf(stderr, "dataset alloc failed\n"); return 1; }
	{
		std::vector<std::thread> th;
		for (int t = 0; t < T; ++t) th.emplace_back([&, t]() { uint64_t a = N * t / T, b = N * (t + 1) / T; randomx_init_dataset(ds, cache, a, b - a); });
		for (auto& x : th) x.join();
	}
	const uint64_t* dm = (const uint64_t*)randomx_get_dataset_memory(ds);
	std::vector<uint64_t> itemOnes(512, 0);
	{
		std::vector<std::vector<uint64_t>> part(T, std::vector<uint64_t>(512, 0));
		std::vector<std::thread> th;
		for (int t = 0; t < T; ++t) th.emplace_back([&, t]() {
			uint64_t a = N * t / T, b = N * (t + 1) / T;
			for (uint64_t i = a; i < b; ++i) for (int w = 0; w < 8; ++w) { uint64_t v = dm[i * 8 + w]; while (v) { int bit = __builtin_ctzll(v); part[t][w * 64 + bit]++; v &= v - 1; } }
		});
		for (auto& x : th) x.join();
		for (int t = 0; t < T; ++t) for (int b = 0; b < 512; ++b) itemOnes[b] += part[t][b];
	}
	randomx_release_dataset(ds);
	// 2) Cache bit frequencies (unweighted)
	const uint64_t* cm = (const uint64_t*)randomx_get_cache_memory(cache);
	std::vector<uint64_t> cacheOnes(512, 0);
	for (uint64_t l = 0; l < LINES; ++l) for (int w = 0; w < 8; ++w) { uint64_t v = cm[l * 8 + w]; while (v) { int bit = __builtin_ctzll(v); cacheOnes[w * 64 + bit]++; v &= v - 1; } }
	// 3) sample: SSH7 output before the final XOR, and the Cache bits of the step-7 line
	std::vector<uint64_t> sshOnes(512, 0), selCacheOnes(512, 0);
	{
		std::vector<std::vector<uint64_t>> pa(T, std::vector<uint64_t>(512, 0)), pb(T, std::vector<uint64_t>(512, 0));
		std::vector<std::thread> th;
		for (int t = 0; t < T; ++t) th.emplace_back([&, t]() {
			for (uint64_t k = t; k < S; k += T) {
				uint64_t it = (k * 2654435761ULL) % N; // spread over the whole item range
				uint64_t rl[8]; uint64_t reg = it;
				rl[0] = (it + 1) * mul0; for (int q = 1; q < 8; ++q) rl[q] = rl[0] ^ xc[q];
				for (unsigned i = 0; i < RANDOMX_CACHE_ACCESSES; ++i) {
					const uint64_t* line = cm + (reg & (LINES - 1)) * 8;
					SuperscalarProgram& prog = cache->programs[i];
					executeSuperscalar(rl, prog, &cache->reciprocalCache);
					if (i == RANDOMX_CACHE_ACCESSES - 1) {
						for (int w = 0; w < 8; ++w) for (int bit = 0; bit < 64; ++bit) { pa[t][w * 64 + bit] += (rl[w] >> bit) & 1; pb[t][w * 64 + bit] += (line[w] >> bit) & 1; }
					}
					for (int q = 0; q < 8; ++q) rl[q] ^= line[q];
					reg = rl[prog.getAddressRegister()];
				}
			}
		});
		for (auto& x : th) x.join();
		for (int t = 0; t < T; ++t) for (int b = 0; b < 512; ++b) { sshOnes[b] += pa[t][b]; selCacheOnes[b] += pb[t][b]; }
	}
	// report
	double chiAll = 0, chiFair = 0; int nFair = 0, nBiased = 0;
	double sxy = 0, sxx = 0, syy = 0;
	printf("key '%s': items %llu, Cache lines %u, SSH7 sample %llu items\n", key.c_str(), (unsigned long long)N, LINES, (unsigned long long)S);
	printf("bits whose SSH7 output is strongly biased (|eps| > 0.05), with item deviation d, Cache deviation delta and prediction -2*eps*delta:\n");
	printf("%5s %9s %10s %10s %10s %8s\n", "bit", "eps", "delta", "d(item)", "pred", "z(item)");
	for (int b = 0; b < 512; ++b) {
		double d = (double)itemOnes[b] / N - 0.5, z = d / (0.5 / sqrt((double)N));
		double eps = (double)sshOnes[b] / S - 0.5, delta = (double)cacheOnes[b] / LINES - 0.5;
		double pred = -2 * eps * delta;
		chiAll += z * z;
		if (fabs(eps) <= 0.05) { chiFair += z * z; nFair++; } else nBiased++;
		if (fabs(eps) > 0.05) printf("%5d %+9.4f %+10.6f %+10.6f %+10.6f %+8.2f\n", b, eps, delta, d, pred, z);
		sxy += d * pred; sxx += d * d; syy += pred * pred;
	}
	printf("item per-bit chi2: all 512 bits %.1f; the %d bits with |eps| <= 0.05: %.1f (dof %d); bits with |eps| > 0.05: %d\n", chiAll, nFair, chiFair, nFair, nBiased);
	printf("correlation over 512 bits between observed d and predicted -2*eps*delta: %.3f\n", sxy / sqrt(sxx * syy));
	double maxSel = 0; for (int b = 0; b < 512; ++b) maxSel = std::max(maxSel, fabs((double)selCacheOnes[b] / S - (double)cacheOnes[b] / LINES));
	printf("check: Cache bit frequencies of the lines selected at step 7 vs all lines, max |difference| %.5f (1-sigma of sample %.5f)\n", maxSel, 0.5 / sqrt((double)S));
	randomx_release_cache(cache);
	return 0;
}
