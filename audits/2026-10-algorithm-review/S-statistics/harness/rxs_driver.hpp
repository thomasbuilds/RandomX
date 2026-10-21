// Agent S: instrumented RandomX driver (re-implements the interpreter loop of vm_interpreted.cpp
// with hooks). Links against the private instrumented copy of the library (build-instr).
// Verified bit-exact against the reference test vectors and the library's own VM (see rxs.cpp verify).
#pragma once
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <string>
#include <algorithm>
#include <xmmintrin.h>
#include "randomx.h"
#include "common.hpp"
#include "dataset.hpp"
#include "bytecode_machine.hpp"
#include "program.hpp"
#include "aes_hash.hpp"
#include "blake2/blake2.h"
#include "intrin_portable.h"
#include "soft_aes.h"

namespace rxs {

using randomx::NativeRegisterFile;

struct Hooks {
	virtual ~Hooks() {}
	virtual void programStart(int k) {}
	// after step 3 (registers loaded), before step 4
	virtual void preExec(int k, int ic, NativeRegisterFile& nreg) {}
	// after step 4 (program executed), before step 5
	virtual void postExec(int k, int ic, NativeRegisterFile& nreg) {}
	// after steps 9-11: R block (as written at spAddr1) and F block (as written at spAddr0)
	virtual void blocks(int k, int ic, const uint64_t* rblock, const uint64_t* fblock, uint32_t spAddr0, uint32_t spAddr1) {}
	virtual void programEnd(int k) {}
};

static inline uint64_t getSmallPositiveFloatBits(uint64_t entropy) {
	auto exponent = entropy >> 59; //0..31
	auto mantissa = entropy & randomx::mantissaMask;
	exponent += randomx::exponentBias;
	exponent &= randomx::exponentMask;
	exponent <<= randomx::mantissaSize;
	return exponent | mantissa;
}
static inline uint64_t getStaticExponent(uint64_t entropy) {
	auto exponent = randomx::constExponentBits;
	exponent |= (entropy >> (64 - randomx::staticExponentBits)) << randomx::dynamicExponentBits;
	exponent <<= randomx::mantissaSize;
	return exponent;
}
static inline uint64_t getFloatMask(uint64_t entropy) {
	constexpr uint64_t mask22bit = (1ULL << 22) - 1;
	return (entropy & mask22bit) | getStaticExponent(entropy);
}

static inline uint64_t lane(const rx_vec_f128& v, int i) {
	alignas(16) uint64_t t[2];
	rx_store_vec_f128((double*)t, v);
	return t[i];
}
static inline void setLane(rx_vec_f128& v, int i, uint64_t x) {
	alignas(16) uint64_t t[2];
	rx_store_vec_f128((double*)t, v);
	t[i] = x;
	v = rx_load_vec_f128((const double*)t);
}

class Driver {
public:
	randomx_cache* cache = nullptr;
	uint8_t* dataset = nullptr; // optional full Dataset memory
	bool v2 = false;
	randomx_flags flags = RANDOMX_FLAG_DEFAULT;
	uint8_t* scratchpad = nullptr;
	alignas(64) randomx::Program program;
	alignas(64) randomx::RegisterFile reg;
	alignas(16) randomx::ProgramConfiguration config;
	randomx::MemoryRegisters mem;
	uint64_t datasetOffset = 0;
	randomx::BytecodeMachine bm;
	randomx::InstructionByteCode bytecode[RANDOMX_PROGRAM_MAX_SIZE];
	alignas(16) uint64_t tempHash[8];
	Hooks* hooks = nullptr;
	bool watchBlocks = true;
	uint32_t curSpAddr0 = 0, curSpAddr1 = 0;
	int stopAfter = -1;                                 // stop the program after this iteration (experiments)
	int traceK = -1, traceJ = -1;                       // single-step one iteration (debug)
	void (*traceFn)(void* ctx, int pc, NativeRegisterFile& nreg) = nullptr;
	void* traceCtx = nullptr;

