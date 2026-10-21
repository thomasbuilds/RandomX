// opcount: operation counts from the library's own generators.
// Part 1: RandomX programs (AesGenerator4R output decoded with the opcode frequency table), v1 (first 256
//         instructions) and v2 (384), per program iteration: instruction types, multiplications, FP ops by
//         kind, Scratchpad reads/writes by level, effective IMUL_RCP/ISWAP, CBRANCH re-execution estimate.
// Part 2: SuperscalarHash instances (Blake2Generator + generateSuperscalar from random keys): instruction
//         types, multiplications, generator latencies, and dataflow critical paths of a full Dataset item
//         (8 programs + 8 dependent Cache loads) under several latency models.
// Usage: opcount progs=20000 keys=2000
#include "common.hpp"
#include "program.hpp"
#include "aes_hash.hpp"
#include "superscalar.hpp"
#include "blake2_generator.hpp"
#include "bytecode_machine.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <cmath>

using namespace randomx;
static uint64_t rs = 0x243F6A8885A308D3ull;
static uint64_t xr() { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }

static const char* tnames[] = {"IADD_RS","IADD_M","ISUB_R","ISUB_M","IMUL_R","IMUL_M","IMULH_R","IMULH_M","ISMULH_R","ISMULH_M",
	"IMUL_RCP","INEG_R","IXOR_R","IXOR_M","IROR_R","IROL_R","ISWAP_R","FSWAP_R","FADD_R","FADD_M","FSUB_R","FSUB_M","FSCAL_R",
	"FMUL_R","FDIV_M","FSQRT_R","CBRANCH","CFROUND","ISTORE","NOP"};
static const int ceils[] = {ceil_IADD_RS, ceil_IADD_M, ceil_ISUB_R, ceil_ISUB_M, ceil_IMUL_R, ceil_IMUL_M, ceil_IMULH_R, ceil_IMULH_M,
	ceil_ISMULH_R, ceil_ISMULH_M, ceil_IMUL_RCP, ceil_INEG_R, ceil_IXOR_R, ceil_IXOR_M, ceil_IROR_R, ceil_IROL_R, ceil_ISWAP_R,
	ceil_FSWAP_R, ceil_FADD_R, ceil_FADD_M, ceil_FSUB_R, ceil_FSUB_M, ceil_FSCAL_R, ceil_FMUL_R, ceil_FDIV_M, ceil_FSQRT_R,
	ceil_CBRANCH, ceil_CFROUND, ceil_ISTORE, ceil_NOP};
enum { T_IADD_RS, T_IADD_M, T_ISUB_R, T_ISUB_M, T_IMUL_R, T_IMUL_M, T_IMULH_R, T_IMULH_M, T_ISMULH_R, T_ISMULH_M, T_IMUL_RCP,
	T_INEG_R, T_IXOR_R, T_IXOR_M, T_IROR_R, T_IROL_R, T_ISWAP_R, T_FSWAP_R, T_FADD_R, T_FADD_M, T_FSUB_R, T_FSUB_M, T_FSCAL_R,
	T_FMUL_R, T_FDIV_M, T_FSQRT_R, T_CBRANCH, T_CFROUND, T_ISTORE, T_NOP, T_COUNT };

static int decode(int opcode) { for (int t = 0; t < T_COUNT; ++t) if (opcode < ceils[t]) return t; return T_NOP; }

struct ProgStats {
	double type[T_COUNT] = {0};
	double mul = 0, rcpEff = 0, swapEff = 0, rdL1 = 0, rdL2 = 0, rdL3 = 0, stL1 = 0, stL2 = 0, stL3 = 0, cbrExtra = 0, cbrLen = 0;
	double fpAddSub = 0, fpMul = 0, fpDiv = 0, fpSqrt = 0, intAlu = 0, n = 0;
};

