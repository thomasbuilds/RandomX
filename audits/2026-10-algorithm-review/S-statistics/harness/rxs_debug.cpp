// Agent S: trace one F_start injection (v2): compare f/e registers after the program of iteration j, and the F block.
#include <cstdio>
#include <cmath>
#include <array>
#include "rxs_driver.hpp"
using namespace rxs;
using randomx::NativeRegisterFile;
struct Tr { std::vector<std::array<uint64_t,2>> f; std::vector<int> pcs; int reg; };
static void traceCb(void* ctx, int pc, NativeRegisterFile& nreg) {
	Tr* t = (Tr*)ctx; t->f.push_back({ lane(nreg.f[t->reg], 0), lane(nreg.f[t->reg], 1) }); t->pcs.push_back(pc);
}
struct DH : Hooks {
	Driver* d; int k, j, reg, bit; bool inject; uint64_t fpost[8], epost[8], fpre[8]; uint64_t fblk[8]; uint32_t sp0;
	int modeAtPost;
	void preExec(int kk, int ic, NativeRegisterFile& nreg) override {
		if (kk != k || ic != j) return;
		if (inject) {
			uint64_t word = load64(d->scratchpad + d->curSpAddr1 + 8 * reg) ^ (1ULL << bit);
			alignas(16) uint64_t tmp[2] = { word, 0 };
			nreg.f[reg] = rx_cvt_packed_int_vec_f128(tmp);
		}
		for (int i = 0; i < 4; ++i) { fpre[2*i] = lane(nreg.f[i], 0); fpre[2*i+1] = lane(nreg.f[i], 1); }
	}
	void postExec(int kk, int ic, NativeRegisterFile& nreg) override {
		if (kk != k || ic != j) return;
		for (int i = 0; i < 4; ++i) { fpost[2*i] = lane(nreg.f[i], 0); fpost[2*i+1] = lane(nreg.f[i], 1); epost[2*i] = lane(nreg.e[i], 0); epost[2*i+1] = lane(nreg.e[i], 1); }
		modeAtPost = G.curMode;
	}
	void blocks(int kk, int ic, const uint64_t* rb, const uint64_t* fb, uint32_t s0, uint32_t s1) override {
		if (kk != k || ic != j) return;
		memcpy(fblk, fb, 64); sp0 = s0;
	}
};
int main(int argc, char** argv) {
	int in = atoi(argv[1]), k = atoi(argv[2]), j = atoi(argv[3]), reg = atoi(argv[4]), bit = atoi(argv[5]);
	randomx_cache* cache = makeCache("test key 000", 12);
	Driver d(cache, true);
	resetCounters();
	uint64_t input[2] = { 11, (uint64_t)in };
	d.seedFromInput(input, 16); d.fillScratchpad(); d.resetMode();
	for (int q = 0; q < k; ++q) { d.runProgram(q, d.tempHash); blake2b(d.tempHash, 64, &d.reg, sizeof(d.reg), nullptr, 0); }
	std::vector<uint8_t> sp(d.scratchpad, d.scratchpad + randomx::ScratchpadSize);
	uint32_t mode = G.curMode;
	alignas(16) uint64_t seed[8]; memcpy(seed, d.tempHash, 64);
	DH h[2];
	Tr tr[2];
	for (int t = 0; t < 2; ++t) {
		memcpy(d.scratchpad, sp.data(), sp.size()); d.setMode(mode);
		tr[t].reg = reg; d.traceK = k; d.traceJ = j; d.traceFn = traceCb; d.traceCtx = &tr[t];
		h[t].d = &d; h[t].k = k; h[t].j = j; h[t].reg = reg; h[t].bit = bit; h[t].inject = t == 1;
		d.hooks = &h[t]; d.generate(seed); d.execute(k);
	}
	printf("input %d k %d j %d reg %d bit %d  mode at end of program of iteration j: ref %d pert %d\n", in, k, j, reg, bit, h[0].modeAtPost, h[1].modeAtPost);
	for (int i = 0; i < 8; ++i) {
		double a, b, c, e; memcpy(&a, &h[0].fpre[i], 8); memcpy(&b, &h[1].fpre[i], 8); memcpy(&c, &h[0].fpost[i], 8); memcpy(&e, &h[1].fpost[i], 8);
		printf("  f lane %d: pre ref %.17g pert %.17g | post ref %.17g pert %.17g %s\n", i, a, b, c, e, h[0].fpost[i] == h[1].fpost[i] ? "EQUAL" : "differ");
	}
	for (int i = 0; i < 8; ++i) printf("  F block word %d: %016llx %016llx %s\n", i, (unsigned long long)h[0].fblk[i], (unsigned long long)h[1].fblk[i], h[0].fblk[i] == h[1].fblk[i] ? "" : "<-");
	// first point where the traced register becomes equal again, with the surrounding values
	for (size_t q = 0; q < tr[0].f.size() && q < tr[1].f.size(); ++q) {
		bool eq = tr[0].f[q] == tr[1].f[q];
		bool preveq = q > 0 && tr[0].f[q-1] == tr[1].f[q-1];
		if (eq && !preveq) {
			for (size_t r = (q > 3 ? q - 3 : 0); r <= q; ++r) {
				double a0, a1, b0, b1; memcpy(&a0, &tr[0].f[r][0], 8); memcpy(&a1, &tr[0].f[r][1], 8); memcpy(&b0, &tr[1].f[r][0], 8); memcpy(&b1, &tr[1].f[r][1], 8);
				auto& ins = d.program(tr[0].pcs[r]);
				printf("    step %zu pc %d opcode %d dst %d src %d: ref (%.17g, %.17g) pert (%.17g, %.17g)\n", r, tr[0].pcs[r], ins.opcode, ins.dst, ins.src, a0, a1, b0, b1);
			}
			break;
		}
	}
	double mx = 0; for (auto& v : tr[0].f) for (int l = 0; l < 2; ++l) { double x; memcpy(&x, &v[l], 8); if (fabs(x) > mx) mx = fabs(x); }
	printf("  max |f%d| during the iteration (reference): %.6g (2^%.2f)\n", reg, mx, log2(mx));
	// print the instructions of the program touching f[reg]
	int fopsReg = 0; unsigned n = d.progSize();
	for (unsigned i = 0; i < n; ++i) {
		auto& ins = d.program(i); int op = ins.opcode;
		bool isF = (op >= randomx::ceil_ISWAP_R && op < randomx::ceil_FSCAL_R);
		if (isF && (ins.dst % 4) == reg && !(op < randomx::ceil_FSWAP_R && (ins.dst % 8) >= 4)) fopsReg++;
	}
	printf("  F instructions with dst f%d: %d\n", reg, fopsReg);
	return 0;
}
