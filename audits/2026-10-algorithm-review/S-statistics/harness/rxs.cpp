// Agent S experiment driver. Subcommands:
//   verify                      : bit-exactness of the instrumented driver vs test vectors and the library VM
//   stats <v2> <n> <seed> <out> [dumpSpads] : per-iteration/per-program statistics over n random hashes
//   infl  <v2> <nInputs> <trialsPerInput> <seed> <out> : single-bit influence experiments (Q3)
//   diverge <n> <seed>          : v1/v2 shared work for the same input (Q8)
#include <cstdio>
#include <cmath>
#include <cinttypes>
#include <chrono>
#include <map>
#include "rxs_driver.hpp"

using namespace rxs;
using randomx::NativeRegisterFile;

static const char* KEY = "test key 000";

static double now() {
	return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static void writeBin(const std::string& fn, const void* p, size_t n) {
	FILE* f = fopen(fn.c_str(), "wb");
	if (!f) { perror(fn.c_str()); exit(1); }
	fwrite(p, 1, n, f);
	fclose(f);
}

// ---------------------------------------------------------------- verify
static int cmdVerify() {
	randomx_cache* cache = makeCache(KEY, strlen(KEY));
	int bad = 0;
	const char* in = "This is a test";
	const char* exp[2] = { "639183aae1bf4c9a35884cb46b09cad9175f04efd7684e7262a0ac1c2f0b4e3f",
	                       "22ec6b861b3eb23686b2efbad69513c967ecfce80983df66c9c5b4fbfb4cdb6f" };
	for (int v = 0; v < 2; ++v) {
		Driver d(cache, v == 1);
		uint8_t h[32];
		resetCounters();
		d.hash(in, strlen(in), h);
		std::string s = hex(h, 32);
		printf("driver v%d: %s %s\n", v + 1, s.c_str(), s == exp[v] ? "OK" : "MISMATCH");
		if (s != exp[v]) bad++;
	}
	// compare with the library's own light VM (JIT) for random inputs
	randomx_flags f = randomx_get_flags();
	for (int v = 0; v < 2; ++v) {
		randomx_flags vf = (randomx_flags)((f & ~(RANDOMX_FLAG_FULL_MEM)) | (v ? RANDOMX_FLAG_V2 : 0));
		randomx_vm* vm = randomx_create_vm(vf, cache, nullptr);
		Driver d(cache, v == 1);
		int n = 20, ok = 0;
		for (int i = 0; i < n; ++i) {
			uint64_t input[2] = { 0x5eed0000ULL + i, (uint64_t)v };
			uint8_t h1[32], h2[32];
			randomx_calculate_hash(vm, input, sizeof(input), h1);
			d.hash(input, sizeof(input), h2);
			if (memcmp(h1, h2, 32) == 0) ok++;
		}
		printf("driver vs library JIT VM v%d: %d/%d equal\n", v + 1, ok, n);
		if (ok != n) bad++;
		randomx_destroy_vm(vm);
	}
	// timing of the driver
	for (int v = 0; v < 2; ++v) {
		Driver d(cache, v == 1);
		double t0 = now();
		int n = 10;
		for (int i = 0; i < n; ++i) { uint64_t input = i; uint8_t h[32]; d.hash(&input, 8, h); }
		printf("driver v%d: %.1f ms per hash\n", v + 1, (now() - t0) * 1000 / n);
	}
	return bad;
}

// ---------------------------------------------------------------- static program analysis
struct StaticCounts {
	int cfround = 0, cbranch = 0, fdiv = 0, fsqrt = 0, fmul = 0, istore = 0, istoreL3 = 0, imulh = 0;
};
static StaticCounts staticCounts(randomx::Program& p, unsigned n) {
	StaticCounts c;
	for (unsigned i = 0; i < n; ++i) {
		auto& in = p(i);
		int op = in.opcode;
		if (op >= randomx::ceil_IMUL_M && op < randomx::ceil_ISMULH_M) c.imulh++;
		if (op >= randomx::ceil_FMUL_R - RANDOMX_FREQ_FMUL_R && op < randomx::ceil_FMUL_R) c.fmul++;
		else if (op >= randomx::ceil_FSQRT_R - RANDOMX_FREQ_FSQRT_R && op < randomx::ceil_FSQRT_R) c.fsqrt++;
		else if (op >= randomx::ceil_FDIV_M - RANDOMX_FREQ_FDIV_M && op < randomx::ceil_FDIV_M) c.fdiv++;
		else if (op >= randomx::ceil_CBRANCH - RANDOMX_FREQ_CBRANCH && op < randomx::ceil_CBRANCH) c.cbranch++;
		else if (op >= randomx::ceil_CFROUND - RANDOMX_FREQ_CFROUND && op < randomx::ceil_CFROUND) c.cfround++;
		else if (op >= randomx::ceil_ISTORE - RANDOMX_FREQ_ISTORE && op < randomx::ceil_ISTORE) {
			c.istore++;
			if (in.getModCond() >= randomx::StoreL3Condition) c.istoreL3++;
		}
	}
	return c;
}

// ---------------------------------------------------------------- stats
struct ProgRec {
	uint32_t hashIdx, k;
	uint16_t sCfround, sCbranch, sFdiv, sFsqrt, sFmul, sIstore, sIstoreL3, sImulh;
	uint32_t eInfIters;      // iterations ending with >=1 infinite E lane
	uint32_t mxcsr;          // accumulated MXCSR status flags during the program
	uint64_t dynInstr;
	uint32_t jumps, itersWithJump;
	uint64_t fpOps[4];
	uint32_t cfExec, cfWrite, cfChange;
	uint32_t modeStart, modeEnd;
	uint32_t maxConsec;
	uint32_t fZeroLanes, eZeroLanes;
	double seconds;          // wall time of the program in the instrumented interpreter (indicative only)
};

struct StatsHooks : Hooks {
	Driver* d = nullptr;
	uint64_t rHist[64][256];
	uint64_t fHist[64][256];
	uint64_t fExp[4096], eExp[4096], fblkTop12[4096];
	uint64_t fblkTop16[65536];
	uint64_t eTop16[65536];
	uint64_t iters = 0, itersEInf = 0, eInfLanes = 0, fInfLanes = 0, fZeroLanes = 0, eZeroLanes = 0, fNaNLanes = 0, eNaNLanes = 0, denormLanes = 0;
	uint64_t eLanes = 0, fLanes = 0;
	uint64_t jumpsPerIter[16];
	uint64_t reexecPerIter[4097];
	ProgRec cur;
	std::vector<ProgRec> recs;
	uint64_t snapDyn, snapFp[4], snapCfE, snapCfW, snapCfC, snapTaken;
	double t0;
	StatsHooks() { memset(this->rHist, 0, (char*)&recs - (char*)this->rHist); }
	void programStart(int k) override {
		memset(&cur, 0, sizeof(cur));
		cur.k = k;
		auto sc = staticCounts(d->program, d->progSize());
		cur.sCfround = sc.cfround; cur.sCbranch = sc.cbranch; cur.sFdiv = sc.fdiv; cur.sFsqrt = sc.fsqrt;
		cur.sFmul = sc.fmul; cur.sIstore = sc.istore; cur.sIstoreL3 = sc.istoreL3; cur.sImulh = sc.imulh;
		snapDyn = G.dynInstr; memcpy(snapFp, G.fpOps, sizeof(snapFp));
		snapCfE = G.cfroundExec; snapCfW = G.cfroundWrite; snapCfC = G.cfroundChange; snapTaken = G.cbranchTaken;
		G.mxcsrFlags = 0; G.maxConsec = 0;
		cur.modeStart = G.curMode;
		t0 = now();
	}
	void postExec(int k, int ic, NativeRegisterFile& nreg) override {
		iters++;
		bool einf = false;
		for (int i = 0; i < 4; ++i) for (int l = 0; l < 2; ++l) {
			uint64_t f = lane(nreg.f[i], l), e = lane(nreg.e[i], l);
			fExp[f >> 52]++; eExp[e >> 52]++;
			eTop16[e >> 48]++;
			fLanes++; eLanes++;
			uint64_t fe = (f >> 52) & 0x7FF, fm = f & ((1ULL << 52) - 1);
			uint64_t ee = (e >> 52) & 0x7FF, em = e & ((1ULL << 52) - 1);
			if (fe == 0x7FF) { if (fm) fNaNLanes++; else fInfLanes++; }
			if (ee == 0x7FF) { if (em) eNaNLanes++; else { eInfLanes++; einf = true; } }
			if (fe == 0) { if (fm) denormLanes++; else { fZeroLanes++; cur.fZeroLanes++; } }
			if (ee == 0) { if (em) denormLanes++; else { eZeroLanes++; cur.eZeroLanes++; } }
		}
		if (einf) { itersEInf++; cur.eInfIters++; }
		uint32_t j = G.jumpsIter;
		jumpsPerIter[j < 15 ? j : 15]++;
		if (j) cur.itersWithJump++;
		uint32_t r = G.reexecIter;
		reexecPerIter[r < 4096 ? r : 4096]++;
	}
	void blocks(int k, int ic, const uint64_t* rb, const uint64_t* fb, uint32_t spAddr0, uint32_t spAddr1) override {
		const uint8_t* r8 = (const uint8_t*)rb;
		const uint8_t* f8 = (const uint8_t*)fb;
		for (int i = 0; i < 64; ++i) { rHist[i][r8[i]]++; fHist[i][f8[i]]++; }
		for (int i = 0; i < 8; ++i) { fblkTop12[fb[i] >> 52]++; fblkTop16[fb[i] >> 48]++; }
		if (G.lw) {
			for (int i = 0; i < 8; ++i) G.lw[spAddr1 / 8 + i] = 1;
			for (int i = 0; i < 8; ++i) G.lw[spAddr0 / 8 + i] = 2;
		}
	}
	void programEnd(int k) override {
		cur.mxcsr = G.mxcsrFlags;
		cur.dynInstr = G.dynInstr - snapDyn;
		for (int i = 0; i < 4; ++i) cur.fpOps[i] = G.fpOps[i] - snapFp[i];
		cur.cfExec = G.cfroundExec - snapCfE; cur.cfWrite = G.cfroundWrite - snapCfW; cur.cfChange = G.cfroundChange - snapCfC;
		cur.jumps = G.cbranchTaken - snapTaken;
		cur.modeEnd = G.curMode;
		cur.maxConsec = G.maxConsec;
		cur.seconds = now() - t0;
		recs.push_back(cur);
	}
};

static int cmdStats(int argc, char** argv) {
	if (argc < 6) { fprintf(stderr, "stats <v2> <n> <seed> <outprefix> [dumpSpads]\n"); return 1; }
	bool v2 = atoi(argv[2]) != 0;
	int n = atoi(argv[3]);
	uint64_t seed = strtoull(argv[4], 0, 0);
	std::string out = argv[5];
	int dumpSpads = argc > 6 ? atoi(argv[6]) : 0;
	randomx_cache* cache = makeCache(KEY, strlen(KEY));
	Driver d(cache, v2);
	StatsHooks* h = new StatsHooks();
	h->d = &d;
	d.hooks = h;
	resetCounters();
	std::vector<uint8_t> wbm(randomx::ScratchpadSize / 64), lw(randomx::ScratchpadSize / 8);
	G.wbm = wbm.data();
	G.lw = lw.data();
	std::vector<uint8_t> init(randomx::ScratchpadSize);
	// final scratchpad statistics
	static uint64_t finalHist[4][8][256];      // [last-writer class][byte pos mod 8][value]
	static uint64_t finalClassWords[4];
	memset(finalHist, 0, sizeof(finalHist));
	memset(finalClassWords, 0, sizeof(finalClassWords));
	FILE* fh = fopen((out + "_hash.csv").c_str(), "w");
	fprintf(fh, "idx,allRNE,written_L1,written_L2only,written_L3only,changed_L1,changed_L2only,changed_L3only,fpOps0,fpOps1,fpOps2,fpOps3,hash\n");
	double t0 = now();
	for (int i = 0; i < n; ++i) {
		uint64_t input[2] = { seed, (uint64_t)i };
		memset(wbm.data(), 0, wbm.size());
		memset(lw.data(), 0, lw.size());
		uint64_t fp0[4]; memcpy(fp0, G.fpOps, sizeof(fp0));
		// replicate Driver::hash to capture the initial fill
		d.seedFromInput(input, sizeof(input));
		d.fillScratchpad();
		memcpy(init.data(), d.scratchpad, init.size());
		d.resetMode();
		for (int k = 0; k < RANDOMX_PROGRAM_COUNT; ++k) {
			d.runProgram(k, d.tempHash);
			for (size_t q = h->recs.size() - 1; q < h->recs.size(); ++q) h->recs[q].hashIdx = i;
			if (k < RANDOMX_PROGRAM_COUNT - 1)
				blake2b(d.tempHash, sizeof(d.tempHash), &d.reg, sizeof(randomx::RegisterFile), nullptr, 0);
		}
		// final scratchpad analysis before the final hash
		uint64_t wr[3] = { 0, 0, 0 }, ch[3] = { 0, 0, 0 };
		const uint64_t* a = (const uint64_t*)init.data();
		const uint64_t* b = (const uint64_t*)d.scratchpad;
		for (uint32_t w = 0; w < randomx::ScratchpadSize / 8; ++w) {
			int region = w < 2048 ? 0 : (w < 32768 ? 1 : 2);
			if (wbm[w >> 3] & (1 << (w & 7))) wr[region]++;
			if (a[w] != b[w]) ch[region]++;
			int cls = lw[w];
			finalClassWords[cls]++;
			const uint8_t* bb = (const uint8_t*)&b[w];
			for (int q = 0; q < 8; ++q) finalHist[cls][q][bb[q]]++;
		}
		if (i < dumpSpads) {
			char fn[512];
			snprintf(fn, sizeof fn, "%s_spad%04d.bin", out.c_str(), i);
			writeBin(fn, d.scratchpad, randomx::ScratchpadSize);
		}
		uint8_t hash[32];
		d.finalHash(hash);
		rx_reset_float_state();
		bool allRNE = (G.fpOps[1] - fp0[1]) + (G.fpOps[2] - fp0[2]) + (G.fpOps[3] - fp0[3]) == 0;
		fprintf(fh, "%d,%d,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%s\n",
			i, allRNE ? 1 : 0, wr[0], wr[1], wr[2], ch[0], ch[1], ch[2],
			G.fpOps[0] - fp0[0], G.fpOps[1] - fp0[1], G.fpOps[2] - fp0[2], G.fpOps[3] - fp0[3], hex(hash, 32).c_str());
		if ((i + 1) % 50 == 0) {
			fflush(fh);
			fprintf(stderr, "[%s] %d/%d hashes, %.1f s\n", out.c_str(), i + 1, n, now() - t0);
		}
	}
	fclose(fh);
	// program records
	FILE* fp = fopen((out + "_prog.csv").c_str(), "w");
	fprintf(fp, "hash,k,sCfround,sCbranch,sFdiv,sFsqrt,sFmul,sIstore,sIstoreL3,sImulh,eInfIters,mxcsr,dynInstr,jumps,itersWithJump,fp0,fp1,fp2,fp3,cfExec,cfWrite,cfChange,modeStart,modeEnd,maxConsec,fZeroLanes,eZeroLanes,seconds\n");
	for (auto& r : h->recs) {
		fprintf(fp, "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%" PRIu64 ",%u,%u,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%u,%u,%u,%u,%u,%u,%u,%u,%.6f\n",
			r.hashIdx, r.k, r.sCfround, r.sCbranch, r.sFdiv, r.sFsqrt, r.sFmul, r.sIstore, r.sIstoreL3, r.sImulh, r.eInfIters, r.mxcsr,
			r.dynInstr, r.jumps, r.itersWithJump, r.fpOps[0], r.fpOps[1], r.fpOps[2], r.fpOps[3], r.cfExec, r.cfWrite, r.cfChange,
			r.modeStart, r.modeEnd, r.maxConsec, r.fZeroLanes, r.eZeroLanes, r.seconds);
	}
	fclose(fp);
	writeBin(out + "_rhist.bin", h->rHist, sizeof(h->rHist));
	writeBin(out + "_fhist.bin", h->fHist, sizeof(h->fHist));
	writeBin(out + "_fexp.bin", h->fExp, sizeof(h->fExp));
	writeBin(out + "_eexp.bin", h->eExp, sizeof(h->eExp));
	writeBin(out + "_fblk12.bin", h->fblkTop12, sizeof(h->fblkTop12));
	writeBin(out + "_fblk16.bin", h->fblkTop16, sizeof(h->fblkTop16));
	writeBin(out + "_etop16.bin", h->eTop16, sizeof(h->eTop16));
	writeBin(out + "_jumpsPerIter.bin", h->jumpsPerIter, sizeof(h->jumpsPerIter));
	writeBin(out + "_reexecPerIter.bin", h->reexecPerIter, sizeof(h->reexecPerIter));
	writeBin(out + "_distHist.bin", G.distHist, sizeof(G.distHist));
	writeBin(out + "_consecHist.bin", G.consecHist, sizeof(G.consecHist));
	writeBin(out + "_finalHist.bin", finalHist, sizeof(finalHist));
	writeBin(out + "_finalClassWords.bin", finalClassWords, sizeof(finalClassWords));
	FILE* fs = fopen((out + "_summary.txt").c_str(), "w");
	fprintf(fs, "version=%d n=%d seed=%" PRIu64 " key=%s\n", v2 ? 2 : 1, n, seed, KEY);
	fprintf(fs, "iters=%" PRIu64 " itersEInf=%" PRIu64 " eInfLanes=%" PRIu64 " fInfLanes=%" PRIu64 " fZeroLanes=%" PRIu64 " eZeroLanes=%" PRIu64 " fNaN=%" PRIu64 " eNaN=%" PRIu64 " denormLanes=%" PRIu64 " lanes=%" PRIu64 "\n",
		h->iters, h->itersEInf, h->eInfLanes, h->fInfLanes, h->fZeroLanes, h->eZeroLanes, h->fNaNLanes, h->eNaNLanes, h->denormLanes, h->eLanes);
	fprintf(fs, "opResultLanes=%" PRIu64 " resNaN=%" PRIu64 " resDenorm=%" PRIu64 " resInf=%" PRIu64 " resZero=%" PRIu64 "\n", G.resLanes, G.resNaN, G.resDenorm, G.resInf, G.resZero);
	fprintf(fs, "dynInstr=%" PRIu64 " cbranchExec=%" PRIu64 " cbranchTaken=%" PRIu64 " cfroundExec=%" PRIu64 " cfroundWrite=%" PRIu64 " cfroundChange=%" PRIu64 "\n",
		G.dynInstr, G.cbranchExec, G.cbranchTaken, G.cfroundExec, G.cfroundWrite, G.cfroundChange);
	fprintf(fs, "fpOps=%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n", G.fpOps[0], G.fpOps[1], G.fpOps[2], G.fpOps[3]);
	fprintf(fs, "seconds=%.1f\n", now() - t0);
	fclose(fs);
	return 0;
}

int cmdInfl(int argc, char** argv);
int cmdDiverge(int argc, char** argv);

int main(int argc, char** argv) {
	if (argc < 2) { fprintf(stderr, "usage: rxs verify|stats|infl|diverge ...\n"); return 1; }
	std::string c = argv[1];
	if (c == "verify") return cmdVerify();
	if (c == "stats") return cmdStats(argc, argv);
	if (c == "infl") return cmdInfl(argc, argv);
	if (c == "diverge") return cmdDiverge(argc, argv);
	fprintf(stderr, "unknown command\n");
	return 1;
}
