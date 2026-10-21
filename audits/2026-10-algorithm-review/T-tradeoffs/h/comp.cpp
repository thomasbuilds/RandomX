// comp: component timings on one pinned core.
//  items: JIT and interpreted Dataset item function, cold random items (Cache lines from DRAM),
//         warm (same item repeated: Cache lines in L1, i.e. pure SuperscalarHash compute), sequential blocks
//  overhead: AesGenerator1R fill (2 MiB), AesHash1R (2 MiB), hashAndFill, AesGenerator4R (3200 B, program
//            generation), Blake2b-512 of the 256-byte register file, x86 JIT compile of one program (v1, v2)
// Usage: comp core=3 reps=5 lp=1
#include "randomx.h"
#include "dataset.hpp"
#include "aes_hash.hpp"
#include "blake2/blake2.h"
#include "program.hpp"
#include "jit_compiler_x86.hpp"
#include <sched.h>
#include <sys/mman.h>
#include <chrono>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <map>

static std::map<std::string, std::string> args;
static long argi(const char* k, long d) { auto it = args.find(k); return it == args.end() ? d : atol(it->second.c_str()); }
static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
static uint64_t rs = 0x9E3779B97F4A7C15ull;
static uint64_t xr() { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }
static std::map<std::string, std::vector<double>> R;
static void rec(const std::string& k, double v) { R[k].push_back(v); }

