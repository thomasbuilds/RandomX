// Agent S: direct measurement of floating-point absorption within one iteration (Q3).
// For each input: program 0, random iteration j in [0,63]; reference run up to the end of the program of iteration j,
// then for every bit b of the 8-byte source word (two int32) of a random F (resp. E) register: perturbed run;
// "absorbed" = the perturbed register is bit-identical to the reference after step 4 of iteration j.
// usage: rxs_absorb <v2> <nInputs> <seed> <out.csv>
#include <cstdio>
#include "rxs_driver.hpp"
using namespace rxs;
using randomx::NativeRegisterFile;
struct AH : Hooks {
	Driver* d; int j; int cls = -1, reg = 0, bit = 0; // cls 0 = F, 1 = E, -1 = none
	uint64_t f[8], e[8];
	void preExec(int k, int ic, NativeRegisterFile& nreg) override {
		if (ic != j || cls < 0) return;
		int w = cls == 0 ? reg : 4 + reg;
		uint64_t word = load64(d->scratchpad + d->curSpAddr1 + 8 * w) ^ (1ULL << bit);
		alignas(16) uint64_t tmp[2] = { word, 0 };
		rx_vec_f128 v = rx_cvt_packed_int_vec_f128(tmp);
		if (cls == 0) nreg.f[reg] = v; else nreg.e[reg] = d->maskE(v);
	}
	void postExec(int k, int ic, NativeRegisterFile& nreg) override {
		if (ic != j) return;
		for (int i = 0; i < 4; ++i) { f[2*i] = lane(nreg.f[i], 0); f[2*i+1] = lane(nreg.f[i], 1); e[2*i] = lane(nreg.e[i], 0); e[2*i+1] = lane(nreg.e[i], 1); }
	}
};
int main(int argc, char** argv) {
	bool v2 = atoi(argv[1]); int n = atoi(argv[2]); uint64_t seed = strtoull(argv[3], 0, 0);
	randomx_cache* cache = makeCache("test key 000", 12);
	Driver d(cache, v2);
	resetCounters();
	SplitMix rng(seed * 31 + v2);
	FILE* fo = fopen(argv[4], "w");
	fprintf(fo, "version,input,j,cls,reg,bit,absorbed\n");
	std::vector<uint8_t> sp(randomx::ScratchpadSize);
	for (int in = 0; in < n; ++in) {
		uint64_t input[2] = { seed, (uint64_t)in };
		d.seedFromInput(input, 16); d.fillScratchpad();
		memcpy(sp.data(), d.scratchpad, sp.size());
		alignas(16) uint64_t s0[8]; memcpy(s0, d.tempHash, 64);
		AH ref; ref.d = &d; ref.j = (int)rng.below(64); ref.cls = -1;
		d.stopAfter = ref.j; d.hooks = &ref; d.resetMode(); d.generate(s0); d.execute(0);
		for (int cls = 0; cls < 2; ++cls) for (int b = 0; b < 64; ++b) {
			AH t; t.d = &d; t.j = ref.j; t.cls = cls; t.reg = (int)rng.below(4); t.bit = b;
			memcpy(d.scratchpad, sp.data(), sp.size());
			d.hooks = &t; d.resetMode(); d.generate(s0); d.execute(0);
			bool same = cls == 0 ? (t.f[2*t.reg] == ref.f[2*t.reg] && t.f[2*t.reg+1] == ref.f[2*t.reg+1])
			                     : (t.e[2*t.reg] == ref.e[2*t.reg] && t.e[2*t.reg+1] == ref.e[2*t.reg+1]);
			fprintf(fo, "%d,%d,%d,%s,%d,%d,%d\n", v2 ? 2 : 1, in, ref.j, cls ? "E" : "F", t.reg, b, same ? 1 : 0);
		}
		memcpy(d.scratchpad, sp.data(), sp.size());
	}
	fclose(fo);
	rx_reset_float_state();
	return 0;
}
