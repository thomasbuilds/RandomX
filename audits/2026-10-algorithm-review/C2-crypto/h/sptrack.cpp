// Agent C2: Scratchpad write coverage and "initial content" reads (item 3), plus capture of step-10 mix samples (item 4).
// Links against the instrumented private copy of the library (agentC2/build-instr/librandomx.a).
// Usage: sptrack <v1|v2> <nhashes> <keyseed> <outprefix> [mixEvery]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>
#include "randomx.h"
#include "instr_hooks.hpp"

static constexpr uint32_t SP = 2097152, NW = SP / 8, NL = SP / 64;
static constexpr int ITERS = 2048, PROGS = 8, TOT = ITERS * PROGS;

struct SpTracker : instr::Tracker {
	std::vector<int32_t> firstWrite; // per 8-byte word: global iteration of first write, -1 = never
	std::vector<uint8_t> lineTouched;
	int iter = 0;
	uint64_t wordsWritten = 0, linesTouched = 0;
	// reads by class: 0 = instr read in L1 region, 1 = instr read in L2-only region, 2 = instr read in L3-only region,
	// 3 = step-2 line read, 4 = step-3 line read
	uint64_t rd[5] = {0}, rdInit[5] = {0};
	uint64_t rdProg[PROGS] = {0}, rdInitProg[PROGS] = {0};
	// time series sampled every 256 iterations
	std::vector<double> wordFrac, lineFrac;
	FILE* mixOut = nullptr; int mixEvery = 0;
	void reset() {
		firstWrite.assign(NW, -1); lineTouched.assign(NL, 0); iter = 0; wordsWritten = 0; linesTouched = 0;
		wordFrac.clear(); lineFrac.clear();
	}
	void read(uint32_t off, uint32_t size, int kind) override {
		for (uint32_t w = off / 8; w < (off + size) / 8; ++w) {
			int cls;
			if (kind == 1) cls = 3; else if (kind == 2) cls = 4;
			else cls = (off < 16384) ? 0 : (off < 262144 ? 1 : 2);
			rd[cls]++; rdProg[iter / ITERS]++;
			if (firstWrite[w] < 0) { rdInit[cls]++; rdInitProg[iter / ITERS]++; }
		}
	}
	void write(uint32_t off, uint32_t size, int kind) override {
		for (uint32_t w = off / 8; w < (off + size) / 8; ++w) {
			if (firstWrite[w] < 0) { firstWrite[w] = iter; wordsWritten++; }
			if (!lineTouched[w / 8]) { lineTouched[w / 8] = 1; linesTouched++; }
		}
	}
	void iterEnd() override {
		iter++;
		if (iter % 256 == 0) { wordFrac.push_back((double)wordsWritten / NW); lineFrac.push_back((double)linesTouched / NL); }
	}
	void mix(const void* fpre, const void* e, const void* fpost) override {
		if (mixOut && mixEvery > 0 && (iter % mixEvery) == 0) {
			fwrite(fpre, 64, 1, mixOut); fwrite(e, 64, 1, mixOut); fwrite(fpost, 64, 1, mixOut);
		}
	}
};

