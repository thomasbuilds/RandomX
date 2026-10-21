// Agent X: instrumented RandomX VM (copy of InterpretedVm::execute) that builds the uop trace,
// collects dynamic statistics and tracks scratchpad liveness across a whole hash.
// Semantics are taken from the library itself (BytecodeMachine::compileProgram/executeInstruction,
// fillAes1Rx4/fillAes4Rx4/hashAes1Rx4, blake2b, the Dataset item function), and the final hash is
// compared with randomx_calculate_hash on a light-mode VM.
#pragma once
#include "trace.hpp"
#include "common.hpp"
#include "program.hpp"
#include "bytecode_machine.hpp"
#include "dataset.hpp"
#include "intrin_portable.h"
#include "soft_aes.h"
#include "aes_hash.hpp"
#include "blake2/blake2.h"
#include <array>
#include <cstring>
#include <cmath>

namespace ax {

using namespace randomx;

// ---------------------------------------------------------------- dynamic statistics
struct DynStats {
	// counts per VM type (0..29 real, 40.. pseudo) of executed instructions
	double vmCount[64] = {0};
	double uopCount[C_NCLASS] = {0};
	double slots = 0;
	double iterations = 0;
	double taken = 0;              // taken CBRANCH
	double cbranchExec = 0;
	double reexec = 0;             // VM instructions re-executed because of taken branches
	double cfroundExec = 0;
	double cfroundOpen = 0;        // gate open (v2) or always (v1)
	double fprcChanges = 0;        // fprc value actually changed
	double loadsByLvl[8] = {0};    // M-operand loads by level
	double storesByLvl[8] = {0};   // ISTORE by level
	double aesRounds = 0;          // main-loop AES rounds
	double rawMem = 0;             // loads with a true RAW dependency on an earlier store of the same program
	double rawMemRecent = 0;       // ... whose producer store is in the same or previous iteration
	void add(const DynStats& o) {
		for (int i = 0; i < 64; ++i) vmCount[i] += o.vmCount[i];
		for (int i = 0; i < C_NCLASS; ++i) uopCount[i] += o.uopCount[i];
		for (int i = 0; i < 8; ++i) { loadsByLvl[i] += o.loadsByLvl[i]; storesByLvl[i] += o.storesByLvl[i]; }
		slots += o.slots; iterations += o.iterations; taken += o.taken; cbranchExec += o.cbranchExec;
		reexec += o.reexec; cfroundExec += o.cfroundExec; cfroundOpen += o.cfroundOpen; fprcChanges += o.fprcChanges;
		aesRounds += o.aesRounds; rawMem += o.rawMem; rawMemRecent += o.rawMemRecent;
	}
};

// ---------------------------------------------------------------- scratchpad liveness (addendum)
// event kinds
enum EvKind : uint8_t { EV_IBLK = 0, EV_FBLK, EV_FQ, EV_ISTORE, EV_NKIND };
// first reader kinds
enum RdKind : uint8_t { RD_BLK0 = 0, RD_BLK1, RD_ML1, RD_ML2, RD_ML3, RD_FINAL, RD_DEAD, RD_NKIND };

struct Event {
	uint32_t time;     // global iteration index (program*2048 + iteration)
	uint8_t kind;
	uint8_t nwords;
	uint8_t overwrittenUnread;
	uint8_t firstReader;  // RD_* or 255 = none yet
	int32_t parent;       // for EV_FQ: the F-block event
};

struct Liveness {
	std::vector<int32_t> wEvent;   // per 8-byte word: event index of last write (-1 = initial fill)
	std::vector<uint8_t> wRead;    // per word: read since last write
	std::vector<int32_t> wQuarter; // per word: F-quarter event (or -1)
	std::vector<Event> ev;
	// histograms of first-read distance (iterations) for F blocks, log2 buckets
	double distHist[EV_NKIND][24] = {{0}};
	Liveness() { reset(); }
	void reset() {
		wEvent.assign(ScratchpadL3, -1);
		wRead.assign(ScratchpadL3, 0);
		wQuarter.assign(ScratchpadL3, -1);
		ev.clear();
		ev.reserve(1 << 20);
	}
	void firstRead(int32_t e, uint8_t rk, uint32_t now) {
		Event& E = ev[e];
		if (E.firstReader == 255) {
			E.firstReader = rk;
			uint32_t d = now - E.time;
			int b = 0; while ((1u << b) <= d && b < 23) ++b;
			distHist[E.kind][b] += 1;
		}
	}
	void read(uint32_t word, uint8_t rk, uint32_t now) {
		if (!wRead[word]) {
			wRead[word] = 1;
			if (wEvent[word] >= 0) firstRead(wEvent[word], rk, now);
			if (wQuarter[word] >= 0) firstRead(wQuarter[word], rk, now);
		}
	}
	void overwrite(uint32_t word) {
		if (!wRead[word]) {
			int32_t e = wEvent[word];
			if (e >= 0) {
				Event& E = ev[e];
				E.overwrittenUnread++;
				if (E.overwrittenUnread == E.nwords && E.firstReader == 255) E.firstReader = RD_DEAD;
			}
			int32_t q = wQuarter[word];
			if (q >= 0) {
				Event& Q = ev[q];
				Q.overwrittenUnread++;
				if (Q.overwrittenUnread == Q.nwords && Q.firstReader == 255) Q.firstReader = RD_DEAD;
			}
		}
	}
	int32_t newEvent(uint8_t kind, uint8_t nwords, uint32_t now, int32_t parent = -1) {
		Event E; E.time = now; E.kind = kind; E.nwords = nwords; E.overwrittenUnread = 0; E.firstReader = 255; E.parent = parent;
		ev.push_back(E);
		return (int32_t)ev.size() - 1;
	}
	void write(uint32_t word, int32_t e, int32_t q) {
		overwrite(word);
		wEvent[word] = e;
		wQuarter[word] = q;
		wRead[word] = 0;
	}
	// final AesHash1R reads every word
	void finalRead(uint32_t now) {
		for (uint32_t w = 0; w < ScratchpadL3; ++w) read(w, RD_FINAL, now);
	}
	// summary: counts[kind][reader]
	void summarize(double counts[EV_NKIND][RD_NKIND]) const {
		for (const Event& E : ev) {
			uint8_t r = E.firstReader == 255 ? RD_FINAL : E.firstReader;
			counts[E.kind][r] += 1;
		}
	}
};

// ---------------------------------------------------------------- the instrumented VM
struct VmState {
	alignas(64) Program program;
	alignas(64) RegisterFile reg;
	alignas(16) ProgramConfiguration config;
	MemoryRegisters mem;
	uint64_t datasetOffset;
	uint8_t* scratchpad;
	randomx_cache* cache;
	alignas(16) uint64_t tempHash[8];
};

static inline uint64_t getSmallPositiveFloatBits(uint64_t entropy) {
	auto exponent = entropy >> 59; //0..31
	auto mantissa = entropy & mantissaMask;
	exponent += exponentBias;
	exponent &= exponentMask;
	exponent <<= mantissaSize;
	return exponent | mantissa;
}
static inline uint64_t getStaticExponent(uint64_t entropy) {
	auto exponent = constExponentBits;
	exponent |= (entropy >> (64 - staticExponentBits)) << dynamicExponentBits;
	exponent <<= mantissaSize;
	return exponent;
}
static inline uint64_t getFloatMask(uint64_t entropy) {
	constexpr uint64_t mask22bit = (1ULL << 22) - 1;
	return (entropy & mask22bit) | getStaticExponent(entropy);
}

class TraceVm : public BytecodeMachine {
public:
	VmState& s;
	bool v2;
	randomx_flags flags;
	uint32_t progIndex = 0;     // program number within the hash
	Liveness* live = nullptr;
	// Dataset item source (light mode, computed from the Cache)
	alignas(64) uint8_t itemBuf[64 * 4];

