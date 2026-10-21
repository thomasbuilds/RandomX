// Agent C2 (item 3): small statistical battery for AesGenerator1R / AesGenerator4R output and a structural check of
// AesHash1R (TestU01 and PractRand are not installed). Seeds = Blake2b-512 of a counter (as in RandomX), plus the
// all-zero seed for AesGenerator1R (design.md Appendix F reports a Crush failure for it).
// Usage: aesbattery <buffers> <bytesPerBuffer>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <vector>
#include <unordered_set>
#include <string>
#include "aes_hash.hpp"
#include "blake2/blake2.h"

// chi-square -> z via Wilson-Hilferty
static double chiZ(double chi, double k) { return (pow(chi / k, 1.0 / 3) - (1 - 2 / (9 * k))) / sqrt(2 / (9 * k)); }

struct Battery {
	uint64_t ones = 0, bits = 0;
	std::vector<uint64_t> byteHist = std::vector<uint64_t>(256, 0);
	std::vector<uint64_t> posHist = std::vector<uint64_t>(64 * 256, 0);
	std::vector<uint64_t> pair0 = std::vector<uint64_t>(65536, 0), pair5 = std::vector<uint64_t>(65536, 0);
	uint64_t pairs = 0;
	double sxy = 0, sx = 0, sy = 0, sxx = 0, syy = 0; uint64_t nc = 0;
	uint64_t dupWords = 0, dupLaneBlocks = 0;
	void add(const uint8_t* buf, size_t n) {
		for (size_t i = 0; i < n; i += 8) { uint64_t w; memcpy(&w, buf + i, 8); ones += __builtin_popcountll(w); }
		bits += 8 * n;
		for (size_t i = 0; i < n; ++i) { byteHist[buf[i]]++; posHist[(i % 64) * 256 + buf[i]]++; }
		for (size_t i = 0; i + 64 < n; i += 64) {
			pair0[buf[i] * 256 + buf[i + 64]]++; pair5[buf[i + 5] * 256 + buf[i + 64 + 5]]++; pairs++;
			for (int w = 0; w < 8; ++w) {
				uint64_t a, b; memcpy(&a, buf + i + 8 * w, 8); memcpy(&b, buf + i + 64 + 8 * w, 8);
				double x = a / 18446744073709551616.0, y = b / 18446744073709551616.0;
				sx += x; sy += y; sxx += x * x; syy += y * y; sxy += x * y; nc++;
			}
		}
		std::unordered_set<uint64_t> s; s.reserve(n / 8 * 2);
		for (size_t i = 0; i < n; i += 8) { uint64_t w; memcpy(&w, buf + i, 8); if (!s.insert(w).second) dupWords++; }
		for (int lane = 0; lane < 4; ++lane) {
			std::unordered_set<std::string> ls;
			for (size_t i = 16 * lane; i < n; i += 64) if (!ls.insert(std::string((const char*)buf + i, 16)).second) dupLaneBlocks++;
		}
	}
	void report(const char* name) {
		double z1 = ((double)ones - bits / 2.0) / sqrt(bits / 4.0);
		double c2 = 0, e = 0; for (auto h : byteHist) e += h; e /= 256; for (auto h : byteHist) c2 += (h - e) * (h - e) / e;
		double c3 = 0; for (int p = 0; p < 64; ++p) { double ep = 0; for (int b = 0; b < 256; ++b) ep += posHist[p * 256 + b]; ep /= 256; for (int b = 0; b < 256; ++b) c3 += (posHist[p * 256 + b] - ep) * (posHist[p * 256 + b] - ep) / ep; }
		double ep4 = (double)pairs / 65536, c40 = 0, c45 = 0;
		for (int i = 0; i < 65536; ++i) { c40 += (pair0[i] - ep4) * (pair0[i] - ep4) / ep4; c45 += (pair5[i] - ep4) * (pair5[i] - ep4) / ep4; }
		double r = (sxy / nc - (sx / nc) * (sy / nc)) / sqrt((sxx / nc - (sx / nc) * (sx / nc)) * (syy / nc - (sy / nc) * (sy / nc)));
		printf("%-40s monobit z %+.2f | bytes chi2(255) z %+.2f | per-position bytes chi2(16320) z %+.2f | lag-64B byte pairs pos0 chi2(65535) z %+.2f, pos5 z %+.2f | lag-64B word corr r %+.2e (z %+.2f) | dup 64-bit words %llu, dup 16-byte lane blocks %llu\n",
			name, z1, chiZ(c2, 255), chiZ(c3, 16320), chiZ(c40, 65535), chiZ(c45, 65535), r, r * sqrt((double)nc),
			(unsigned long long)dupWords, (unsigned long long)dupLaneBlocks);
	}
};