static void analyse(Program& p, int size, ProgStats& s) {
	int regUse[8]; for (int i = 0; i < 8; ++i) regUse[i] = -1;
	for (int i = 0; i < size; ++i) {
		Instruction& in = p(i);
		int t = decode(in.opcode);
		int dst = in.dst % 8, src = in.src % 8;
		s.type[t] += 1;
		bool modifiesDst = false;
		switch (t) {
		case T_IADD_M: case T_ISUB_M: case T_IMUL_M: case T_IMULH_M: case T_ISMULH_M: case T_IXOR_M:
			if (src == dst) s.rdL3 += 1; else if (in.getModMem() == 0) s.rdL2 += 1; else s.rdL1 += 1;
			modifiesDst = true; break;
		case T_FADD_M: case T_FSUB_M: case T_FDIV_M:
			if (in.getModMem() == 0) s.rdL2 += 1; else s.rdL1 += 1; break;
		case T_ISTORE:
			if (in.getModCond() >= StoreL3Condition) s.stL3 += 1; else if (in.getModMem() == 0) s.stL2 += 1; else s.stL1 += 1; break;
		case T_IMUL_RCP:
			if (!isZeroOrPowerOf2(in.getImm32())) { s.rcpEff += 1; modifiesDst = true; } break;
		case T_ISWAP_R:
			if (src != dst) { s.swapEff += 1; regUse[dst] = i; regUse[src] = i; } break;
		case T_CBRANCH: {
			int target = regUse[dst]; // jump goes to target+1 (or 0 when -1)
			int len = i - target;     // instructions re-executed on a taken jump (target+1 .. i)
			s.cbrLen += len;
			s.cbrExtra += len / 255.0; // P(jump)=1/256 per execution, geometric repeats
			for (int r = 0; r < 8; ++r) regUse[r] = i;
			break; }
		case T_CFROUND: case T_NOP: case T_FSWAP_R: case T_FADD_R: case T_FSUB_R: case T_FSCAL_R: case T_FMUL_R: case T_FSQRT_R: break;
		default: modifiesDst = true; break;
		}
		if (modifiesDst) regUse[dst] = i;
		if (t == T_IMUL_R || t == T_IMUL_M || t == T_IMULH_R || t == T_IMULH_M || t == T_ISMULH_R || t == T_ISMULH_M) s.mul += 1;
		if (t == T_IMUL_RCP && !isZeroOrPowerOf2(in.getImm32())) s.mul += 1;
		if (t == T_FADD_R || t == T_FADD_M || t == T_FSUB_R || t == T_FSUB_M) s.fpAddSub += 2;
		if (t == T_FMUL_R) s.fpMul += 2;
		if (t == T_FDIV_M) s.fpDiv += 2;
		if (t == T_FSQRT_R) s.fpSqrt += 2;
		if (t == T_IADD_RS || t == T_IADD_M || t == T_ISUB_R || t == T_ISUB_M || t == T_INEG_R || t == T_IXOR_R || t == T_IXOR_M ||
			t == T_IROR_R || t == T_IROL_R || t == T_CBRANCH) s.intAlu += 1;
	}
	s.n += 1;
}

// ---------------- SuperscalarHash ----------------
enum SsKind { SS_ALU, SS_MUL, SS_MULH };
static SsKind ssKind(int op, bool& usesSrc) {
	auto t = (SuperscalarInstructionType)op;
	usesSrc = false;
	switch (t) {
	case SuperscalarInstructionType::ISUB_R: case SuperscalarInstructionType::IXOR_R: case SuperscalarInstructionType::IADD_RS:
		usesSrc = true; return SS_ALU;
	case SuperscalarInstructionType::IMUL_R: usesSrc = true; return SS_MUL;
	case SuperscalarInstructionType::IMULH_R: case SuperscalarInstructionType::ISMULH_R: usesSrc = true; return SS_MULH;
	case SuperscalarInstructionType::IMUL_RCP: return SS_MUL;
	default: return SS_ALU; // IROR_C, IADD_C*, IXOR_C*
	}
}

// dataflow latency of one Dataset item: 8 programs + 8 Cache loads (load j addressed by the address register
// of program j-1 after the XOR with Cache line j-1; load 0 addressed by the item number, known at t=0).
static double itemLatency(SuperscalarProgram* progs, double tAlu, double tMul, double tMulh, double tMem, double tXor) {
	double rdy[8];
	for (int r = 0; r < 8; ++r) rdy[r] = tMul + (r ? tXor : 0); // r0 = (item+1)*C, r1..7 = r0 ^ C
	double loadReady = tMem; // load 0 issued at t=0
	for (int j = 0; j < RANDOMX_CACHE_ACCESSES; ++j) {
		SuperscalarProgram& p = progs[j];
		for (unsigned i = 0; i < p.getSize(); ++i) {
			Instruction& in = p(i);
			bool us; SsKind k = ssKind(in.opcode, us);
			double lat = k == SS_ALU ? tAlu : (k == SS_MUL ? tMul : tMulh);
			double start = rdy[in.dst];
			if (us && in.src != in.dst) start = std::max(start, rdy[in.src]);
			rdy[in.dst] = start + lat;
		}
		for (int r = 0; r < 8; ++r) rdy[r] = std::max(rdy[r], loadReady) + tXor;
		loadReady = rdy[p.getAddressRegister()] + tMem;
	}
	double m = 0; for (int r = 0; r < 8; ++r) m = std::max(m, rdy[r]);
	return m;
}