	TraceVm(VmState& st, bool isV2) : s(st), v2(isV2) {
		flags = (randomx_flags)(isV2 ? RANDOMX_FLAG_V2 : 0);
	}

	void initialize() {
		Program& program = s.program;
		store64(&s.reg.a[0].lo, getSmallPositiveFloatBits(program.getEntropy(0)));
		store64(&s.reg.a[0].hi, getSmallPositiveFloatBits(program.getEntropy(1)));
		store64(&s.reg.a[1].lo, getSmallPositiveFloatBits(program.getEntropy(2)));
		store64(&s.reg.a[1].hi, getSmallPositiveFloatBits(program.getEntropy(3)));
		store64(&s.reg.a[2].lo, getSmallPositiveFloatBits(program.getEntropy(4)));
		store64(&s.reg.a[2].hi, getSmallPositiveFloatBits(program.getEntropy(5)));
		store64(&s.reg.a[3].lo, getSmallPositiveFloatBits(program.getEntropy(6)));
		store64(&s.reg.a[3].hi, getSmallPositiveFloatBits(program.getEntropy(7)));
		s.mem.ma = program.getEntropy(8) & CacheLineAlignMask;
		s.mem.mx = program.getEntropy(10);
		auto addressRegisters = program.getEntropy(12);
		s.config.readReg0 = 0 + (addressRegisters & 1);
		addressRegisters >>= 1;
		s.config.readReg1 = 2 + (addressRegisters & 1);
		addressRegisters >>= 1;
		s.config.readReg2 = 4 + (addressRegisters & 1);
		addressRegisters >>= 1;
		s.config.readReg3 = 6 + (addressRegisters & 1);
		s.datasetOffset = (program.getEntropy(13) % (DatasetExtraItems + 1)) * CacheLineSize;
		store64(&s.config.eMask[0], getFloatMask(program.getEntropy(14)));
		store64(&s.config.eMask[1], getFloatMask(program.getEntropy(15)));
	}

