// Agent S: single-bit influence experiments (Q3) and v1/v2 shared-work check (Q8).
#include <cstdio>
#include <cinttypes>
#include <chrono>
#include "rxs_driver.hpp"

using namespace rxs;
using randomx::NativeRegisterFile;

static const char* KEY = "test key 000";

enum Cls { C_INPUT = 0, C_SPAD, C_R_START, C_F_START, C_E_START, C_R_END, C_F_END, C_E_END, C_COUNT };
static const char* clsName[C_COUNT] = { "input", "spad", "R_start", "F_start", "E_start", "R_end", "F_end", "E_end" };

struct InjHooks : Hooks {
	Driver* d = nullptr;
	int k = -1, j = -1, cls = -1, reg = 0, bit = 0; // bit: 0..63 for R and source words, 0..127 (lane*64+b) for F/E end
	bool done = false;
	void preExec(int kk, int ic, NativeRegisterFile& nreg) override {
		if (kk != k || ic != j) return;
		if (cls == C_R_START) {
			nreg.r[reg] ^= 1ULL << bit;
			done = true;
		}
		else if (cls == C_F_START || cls == C_E_START) {
			// flip a bit of the 8-byte source word (two int32) and redo the conversion: a pure register-input flip
			int w = (cls == C_F_START) ? reg : 4 + reg;
			uint64_t word = load64(d->scratchpad + d->curSpAddr1 + 8 * w) ^ (1ULL << bit);
			alignas(16) uint64_t tmp[2] = { word, 0 };
			rx_vec_f128 v = rx_cvt_packed_int_vec_f128(tmp);
			if (cls == C_F_START) nreg.f[reg] = v; else nreg.e[reg] = d->maskE(v);
			done = true;
		}
	}
	void postExec(int kk, int ic, NativeRegisterFile& nreg) override {
		if (kk != k || ic != j) return;
		if (cls == C_R_END) { nreg.r[reg] ^= 1ULL << bit; done = true; }
		else if (cls == C_F_END) { int l = bit >> 6; setLane(nreg.f[reg], l, lane(nreg.f[reg], l) ^ (1ULL << (bit & 63))); done = true; }
		else if (cls == C_E_END) { int l = bit >> 6; setLane(nreg.e[reg], l, lane(nreg.e[reg], l) ^ (1ULL << (bit & 63))); done = true; }
	}
};

struct Snap {
	std::vector<uint8_t> sp;
	alignas(16) uint64_t seed[8];
	uint32_t mode;
	randomx::RegisterFile regEnd;
};

static int popcount256(const uint8_t* a, const uint8_t* b) {
	int c = 0;
	for (int i = 0; i < 32; ++i) c += __builtin_popcount(a[i] ^ b[i]);
	return c;
}

static bool regEqual(const randomx::RegisterFile& a, const randomx::RegisterFile& b) {
	return memcmp(a.r, b.r, sizeof(a.r)) == 0 && memcmp(a.f, b.f, sizeof(a.f)) == 0 && memcmp(a.e, b.e, sizeof(a.e)) == 0;
}

