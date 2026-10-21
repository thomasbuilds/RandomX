// Agent X: RandomX execution-model analysis - dynamic trace definitions.
// The trace is a dependency-annotated list of micro-operations (uops) that mirrors the
// x86-64 JIT code sequence of each executed VM instruction and of the loop overhead
// (steps 1-3 and 5-11 of spec 4.6.2). Dependencies are explicit producer indices
// (register renaming done at trace build time), so every analysis (dataflow critical
// path, out-of-order simulation) works on the same graph.
#pragma once
#include <cstdint>
#include <vector>
#include <string>

enum UClass : uint8_t {
	C_ALU = 0,  // integer add/sub/xor/neg/rot/and/xchg
	C_LEA3,     // 3-component lea (IADD_RS with r5 + imm32)
	C_AG1,      // address generation part 1 (lea src+imm)
	C_AG2,      // address generation part 2 (and mask)
	C_IMUL,     // 64x64->64 multiply
	C_IMULH,    // 64x64->128 high half (signed or unsigned)
	C_LOADI,    // integer load (scratchpad word, block word or Dataset word)
	C_LOADF,    // FP load + cvtdq2pd
	C_STORE,    // store (8 or 16 bytes)
	C_FADD,     // addpd/subpd
	C_FMUL,     // mulpd
	C_FDIV,     // divpd
	C_FSQRT,    // sqrtpd
	C_FLOG,     // xorps/andps/orps/shufpd (FP domain logic)
	C_AES,      // one AES round (aesenc/aesdec)
	C_BR,       // fused test+jcc (CBRANCH test, v2 CFROUND gate, loop branch)
	C_MXCSR,    // ldmxcsr (rounding mode write)
	C_PREF,     // prefetch (Dataset or scratchpad line)
	C_NCLASS
};

// memory level of a load/store
enum MemLvl : uint8_t { M_NONE = 0, M_L1, M_L2, M_L3, M_BLK, M_DS, M_STACK };

enum UFlags : uint8_t {
	F_MISP = 1,      // mispredicted branch (taken CBRANCH, or v2 CFROUND gate that opened)
	F_RND = 2,       // FP op that depends on the rounding mode
	F_ITEREND = 4,   // marks the uop that produces the last r_end register of an iteration
	F_FSTORE = 8,    // F-block store (step 11)
	F_SPLOAD = 16,   // block load (steps 2-3)
	F_DSPREF = 32,   // Dataset prefetch
};

// pseudo VM types for loop overhead (VM types 0..29 from randomx::InstructionType)
enum PseudoType : uint8_t {
	P_SPMIX = 40, P_SPADDR, P_BLKLD_I, P_BLKLD_F, P_EMASK, P_MPXOR, P_DSPREF, P_DSLOAD, P_DSXOR,
	P_SPPREF, P_BLKST_I, P_MIX, P_BLKST_F, P_LOOP, P_STACK, P_IMUL_RCP, P_LAST
};

struct Uop {
	int32_t d[3];     // producer uops (register / flag dependencies), -1 = none
	int32_t md;       // producer store (true RAW through scratchpad memory), -1 = none
	int32_t aux;      // for M_DS loads: the Dataset prefetch uop; for M_BLK loads: the scratchpad prefetch uop
	uint32_t iter;    // iteration index within the program
	uint8_t cls;      // UClass
	uint8_t lvl;      // MemLvl
	uint8_t flags;    // UFlags
	uint8_t slots;    // x86 macro-op slots (ROB/dispatch) in the CPU model
	uint8_t vm;       // VM instruction type or PseudoType
	uint8_t pad[3];
};

struct ProgramTrace {
	std::vector<Uop> u;
	std::vector<int32_t> iterEnd;   // index of last uop of each iteration (exclusive end = iterEnd[i]+1)
	std::vector<int32_t> rEndUop;   // per iteration: uops producing r_end (8 Dataset XORs); index of the first
	int iterations = 0;
};