// critical path of a single program with all registers ready at 0
static double progLatency(SuperscalarProgram& p, double tAlu, double tMul, double tMulh, int* mulsOnPath = nullptr) {
	double rdy[8] = {0}; int muls[8] = {0};
	for (unsigned i = 0; i < p.getSize(); ++i) {
		Instruction& in = p(i);
		bool us; SsKind k = ssKind(in.opcode, us);
		double lat = k == SS_ALU ? tAlu : (k == SS_MUL ? tMul : tMulh);
		double start = rdy[in.dst]; int m = muls[in.dst];
		if (us && in.src != in.dst && rdy[in.src] > start) { start = rdy[in.src]; m = muls[in.src]; }
		rdy[in.dst] = start + lat; muls[in.dst] = m + (k != SS_ALU);
	}
	double mx = 0; int mm = 0;
	for (int r = 0; r < 8; ++r) if (rdy[r] > mx) { mx = rdy[r]; mm = muls[r]; }
	if (mulsOnPath) *mulsOnPath = mm;
	return mx;
}

int main(int argc, char** argv) {
	int progs = 20000, keys = 2000;
	for (int i = 1; i < argc; ++i) {
		if (!strncmp(argv[i], "progs=", 6)) progs = atoi(argv[i] + 6);
		if (!strncmp(argv[i], "keys=", 5)) keys = atoi(argv[i] + 5);
	}
	// ---------- Part 1 ----------
	ProgStats st[2];
	alignas(64) static Program program;
	alignas(64) uint8_t seed[64];
	for (int k = 0; k < progs; ++k) {
		for (int i = 0; i < 64; ++i) seed[i] = (uint8_t)xr();
		fillAes4Rx4<false>(seed, sizeof(program), &program);
		analyse(program, 256, st[0]);
		analyse(program, 384, st[1]);
	}
	printf("== RandomX programs: %d random programs (AesGenerator4R), counts per program iteration ==\n", progs);
	printf("%-12s %10s %10s\n", "type", "v1(256)", "v2(384)");
	for (int t = 0; t < T_COUNT; ++t) printf("%-12s %10.3f %10.3f\n", tnames[t], st[0].type[t] / st[0].n, st[1].type[t] / st[1].n);
	const char* nm[] = {"mul64_total", "IMUL_RCP_effective", "ISWAP_effective", "read_L1", "read_L2", "read_L3(fixed addr)", "store_L1", "store_L2", "store_L3",
		"fp_addsub_lanes", "fp_mul_lanes", "fp_div_lanes", "fp_sqrt_lanes", "int_alu_ops", "cbranch_reexec_instr", "cbranch_mean_block"};
	for (int q = 0; q < 16; ++q) {
		double v[2];
		for (int w = 0; w < 2; ++w) {
			ProgStats& s = st[w];
			double a[] = {s.mul, s.rcpEff, s.swapEff, s.rdL1, s.rdL2, s.rdL3, s.stL1, s.stL2, s.stL3, s.fpAddSub, s.fpMul, s.fpDiv, s.fpSqrt, s.intAlu, s.cbrExtra, s.cbrLen / std::max(1.0, s.type[T_CBRANCH])};
			v[w] = q == 15 ? a[q] : a[q] / s.n;
		}
		printf("%-22s %10.3f %10.3f\n", nm[q], v[0], v[1]);
	}
	// ---------- Part 2 ----------
	std::vector<double> size, mul, asic, cpu, ipc;
	double typeCnt[(int)SuperscalarInstructionType::COUNT] = {0};
	struct M { const char* name; double tAlu, tMul, tMulh, tMem, tXor; };
	std::vector<M> models = {
		{"unit (all ops 1, mem 0)", 1, 1, 1, 0, 1},
		{"refCPU lat (mul 3, mulh 4 incl. movs; mem 0)", 1, 3, 4, 0, 1},
		{"refCPU lat + mem 170 cyc (ideal prefetch)", 1, 3, 4, 170, 1},
		{"ASIC mul=2 alu, mem 0", 1, 2, 2, 0, 1},
		{"ASIC mul=3 alu, mem 0", 1, 3, 3, 0, 1},
		{"ASIC mul=4 alu, mem 0", 1, 4, 4, 0, 1},
		{"ASIC mul=6 alu, mem 0", 1, 6, 6, 0, 1},
		{"unit + mem 10", 1, 1, 1, 10, 1},
		{"unit + mem 50", 1, 1, 1, 50, 1},
		{"unit + mem 100", 1, 1, 1, 100, 1},
		{"unit + mem 200", 1, 1, 1, 200, 1},
	};
	std::vector<std::vector<double>> itemLat(models.size());
	std::vector<double> sumProgUnit, sumProgRef, mulsPath;
	SuperscalarProgram ssp[RANDOMX_CACHE_ACCESSES];
	for (int k = 0; k < keys; ++k) {
		uint8_t key[32]; for (int i = 0; i < 32; ++i) key[i] = (uint8_t)xr();
		Blake2Generator gen(key, sizeof(key));
		double su = 0, sr = 0, mp = 0;
		for (int j = 0; j < RANDOMX_CACHE_ACCESSES; ++j) {
			generateSuperscalar(ssp[j], gen);
			SuperscalarProgram& p = ssp[j];
			size.push_back(p.getSize()); mul.push_back(p.mulCount); asic.push_back(p.asicLatency); cpu.push_back(p.cpuLatency); ipc.push_back(p.ipc);
			for (unsigned i = 0; i < p.getSize(); ++i) typeCnt[p(i).opcode] += 1;
			int m = 0;
			su += progLatency(p, 1, 1, 1, &m); mp += m;
			sr += progLatency(p, 1, 3, 4);
		}
		sumProgUnit.push_back(su); sumProgRef.push_back(sr); mulsPath.push_back(mp);
		for (size_t q = 0; q < models.size(); ++q)
			itemLat[q].push_back(itemLatency(ssp, models[q].tAlu, models[q].tMul, models[q].tMulh, models[q].tMem, models[q].tXor));
	}
	auto stat = [](std::vector<double> v, const char* name) {
		double s = 0, s2 = 0; for (double x : v) { s += x; s2 += x * x; }
		double m = s / v.size(), sd = sqrt(std::max(0.0, s2 / v.size() - m * m));
		std::sort(v.begin(), v.end());
		printf("%-48s mean %9.2f sd %7.2f min %8.1f p50 %8.1f max %8.1f\n", name, m, sd, v.front(), v[v.size() / 2], v.back());
	};
	printf("\n== SuperscalarHash: %d keys x %d programs ==\n", keys, RANDOMX_CACHE_ACCESSES);
	stat(size, "instructions per program");
	stat(mul, "multiplications per program (generator mulCount)");
	stat(asic, "generator asicLatency (1 cycle/op, per program)");
	stat(cpu, "generator cpuLatency (ref. CPU cycles, per prog)");
	stat(ipc, "generator IPC (macro-ops/cycle)");
	stat(sumProgUnit, "sum of 8 program crit. paths, unit latency");
	stat(mulsPath, "multiplications on those 8 critical paths");
	stat(sumProgRef, "sum of 8 program crit. paths, ref CPU lat");
	double tot = 0; for (double x : typeCnt) tot += x;
	const char* sn[] = {"ISUB_R","IXOR_R","IADD_RS","IMUL_R","IROR_C","IADD_C7","IXOR_C7","IADD_C8","IXOR_C8","IADD_C9","IXOR_C9","IMULH_R","ISMULH_R","IMUL_RCP"};
	printf("instruction mix (per program):\n");
	for (int t = 0; t < (int)SuperscalarInstructionType::COUNT; ++t) printf("  %-10s %8.3f (%.4f)\n", sn[t], typeCnt[t] / size.size(), typeCnt[t] / tot);
	printf("item dataflow latency (8 programs + 8 dependent Cache loads, unlimited parallelism):\n");
	for (size_t q = 0; q < models.size(); ++q) stat(itemLat[q], models[q].name);
	return 0;
}
