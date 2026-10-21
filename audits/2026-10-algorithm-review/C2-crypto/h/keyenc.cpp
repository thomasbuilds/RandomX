// Agent C2 (item 1): Blake2Generator seed encoding. The key is copied into a 64-byte buffer, zero padded, with a
// 32-bit nonce at bytes 60-63, then hashed; the key length is not encoded. Argon2 (H0) encodes the length.
// Checks: K vs K||00 and two 61-byte keys differing only in byte 60 -> same SuperscalarHash programs? same Cache?
#include <cstdio>
#include <cstring>
#include <string>
#include "randomx.h"
#include "dataset.hpp"
#include "superscalar_program.hpp"

static bool samePrograms(randomx_cache* a, randomx_cache* b) {
	for (int i = 0; i < RANDOMX_CACHE_ACCESSES; ++i) {
		auto& p = a->programs[i]; auto& q = b->programs[i];
		if (p.getSize() != q.getSize() || p.getAddressRegister() != q.getAddressRegister()) return false;
		for (unsigned j = 0; j < p.getSize(); ++j)
			if (memcmp(&p(j), &q(j), sizeof(randomx::Instruction))) return false;
	}
	return a->reciprocalCache == b->reciprocalCache;
}

int main() {
	struct Case { std::string k1, k2, what; };
	std::string k61a(61, 'x'), k61b(61, 'x'); k61b[60] = 'y';
	Case cases[] = {
		{std::string("test key 000"), std::string("test key 000\0", 13), "K vs K||0x00"},
		{std::string("abc"), std::string("abc\0\0\0\0", 7), "K vs K||0x00000000"},
		{k61a, k61b, "61-byte keys differing only in byte 60"},
		{std::string("test key 000"), std::string("test key 001"), "control: different keys"},
	};
	for (auto& c : cases) {
		randomx_cache* a = randomx_alloc_cache(RANDOMX_FLAG_ARGON2_AVX2);
		randomx_cache* b = randomx_alloc_cache(RANDOMX_FLAG_ARGON2_AVX2);
		randomx_init_cache(a, c.k1.data(), c.k1.size());
		randomx_init_cache(b, c.k2.data(), c.k2.size());
		bool sp = samePrograms(a, b);
		bool sc = memcmp(randomx_get_cache_memory(a), randomx_get_cache_memory(b), 1024) == 0;
		printf("%-42s same SuperscalarHash programs: %s, same Cache (first block): %s\n", c.what.c_str(), sp ? "YES" : "no", sc ? "YES" : "no");
		randomx_release_cache(a); randomx_release_cache(b);
	}
	return 0;
}