int main(int argc, char** argv) {
	if (argc < 5) { fprintf(stderr, "usage: %s v1|v2 nhashes keyseed outprefix [mixEvery]\n", argv[0]); return 1; }
	bool v2 = std::string(argv[1]) == "v2";
	int n = atoi(argv[2]);
	uint32_t keyseed = (uint32_t)strtoul(argv[3], nullptr, 0);
	std::string out = argv[4];
	int mixEvery = argc > 5 ? atoi(argv[5]) : 0;
	char key[32]; snprintf(key, sizeof key, "C2 key %08x", keyseed);
	randomx_flags fl = RANDOMX_FLAG_HARD_AES;
	randomx_cache* cache = randomx_alloc_cache(RANDOMX_FLAG_ARGON2_AVX2);
	randomx_init_cache(cache, key, strlen(key));
	randomx_vm* vm = randomx_create_vm(v2 ? (fl | RANDOMX_FLAG_V2) : fl, cache, nullptr);
	if (!vm) { fprintf(stderr, "vm failed\n"); return 1; }
	SpTracker t;
	if (mixEvery > 0) { t.mixOut = fopen((out + ".mix").c_str(), "wb"); t.mixEvery = mixEvery; }
	FILE* f = fopen((out + ".txt").c_str(), "w");
	std::vector<double> sumWord(TOT / 256, 0), sumLine(TOT / 256, 0);
	uint64_t rdAll[5] = {0}, rdInitAll[5] = {0}, rdP[PROGS] = {0}, rdIP[PROGS] = {0};
	double sumFinalWord = 0, sumFinalLine = 0, sumAvgWord = 0, sumAvgLine = 0;
	uint8_t input[76];
	for (int h = 0; h < n; ++h) {
		for (int i = 0; i < 76; ++i) input[i] = (uint8_t)(i * 37 + keyseed);
		memcpy(input + 39, &h, 4); // nonce field position as in Monero blobs
		t.reset();
		memset(t.rd, 0, sizeof t.rd); memset(t.rdInit, 0, sizeof t.rdInit);
		memset(t.rdProg, 0, sizeof t.rdProg); memset(t.rdInitProg, 0, sizeof t.rdInitProg);
		instr::g = &t;
		uint8_t hash[32];
		randomx_calculate_hash(vm, input, sizeof input, hash);
		instr::g = nullptr;
		double avgW = 0, avgL = 0;
		for (size_t k = 0; k < t.wordFrac.size(); ++k) { sumWord[k] += t.wordFrac[k]; sumLine[k] += t.lineFrac[k]; avgW += t.wordFrac[k]; avgL += t.lineFrac[k]; }
		avgW /= t.wordFrac.size(); avgL /= t.lineFrac.size();
		sumAvgWord += avgW; sumAvgLine += avgL;
		sumFinalWord += (double)t.wordsWritten / NW; sumFinalLine += (double)t.linesTouched / NL;
		for (int c = 0; c < 5; ++c) { rdAll[c] += t.rd[c]; rdInitAll[c] += t.rdInit[c]; }
		for (int p = 0; p < PROGS; ++p) { rdP[p] += t.rdProg[p]; rdIP[p] += t.rdInitProg[p]; }
		fprintf(f, "hash %d finalWordFrac %.5f finalLineFrac %.5f avgWordFrac %.5f avgLineFrac %.5f\n", h, (double)t.wordsWritten / NW, (double)t.linesTouched / NL, avgW, avgL);
	}
	fprintf(f, "SUMMARY version %s n %d key '%s'\n", v2 ? "v2" : "v1", n, key);
	fprintf(f, "mean final written fraction: words %.5f lines(touched) %.5f\n", sumFinalWord / n, sumFinalLine / n);
	fprintf(f, "mean time-averaged written fraction over the hash: words %.5f lines %.5f\n", sumAvgWord / n, sumAvgLine / n);
	const char* names[5] = {"instr read, L1 region", "instr read, L2-only region", "instr read, L3-only region", "step2 line read (spAddr0)", "step3 line read (spAddr1)"};
	uint64_t tot = 0, totInit = 0;
	for (int c = 0; c < 5; ++c) {
		tot += rdAll[c]; totInit += rdInitAll[c];
		fprintf(f, "reads %-28s words/hash %10.1f  initial-content fraction %.5f\n", names[c], (double)rdAll[c] / n, rdAll[c] ? (double)rdInitAll[c] / rdAll[c] : 0.0);
	}
	fprintf(f, "reads all: words/hash %.1f initial-content fraction %.5f\n", (double)tot / n, (double)totInit / tot);
	for (int p = 0; p < PROGS; ++p)
		fprintf(f, "program %d: reads/hash %.1f initial-content fraction %.5f\n", p, (double)rdP[p] / n, (double)rdIP[p] / rdP[p]);
	fprintf(f, "time series (iteration, mean written word fraction, mean touched line fraction):\n");
	for (size_t k = 0; k < sumWord.size(); ++k)
		fprintf(f, "%d %.5f %.5f\n", (int)(k + 1) * 256, sumWord[k] / n, sumLine[k] / n);
	fclose(f);
	if (t.mixOut) fclose(t.mixOut);
	randomx_destroy_vm(vm);
	randomx_release_cache(cache);
	return 0;
}
