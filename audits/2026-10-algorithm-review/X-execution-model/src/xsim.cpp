// Agent X: RandomX v1/v2 execution-model analysis driver.
// Build: see ../build.sh. Run under taskset -c 0-2 nice -n 10.
#include "builder.hpp"
#include "analyze.hpp"
#include "randomx.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <map>
#include <chrono>

using namespace ax;

static const char* vmName(int t) {
	static const char* names[] = { "IADD_RS","IADD_M","ISUB_R","ISUB_M","IMUL_R","IMUL_M","IMULH_R","IMULH_M","ISMULH_R","ISMULH_M",
		"IMUL_RCP","INEG_R","IXOR_R","IXOR_M","IROR_R","IROL_R","ISWAP_R","FSWAP_R","FADD_R","FADD_M","FSUB_R","FSUB_M","FSCAL_R",
		"FMUL_R","FDIV_M","FSQRT_R","CBRANCH","CFROUND","ISTORE","NOP" };
	return t < 30 ? names[t] : "?";
}
static const char* clsName(int c) {
	static const char* names[] = { "ALU","LEA3","AG1","AG2","IMUL","IMULH","LOADI","LOADF","STORE","FADD","FMUL","FDIV","FSQRT","FLOG","AES","BR","MXCSR","PREF" };
	return names[c];
}

struct Opts {
	int hashes = 4;
	uint32_t seed = 1;
	bool validate = false;
	bool doOoo = true;
	bool doSmt = true;
	bool v1 = true, v2 = true;
	bool lean = false;
	std::string key = "agentX execution model key";
	std::string tag = "run";
};