	void datasetRead(uint64_t address, int_reg_t(&r)[8]) {
		uint32_t itemNumber = address / CacheLineSize;
		// JIT dataset init computes items in groups of 4 (see randomx_init_dataset)
		s.cache->datasetInit(s.cache, itemBuf, itemNumber, itemNumber + 4);
		const uint64_t* rl = (const uint64_t*)itemBuf;
		for (unsigned q = 0; q < 8; ++q)
			r[q] ^= rl[q];
	}

	// ------------------------------------------------------------ trace emission
	ProgramTrace* T = nullptr;
	DynStats* st = nullptr;
	int32_t prod[17];             // r0-7, f0-3 (8-11), e0-3 (12-15), fprc (16)
	std::vector<int32_t> wordProd; // scratchpad word -> producer store uop (this program)
	uint32_t curIter = 0;

	int32_t emit(uint8_t cls, uint8_t vm, int32_t d0 = -1, int32_t d1 = -1, int32_t d2 = -1, uint8_t slots = 1,
		uint8_t lvl = M_NONE, int32_t md = -1, uint8_t flags = 0, int32_t aux = -1) {
		Uop u;
		u.d[0] = d0; u.d[1] = d1; u.d[2] = d2; u.md = md; u.aux = aux; u.iter = curIter;
		u.cls = cls; u.lvl = lvl; u.flags = flags; u.slots = slots; u.vm = vm;
		u.pad[0] = u.pad[1] = u.pad[2] = 0;
		T->u.push_back(u);
		st->uopCount[cls] += 1;
		st->slots += slots;
		return (int32_t)T->u.size() - 1;
	}
	uint32_t globalIter() const { return progIndex * RANDOMX_PROGRAM_ITERATIONS + curIter; }

	int32_t loadWord(uint32_t byteAddr, uint8_t cls, uint8_t vm, int32_t agen, uint8_t lvl, uint8_t rk, uint8_t slots, uint8_t flags = 0, int32_t aux = -1) {
		uint32_t w = byteAddr >> 3;
		int32_t md = wordProd[w];
		if (md >= 0) {
			st->rawMem += 1;
			if (T->u[md].iter + 1 >= curIter) st->rawMemRecent += 1;
		}
		if (live) live->read(w, rk, globalIter());
		return emit(cls, vm, agen, -1, -1, slots, lvl, md, flags, aux);
	}
	int32_t storeWords(uint32_t byteAddr, int nwords, int32_t agen, int32_t data, uint8_t vm, uint8_t lvl, int32_t ev, int32_t q, uint8_t flags = 0) {
		int32_t id = emit(C_STORE, vm, agen, data, -1, 1, lvl, -1, flags);
		for (int i = 0; i < nwords; ++i) {
			uint32_t w = (byteAddr >> 3) + i;
			wordProd[w] = id;
			if (live) live->write(w, ev, q);
		}
		return id;
	}

