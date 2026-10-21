// pfk: does software prefetch k steps ahead hide DRAM latency on this host?
// Loop: prefetch(a[i+k]); x = work(x) (dependent multiply chain of W steps); x ^= load(a[i]).
// The next step's work depends on the loaded value (like the RandomX main loop).
// Usage: pfk <MiB> <huge 0|1> <W> <n>
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>
#include <time.h>
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + 1e-9 * t.tv_nsec; }
static uint64_t rng = 0x123456789ULL;
static uint64_t xr(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }
int main(int argc, char** argv) {
	size_t size = (size_t)atol(argv[1]) << 20; int huge = atoi(argv[2]); int W = atoi(argv[3]); long n = atol(argv[4]);
	uint8_t* buf = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | (huge ? MAP_HUGETLB : 0), -1, 0);
	if (buf == MAP_FAILED) { perror("mmap"); return 1; }
	if (!huge) madvise(buf, size, MADV_NOHUGEPAGE);
	for (size_t i = 0; i < size; i += 4096) buf[i] = 1;
	uint64_t* a = malloc((n + 16) * sizeof(uint64_t));
	for (long i = 0; i < n + 16; ++i) a[i] = (xr() % (size / 64)) * 64;
	int ks[] = {-1, 0, 1, 2, 3, 4};
	for (int q = 0; q < 6; ++q) {
		int k = ks[q];
		uint64_t x = 1;
		double t0 = now();
		if (k < 0) { // compute only (no loads)
			for (long i = 0; i < n; ++i) { for (int w = 0; w < W; ++w) x = x * 0x9E3779B97F4A7C15ULL + 1; x ^= a[i] & 0; }
		} else {
			for (long i = 0; i < n; ++i) {
				if (k > 0) __builtin_prefetch(buf + a[i + k], 0, 3);
				for (int w = 0; w < W; ++w) x = x * 0x9E3779B97F4A7C15ULL + 1;
				x ^= *(volatile uint64_t*)(buf + (a[i] ^ (x & 0)));
			}
		}
		double t = now() - t0;
		printf("size=%zuMiB huge=%d W=%d k=%2d ns_per_iter=%.1f (%llx)\n", size >> 20, huge, W, k, 1e9 * t / n, (unsigned long long)(x & 0xf));
	}
	return 0;
}
