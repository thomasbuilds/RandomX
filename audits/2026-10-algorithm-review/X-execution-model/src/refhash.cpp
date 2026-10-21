// Agent X: reference hashes from the public API (light-mode JIT VM, hardware AES) for xsim inputs {seed, h}.
// usage: refhash <seed> <count>   -> prints "REF v1|v2 h=<h> <hex>"
#include "randomx.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
int main(int argc, char** argv) {
	uint32_t seed = (uint32_t)strtoul(argv[1], nullptr, 0);
	int count = atoi(argv[2]);
	const char key[] = "agentX execution model key";
	randomx_cache* cache = randomx_alloc_cache((randomx_flags)(RANDOMX_FLAG_JIT | RANDOMX_FLAG_ARGON2));
	randomx_init_cache(cache, key, strlen(key));
	for (int v = 1; v <= 2; ++v) {
		randomx_vm* vm = randomx_create_vm((randomx_flags)(RANDOMX_FLAG_JIT | RANDOMX_FLAG_HARD_AES | (v == 2 ? RANDOMX_FLAG_V2 : 0)), cache, nullptr);
		for (int h = 0; h < count; ++h) {
			uint32_t input[2] = { seed, (uint32_t)h };
			uint8_t out[32];
			randomx_calculate_hash(vm, input, sizeof(input), out);
			printf("REF v%d h=%d ", v, h);
			for (int i = 0; i < 32; ++i) printf("%02x", out[i]);
			printf("\n");
		}
		randomx_destroy_vm(vm);
	}
	randomx_release_cache(cache);
	return 0;
}