	int regOf(const int_reg_t* p, NativeRegisterFile& n) {
		if (p >= &n.r[0] && p < &n.r[RegistersCount]) return (int)(p - &n.r[0]);
		return -1;
	}
	int fregOf(const rx_vec_f128* p, NativeRegisterFile& n) {
		if (p >= &n.f[0] && p < &n.f[RegisterCountFlt]) return 8 + (int)(p - &n.f[0]);
		if (p >= &n.e[0] && p < &n.e[RegisterCountFlt]) return 12 + (int)(p - &n.e[0]);
		return -1;
	}
	static uint8_t lvlOfMask(uint32_t mask) {
		if (mask == (uint32_t)ScratchpadL1Mask) return M_L1;
		if (mask == (uint32_t)ScratchpadL2Mask) return M_L2;
		return M_L3;
	}
	static uint8_t rdOfLvl(uint8_t l) { return l == M_L1 ? RD_ML1 : (l == M_L2 ? RD_ML2 : RD_ML3); }

	// original VM type of each program instruction (IMUL_RCP is compiled as IMUL_R)
	uint8_t origType[RANDOMX_PROGRAM_MAX_SIZE];
	void decodeOrig() {
		for (unsigned i = 0, n = Program::getSize(flags); i < n; ++i) {
			int op = s.program(i).opcode;
			uint8_t t;
			if (op < ceil_IADD_RS) t = 0; else if (op < ceil_IADD_M) t = 1; else if (op < ceil_ISUB_R) t = 2;
			else if (op < ceil_ISUB_M) t = 3; else if (op < ceil_IMUL_R) t = 4; else if (op < ceil_IMUL_M) t = 5;
			else if (op < ceil_IMULH_R) t = 6; else if (op < ceil_IMULH_M) t = 7; else if (op < ceil_ISMULH_R) t = 8;
			else if (op < ceil_ISMULH_M) t = 9; else if (op < ceil_IMUL_RCP) t = 10; else if (op < ceil_INEG_R) t = 11;
			else if (op < ceil_IXOR_R) t = 12; else if (op < ceil_IXOR_M) t = 13; else if (op < ceil_IROR_R) t = 14;
			else if (op < ceil_IROL_R) t = 15; else if (op < ceil_ISWAP_R) t = 16; else if (op < ceil_FSWAP_R) t = 17;
			else if (op < ceil_FADD_R) t = 18; else if (op < ceil_FADD_M) t = 19; else if (op < ceil_FSUB_R) t = 20;
			else if (op < ceil_FSUB_M) t = 21; else if (op < ceil_FSCAL_R) t = 22; else if (op < ceil_FMUL_R) t = 23;
			else if (op < ceil_FDIV_M) t = 24; else if (op < ceil_FSQRT_R) t = 25; else if (op < ceil_CBRANCH) t = 26;
			else if (op < ceil_CFROUND) t = 27; else if (op < ceil_ISTORE) t = 28; else t = 29;
			origType[i] = t;
		}
	}

