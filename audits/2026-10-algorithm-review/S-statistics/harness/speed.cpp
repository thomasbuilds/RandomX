#include <cstdio>
#include <chrono>
#include "rxs_driver.hpp"
using namespace rxs;
int main() {
	randomx_cache* cache = makeCache("test key 000", 12);
	Driver d(cache, false);
	resetCounters();
	auto t0 = std::chrono::steady_clock::now();
	uint64_t r[8] = {0};
	for (uint32_t i = 0; i < 16384; ++i) d.datasetRead((uint64_t)(i * 2654435761u % 33554432u) * 64, r);
	double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	printf("16384 JIT items: %.1f ms (%.2f us/item) %llx\n", el * 1000, el * 1e6 / 16384, (unsigned long long)r[0]);
	t0 = std::chrono::steady_clock::now();
	for (uint32_t i = 0; i < 2048; ++i) { uint8_t buf[64]; randomx::initDatasetItem(cache, buf, i * 7919); r[0] ^= buf[0]; }
	el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	printf("2048 interpreted items: %.1f ms (%.2f us/item)\n", el * 1000, el * 1e6 / 2048);
	return 0;
}