int main(int argc, char** argv) {
	int nb = argc > 1 ? atoi(argv[1]) : 64;
	size_t sz = argc > 2 ? strtoull(argv[2], nullptr, 0) : 2097152;
	std::vector<uint8_t> buf(sz);
	alignas(16) uint8_t seed[64];
	{
		Battery b1, b4, bz;
		for (int k = 0; k < nb; ++k) {
			uint64_t ctr = k; blake2b(seed, 64, &ctr, 8, nullptr, 0);
			alignas(16) uint8_t st[64]; memcpy(st, seed, 64);
			fillAes1Rx4<false>(st, sz, buf.data()); b1.add(buf.data(), sz);
			memcpy(st, seed, 64);
			fillAes4Rx4<false>(st, sz, buf.data()); b4.add(buf.data(), sz);
		}
		// all-zero initial state for AesGenerator1R, one long stream of nb buffers
		alignas(16) uint8_t st[64]; memset(st, 0, 64);
		for (int k = 0; k < nb; ++k) { fillAes1Rx4<false>(st, sz, buf.data()); bz.add(buf.data(), sz); }
		printf("%d buffers of %zu bytes each (z-scores ~ N(0,1) under the null hypothesis)\n", nb, sz);
		b1.report("AesGenerator1R, Blake2b seeds");
		b4.report("AesGenerator4R, Blake2b seeds");
		bz.report("AesGenerator1R, all-zero seed (continued)");
	}
	// AesHash1R structure: flip one bit of a 2 MiB input; which output lanes change?
	{
		std::vector<uint8_t> sp(2097152);
		uint64_t laneChanged[4] = {0}, hdSum = 0, trials = 0, hdLane[4] = {0};
		for (int t = 0; t < 2000; ++t) {
			uint64_t ctr = 1000000 + t; blake2b(seed, 64, &ctr, 8, nullptr, 0);
			alignas(16) uint8_t st[64]; memcpy(st, seed, 64);
			fillAes1Rx4<false>(st, sp.size(), sp.data());
			alignas(16) uint8_t h0[64], h1[64];
			hashAes1Rx4<false>(sp.data(), sp.size(), h0);
			uint64_t bit = ((uint64_t)seed[0] | (uint64_t)seed[1] << 8 | (uint64_t)seed[2] << 16 | (uint64_t)seed[3] << 24) % (sp.size() * 8);
			sp[bit / 8] ^= 1 << (bit % 8);
			hashAes1Rx4<false>(sp.data(), sp.size(), h1);
			int inLane = (int)((bit / 8) % 64) / 16;
			for (int l = 0; l < 4; ++l) {
				int d = 0; for (int i = 0; i < 16; ++i) d += __builtin_popcount(h0[16 * l + i] ^ h1[16 * l + i]);
				if (d) laneChanged[l] += (l != inLane);
				if (l == inLane) hdLane[l] += d;
				hdSum += d;
			}
			trials++;
		}
		printf("AesHash1R, 1-bit change of a 2 MiB input (%llu trials): mean output HD %.2f of 512; output lanes other than the input bit's lane that changed: %llu\n",
			(unsigned long long)trials, (double)hdSum / trials, (unsigned long long)(laneChanged[0] + laneChanged[1] + laneChanged[2] + laneChanged[3]));
	}
	return 0;
}
