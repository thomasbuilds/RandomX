// Agent C2 (item 5): how often do SuperscalarHash programs have constant (input-independent) output bits?
// Programs are generated exactly as for a Cache (Blake2Generator(key) -> 8 x generateSuperscalar), for many keys.
// Each program is run on 1024 uniformly random inputs; a bit is constant if it never changes (a fair bit would
// fail this with probability 2^-1023). Also counts strongly biased bits (|P(1)-1/2| > 0.25) and the last program.
// Usage: sshconst <keys> [samples]
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>
#include <string>
#include "superscalar.hpp"
#include "superscalar_program.hpp"
#include "blake2_generator.hpp"

using namespace randomx;
int main(int argc, char** argv) {
	int K = argc > 1 ? atoi(argv[1]) : 500;
	int M = argc > 2 ? atoi(argv[2]) : 1024;
	std::mt19937_64 rng(2024);
	uint64_t progs = 0, progsWithConst = 0, constBits = 0, biasedBits = 0, lastProgs = 0, lastWithConst = 0, lastConstBits = 0;
	uint64_t keysWithConstInLast = 0;
	std::vector<uint64_t> posConst(64, 0); uint64_t addrRegConstLow = 0;
	std::vector<uint64_t> hist(65, 0);
	for (int k = 0; k < K; ++k) {
		char key[32]; int len = snprintf(key, sizeof key, "C2 survey key %d", k);
		Blake2Generator gen(key, len);
		for (int p = 0; p < 8; ++p) {
			SuperscalarProgram prog;
			generateSuperscalar(prog, gen);
			uint64_t andv[8], orv[8]; std::vector<uint64_t> ones(512, 0);
			for (int q = 0; q < 8; ++q) { andv[q] = ~0ULL; orv[q] = 0; }
			for (int s = 0; s < M; ++s) {
				int_reg_t r[8]; for (int q = 0; q < 8; ++q) r[q] = rng();
				executeSuperscalar(r, prog, nullptr);
				for (int q = 0; q < 8; ++q) { andv[q] &= r[q]; orv[q] |= r[q]; for (int b = 0; b < 64; ++b) ones[q * 64 + b] += (r[q] >> b) & 1; }
			}
			int c = 0;
			for (int q = 0; q < 8; ++q) {
				uint64_t cm = ~(andv[q] ^ orv[q]);
				c += __builtin_popcountll(cm);
				for (int b = 0; b < 64; ++b) if ((cm >> b) & 1) posConst[b]++;
				if (q == prog.getAddressRegister() && (cm & 0x3FFFFF)) addrRegConstLow++;
			}
			for (int b = 0; b < 512; ++b) if (fabs((double)ones[b] / M - 0.5) > 0.25) biasedBits++;
			progs++; constBits += c; if (c) progsWithConst++; hist[c > 64 ? 64 : c]++;
			if (p == 7) { lastProgs++; lastConstBits += c; if (c) { lastWithConst++; keysWithConstInLast++; } }
		}
	}
	printf("%llu programs (%d keys x 8), %d random inputs each\n", (unsigned long long)progs, K, M);
	printf("programs with >= 1 constant output bit: %.2f%%; mean constant bits per program %.2f of 512; mean bits with |P(1)-1/2| > 0.25: %.2f\n",
		100.0 * progsWithConst / progs, (double)constBits / progs, (double)biasedBits / progs);
	printf("last program (SuperscalarHash[7], its constant bits pass into Dataset items as Cache bits XOR a constant): %.2f%% of keys have >= 1 constant bit; mean %.2f bits\n",
		100.0 * lastWithConst / lastProgs, (double)lastConstBits / lastProgs);
	printf("programs whose address register has a constant bit among bits 0-21 (masked by the Cache XOR anyway): %llu\n", (unsigned long long)addrRegConstLow);
	printf("histogram of constant bits per program (count: programs):");
	for (int c = 0; c <= 64; ++c) if (hist[c]) printf(" %d:%llu", c, (unsigned long long)hist[c]);
	printf("\nconstant bits by bit position within a register (0 = least significant):");
	for (int b = 0; b < 64; ++b) if (posConst[b]) printf(" %d:%llu", b, (unsigned long long)posConst[b]);
	printf("\n");
	return 0;
}
