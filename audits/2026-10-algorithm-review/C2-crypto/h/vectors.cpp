// Agent C2: sanity check that the instrumented private library (hooks installed or not) reproduces the reference
// test vectors, and compute the commitment (v1/v2) with an independent Blake2b call for comparison.
#include <cstdio>
#include <cstring>
#include "randomx.h"
#include "instr_hooks.hpp"
#include "blake2/blake2.h"

static void hex(const char* name, const unsigned char* h) { printf("%s ", name); for (int i = 0; i < 32; ++i) printf("%02x", h[i]); printf("\n"); }

int main() {
	const char key[] = "test key 000", in[] = "This is a test";
	randomx_cache* c = randomx_alloc_cache(RANDOMX_FLAG_ARGON2_AVX2);
	randomx_init_cache(c, key, sizeof key - 1);
	instr::Tracker dummy;
	for (int v = 0; v < 2; ++v) {
		randomx_vm* vm = randomx_create_vm(v ? (RANDOMX_FLAG_V2 | RANDOMX_FLAG_HARD_AES) : RANDOMX_FLAG_HARD_AES, c, nullptr);
		unsigned char h[32], h2[32], com[32], com2[32];
		randomx_calculate_hash(vm, in, sizeof in - 1, h);
		instr::g = &dummy; randomx_calculate_hash(vm, in, sizeof in - 1, h2); instr::g = nullptr;
		hex(v ? "v2 hash          " : "v1 hash          ", h);
		printf("hash with tracker installed identical: %s\n", memcmp(h, h2, 32) ? "NO" : "yes");
		randomx_calculate_commitment(in, sizeof in - 1, h, com);
		unsigned char buf[sizeof in - 1 + 32]; memcpy(buf, in, sizeof in - 1); memcpy(buf + sizeof in - 1, h, 32);
		blake2b(com2, 32, buf, sizeof buf, nullptr, 0);
		hex(v ? "v2 commitment    " : "v1 commitment    ", com);
		printf("commitment == Blake2b-256(input || hash) computed independently: %s\n", memcmp(com, com2, 32) ? "NO" : "yes");
		randomx_destroy_vm(vm);
	}
	randomx_release_cache(c);
	return 0;
}