	Driver(randomx_cache* c, bool isV2) : cache(c) {
		setVersion(isV2);
		scratchpad = (uint8_t*)aligned_alloc(64, randomx::ScratchpadSize);
	}
	~Driver() { free(scratchpad); }
	void setVersion(bool isV2) {
		v2 = isV2;
		flags = isV2 ? RANDOMX_FLAG_V2 : RANDOMX_FLAG_DEFAULT;
	}
	unsigned progSize() const { return randomx::Program::getSize(flags); }

	void seedFromInput(const void* in, size_t n) {
		blake2b(tempHash, sizeof(tempHash), in, n, nullptr, 0);
	}
	void fillScratchpad() {
		::fillAes1Rx4<false>(tempHash, randomx::ScratchpadSize, scratchpad);
	}
	void resetMode() {
		rx_reset_float_state();
		G.curMode = 0;
	}
	void setMode(uint32_t m) {
		rx_set_rounding_mode(m);
		G.curMode = m;
	}
	void generate(const void* seed) {
		alignas(16) uint64_t st[8];
		memcpy(st, seed, sizeof(st)); //the generator state is replaced after each program (spec step 10)
		::fillAes4Rx4<false>(st, sizeof(program), &program);
		initConfig();
	}
	void initConfig() {
		store64(&reg.a[0].lo, getSmallPositiveFloatBits(program.getEntropy(0)));
		store64(&reg.a[0].hi, getSmallPositiveFloatBits(program.getEntropy(1)));
		store64(&reg.a[1].lo, getSmallPositiveFloatBits(program.getEntropy(2)));
		store64(&reg.a[1].hi, getSmallPositiveFloatBits(program.getEntropy(3)));
		store64(&reg.a[2].lo, getSmallPositiveFloatBits(program.getEntropy(4)));
		store64(&reg.a[2].hi, getSmallPositiveFloatBits(program.getEntropy(5)));
		store64(&reg.a[3].lo, getSmallPositiveFloatBits(program.getEntropy(6)));
		store64(&reg.a[3].hi, getSmallPositiveFloatBits(program.getEntropy(7)));
		mem.ma = program.getEntropy(8) & randomx::CacheLineAlignMask;
		mem.mx = program.getEntropy(10);
		auto addressRegisters = program.getEntropy(12);
		config.readReg0 = 0 + (addressRegisters & 1);
		addressRegisters >>= 1;
		config.readReg1 = 2 + (addressRegisters & 1);
		addressRegisters >>= 1;
		config.readReg2 = 4 + (addressRegisters & 1);
		addressRegisters >>= 1;
		config.readReg3 = 6 + (addressRegisters & 1);
		datasetOffset = (program.getEntropy(13) % (randomx::DatasetExtraItems + 1)) * randomx::CacheLineSize;
		store64(&config.eMask[0], getFloatMask(program.getEntropy(14)));
		store64(&config.eMask[1], getFloatMask(program.getEntropy(15)));
	}
	rx_vec_f128 maskE(rx_vec_f128 x) {
		const rx_vec_f128 xmantissaMask = rx_set_vec_f128(randomx::dynamicMantissaMask, randomx::dynamicMantissaMask);
		const rx_vec_f128 xexponentMask = rx_load_vec_f128((const double*)&config.eMask);
		x = rx_and_vec_f128(x, xmantissaMask);
		x = rx_or_vec_f128(x, xexponentMask);
		return x;
	}
	void datasetRead(uint64_t address, uint64_t(&r)[8]) {
		if (dataset) {
			const uint64_t* line = (const uint64_t*)(dataset + address);
			for (int i = 0; i < 8; ++i) r[i] ^= line[i];
			return;
		}
		uint32_t itemNumber = (uint32_t)(address / randomx::CacheLineSize);
		alignas(64) uint64_t buf[8 * 4];
		if (cache->jit)
			cache->datasetInit(cache, (uint8_t*)buf, itemNumber, itemNumber + 1);
		else
			randomx::initDatasetItem(cache, (uint8_t*)buf, itemNumber);
		for (int i = 0; i < 8; ++i) r[i] ^= buf[i];
	}

