// Agent S: runtime distribution (Q7) and v1/v2 output pairs (Q8), using the optimised library
// (lead/build-x86/librandomx.a, JIT + hardware AES), public API plus randomx_vm::run for per-program timing.
// usage: rxtime light|fast <nInputs> <repeats> <seed> <out.csv> [largePages] [initThreads]
//   For every input, v1 and v2 are computed alternately, `repeats` times; the CSV holds, per input and version,
//   the minimum over repeats of each program's time, of the fill, of the final hash and of the whole hash,
//   and the hash (identical across repeats, checked).
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cinttypes>
#include <chrono>
#include <thread>
#include <vector>
#include <string>
#include "randomx.h"
#include "virtual_machine.hpp"
#include "blake2/blake2.h"

static inline double nowNs() {
	return (double)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct Timing { double prog[RANDOMX_PROGRAM_COUNT]; double fill, fin, total; uint8_t hash[32]; };

static void timedHash(randomx_vm* vm, const void* in, size_t n, Timing& t) {
	alignas(16) uint64_t tempHash[8];
	double t0 = nowNs();
	blake2b(tempHash, sizeof(tempHash), in, n, nullptr, 0);
	vm->initScratchpad(&tempHash);
	vm->resetRoundingMode();
	double t1 = nowNs();
	t.fill = t1 - t0;
	for (int k = 0; k < RANDOMX_PROGRAM_COUNT; ++k) {
		double a = nowNs();
		vm->run(&tempHash);
		if (k < RANDOMX_PROGRAM_COUNT - 1)
			blake2b(tempHash, sizeof(tempHash), vm->getRegisterFile(), sizeof(randomx::RegisterFile), nullptr, 0);
		t.prog[k] = nowNs() - a;
	}
	double a = nowNs();
	vm->getFinalResult(t.hash, RANDOMX_HASH_SIZE);
	double b = nowNs();
	t.fin = b - a;
	t.total = b - t0;
	vm->resetRoundingMode();
}

int main(int argc, char** argv) {
	if (argc < 6) { fprintf(stderr, "usage: rxtime light|fast nInputs repeats seed out.csv [largePages] [initThreads]\n"); return 1; }
	bool fast = std::string(argv[1]) == "fast";
	int n = atoi(argv[2]);
	int reps = atoi(argv[3]);
	uint64_t seed = strtoull(argv[4], 0, 0);
	std::string out = argv[5];
	bool lp = argc > 6 && atoi(argv[6]) != 0;
	int initThreads = argc > 7 ? atoi(argv[7]) : 1;
	randomx_flags f = randomx_get_flags(); // JIT | HARD_AES | ARGON2 flags on this host
	if (lp) f = (randomx_flags)(f | RANDOMX_FLAG_LARGE_PAGES);
	randomx_cache* cache = randomx_alloc_cache(f);
	if (!cache && lp) { fprintf(stderr, "cache with large pages failed, retrying without\n"); cache = randomx_alloc_cache((randomx_flags)(f & ~RANDOMX_FLAG_LARGE_PAGES)); }
	if (!cache) { fprintf(stderr, "cache alloc failed\n"); return 1; }
	randomx_init_cache(cache, "test key 000", 12);
	randomx_dataset* ds = nullptr;
	if (fast) {
		ds = randomx_alloc_dataset(f);
		if (!ds) { fprintf(stderr, "dataset alloc with flags %d failed\n", (int)f); return 2; }
		double t0 = nowNs();
		unsigned long items = randomx_dataset_item_count();
		std::vector<std::thread> th;
		for (int i = 0; i < initThreads; ++i) {
			unsigned long a = items * i / initThreads, b = items * (i + 1) / initThreads;
			th.emplace_back(randomx_init_dataset, ds, cache, a, b - a);
		}
		for (auto& x : th) x.join();
		fprintf(stderr, "dataset init %.1f s\n", (nowNs() - t0) / 1e9);
	}
	randomx_flags base = (randomx_flags)(f | (fast ? RANDOMX_FLAG_FULL_MEM : 0));
	randomx_vm* vm1 = randomx_create_vm(base, fast ? nullptr : cache, ds);
	randomx_vm* vm2 = randomx_create_vm((randomx_flags)(base | RANDOMX_FLAG_V2), fast ? nullptr : cache, ds);
	if (fast) { randomx_release_cache(cache); cache = nullptr; }
	if (!vm1 || !vm2) { fprintf(stderr, "vm alloc failed\n"); return 3; }
	fprintf(stderr, "flags=%d fast=%d largePages=%d\n", (int)base, fast, lp);
	FILE* fo = fopen(out.c_str(), "w");
	fprintf(fo, "input,version,fill,p0,p1,p2,p3,p4,p5,p6,p7,final,total,hash\n");
	double tStart = nowNs();
	for (int i = 0; i < n; ++i) {
		uint64_t input[2] = { seed, (uint64_t)i };
		Timing best[2];
		for (int v = 0; v < 2; ++v) { best[v].total = 1e30; for (int k = 0; k < 8; ++k) best[v].prog[k] = 1e30; best[v].fill = best[v].fin = 1e30; }
		for (int r = 0; r < reps; ++r) {
			for (int vv = 0; vv < 2; ++vv) {
				int v = (r & 1) ? 1 - vv : vv; // alternate order
				Timing t;
				timedHash(v ? vm2 : vm1, input, sizeof(input), t);
				if (r > 0 && memcmp(t.hash, best[v].hash, 32)) { fprintf(stderr, "nondeterministic hash!\n"); return 4; }
				memcpy(best[v].hash, t.hash, 32);
				for (int k = 0; k < 8; ++k) if (t.prog[k] < best[v].prog[k]) best[v].prog[k] = t.prog[k];
				if (t.fill < best[v].fill) best[v].fill = t.fill;
				if (t.fin < best[v].fin) best[v].fin = t.fin;
				if (t.total < best[v].total) best[v].total = t.total;
			}
		}
		for (int v = 0; v < 2; ++v) {
			fprintf(fo, "%d,%d,%.0f", i, v + 1, best[v].fill);
			for (int k = 0; k < 8; ++k) fprintf(fo, ",%.0f", best[v].prog[k]);
			fprintf(fo, ",%.0f,%.0f,", best[v].fin, best[v].total);
			for (int q = 0; q < 32; ++q) fprintf(fo, "%02x", best[v].hash[q]);
			fprintf(fo, "\n");
		}
		if ((i + 1) % 200 == 0) { fflush(fo); fprintf(stderr, "%d/%d inputs, %.1f s\n", i + 1, n, (nowNs() - tStart) / 1e9); }
	}
	fclose(fo);
	randomx_destroy_vm(vm1);
	randomx_destroy_vm(vm2);
	if (ds) randomx_release_dataset(ds);
	if (cache) randomx_release_cache(cache);
	return 0;
}
