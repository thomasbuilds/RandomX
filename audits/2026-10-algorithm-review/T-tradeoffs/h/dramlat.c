// dramlat: random pointer-chase latency over a buffer (64-byte lines), with K independent chains.
// Usage: dramlat <MiB> <hugepages 0|1> <chains K> <steps per chain>
// Prints ns per dependent load (K=1: latency) and ns per load overall (K>1: throughput with MLP).
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + 1e-9 * t.tv_nsec; }
static uint64_t rng = 88172645463325252ULL;
static uint64_t xr(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }

int main(int argc, char** argv) {
	size_t mib = atol(argv[1]); int huge = atoi(argv[2]); int K = atoi(argv[3]); long steps = atol(argv[4]);
	size_t size = mib << 20, lines = size / 64;
	int fl = MAP_PRIVATE | MAP_ANONYMOUS | (huge ? MAP_HUGETLB : 0);
	uint8_t* buf = mmap(NULL, size, PROT_READ | PROT_WRITE, fl, -1, 0);
	if (buf == MAP_FAILED) { perror("mmap"); return 1; }
	if (!huge) madvise(buf, size, MADV_NOHUGEPAGE);
	// random cyclic permutation of all lines (Sattolo)
	uint32_t* perm = malloc(lines * sizeof(uint32_t));
	for (size_t i = 0; i < lines; ++i) perm[i] = i;
	for (size_t i = lines - 1; i > 0; --i) { size_t j = xr() % i; uint32_t t = perm[i]; perm[i] = perm[j]; perm[j] = t; }
	for (size_t i = 0; i < lines; ++i) *(uint64_t*)(buf + (size_t)perm[i] * 64) = (uint64_t)perm[(i + 1) % lines] * 64;
	free(perm);
	uint64_t p[64];
	for (int k = 0; k < K; ++k) p[k] = (xr() % lines) * 64;
	// warm-up
	for (long s = 0; s < steps / 10; ++s) for (int k = 0; k < K; ++k) p[k] = *(uint64_t*)(buf + p[k]);
	double t0 = now();
	for (long s = 0; s < steps; ++s) for (int k = 0; k < K; ++k) p[k] = *(uint64_t*)(buf + p[k]);
	double t = now() - t0;
	uint64_t x = 0; for (int k = 0; k < K; ++k) x ^= p[k];
	printf("size=%zuMiB huge=%d chains=%d ns_per_step=%.1f ns_per_load=%.2f Mloads_per_s=%.1f (%llx)\n", mib, huge, K,
		1e9 * t / steps, 1e9 * t / (steps * K), steps * K / t / 1e6, (unsigned long long)(x & 0xf));
	return 0;
}