int cmdInfl(int argc, char** argv) {
	if (argc < 7) { fprintf(stderr, "infl <v2> <nInputs> <trialsPerInput> <seed> <out.csv>\n"); return 1; }
	bool v2 = atoi(argv[2]) != 0;
	int nInputs = atoi(argv[3]);
	int tpi = atoi(argv[4]);
	uint64_t seed = strtoull(argv[5], 0, 0);
	std::string out = argv[6];
	randomx_cache* cache = makeCache(KEY, strlen(KEY));
	Driver d(cache, v2);
	InjHooks inj;
	inj.d = &d;
	resetCounters();
	SplitMix rng(seed * 0x9E3779B97F4A7C15ULL + (v2 ? 7 : 3));
	FILE* fo = fopen(out.c_str(), "w");
	fprintf(fo, "version,input,cls,k,j,reg,bit,outcome,programsRun,watchEvent,hd\n");
	Snap S[RANDOMX_PROGRAM_COUNT + 1];
	for (auto& s : S) s.sp.resize(randomx::ScratchpadSize);
	auto t0 = std::chrono::steady_clock::now();
	uint64_t trialNo = 0;
	for (int in = 0; in < nInputs; ++in) {
		uint64_t input[2] = { seed, (uint64_t)in };
		// reference run with snapshots
		d.hooks = nullptr;
		d.seedFromInput(input, sizeof(input));
		d.fillScratchpad();
		d.resetMode();
		for (int k = 0; k < RANDOMX_PROGRAM_COUNT; ++k) {
			memcpy(S[k].sp.data(), d.scratchpad, randomx::ScratchpadSize);
			memcpy(S[k].seed, d.tempHash, 64);
			S[k].mode = G.curMode;
			d.runProgram(k, d.tempHash);
			S[k].regEnd = d.reg;
			if (k < RANDOMX_PROGRAM_COUNT - 1)
				blake2b(d.tempHash, sizeof(d.tempHash), &d.reg, sizeof(randomx::RegisterFile), nullptr, 0);
		}
		memcpy(S[RANDOMX_PROGRAM_COUNT].sp.data(), d.scratchpad, randomx::ScratchpadSize);
		S[RANDOMX_PROGRAM_COUNT].mode = G.curMode;
		uint8_t refHash[32];
		d.finalHash(refHash);
		rx_reset_float_state();
		{ // sanity: reference equals plain hash
			uint8_t h2[32];
			d.hash(input, sizeof(input), h2);
			if (memcmp(h2, refHash, 32)) { fprintf(stderr, "reference mismatch\n"); return 2; }
		}
		for (int t = 0; t < tpi; ++t, ++trialNo) {
			int cls;
			if (t == 0) cls = C_INPUT;
			else cls = 1 + (int)((trialNo) % (C_COUNT - 1));
			int k = (int)rng.below(RANDOMX_PROGRAM_COUNT);
			int j = (int)rng.below(RANDOMX_PROGRAM_ITERATIONS);
			int reg = 0, bit = 0;
			uint32_t spByte = 0;
			bool wantHd = (rng.below(40) == 0) || cls == C_INPUT;
			int outcome = 1, programsRun = 0, hd = -1;
			G.watchWord = -1; G.watchEvent = 0;
			if (cls == C_INPUT) {
				bit = (int)rng.below(128);
				uint64_t in2[2] = { input[0], input[1] };
				in2[bit >> 6] ^= 1ULL << (bit & 63);
				uint8_t h[32];
				d.hooks = nullptr;
				d.hash(in2, sizeof(in2), h);
				hd = popcount256(h, refHash);
				outcome = hd != 0;
				programsRun = 8;
				k = j = -1;
			}
			else {
				switch (cls) {
				case C_SPAD: spByte = (uint32_t)rng.below(randomx::ScratchpadSize); bit = (int)rng.below(8); j = -1; reg = (int)spByte; break;
				case C_R_START: case C_R_END: reg = (int)rng.below(8); bit = (int)rng.below(64); break;
				case C_F_START: case C_E_START: reg = (int)rng.below(4); bit = (int)rng.below(64); break;
				case C_F_END: case C_E_END: reg = (int)rng.below(4); bit = (int)rng.below(128); break;
				}
				// restore start of program k
				memcpy(d.scratchpad, S[k].sp.data(), randomx::ScratchpadSize);
				d.setMode(S[k].mode);
				alignas(16) uint64_t curSeed[8];
				memcpy(curSeed, S[k].seed, 64);
				bool diverged = false; // register file differs (certain hash change)
				int kk = k;
				for (; kk < RANDOMX_PROGRAM_COUNT; ++kk) {
					d.generate(curSeed);
					if (kk == k) {
						if (cls == C_SPAD) {
							d.scratchpad[spByte] ^= (uint8_t)(1u << bit);
							G.watchWord = spByte >> 3; G.watchEvent = 0;
							d.hooks = nullptr;
						}
						else {
							inj.k = k; inj.j = j; inj.cls = cls; inj.reg = reg; inj.bit = bit; inj.done = false;
							d.hooks = &inj;
						}
					}
					else d.hooks = nullptr;
					d.execute(kk);
					programsRun++;
					if (!diverged) {
						bool re = regEqual(d.reg, S[kk].regEnd);
						bool me = G.curMode == S[kk + 1].mode;
						bool se = memcmp(d.scratchpad, S[kk + 1].sp.data(), randomx::ScratchpadSize) == 0;
						if (re && me && se) { outcome = 0; break; }
						if (!re) {
							diverged = true;
							if (!wantHd) { outcome = 1; break; }
						}
					}
					if (kk < RANDOMX_PROGRAM_COUNT - 1)
						blake2b(curSeed, 64, &d.reg, sizeof(randomx::RegisterFile), nullptr, 0);
				}
				if (kk == RANDOMX_PROGRAM_COUNT) {
					uint8_t h[32];
					d.finalHash(h);
					hd = popcount256(h, refHash);
					outcome = hd != 0;
				}
				else if (outcome == 0) hd = 0;
				rx_reset_float_state();
				G.watchWord = -1;
			}
			fprintf(fo, "%d,%d,%s,%d,%d,%d,%d,%d,%d,%d,%d\n", v2 ? 2 : 1, in, clsName[cls], k, j, reg, bit, outcome, programsRun, G.watchEvent, hd);
		}
		fflush(fo);
		double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		fprintf(stderr, "[infl v%d] input %d/%d done, %.0f s\n", v2 ? 2 : 1, in + 1, nInputs, el);
	}
	fclose(fo);
	return 0;
}