	// Executes the loaded program (spec 4.6). k is only passed to hooks.
	void execute(int k, int itersFrom = 0, int itersTo = RANDOMX_PROGRAM_ITERATIONS) {
		NativeRegisterFile nreg;
		for (unsigned i = 0; i < 4; ++i)
			nreg.a[i] = rx_load_vec_f128(&reg.a[i].lo);
		bm.compileProgram(program, bytecode, nreg, flags);
		uint32_t spAddr0 = mem.mx;
		uint32_t spAddr1 = mem.ma;
		if (hooks) hooks->programStart(k);
		for (int ic = 0; ic < RANDOMX_PROGRAM_ITERATIONS; ++ic) {
			uint64_t spMix = nreg.r[config.readReg0] ^ nreg.r[config.readReg1];
			spAddr0 ^= spMix;
			spAddr0 &= randomx::ScratchpadL3Mask64;
			spAddr1 ^= spMix >> 32;
			spAddr1 &= randomx::ScratchpadL3Mask64;
			for (unsigned i = 0; i < 8; ++i) {
				nreg.r[i] ^= load64(scratchpad + spAddr0 + 8 * i);
			}
			for (unsigned i = 0; i < 4; ++i)
				nreg.f[i] = rx_cvt_packed_int_vec_f128(scratchpad + spAddr1 + 8 * i);
			for (unsigned i = 0; i < 4; ++i)
				nreg.e[i] = maskE(rx_cvt_packed_int_vec_f128(scratchpad + spAddr1 + 8 * (4 + i)));
			if (G.watchWord >= 0 && G.watchEvent == 0) {
				for (unsigned i = 0; i < 8; ++i) { watchRead(spAddr0 + 8 * i); watchRead(spAddr1 + 8 * i); }
			}
			curSpAddr0 = spAddr0; curSpAddr1 = spAddr1;
			if (hooks) hooks->preExec(k, ic, nreg);
			_mm_setcsr(_mm_getcsr() & ~0x3Fu);
			G.jumpsIter = 0; G.reexecIter = 0; G.consecPc = -1; G.consec = 0;
			if (k == traceK && ic == traceJ && traceFn) {
				for (int pc = 0, n = (int)progSize(); pc < n; ++pc) {
					randomx::BytecodeMachine::executeInstruction(bytecode[pc], pc, scratchpad, config, flags);
					traceFn(traceCtx, pc, nreg);
				}
			}
			else
				randomx::BytecodeMachine::executeBytecode(bytecode, scratchpad, config, flags);
			G.mxcsrFlags |= _mm_getcsr() & 0x3F;
			if (hooks) hooks->postExec(k, ic, nreg);
			if (ic == stopAfter) break;
			const uint64_t readPtr = datasetOffset + (mem.ma & randomx::CacheLineAlignMask);
			auto& mp = v2 ? mem.ma : mem.mx;
			mp ^= nreg.r[config.readReg2] ^ nreg.r[config.readReg3];
			datasetRead(readPtr, nreg.r);
			std::swap(mem.mx, mem.ma);
			alignas(16) uint64_t rblk[8], fblk[8];
			for (unsigned i = 0; i < 8; ++i) {
				store64(scratchpad + spAddr1 + 8 * i, nreg.r[i]);
				rblk[i] = nreg.r[i];
				watchWrite(spAddr1 + 8 * i);
			}
			if (v2) {
				rx_vec_i128 ekey[4], freg[4];
				for (unsigned i = 0; i < 4; ++i) {
					ekey[i] = rx_cast_vec_f2i(nreg.e[i]);
					freg[i] = rx_cast_vec_f2i(nreg.f[i]);
				}
				for (unsigned i = 0; i < 4; ++i) {
					freg[0] = ::aesenc<false>(freg[0], ekey[i]);
					freg[1] = ::aesdec<false>(freg[1], ekey[i]);
					freg[2] = ::aesenc<false>(freg[2], ekey[i]);
					freg[3] = ::aesdec<false>(freg[3], ekey[i]);
				}
				for (unsigned i = 0; i < 4; ++i)
					nreg.f[i] = rx_cast_vec_i2f(freg[i]);
			}
			else {
				for (unsigned i = 0; i < 4; ++i)
					nreg.f[i] = rx_xor_vec_f128(nreg.f[i], nreg.e[i]);
			}
			for (unsigned i = 0; i < 4; ++i) {
				rx_store_vec_f128((double*)(scratchpad + spAddr0 + 16 * i), nreg.f[i]);
				rx_store_vec_f128((double*)(fblk + 2 * i), nreg.f[i]);
				watchWrite(spAddr0 + 16 * i);
				watchWrite(spAddr0 + 16 * i + 8);
			}
			if (hooks) hooks->blocks(k, ic, rblk, fblk, spAddr0, spAddr1);
			spAddr0 = 0;
			spAddr1 = 0;
		}
		for (unsigned i = 0; i < 8; ++i)
			store64(&reg.r[i], nreg.r[i]);
		for (unsigned i = 0; i < 4; ++i)
			rx_store_vec_f128(&reg.f[i].lo, nreg.f[i]);
		for (unsigned i = 0; i < 4; ++i)
			rx_store_vec_f128(&reg.e[i].lo, nreg.e[i]);
		if (hooks) hooks->programEnd(k);
	}