	// execute one program (steps 7-8 of the algorithm) and build its trace
	void runProgram(ProgramTrace& trace, DynStats& stats) {
		T = &trace; st = &stats;
		trace.u.clear(); trace.iterEnd.clear(); trace.rEndUop.clear();
		trace.u.reserve(2048 * 900);
		fillAes4Rx4<false>(s.tempHash, sizeof(s.program), &s.program);
		initialize();
		decodeOrig();
		wordProd.assign(ScratchpadL3, -1);
		for (int i = 0; i < 17; ++i) prod[i] = -1;

		NativeRegisterFile nreg;
		for (unsigned i = 0; i < RegisterCountFlt; ++i)
			nreg.a[i] = rx_load_vec_f128(&s.reg.a[i].lo);
		InstructionByteCode bytecode[RANDOMX_PROGRAM_MAX_SIZE];
		compileProgram(s.program, bytecode, nreg, flags);
		const int n = Program::getSize(flags);

		uint32_t spAddr0 = s.mem.mx;
		uint32_t spAddr1 = s.mem.ma;
		int32_t addr0 = -1, addr1 = -1;     // uops producing spAddr0 / spAddr1
		int32_t spPref0 = -1, spPref1 = -1; // scratchpad prefetch uops for the next iteration
		// Dataset address registers: producer uop of value and of the prefetch that fetched it
		int32_t mProd[2] = { -1, -1 };      // [0] = ma, [1] = mx
		int32_t mPref[2] = { -1, -1 };
		const uint32_t spMask = ScratchpadL3Mask64;

		for (unsigned ic = 0; ic < RANDOMX_PROGRAM_ITERATIONS; ++ic) {
			curIter = ic;
			stats.iterations += 1;
			// ---- step 1 (for ic > 0 the uops were emitted at the end of the previous iteration)
			uint64_t spMix = nreg.r[s.config.readReg0] ^ nreg.r[s.config.readReg1];
			spAddr0 ^= spMix;
			spAddr0 &= spMask;
			spAddr1 ^= spMix >> 32;
			spAddr1 &= spMask;
			// ---- step 2: integer block load at spAddr0
			int32_t lea0 = emit(C_ALU, P_STACK, addr0, -1, -1, 2);  // lea rcx,[rsi+rax]; mov [rsp+8],rcx
			for (unsigned i = 0; i < RegistersCount; ++i) {
				int32_t ld = loadWord(spAddr0 + 8 * i, C_LOADI, P_BLKLD_I, lea0, M_BLK, RD_BLK0, 0, F_SPLOAD, spPref0);
				prod[i] = emit(C_ALU, P_BLKLD_I, prod[i], ld, -1, 1);
				nreg.r[i] ^= load64(s.scratchpad + spAddr0 + 8 * i);
			}
			// ---- step 3: FP block load at spAddr1
			int32_t lea1 = emit(C_ALU, P_STACK, addr1, -1, -1, 2);
			for (unsigned i = 0; i < RegisterCountFlt; ++i) {
				prod[8 + i] = loadWord(spAddr1 + 8 * i, C_LOADF, P_BLKLD_F, lea1, M_BLK, RD_BLK1, 1, F_SPLOAD, spPref1);
				nreg.f[i] = rx_cvt_packed_int_vec_f128(s.scratchpad + spAddr1 + 8 * i);
			}
			for (unsigned i = 0; i < RegisterCountFlt; ++i) {
				int32_t ld = loadWord(spAddr1 + 8 * (RegisterCountFlt + i), C_LOADF, P_BLKLD_F, lea1, M_BLK, RD_BLK1, 1, F_SPLOAD, spPref1);
				int32_t a = emit(C_FLOG, P_EMASK, ld);
				prod[12 + i] = emit(C_FLOG, P_EMASK, a);
				nreg.e[i] = maskRegisterExponentMantissa(s.config, rx_cvt_packed_int_vec_f128(s.scratchpad + spAddr1 + 8 * (RegisterCountFlt + i)));
			}
			// ---- step 4: program
			for (int pc = 0; pc < n; ++pc) {
				auto& ibc = bytecode[pc];
				uint8_t ot = origType[pc];
				stats.vmCount[ot] += 1;
				int dst = -1, src = -1;
				switch (ibc.type) {
				case InstructionType::IADD_RS: {
					dst = regOf(ibc.idst, nreg); src = regOf(ibc.isrc, nreg);
					int32_t u = emit(ibc.imm != 0 ? C_LEA3 : C_ALU, ot, prod[dst], prod[src]);
					prod[dst] = u;
				} break;
				case InstructionType::IADD_M: case InstructionType::ISUB_M: case InstructionType::IMUL_M:
				case InstructionType::IMULH_M: case InstructionType::ISMULH_M: case InstructionType::IXOR_M: {
					dst = regOf(ibc.idst, nreg); src = regOf(ibc.isrc, nreg);
					uint32_t addr = (uint32_t)((*ibc.isrc + ibc.imm) & ibc.memMask);
					uint8_t lvl = lvlOfMask(ibc.memMask);
					stats.loadsByLvl[lvl] += 1;
					int32_t ag = -1;
					if (src >= 0) {
						int32_t a1 = emit(C_AG1, ot, prod[src]);
						ag = emit(C_AG2, ot, a1);
					}
					bool mulh = ibc.type == InstructionType::IMULH_M || ibc.type == InstructionType::ISMULH_M;
					int32_t ld = loadWord(addr, C_LOADI, ot, ag, lvl, rdOfLvl(lvl), 0);
					uint8_t cls = ibc.type == InstructionType::IMUL_M ? C_IMUL : (mulh ? C_IMULH : C_ALU);
					prod[dst] = emit(cls, ot, prod[dst], ld, -1, mulh ? 4 : 1);
				} break;
				case InstructionType::ISUB_R: case InstructionType::IXOR_R: {
					dst = regOf(ibc.idst, nreg); src = regOf(ibc.isrc, nreg);
					prod[dst] = emit(C_ALU, ot, prod[dst], src >= 0 ? prod[src] : -1);
				} break;
				case InstructionType::IMUL_R: {
					dst = regOf(ibc.idst, nreg); src = regOf(ibc.isrc, nreg);
					prod[dst] = emit(C_IMUL, ot == 10 ? (uint8_t)P_IMUL_RCP : ot, prod[dst], src >= 0 ? prod[src] : -1, -1, ot == 10 ? 2 : 1);
				} break;
				case InstructionType::IMULH_R: case InstructionType::ISMULH_R: {
					dst = regOf(ibc.idst, nreg); src = regOf(ibc.isrc, nreg);
					prod[dst] = emit(C_IMULH, ot, prod[dst], prod[src], -1, 4);
				} break;
				case InstructionType::INEG_R: {
					dst = regOf(ibc.idst, nreg);
					prod[dst] = emit(C_ALU, ot, prod[dst]);
				} break;
				case InstructionType::IROR_R: case InstructionType::IROL_R: {
					dst = regOf(ibc.idst, nreg); src = regOf(ibc.isrc, nreg);
					prod[dst] = emit(C_ALU, ot, prod[dst], src >= 0 ? prod[src] : -1, -1, src >= 0 ? 2 : 1);
				} break;
				case InstructionType::ISWAP_R: {
					dst = regOf(ibc.idst, nreg); src = regOf(ibc.isrc, nreg);
					int32_t u = emit(C_ALU, ot, prod[dst], prod[src], -1, 2);
					prod[dst] = u; prod[src] = u;
				} break;
				case InstructionType::FSWAP_R: {
					int f = fregOf(ibc.fdst, nreg);
					prod[f] = emit(C_FLOG, ot, prod[f]);
				} break;
				case InstructionType::FADD_R: case InstructionType::FSUB_R: {
					int f = fregOf(ibc.fdst, nreg);
					prod[f] = emit(C_FADD, ot, prod[f], prod[16], -1, 1, M_NONE, -1, F_RND);
				} break;
				case InstructionType::FADD_M: case InstructionType::FSUB_M: case InstructionType::FDIV_M: {
					int f = fregOf(ibc.fdst, nreg); src = regOf(ibc.isrc, nreg);
					uint32_t addr = (uint32_t)((*ibc.isrc + ibc.imm) & ibc.memMask);
					uint8_t lvl = lvlOfMask(ibc.memMask);
					stats.loadsByLvl[lvl] += 1;
					int32_t a1 = emit(C_AG1, ot, prod[src]);
					int32_t ag = emit(C_AG2, ot, a1);
					int32_t ld = loadWord(addr, C_LOADF, ot, ag, lvl, rdOfLvl(lvl), 1);
					if (ibc.type == InstructionType::FDIV_M) {
						int32_t m1 = emit(C_FLOG, ot, ld);
						int32_t m2 = emit(C_FLOG, ot, m1);
						prod[f] = emit(C_FDIV, ot, prod[f], m2, prod[16], 1, M_NONE, -1, F_RND);
					}
					else {
						prod[f] = emit(C_FADD, ot, prod[f], ld, prod[16], 1, M_NONE, -1, F_RND);
					}
				} break;
				case InstructionType::FSCAL_R: {
					int f = fregOf(ibc.fdst, nreg);
					prod[f] = emit(C_FLOG, ot, prod[f]);
				} break;
				case InstructionType::FMUL_R: {
					int f = fregOf(ibc.fdst, nreg);
					prod[f] = emit(C_FMUL, ot, prod[f], prod[16], -1, 1, M_NONE, -1, F_RND);
				} break;
				case InstructionType::FSQRT_R: {
					int f = fregOf(ibc.fdst, nreg);
					prod[f] = emit(C_FSQRT, ot, prod[f], prod[16], -1, 1, M_NONE, -1, F_RND);
				} break;
				case InstructionType::CBRANCH: {
					dst = regOf(ibc.idst, nreg);
					stats.cbranchExec += 1;
					int32_t add = emit(C_ALU, ot, prod[dst]);
					prod[dst] = add;
					int oldpc = pc;
					executeInstruction(ibc, pc, s.scratchpad, s.config, flags);
					bool taken = pc != oldpc;
					emit(C_BR, ot, add, -1, -1, 1, M_NONE, -1, taken ? F_MISP : 0);
					if (taken) {
						stats.taken += 1;
						stats.reexec += oldpc - pc;  // instructions pc+1 .. oldpc are executed again
					}
					continue;  // already executed
				}
				case InstructionType::CFROUND: {
					src = regOf(ibc.isrc, nreg);
					stats.cfroundExec += 1;
					uint64_t x = rotr(*ibc.isrc, ibc.imm);
					bool open = !v2 || ((x & 60) == 0);
					int32_t rol = emit(C_ALU, ot, prod[src], -1, -1, 2);
					int32_t dep = rol;
					if (v2) {
						emit(C_BR, ot, rol, -1, -1, 1, M_NONE, -1, open ? F_MISP : 0);
					}
					if (open) {
						stats.cfroundOpen += 1;
						int32_t ao = emit(C_ALU, ot, dep, -1, -1, 2);
						int32_t stk = emit(C_STORE, ot, ao, -1, -1, 1, M_STACK);
						prod[16] = emit(C_MXCSR, ot, stk, -1, -1, 1);
						uint32_t newMode = x % 4;
						if (newMode != curMode) stats.fprcChanges += 1;
						curMode = newMode;
					}
				} break;
				case InstructionType::ISTORE: {
					dst = regOf(ibc.idst, nreg); src = regOf(ibc.isrc, nreg);
					uint32_t addr = (uint32_t)((*ibc.idst + ibc.imm) & ibc.memMask);
					uint8_t lvl = lvlOfMask(ibc.memMask);
					stats.storesByLvl[lvl] += 1;
					int32_t a1 = emit(C_AG1, ot, prod[dst]);
					int32_t ag = emit(C_AG2, ot, a1);
					int32_t ev = live ? live->newEvent(EV_ISTORE, 1, globalIter()) : -1;
					storeWords(addr, 1, ag, prod[src], ot, lvl, ev, -1);
				} break;
				case InstructionType::NOP:
					break;
				default:
					break;
				}
				executeInstruction(ibc, pc, s.scratchpad, s.config, flags);
			}
			// ---- step 5: mp ^= r[readReg2] ^ r[readReg3]   (JIT: mov eax,r2; xor eax,r3)
			int32_t t = emit(C_ALU, P_MPXOR, prod[s.config.readReg2], prod[s.config.readReg3], -1, 2);
			// ---- step 7 address (mov ecx, ebp; and ecx, mask) uses ma
			int32_t dsAddr = emit(C_ALU, P_DSLOAD, mProd[0], -1, -1, 2);
			int32_t dsPrefOfThis = mPref[0];
			const uint64_t readPtr = s.datasetOffset + (s.mem.ma & CacheLineAlignMask);
			auto& mp = v2 ? s.mem.ma : s.mem.mx;
			mp ^= nreg.r[s.config.readReg2] ^ nreg.r[s.config.readReg3];
			int mpi = v2 ? 0 : 1;
			int32_t nmp = emit(C_ALU, P_MPXOR, t, mProd[mpi], -1, 2);  // xor rbp,rax; ror rbp,32
			int32_t pa = emit(C_ALU, P_DSPREF, nmp, -1, -1, 2);       // mov edx,ebp; and edx,mask
			int32_t pf = emit(C_PREF, P_DSPREF, pa, -1, -1, 1, M_DS, -1, F_DSPREF);
			mProd[mpi] = nmp; mPref[mpi] = pf;
			// ---- step 7: Dataset read (8 words, fused load+xor in the JIT)
			datasetRead(readPtr, nreg.r);
			int32_t firstEnd = -1;
			for (unsigned i = 0; i < RegistersCount; ++i) {
				int32_t ld = emit(C_LOADI, P_DSLOAD, dsAddr, -1, -1, 0, M_DS, -1, 0, dsPrefOfThis);
				prod[i] = emit(C_ALU, P_DSXOR, prod[i], ld, -1, 1, M_NONE, -1, i == 7 ? F_ITEREND : 0);
				if (i == 0) firstEnd = prod[i];
			}
			trace.rEndUop.push_back(firstEnd);
			// ---- step 8: swap mx, ma
			std::swap(s.mem.mx, s.mem.ma);
			std::swap(mProd[0], mProd[1]);
			std::swap(mPref[0], mPref[1]);
			// ---- step 1 of the next iteration + scratchpad prefetch (JIT order)
			int32_t mix = emit(C_ALU, P_SPMIX, prod[s.config.readReg0], prod[s.config.readReg1], -1, 2);
			int32_t na0 = emit(C_ALU, P_SPADDR, mix, -1, -1, 2);   // mov rdx,rax; and eax,mask
			int32_t sp0 = emit(C_PREF, P_SPPREF, na0, -1, -1, 1, M_BLK);
			int32_t na1 = emit(C_ALU, P_SPADDR, mix, -1, -1, 2);   // ror rdx,32; and edx,mask
			int32_t sp1 = emit(C_PREF, P_SPPREF, na1, -1, -1, 1, M_BLK);
			// ---- step 9: integer block store at spAddr1
			int32_t ldA1 = emit(C_ALU, P_STACK, lea1, -1, -1, 1);   // mov rcx,[rsp+16]
			int32_t evI = live ? live->newEvent(EV_IBLK, 8, globalIter()) : -1;
			for (unsigned i = 0; i < RegistersCount; ++i) {
				storeWords(spAddr1 + 8 * i, 1, ldA1, prod[i], P_BLKST_I, M_BLK, evI, -1);
				store64(s.scratchpad + spAddr1 + 8 * i, nreg.r[i]);
			}
			// ---- step 10: mix F with E
			int32_t ldA0 = emit(C_ALU, P_STACK, lea0, -1, -1, 1);   // mov rcx,[rsp+8]
			if (v2) {
				rx_vec_i128 ekey[RegisterCountFlt];
				rx_vec_i128 freg[RegisterCountFlt];
				for (unsigned i = 0; i < RegisterCountFlt; ++i) {
					ekey[i] = rx_cast_vec_f2i(nreg.e[i]);
					freg[i] = rx_cast_vec_f2i(nreg.f[i]);
				}
				for (unsigned i = 0; i < RegisterCountFlt; ++i) {
					freg[0] = aesenc<false>(freg[0], ekey[i]);
					freg[1] = aesdec<false>(freg[1], ekey[i]);
					freg[2] = aesenc<false>(freg[2], ekey[i]);
					freg[3] = aesdec<false>(freg[3], ekey[i]);
					for (int j = 0; j < 4; ++j)
						prod[8 + j] = emit(C_AES, P_MIX, prod[8 + j], prod[12 + i]);
					stats.aesRounds += 4;
				}
				for (unsigned i = 0; i < RegisterCountFlt; ++i)
					nreg.f[i] = rx_cast_vec_i2f(freg[i]);
			}
			else {
				for (unsigned i = 0; i < RegisterCountFlt; ++i) {
					nreg.f[i] = rx_xor_vec_f128(nreg.f[i], nreg.e[i]);
					prod[8 + i] = emit(C_FLOG, P_MIX, prod[8 + i], prod[12 + i]);
				}
			}
			// ---- step 11: F block store at spAddr0
			int32_t evF = live ? live->newEvent(EV_FBLK, 8, globalIter()) : -1;
			for (unsigned i = 0; i < RegisterCountFlt; ++i) {
				int32_t evQ = live ? live->newEvent(EV_FQ, 2, globalIter(), evF) : -1;
				storeWords(spAddr0 + 16 * i, 2, ldA0, prod[8 + i], P_BLKST_F, M_BLK, evF, evQ, F_FSTORE);
				rx_store_vec_f128((double*)(s.scratchpad + spAddr0 + 16 * i), nreg.f[i]);
			}
			// loop branch
			emit(C_BR, P_LOOP, -1, -1, -1, 1);
			trace.iterEnd.push_back((int32_t)trace.u.size() - 1);
			addr0 = na0; addr1 = na1; spPref0 = sp0; spPref1 = sp1;
			spAddr0 = 0;
			spAddr1 = 0;
		}
		trace.iterations = RANDOMX_PROGRAM_ITERATIONS;
		for (unsigned i = 0; i < RegistersCount; ++i)
			store64(&s.reg.r[i], nreg.r[i]);
		for (unsigned i = 0; i < RegisterCountFlt; ++i)
			rx_store_vec_f128(&s.reg.f[i].lo, nreg.f[i]);
		for (unsigned i = 0; i < RegisterCountFlt; ++i)
			rx_store_vec_f128(&s.reg.e[i].lo, nreg.e[i]);
	}
	uint32_t curMode = 0;
};

} // namespace ax