// ---------------------------------------------------------------- v1/v2 shared work
// For n inputs: confirm identical seed/fill/first-program prefix, measure how far the v1 and v2 executions agree.
struct TraceHooks : Hooks {
	std::vector<std::array<uint64_t, 8>> rStart, rEnd;
	std::vector<std::array<uint64_t, 16>> fbl;
	int maxIter = 4;
	void preExec(int k, int ic, NativeRegisterFile& nreg) override {
		if (k == 0 && ic < maxIter) { std::array<uint64_t, 8> a; memcpy(a.data(), nreg.r, 64); rStart.push_back(a); }
	}
	void postExec(int k, int ic, NativeRegisterFile& nreg) override {
		if (k == 0 && ic < maxIter) { std::array<uint64_t, 8> a; memcpy(a.data(), nreg.r, 64); rEnd.push_back(a); }
	}
};

int cmdDiverge(int argc, char** argv) {
	int n = argc > 2 ? atoi(argv[2]) : 100;
	uint64_t seed = argc > 3 ? strtoull(argv[3], 0, 0) : 1;
	randomx_cache* cache = makeCache(KEY, strlen(KEY));
	Driver d1(cache, false), d2(cache, true);
	resetCounters();
	int fillEq = 0, prefixEq = 0, cfgEq = 0, iter0StartEq = 0, iter0EndEq = 0, iter1StartEq = 0, spEqAfterProg0 = 0;
	int r256eq = 0;
	for (int i = 0; i < n; ++i) {
		uint64_t input[2] = { seed, (uint64_t)i };
		d1.seedFromInput(input, 16); d2.seedFromInput(input, 16);
		d1.fillScratchpad(); d2.fillScratchpad();
		fillEq += memcmp(d1.scratchpad, d2.scratchpad, randomx::ScratchpadSize) == 0;
		d1.resetMode();
		d1.generate(d1.tempHash);
		d2.generate(d2.tempHash);
		// program buffer: config (128 B) + first 256 instructions identical?
		prefixEq += memcmp(&d1.program, &d2.program, 128 + 8 * 256) == 0;
		cfgEq += (d1.mem.ma == d2.mem.ma && d1.mem.mx == d2.mem.mx && d1.datasetOffset == d2.datasetOffset) ? 1 : 0;
		// execute iteration 0 of v2's program truncated to its first 256 instructions == v1 iteration 0 (integer part)
		{
			// v1 compiled view of the v2 program (first 256 instructions) - same bytes
			r256eq += memcmp((uint8_t*)&d2.program + 128, (uint8_t*)&d1.program + 128, 8 * 256) == 0;
		}
		TraceHooks t1, t2;
		d1.hooks = &t1; d2.hooks = &t2;
		d1.resetMode(); d1.execute(0);
		d2.resetMode(); d2.execute(0);
		d1.hooks = d2.hooks = nullptr;
		iter0StartEq += t1.rStart[0] == t2.rStart[0];
		iter0EndEq += t1.rEnd[0] == t2.rEnd[0];
		iter1StartEq += t1.rStart[1] == t2.rStart[1];
		spEqAfterProg0 += memcmp(d1.scratchpad, d2.scratchpad, randomx::ScratchpadSize) == 0;
	}
	rx_reset_float_state();
	printf("n=%d fillEqual=%d programPrefix(128+2048 B)Equal=%d ma/mx/datasetOffsetEqual=%d first256Equal=%d iter0RegsAtStartEqual=%d iter0RegsAfterProgramEqual=%d iter1RegsAtStartEqual=%d scratchpadEqualAfterProgram0=%d\n",
		n, fillEq, prefixEq, cfgEq, r256eq, iter0StartEq, iter0EndEq, iter1StartEq, spEqAfterProg0);
	return 0;
}