	void runProgram(int k, const void* seed) {
		generate(seed);
		execute(k);
	}
	void finalHash(void* out) {
		::hashAes1Rx4<false>(scratchpad, randomx::ScratchpadSize, &reg.a);
		blake2b(out, RANDOMX_HASH_SIZE, &reg, sizeof(randomx::RegisterFile), nullptr, 0);
	}
	// Full hash computation (spec chapter 2)
	void hash(const void* in, size_t n, void* out) {
		seedFromInput(in, n);
		fillScratchpad();
		resetMode();
		for (int k = 0; k < RANDOMX_PROGRAM_COUNT; ++k) {
			runProgram(k, tempHash);
			if (k < RANDOMX_PROGRAM_COUNT - 1)
				blake2b(tempHash, sizeof(tempHash), &reg, sizeof(randomx::RegisterFile), nullptr, 0);
		}
		finalHash(out);
		rx_reset_float_state();
	}
};

static inline void resetCounters() {
	uint8_t* wbm = G.wbm; uint8_t* lw = G.lw;
	memset(&G, 0, sizeof(G));
	G.wbm = wbm; G.lw = lw;
	G.watchWord = -1;
	G.consecPc = -1;
}

static inline std::string hex(const void* p, size_t n) {
	static const char* d = "0123456789abcdef";
	std::string s;
	const uint8_t* b = (const uint8_t*)p;
	for (size_t i = 0; i < n; ++i) { s += d[b[i] >> 4]; s += d[b[i] & 15]; }
	return s;
}

static inline randomx_cache* makeCache(const char* key, size_t keyLen, bool jit = true) {
	randomx_flags f = randomx_get_flags();
	randomx_flags cf = (randomx_flags)(f & (RANDOMX_FLAG_ARGON2_AVX2 | RANDOMX_FLAG_ARGON2_SSSE3));
	if (jit) cf = (randomx_flags)(cf | RANDOMX_FLAG_JIT);
	randomx_cache* c = randomx_alloc_cache(cf);
	if (!c) { fprintf(stderr, "cache alloc failed\n"); exit(1); }
	randomx_init_cache(c, key, keyLen);
	return c;
}

// SplitMix64 for reproducible pseudo-random choices
struct SplitMix {
	uint64_t s;
	explicit SplitMix(uint64_t seed) : s(seed) {}
	uint64_t next() {
		uint64_t z = (s += 0x9e3779b97f4a7c15ULL);
		z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
		z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
		return z ^ (z >> 31);
	}
	uint64_t below(uint64_t n) { return next() % n; }
};

} // namespace rxs
