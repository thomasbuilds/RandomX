// Agent C2: indicative cost of one commitment (Blake2b-256 over a 76-byte blob || 32-byte hash) on this host.
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "randomx.h"
int main() {
	unsigned char in[76] = {0}, h[32] = {0}, out[32];
	const int N = 2000000;
	auto t0 = std::chrono::steady_clock::now();
	for (int i = 0; i < N; ++i) { memcpy(in + 39, &i, 4); randomx_calculate_commitment(in, sizeof in, h, out); h[0] ^= out[0]; }
	auto t1 = std::chrono::steady_clock::now();
	double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / N;
	printf("commitment (Blake2b-256 of 108 bytes): %.1f ns per call, single thread (checksum %02x)\n", ns, h[0]);
	return 0;
}
