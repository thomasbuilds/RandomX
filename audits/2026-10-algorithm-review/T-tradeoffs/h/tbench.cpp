// tbench: interleaved v1/v2 timing of RandomX (fast or light mode) in one process.
// The Dataset (fast mode) is initialised once with several threads (all cores allowed),
// then the measuring thread is pinned to one core and v1 and v2 machines (sharing the
// same Cache/Dataset) are timed alternately: v1, v2, v1, v2, ...
// Usage: tbench mode=fast|light jit=0|1 softaes=0|1 lp=0|1 nonces=N reps=R core=C init=T
//               batch=0|1 order=12|21 warm=W
// Output: one line per measurement, then a summary (median, min, max) per version.
#include "randomx.h"
#include <sched.h>
#include <pthread.h>
#include <chrono>
#include <thread>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <map>

static const uint8_t blockTemplate_[] = {
	0x07, 0x07, 0xf7, 0xa4, 0xf0, 0xd6, 0x05, 0xb3, 0x03, 0x26, 0x08, 0x16, 0xba, 0x3f, 0x10, 0x90, 0x2e, 0x1a, 0x14,
	0x5a, 0xc5, 0xfa, 0xd3, 0xaa, 0x3a, 0xf6, 0xea, 0x44, 0xc1, 0x18, 0x69, 0xdc, 0x4f, 0x85, 0x3f, 0x00, 0x2b, 0x2e,
	0xea, 0x00, 0x00, 0x00, 0x00, 0x77, 0xb2, 0x06, 0xa0, 0x2c, 0xa5, 0xb1, 0xd4, 0xce, 0x6b, 0xbf, 0xdf, 0x0a, 0xca,
	0xc3, 0x8b, 0xde, 0xd3, 0x4d, 0x2d, 0xcd, 0xee, 0xf9, 0x5c, 0xd2, 0x0c, 0xef, 0xc1, 0x2f, 0x61, 0xd5, 0x61, 0x09
};

static std::map<std::string, std::string> args;
static long argi(const char* k, long d) { auto it = args.find(k); return it == args.end() ? d : atol(it->second.c_str()); }
static std::string args_s(const char* k, const char* d) { auto it = args.find(k); return it == args.end() ? d : it->second; }

static void pin(int core) {
	cpu_set_t set; CPU_ZERO(&set); CPU_SET(core, &set);
	if (sched_setaffinity(0, sizeof(set), &set) != 0) { perror("sched_setaffinity"); exit(1); }
}

static double now() {
	return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Hash nonces [n0, n0+n) and return seconds; xor of the hashes into acc (for a correctness check).
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

int main(int argc, char** argv) {
	for (int i = 1; i < argc; ++i) {
		const char* e = strchr(argv[i], '=');
		if (e) args[std::string(argv[i], e - argv[i])] = e + 1;
	}
	bool fast = args_s("mode", "light") == "fast";
	bool jit = argi("jit", 1), softaes = argi("softaes", 0), lp = argi("lp", 0), batch = argi("batch", 1);
	int nonces = argi("nonces", 100), reps = argi("reps", 3), core = argi("core", 3), init = argi("init", 4);
	int warm = argi("warm", 10);
	std::string order = args_s("order", "12");
	int32_t seedValue = argi("seed", 0);
	char seed[4]; memcpy(seed, &seedValue, 4);

	randomx_flags flags = RANDOMX_FLAG_DEFAULT;
	if (jit) flags |= RANDOMX_FLAG_JIT;
	if (!softaes) flags |= RANDOMX_FLAG_HARD_AES;
	if (lp) flags |= RANDOMX_FLAG_LARGE_PAGES;
	flags |= RANDOMX_FLAG_ARGON2_AVX2;
	if (fast) flags |= RANDOMX_FLAG_FULL_MEM;

	double t0 = now();
	randomx_cache* cache = randomx_alloc_cache((randomx_flags)(flags & ~RANDOMX_FLAG_FULL_MEM));
	if (!cache) { fprintf(stderr, "cache alloc failed\n"); return 1; }
	randomx_init_cache(cache, seed, sizeof(seed));
	randomx_dataset* ds = nullptr;
	if (fast) {
		ds = randomx_alloc_dataset(flags);
		if (!ds) { fprintf(stderr, "dataset alloc failed\n"); return 1; }
		unsigned long cnt = randomx_dataset_item_count();
		std::vector<std::thread> th;
		for (int i = 0; i < init; ++i) {
			unsigned long a = cnt * i / init, b = cnt * (i + 1) / init;
			th.emplace_back(randomx_init_dataset, ds, cache, a, b - a);
		}
		for (auto& t : th) t.join();
		randomx_release_cache(cache);
		cache = nullptr;
	}
	double tinit = now() - t0;
	pin(core);
	randomx_vm* vm[3];
	vm[1] = randomx_create_vm(flags, cache, ds);
	vm[2] = randomx_create_vm((randomx_flags)(flags | RANDOMX_FLAG_V2), cache, ds);
	if (!vm[1] || !vm[2]) { fprintf(stderr, "vm create failed\n"); return 1; }
	printf("# tbench mode=%s jit=%d softaes=%d lp=%d batch=%d nonces=%d reps=%d core=%d init_s=%.2f\n",
		fast ? "fast" : "light", jit, softaes, lp, batch, nonces, reps, core, tinit);
	uint64_t dummy[4] = {0, 0, 0, 0};
	for (int v = 1; v <= 2; ++v) run(vm[v], 100000, warm, batch, dummy);
	std::vector<double> res[3];
	uint64_t acc[3][4];
	for (int r = 0; r < reps; ++r) {
		for (char c : order) {
			int v = c - '0';
			memset(acc[v], 0, sizeof(acc[v]));
			double s = run(vm[v], 0, nonces, batch, acc[v]);
			double ms = 1000.0 * s / nonces;
			res[v].push_back(ms);
			char hx[65]; const uint8_t* b = (const uint8_t*)acc[v];
			for (int q = 0; q < 32; ++q) sprintf(hx + 2 * q, "%02x", b[q]);
			printf("rep %d v%d %.4f ms/hash %.2f H/s xor=%s\n", r, v, ms, 1000.0 / ms, hx);
			fflush(stdout);
		}
	}
	for (int v = 1; v <= 2; ++v) {
		auto x = res[v];
		std::sort(x.begin(), x.end());
		double med = x.size() % 2 ? x[x.size() / 2] : 0.5 * (x[x.size() / 2 - 1] + x[x.size() / 2]);
		printf("summary v%d median %.4f ms/hash (%.2f H/s) min %.4f max %.4f spread %.2f%% n=%zu\n", v, med, 1000.0 / med,
			x.front(), x.back(), 100.0 * (x.back() - x.front()) / med, x.size());
	}
	if (nonces == 1000 && seedValue == 0 && batch) {
		// reference XOR results of randomx-benchmark for seed 0 and nonces 0..999
		printf("# ref v1 10b649a3f15c7c7f88277812f2e74b337a0f20ce909af09199cccb960771cfa1\n# ref v2 b85d79e080b10b6ad28c2e6c993601a1361917dba979e03a0a8f7248aaf4ba52\n");
	}
	randomx_destroy_vm(vm[1]);
	randomx_destroy_vm(vm[2]);
	if (ds) randomx_release_dataset(ds);
	if (cache) randomx_release_cache(cache);
	return 0;
}
