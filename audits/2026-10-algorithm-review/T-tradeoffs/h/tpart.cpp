// tpart: one-process, interleaved timing of RandomX v1/v2 in several "item supply" configurations,
// all sharing one Cache and one Dataset buffer (initialised once with several threads):
//   fast      : the real fast-mode VM (Dataset read from memory, prefetched 1 (v1) or 2 (v2) iterations ahead)
//   light     : the real light-mode VM (every item computed from the Cache, no lookahead)
//   p=<frac>  : partial Dataset: items with index < threshold are stored (read from memory, prefetched like
//               fast mode), the others are computed from the Cache on demand (light-mode item function)
//   tiny      : all items read from a 4 KiB L1-resident buffer: VM + fill/hash/generation without any item
//               cost (hash values are wrong in this mode by construction; timing only)
// Requires the patched library from agentT/rx-p (tpart_* hooks in the x86 JIT light-mode generator).
// Usage: tpart cfgs=fast,light,p0.5,tiny nonces=N reps=R lp=1 softaes=0 core=3 init=4 maxp=1 batch=1
#include "randomx.h"
#include "dataset.hpp"
#include <sched.h>
#include <sys/mman.h>
#include <chrono>
#include <thread>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sstream>
#include <map>

extern "C" {
	extern int tpart_enabled;
	extern uint32_t tpart_thresh;
	extern uint32_t tpart_mask;
	extern uint64_t tpart_base;
	extern int tpart_prefetch_t0;
}

static const uint8_t blockTemplate_[] = {
	0x07, 0x07, 0xf7, 0xa4, 0xf0, 0xd6, 0x05, 0xb3, 0x03, 0x26, 0x08, 0x16, 0xba, 0x3f, 0x10, 0x90, 0x2e, 0x1a, 0x14,
	0x5a, 0xc5, 0xfa, 0xd3, 0xaa, 0x3a, 0xf6, 0xea, 0x44, 0xc1, 0x18, 0x69, 0xdc, 0x4f, 0x85, 0x3f, 0x00, 0x2b, 0x2e,
	0xea, 0x00, 0x00, 0x00, 0x00, 0x77, 0xb2, 0x06, 0xa0, 0x2c, 0xa5, 0xb1, 0xd4, 0xce, 0x6b, 0xbf, 0xdf, 0x0a, 0xca,
	0xc3, 0x8b, 0xde, 0xd3, 0x4d, 0x2d, 0xcd, 0xee, 0xf9, 0x5c, 0xd2, 0x0c, 0xef, 0xc1, 0x2f, 0x61, 0xd5, 0x61, 0x09
};

static std::map<std::string, std::string> args;
static long argi(const char* k, long d) { auto it = args.find(k); return it == args.end() ? d : atol(it->second.c_str()); }
static std::string args_s(const char* k, const char* d) { auto it = args.find(k); return it == args.end() ? d : it->second; }
static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
static void pin(int core) { cpu_set_t s; CPU_ZERO(&s); CPU_SET(core, &s); if (sched_setaffinity(0, sizeof(s), &s)) { perror("affinity"); exit(1); } }

static double run(randomx_vm* vm, uint32_t n0, uint32_t n, bool batch, uint64_t acc[4]) {
	uint8_t blk[sizeof(blockTemplate_)];
	memcpy(blk, blockTemplate_, sizeof(blk));
	uint64_t h[4];
	double t0 = now();
	if (batch) {
		memcpy(blk + 39, &n0, 4);
		randomx_calculate_hash_first(vm, blk, sizeof(blk));
		for (uint32_t i = 1; i <= n; ++i) {
			uint32_t nn = n0 + i;
			memcpy(blk + 39, &nn, 4);
			if (i < n) randomx_calculate_hash_next(vm, blk, sizeof(blk), h);
			else randomx_calculate_hash_last(vm, h);
			for (int q = 0; q < 4; ++q) acc[q] ^= h[q];
		}
	}
	else {
		for (uint32_t i = 0; i < n; ++i) {
			uint32_t nn = n0 + i;
			memcpy(blk + 39, &nn, 4);
			randomx_calculate_hash(vm, blk, sizeof(blk), h);
			for (int q = 0; q < 4; ++q) acc[q] ^= h[q];
		}
	}
	return now() - t0;
}