int main(int argc, char** argv) {
	for (int i = 1; i < argc; ++i) {
		const char* e = strchr(argv[i], '=');
		if (e) args[std::string(argv[i], e - argv[i])] = e + 1;
	}
	int core = argi("core", 3), reps = argi("reps", 5), lp = argi("lp", 1);
	cpu_set_t s; CPU_ZERO(&s); CPU_SET(core, &s); sched_setaffinity(0, sizeof(s), &s);
	randomx_flags flags = (randomx_flags)(RANDOMX_FLAG_JIT | RANDOMX_FLAG_ARGON2_AVX2 | RANDOMX_FLAG_HARD_AES | (lp ? RANDOMX_FLAG_LARGE_PAGES : 0));
	randomx_cache* cache = randomx_alloc_cache(flags);
	int32_t seed = 0;
	randomx_init_cache(cache, &seed, 4);
	const uint32_t total = (uint32_t)randomx_dataset_item_count();
	alignas(64) static uint8_t out[64 * 4096];
	const int N = 200000;
	uint8_t* sp = (uint8_t*)mmap(nullptr, 2 << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | (lp ? MAP_HUGETLB : 0), -1, 0);
	memset(sp, 1, 2 << 20);
	alignas(64) uint8_t state[64], hash[64], fillState[64], prog3200[3200 + 64];
	for (int i = 0; i < 64; ++i) { state[i] = xr(); fillState[i] = xr(); }
	randomx::JitCompilerX86 jit;
	jit.enableAll();
	randomx::Program program;
	randomx::ProgramConfiguration pcfg;
	pcfg.readReg0 = 0; pcfg.readReg1 = 2; pcfg.readReg2 = 4; pcfg.readReg3 = 6; pcfg.eMask[0] = pcfg.eMask[1] = 0x3c000000000fffffull;
	for (int r = 0; r < reps; ++r) {
		// JIT item function, cold random items
		double t0 = now();
		for (int i = 0; i < N; ++i) { uint32_t it = xr() % total; cache->datasetInit(cache, out, it, it + 1); }
		rec("item_jit_cold_random_ns", 1e9 * (now() - t0) / N);
		// JIT item function, warm (the same item: Cache lines L1-resident)
		t0 = now();
		for (int i = 0; i < N; ++i) cache->datasetInit(cache, out, 12345, 12346);
		rec("item_jit_warm_ns", 1e9 * (now() - t0) / N);
		// JIT, sequential blocks of 4096 items (what Dataset initialisation does)
		t0 = now();
		uint32_t st = xr() % (total - 4096 * 50);
		for (int b = 0; b < 50; ++b) cache->datasetInit(cache, out, st + 4096 * b, st + 4096 * (b + 1));
		rec("item_jit_seqblock_ns", 1e9 * (now() - t0) / (4096 * 50));
		// interpreted SuperscalarHash (portable C++), cold and warm
		const int NI = 20000;
		t0 = now();
		for (int i = 0; i < NI; ++i) randomx::initDatasetItem(cache, out, xr() % total);
		rec("item_interp_cold_random_ns", 1e9 * (now() - t0) / NI);
		t0 = now();
		for (int i = 0; i < NI; ++i) randomx::initDatasetItem(cache, out, 12345);
		rec("item_interp_warm_ns", 1e9 * (now() - t0) / NI);
		// AES fill / hash of the 2 MiB scratchpad
		const int NA = 200;
		t0 = now(); for (int i = 0; i < NA; ++i) fillAes1Rx4<false>(state, 2 << 20, sp); rec("fill1R_hw_us", 1e6 * (now() - t0) / NA);
		t0 = now(); for (int i = 0; i < NA; ++i) hashAes1Rx4<false>(sp, 2 << 20, hash); rec("hash1R_hw_us", 1e6 * (now() - t0) / NA);
		t0 = now(); for (int i = 0; i < NA; ++i) hashAndFillAes1Rx4<false>(sp, 2 << 20, hash, fillState); rec("hashAndFill_hw_us", 1e6 * (now() - t0) / NA);
		t0 = now(); for (int i = 0; i < NA / 4; ++i) fillAes1Rx4<true>(state, 2 << 20, sp); rec("fill1R_sw_us", 1e6 * (now() - t0) / (NA / 4));
		t0 = now(); for (int i = 0; i < NA / 4; ++i) hashAes1Rx4<true>(sp, 2 << 20, hash); rec("hash1R_sw_us", 1e6 * (now() - t0) / (NA / 4));
		t0 = now(); for (int i = 0; i < NA / 4; ++i) hashAndFillAes1Rx4<true>(sp, 2 << 20, hash, fillState); rec("hashAndFill_sw_us", 1e6 * (now() - t0) / (NA / 4));
		// program generation (AesGenerator4R, 3200 bytes) and Blake2b of the register file
		const int NG = 100000;
		t0 = now(); for (int i = 0; i < NG; ++i) fillAes4Rx4<false>(state, 3200, prog3200); rec("gen4R_hw_us", 1e6 * (now() - t0) / NG);
		t0 = now(); for (int i = 0; i < NG / 4; ++i) fillAes4Rx4<true>(state, 3200, prog3200); rec("gen4R_sw_us", 1e6 * (now() - t0) / (NG / 4));
		t0 = now(); for (int i = 0; i < NG; ++i) blake2b(hash, 64, prog3200, 256, nullptr, 0); rec("blake2b_256B_us", 1e6 * (now() - t0) / NG);
		// JIT compilation of one program
		for (int v = 1; v <= 2; ++v) {
			jit.setFlags((randomx_flags)(RANDOMX_FLAG_JIT | RANDOMX_FLAG_HARD_AES | (v == 2 ? RANDOMX_FLAG_V2 : 0)));
			const int NJ = 20000;
			double tt = 0;
			for (int i = 0; i < NJ; ++i) {
				fillAes4Rx4<false>(state, sizeof(program), &program);
				double a = now();
				jit.generateProgram(program, pcfg);
				tt += now() - a;
			}
			rec(v == 1 ? "jit_compile_v1_us" : "jit_compile_v2_us", 1e6 * tt / NJ);
		}
		fprintf(stderr, "rep %d done\n", r);
	}
	for (auto& kv : R) {
		auto x = kv.second; std::sort(x.begin(), x.end());
		double med = x.size() % 2 ? x[x.size() / 2] : 0.5 * (x[x.size() / 2 - 1] + x[x.size() / 2]);
		printf("%-28s median %10.4f min %10.4f max %10.4f n=%zu\n", kv.first.c_str(), med, x.front(), x.back(), x.size());
	}
	return 0;
}
