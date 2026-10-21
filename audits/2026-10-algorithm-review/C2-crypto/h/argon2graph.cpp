// Agent C2 (item 2): recomputation cost of missing RandomX Cache blocks under partial storage, on the real Argon2d
// reference graph of the RandomX Cache (3 passes, 1 lane, 262144 blocks, version 0x13).
// Node (r, i) = value of block i after pass r. Inputs of (r, i): prev (r, i-1) [or (r-1, m-1) for i = 0],
// ref (r, j) if j < i else (r-1, j), and for r >= 1 the old value (r-1, i) (v1.3 XORs the new block into the old one).
// The attacker stores the final blocks (r = 2) except a random fraction delta, plus optional checkpoints of passes 0
// and 1 every c blocks; the cost of a missing final block is the number of distinct unavailable nodes in its
// dependency cone (one compression G each).
// Usage: argon2graph <key> <queries>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include <random>
#include <algorithm>
#include <string>
#include "randomx.h"

extern "C" uint32_t* rx_c2_refs;
static constexpr uint32_t M = 262144;
static constexpr int P = 3;

int main(int argc, char** argv) {
	std::string key = argc > 1 ? argv[1] : "test key 000";
	int Q = argc > 2 ? atoi(argv[2]) : 1000;
	// counterfactual: argv[3] == "noxor" drops the (r-1, i) input, i.e. pre-v1.3 semantics where later passes
	// replace blocks; the same reference pattern is reused, so this only isolates the effect of the XOR edge
	bool noxor = argc > 3 && std::string(argv[3]) == "noxor";
	if (noxor) printf("COUNTERFACTUAL: no XOR edge (pre-v1.3 replace semantics), same reference pattern\n");
	std::vector<uint32_t> refs((size_t)P * M, 0xFFFFFFFFu);
	rx_c2_refs = refs.data();
	randomx_cache* cache = randomx_alloc_cache(RANDOMX_FLAG_DEFAULT); // reference Argon2 implementation
	randomx_init_cache(cache, key.data(), key.size());
	rx_c2_refs = nullptr;
	// sanity checks of the recorded graph
	uint64_t unset = 0, bad = 0, prevPassRefs = 0, curPassRefs = 0;
	for (int r = 0; r < P; ++r)
		for (uint32_t i = 0; i < M; ++i) {
			uint32_t j = refs[(size_t)r * M + i];
			if (r == 0 && i < 2) continue;
			if (j == 0xFFFFFFFFu) { unset++; continue; }
			if (j >= M || j == i) bad++;
			if (r == 0 && j >= i) bad++;
			if (r > 0) { if (j < i) curPassRefs++; else prevPassRefs++; }
		}
	printf("key '%s': graph recorded, unset %llu, rule violations %llu; passes 2-3 references into the current pass %.4f, into the previous pass %.4f\n",
		key.c_str(), (unsigned long long)unset, (unsigned long long)bad, (double)curPassRefs / (curPassRefs + prevPassRefs), (double)prevPassRefs / (curPassRefs + prevPassRefs));
	randomx_release_cache(cache);

	auto node = [](int r, uint32_t i) { return (uint32_t)(r * M + i); };
	std::vector<uint8_t> avail((size_t)P * M);
	std::vector<uint32_t> stamp((size_t)P * M, 0); uint32_t epoch = 0;
	std::vector<uint32_t> stack; stack.reserve(P * M);
	std::mt19937_64 rng(42);
	const double deltas[] = {1.0 / 64, 1.0 / 16, 1.0 / 4, 1.0 / 2};
	const int cs[] = {0, 256, 64, 16, 4};
	printf("%8s %6s %10s %12s %12s %12s %12s\n", "delta", "c", "memory", "mean cost", "median", "p90", "max");
	for (double delta : deltas) {
		for (int c : cs) {
			std::fill(avail.begin(), avail.end(), 0);
			avail[node(0, 0)] = avail[node(0, 1)] = 1; // derived from H0 only
			std::vector<uint32_t> missing;
			for (uint32_t i = 0; i < M; ++i) {
				if (std::uniform_real_distribution<double>(0, 1)(rng) < delta) missing.push_back(i);
				else avail[node(2, i)] = 1;
				if (c > 0 && i % c == 0) { avail[node(0, i)] = 1; avail[node(1, i)] = 1; }
			}
			double mem = (double)(M - missing.size()) / M + (c > 0 ? 2.0 / c : 0.0);
			std::vector<uint64_t> costs;
			for (int q = 0; q < Q && !missing.empty(); ++q) {
				uint32_t i0 = missing[rng() % missing.size()];
				++epoch; uint64_t cost = 0;
				stack.clear(); stack.push_back(node(2, i0)); stamp[node(2, i0)] = epoch;
				while (!stack.empty()) {
					uint32_t v = stack.back(); stack.pop_back();
					cost++;
					int r = v / M; uint32_t i = v % M;
					uint32_t par[3]; int np = 0;
					if (!(r == 0 && i < 2)) {
						par[np++] = (i == 0) ? node(r - 1, M - 1) : node(r, i - 1);
						uint32_t j = refs[(size_t)r * M + i];
						par[np++] = (r == 0 || j < i) ? node(r, j) : node(r - 1, j);
						if (r > 0 && !noxor) par[np++] = node(r - 1, i);
					}
					for (int k = 0; k < np; ++k) {
						uint32_t u = par[k];
						if (avail[u] || stamp[u] == epoch) continue;
						stamp[u] = epoch; stack.push_back(u);
					}
				}
				costs.push_back(cost);
			}
			std::sort(costs.begin(), costs.end());
			double mean = 0; for (auto x : costs) mean += x; mean /= costs.size();
			printf("%8.4f %6d %10.4f %12.1f %12llu %12llu %12llu\n", delta, c, mem, mean,
				(unsigned long long)costs[costs.size() / 2], (unsigned long long)costs[costs.size() * 9 / 10], (unsigned long long)costs.back());
			fflush(stdout);
		}
	}
	return 0;
}