struct Cfg { std::string name; int kind; double p; bool t0; }; // kind 0 fast, 1 light, 2 partial, 3 tiny; t0: prefetcht0 instead of prefetchnta

int main(int argc, char** argv) {
	for (int i = 1; i < argc; ++i) {
		const char* e = strchr(argv[i], '=');
		if (e) args[std::string(argv[i], e - argv[i])] = e + 1;
	}
	int nonces = argi("nonces", 50), reps = argi("reps", 3), core = argi("core", 3), init = argi("init", 4);
	bool lp = argi("lp", 1), softaes = argi("softaes", 0), batch = argi("batch", 1);
	int lightNonces = argi("lightnonces", nonces);
	double maxp = atof(args_s("maxp", "1").c_str());
	std::vector<Cfg> cfgs;
	{
		std::stringstream ss(args_s("cfgs", "fast,light,tiny"));
		std::string t;
		while (std::getline(ss, t, ',')) {
			bool t0 = t.size() > 2 && t.substr(t.size() - 2) == "t0";
			std::string b = t0 ? t.substr(0, t.size() - 2) : t;
			if (b == "fast") cfgs.push_back({t, 0, 1, t0});
			else if (b == "light") cfgs.push_back({t, 1, 0, t0});
			else if (b == "tiny") cfgs.push_back({t, 3, 1, t0});
			else if (b[0] == 'p') cfgs.push_back({t, 2, atof(b.c_str() + 1), t0});
		}
	}
	bool needFast = false;
	for (auto& c : cfgs) if (c.kind == 0) needFast = true;
	if (needFast) maxp = 1.0;

	randomx_flags flags = (randomx_flags)(RANDOMX_FLAG_JIT | RANDOMX_FLAG_ARGON2_AVX2 | (softaes ? 0 : RANDOMX_FLAG_HARD_AES) | (lp ? RANDOMX_FLAG_LARGE_PAGES : 0));
	int32_t seedValue = argi("seed", 0);
	double t0 = now();
	randomx_cache* cache = randomx_alloc_cache(flags);
	if (!cache) { fprintf(stderr, "cache alloc failed\n"); return 1; }
	randomx_init_cache(cache, &seedValue, 4);
	const uint32_t totalItems = (uint32_t)randomx_dataset_item_count();
	const uint32_t base = 1u << 25; // items per program window (RANDOMX_DATASET_BASE_SIZE / 64)
	const uint32_t meanOffset = 262144; // mean datasetOffset / 64 = (RANDOMX_DATASET_EXTRA_SIZE/64)/2
	auto threshFor = [&](double p) -> uint32_t {
		if (p <= 0) return 0;
		if (p >= 1) return 0xFFFFFFFFu;
		return (uint32_t)(p * base) + meanOffset;
	};
	uint32_t storeItems = maxp >= 1 ? totalItems : std::min<uint32_t>(totalItems, threshFor(maxp));
	if (maxp <= 0) storeItems = 0;
	size_t bytes = (size_t)storeItems * 64;
	uint8_t* mem = nullptr;
	if (bytes) {
		size_t mapBytes = (bytes + (2u << 20) - 1) & ~(size_t)((2u << 20) - 1);
		mem = (uint8_t*)mmap(nullptr, mapBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | (lp ? MAP_HUGETLB : 0), -1, 0);
		if (mem == MAP_FAILED) { perror("mmap dataset"); return 1; }
		std::vector<std::thread> th;
		for (int i = 0; i < init; ++i) {
			uint32_t a = (uint64_t)storeItems * i / init, b = (uint64_t)storeItems * (i + 1) / init;
			th.emplace_back([=]() { cache->datasetInit(cache, mem + (size_t)a * 64, a, b); });
		}
		for (auto& t : th) t.join();
	}
	alignas(64) static uint8_t tiny[4096];
	memset(tiny, 0x5a, sizeof(tiny));
	double tinit = now() - t0;
	pin(core);
	printf("# tpart nonces=%d lightnonces=%d reps=%d lp=%d softaes=%d batch=%d core=%d storedItems=%u (%.1f MiB) init_s=%.1f\n",
		nonces, lightNonces, reps, lp, softaes, batch, core, storeItems, bytes / 1048576.0, tinit);

	randomx_dataset dsObj;
	dsObj.memory = mem;
	randomx_vm* vmL[3] = {nullptr, nullptr, nullptr};
	randomx_vm* vmF[3] = {nullptr, nullptr, nullptr};
	vmL[1] = randomx_create_vm(flags, cache, nullptr);
	vmL[2] = randomx_create_vm((randomx_flags)(flags | RANDOMX_FLAG_V2), cache, nullptr);
	if (needFast) {
		vmF[1] = randomx_create_vm((randomx_flags)(flags | RANDOMX_FLAG_FULL_MEM), nullptr, &dsObj);
		vmF[2] = randomx_create_vm((randomx_flags)(flags | RANDOMX_FLAG_FULL_MEM | RANDOMX_FLAG_V2), nullptr, &dsObj);
	}
	auto setup = [&](const Cfg& c) {
		tpart_prefetch_t0 = c.t0 ? 1 : 0;
		if (c.kind == 2) { tpart_enabled = 1; tpart_thresh = threshFor(c.p); tpart_mask = 0xFFFFFFFFu; tpart_base = (uint64_t)mem; }
		else if (c.kind == 3) { tpart_enabled = 1; tpart_thresh = 0xFFFFFFFFu; tpart_mask = 63; tpart_base = (uint64_t)tiny; }
		else { tpart_enabled = 0; }
	};
	// reference XOR (real light mode) for the correctness check of the partial configurations
	std::map<std::string, std::vector<double>> res;
	std::map<std::string, std::string> xorOf;
	uint64_t dummy[4] = {0, 0, 0, 0};
	for (auto& c : cfgs) {
		setup(c);
		for (int v = 1; v <= 2; ++v) {
			randomx_vm* vm = c.kind == 0 ? vmF[v] : vmL[v];
			run(vm, 100000, 2, batch, dummy); // warm-up
		}
	}
	for (int r = 0; r < reps; ++r) {
		for (auto& c : cfgs) {
			setup(c);
			for (int vi = 0; vi < 2; ++vi) {
				int v = (r % 2 == 0) ? vi + 1 : 2 - vi; // alternate the order: v1,v2 then v2,v1
				randomx_vm* vm = c.kind == 0 ? vmF[v] : vmL[v];
				int n = (c.kind == 1 || (c.kind == 2 && c.p < 0.5)) ? lightNonces : nonces;
				uint64_t acc[4] = {0, 0, 0, 0};
				double s = run(vm, 0, n, batch, acc);
				double ms = 1000.0 * s / n;
				char hx[65]; const uint8_t* b = (const uint8_t*)acc;
				for (int q = 0; q < 32; ++q) sprintf(hx + 2 * q, "%02x", b[q]);
				std::string key = c.name + "/v" + std::to_string(v);
				res[key].push_back(ms);
				printf("rep %d %-8s v%d n=%d %.4f ms/hash %.2f H/s xor=%s\n", r, c.name.c_str(), v, n, ms, 1000.0 / ms, hx);
				fflush(stdout);
			}
		}
	}
	for (auto& c : cfgs) for (int v = 1; v <= 2; ++v) {
		auto x = res[c.name + "/v" + std::to_string(v)];
		std::sort(x.begin(), x.end());
		double med = x.size() % 2 ? x[x.size() / 2] : 0.5 * (x[x.size() / 2 - 1] + x[x.size() / 2]);
		printf("summary %-8s v%d median %.4f ms/hash (%.2f H/s) min %.4f max %.4f spread %.2f%% n=%zu\n", c.name.c_str(), v, med,
			1000.0 / med, x.front(), x.back(), 100.0 * (x.back() - x.front()) / med, x.size());
	}
	return 0;
}