int main(int argc, char** argv) {
	Opts o;
	for (int i = 1; i < argc; ++i) {
		std::string a = argv[i];
		if (a == "--hashes") o.hashes = atoi(argv[++i]);
		else if (a == "--seed") o.seed = (uint32_t)strtoul(argv[++i], nullptr, 0);
		else if (a == "--validate") o.validate = true;
		else if (a == "--no-ooo") o.doOoo = false;
		else if (a == "--no-smt") o.doSmt = false;
		else if (a == "--lean") o.lean = true;
		else if (a == "--v1only") o.v2 = false;
		else if (a == "--v2only") o.v1 = false;
		else if (a == "--key") o.key = argv[++i];
		else if (a == "--tag") o.tag = argv[++i];
		else { fprintf(stderr, "unknown option %s\n", a.c_str()); return 1; }
	}
	randomx_cache* cache = randomx_alloc_cache((randomx_flags)(RANDOMX_FLAG_JIT | RANDOMX_FLAG_ARGON2));
	if (!cache) cache = randomx_alloc_cache(RANDOMX_FLAG_JIT);
	randomx_init_cache(cache, o.key.data(), o.key.size());
	randomx_vm* refVm[2] = { nullptr, nullptr };
	if (o.validate) {
		refVm[0] = randomx_create_vm((randomx_flags)(RANDOMX_FLAG_HARD_AES | RANDOMX_FLAG_JIT), cache, nullptr);
		refVm[1] = randomx_create_vm((randomx_flags)(RANDOMX_FLAG_HARD_AES | RANDOMX_FLAG_JIT | RANDOMX_FLAG_V2), cache, nullptr);
	}
	VmState* s = new VmState();
	s->scratchpad = (uint8_t*)aligned_alloc(64, ScratchpadSize);
	s->cache = cache;

	// latency models for the dataflow analysis
	LatModel mA = latAppB(), mZ = latZen4(47), mC = latAsic(2, 6, 2), mC2 = latAsic(3, 12, 2);
	mC2.name = "asic_slow";
	// CPU models
	CpuModel z4 = cpuZen4(), z5 = cpuZen5(), skx = cpuSkylake(), gc = cpuGoldenCove(), z2 = cpuZen2();
	z4.mxcsrMode = 3; z4.lat.lat[C_MXCSR] = 10; z5.mxcsrMode = 3; z5.lat.lat[C_MXCSR] = 10; z2.mxcsrMode = 3; z2.lat.lat[C_MXCSR] = 10;
	skx.mxcsrMode = 3; skx.lat.lat[C_MXCSR] = 7;
	std::vector<ThreadCtx> th1(1), th2(2);

	ProgramTrace tr, trPrev;
	DynStats tot[2];
	Liveness live;
	int mismatches = 0;
	auto t0 = std::chrono::steady_clock::now();
	for (int h = 0; h < o.hashes; ++h) {
		uint32_t input[2] = { o.seed, (uint32_t)h };
		for (int ver = 0; ver < 2; ++ver) {
			if ((ver == 0 && !o.v1) || (ver == 1 && !o.v2)) continue;
			bool v2 = ver == 1;
			TraceVm vm(*s, v2);
			vm.live = &live;
			live.reset();
			blake2b(s->tempHash, sizeof(s->tempHash), input, sizeof(input), nullptr, 0);
			fillAes1Rx4<false>(s->tempHash, ScratchpadSize, s->scratchpad);
			rx_reset_float_state();
			vm.curMode = 0;
			DynStats hs;
			trPrev.u.clear();
			for (int p = 0; p < RANDOMX_PROGRAM_COUNT; ++p) {
				vm.progIndex = p;
				DynStats ps;
				vm.runProgram(tr, ps);
				hs.add(ps);
				if (p < RANDOMX_PROGRAM_COUNT - 1)
					blake2b(s->tempHash, sizeof(s->tempHash), &s->reg, sizeof(RegisterFile), nullptr, 0);
				// ---------------- per-program analyses
				printf("PROG %s v%d h=%d p=%d uops=%zu slots=%.0f taken=%.0f reexec=%.0f cfopen=%.0f fprcchg=%.0f rawMem=%.0f rawRecent=%.0f",
					o.tag.c_str(), v2 ? 2 : 1, h, p, tr.u.size(), ps.slots, ps.taken, ps.reexec, ps.cfroundOpen, ps.fprcChanges, ps.rawMem, ps.rawMemRecent);
				// program composition (static counts per type over the dynamic stream / iterations)
				printf(" nFDIV=%.0f nFSQRT=%.0f nIMULH=%.0f nCBR=%.0f nCFR=%.0f",
					ps.vmCount[24] / 2048.0, ps.vmCount[25] / 2048.0, (ps.vmCount[6] + ps.vmCount[7] + ps.vmCount[8] + ps.vmCount[9]) / 2048.0,
					ps.vmCount[26] / 2048.0, ps.vmCount[27] / 2048.0);
				struct DFRun { std::string name; const LatModel* m; DFOptions opt; };
				std::vector<DFRun> runs;
				DFOptions d0; d0.Lmem = 0;
				DFOptions dns = d0; dns.speculate = false;
				const uint32_t fpMask = (1u << C_FADD) | (1u << C_FMUL) | (1u << C_FDIV) | (1u << C_FSQRT) | (1u << C_FLOG) | (1u << C_AES) | (1u << C_LOADF) | (1u << C_MXCSR);
				runs.push_back({ "A0", &mA, d0 });
				runs.push_back({ "A0ns", &mA, dns });
				runs.push_back({ "Z0", &mZ, d0 });
				{ DFOptions x = d0; x.zeroMask = 1u << C_AES; runs.push_back({ "Z0noAES", &mZ, x }); }
				{ DFOptions x = d0; x.zeroMask = fpMask; runs.push_back({ "Z0noFP", &mZ, x }); }
				{ DFOptions x = d0; x.rndSerialize = true; runs.push_back({ "Z0ser", &mZ, x }); }
				for (int ns : { 50, 75, 100 }) { DFOptions x = d0; x.Lmem = ns * 5.0; runs.push_back({ "Z5g" + std::to_string(ns), &mZ, x }); }
				runs.push_back({ "C0", &mC, d0 });
				{ DFOptions x = d0; x.zeroMask = 1u << C_AES; runs.push_back({ "C0noAES", &mC, x }); }
				{ DFOptions x = d0; x.zeroMask = fpMask; runs.push_back({ "C0noFP", &mC, x }); }
				for (int ns : { 50, 75, 100 }) { DFOptions x = d0; x.Lmem = ns * 1.0; runs.push_back({ "C1g" + std::to_string(ns), &mC, x }); }
				for (int ns : { 50, 100 }) { DFOptions x = d0; x.Lmem = ns * 2.0; runs.push_back({ "C2g" + std::to_string(ns), &mC, x }); }
				runs.push_back({ "S0", &mC2, d0 });
				for (auto& r : runs) {
					DFResult d = dataflow(tr, *r.m, r.opt);
					printf(" %s:tot=%.2f,rec=%.2f,lag=%.1f,maxlag=%.0f,isoCP=%.2f,isoRec=%.2f,isoFp=%.2f",
						r.name.c_str(), d.total / 2048.0, d.meanRec, d.meanFpLag, d.maxFpLag, d.isoCP, d.isoRec, d.isoFp);
				}
				if (o.doOoo) {
					struct ORun { std::string name; CpuModel c; double ns; };
					std::vector<ORun> oruns;
					CpuModel z4r = z4; z4r.mxcsrMode = 0;   // renamed rounding mode (no LDMXCSR penalty)
					if (o.lean) {
						oruns.push_back({ "z4r320", z4r, 0 });
						oruns.push_back({ "z5m0", z5, 0 });
						oruns.push_back({ "z5m75", z5, 75 });
						oruns.push_back({ "skxm0", skx, 0 });
						oruns.push_back({ "skxm150", skx, 150 });
						oruns.push_back({ "skxm190", skx, 190 });
						{ CpuModel c = skx; c.ghz = 3.5; oruns.push_back({ "skx35m0", c, 0 }); }
						{ CpuModel c = skx; c.ghz = 3.5; oruns.push_back({ "skx35m170", c, 170 }); }
					} else {
					for (int rob : { 224, 320, 448, 512, 4096 }) { CpuModel c = z4r; c.rob = rob; oruns.push_back({ "z4r" + std::to_string(rob), c, 0 }); }
					{ CpuModel c = z4; c.mxcsrMode = 1; oruns.push_back({ "z4drain", c, 0 }); }
					{ CpuModel c = z4; c.mxcsrMode = 3; c.lat.lat[C_MXCSR] = 10; oruns.push_back({ "z4st10", c, 0 }); }
					for (int ns : { 75, 100 }) oruns.push_back({ "z4m" + std::to_string(ns), z4, (double)ns });
					{ CpuModel c = z4; c.ghz = 3.5; oruns.push_back({ "z4f35m75", c, 75 }); }
					for (int ns : { 0, 75, 100 }) oruns.push_back({ "z5m" + std::to_string(ns), z5, (double)ns });
					{ CpuModel c = z5; c.mxcsrMode = 0; oruns.push_back({ "z5renm75", c, 75 }); }
					oruns.push_back({ "z2m70", z2, 70 });
					oruns.push_back({ "skxm0", skx, 0 });
					oruns.push_back({ "skxm90", skx, 90 });
					oruns.push_back({ "gcm80", gc, 80 });
					}
					for (auto& r : oruns) {
						th1[0].tr = &tr;
						ooo(th1, r.c, r.ns * r.c.ghz);
						printf(" %s:it=%.2f,robw=%.2f,dsw=%.2f", r.name.c_str(), th1[0].res.perIter, th1[0].res.robFullCycles / 2048.0, th1[0].res.dsWait / 2048.0 / 8);
					}
					if (o.doSmt && !trPrev.u.empty()) {
						struct SRun { std::string name; CpuModel c; double ns; };
						std::vector<SRun> sruns;
						if (o.lean) { CpuModel c = z5; c.rob = 224; sruns.push_back({ "smt_z5m75", c, 75 }); }
						else {
						for (int ns : { 0, 75, 100 }) { CpuModel c = z5; c.rob = 224; sruns.push_back({ "smt_z5m" + std::to_string(ns), c, (double)ns }); }
						{ CpuModel c = z5; c.rob = 224; c.mxcsrMode = 0; sruns.push_back({ "smt_z5renm75", c, 75 }); }
						{ CpuModel c = z4; c.rob = 160; sruns.push_back({ "smt_z4m75", c, 75 }); }
						{ CpuModel c = z4; c.rob = 160; c.ghz = 3.5; sruns.push_back({ "smt_z4f35m75", c, 75 }); }
						{ CpuModel c = z2; c.rob = 112; sruns.push_back({ "smt_z2m70", c, 70 }); }
						}
						for (auto& r : sruns) {
							th2[0].tr = &trPrev; th2[1].tr = &tr;
							ooo(th2, r.c, r.ns * r.c.ghz);
							double core = std::max(th2[0].res.cycles, th2[1].res.cycles);
							printf(" %s:core=%.2f,t0=%.2f,t1=%.2f", r.name.c_str(), core / 4096.0, th2[0].res.perIter, th2[1].res.perIter);
						}
					}
				}
				printf("\n");
				fflush(stdout);
				std::swap(trPrev, tr);
			}
			// final result
			live.finalRead(RANDOMX_PROGRAM_COUNT * RANDOMX_PROGRAM_ITERATIONS);
			hashAes1Rx4<false>(s->scratchpad, ScratchpadSize, &s->reg.a);
			uint8_t out[32];
			blake2b(out, sizeof(out), &s->reg, sizeof(RegisterFile), nullptr, 0);
			tot[ver].add(hs);
			// liveness summary for this hash
			double cnt[EV_NKIND][RD_NKIND] = { {0} };
			live.summarize(cnt);
			printf("LIVE %s v%d h=%d", o.tag.c_str(), v2 ? 2 : 1, h);
			const char* kn[] = { "IBLK", "FBLK", "FQ", "ISTORE" };
			const char* rn[] = { "blk0", "blk1", "mL1", "mL2", "mL3", "final", "dead" };
			for (int k = 0; k < EV_NKIND; ++k) {
				printf(" %s:", kn[k]);
				for (int r = 0; r < RD_NKIND; ++r) printf("%s=%.0f%s", rn[r], cnt[k][r], r + 1 < RD_NKIND ? "," : "");
			}
			printf("\n");
			printf("DIST %s v%d h=%d", o.tag.c_str(), v2 ? 2 : 1, h);
			for (int k = 0; k < EV_NKIND; ++k) {
				printf(" %s:", kn[k]);
				for (int b = 0; b < 24; ++b) printf("%.0f%s", live.distHist[k][b], b < 23 ? "," : "");
			}
			printf("\n");
			for (int k = 0; k < EV_NKIND; ++k) for (int b = 0; b < 24; ++b) live.distHist[k][b] = 0;
			printf("HASH %s v%d h=%d ", o.tag.c_str(), v2 ? 2 : 1, h);
			for (int i = 0; i < 32; ++i) printf("%02x", out[i]);
			if (o.validate) {
				uint8_t ref[32];
				randomx_calculate_hash(refVm[ver], input, sizeof(input), ref);
				bool ok = memcmp(ref, out, 32) == 0;
				if (!ok) mismatches++;
				printf(" ref=");
				for (int i = 0; i < 32; ++i) printf("%02x", ref[i]);
				printf(" %s", ok ? "MATCH" : "MISMATCH");
			}
			printf("\n");
			fflush(stdout);
		}
	}
	// totals
	for (int ver = 0; ver < 2; ++ver) {
		DynStats& d = tot[ver];
		if (d.iterations == 0) continue;
		printf("TOTAL v%d iterations=%.0f\n", ver + 1, d.iterations);
		for (int t = 0; t < 30; ++t) printf("  VM %-9s %.4f per iteration\n", vmName(t), d.vmCount[t] / d.iterations);
		for (int c = 0; c < C_NCLASS; ++c) printf("  UOP %-6s %.3f per iteration\n", clsName(c), d.uopCount[c] / d.iterations);
		printf("  slots %.2f per iteration\n", d.slots / d.iterations);
		printf("  taken CBRANCH %.5f per iteration (exec %.3f), re-executed VM instr %.4f per iteration (%.3f per taken)\n",
			d.taken / d.iterations, d.cbranchExec / d.iterations, d.reexec / d.iterations, d.reexec / std::max(1.0, d.taken));
		printf("  CFROUND exec %.4f open %.5f fprc-changes %.5f per iteration\n", d.cfroundExec / d.iterations, d.cfroundOpen / d.iterations, d.fprcChanges / d.iterations);
		printf("  M-loads L1 %.3f L2 %.3f L3 %.3f per iteration\n", d.loadsByLvl[M_L1] / d.iterations, d.loadsByLvl[M_L2] / d.iterations, d.loadsByLvl[M_L3] / d.iterations);
		printf("  ISTORE L1 %.3f L2 %.3f L3 %.3f per iteration\n", d.storesByLvl[M_L1] / d.iterations, d.storesByLvl[M_L2] / d.iterations, d.storesByLvl[M_L3] / d.iterations);
		printf("  main-loop AES rounds %.3f per iteration\n", d.aesRounds / d.iterations);
		printf("  loads with RAW on an earlier store (same program) %.4f per iteration, of which producer in same/prev iteration %.4f\n", d.rawMem / d.iterations, d.rawMemRecent / d.iterations);
	}
	auto t1 = std::chrono::steady_clock::now();
	printf("DONE hashes=%d mismatches=%d seconds=%.1f\n", o.hashes, mismatches, std::chrono::duration<double>(t1 - t0).count());
	return mismatches ? 2 : 0;
}
