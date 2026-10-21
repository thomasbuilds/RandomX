// Agent X: analyses on the uop trace.
//  - LatModel: per-class latencies (design.md App. B "all ops 1 cycle", modern CPU, optimistic ASIC)
//  - dataflow(): unbounded resources, perfect memory disambiguation, branch mispredict = redirect penalty;
//    gives the loop-carried recurrence, critical paths and available ILP
//  - ooo(): finite out-of-order window (ROB slots), dispatch/retire width, port counts, non-pipelined
//    divider, load/store ports, Dataset prefetch arrival, branch mispredict stalls, LDMXCSR modes; 1 or 2 threads
#pragma once
#include "trace.hpp"
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <deque>

namespace ax {

struct LatModel {
	const char* name;
	int lat[C_NCLASS];
	int lvl[7];        // load latency by MemLvl (M_NONE unused); M_DS = latency after the line has arrived
	int fpLoadExtra;   // extra latency of C_LOADF (FP-domain load + cvtdq2pd)
	int fwd;           // store-to-load forwarding latency
	int misp;          // branch mispredict redirect penalty (cycles after the branch resolves)
};

// (a) design.md Appendix B: every operation 1 cycle; memory stage 1 cycle including address calculation
static LatModel latAppB() {
	LatModel m; m.name = "appB";
	for (int i = 0; i < C_NCLASS; ++i) m.lat[i] = 1;
	m.lat[C_AG1] = 0; m.lat[C_AG2] = 0; m.lat[C_PREF] = 0; m.lat[C_STORE] = 1;
	for (int i = 0; i < 7; ++i) m.lvl[i] = 1;
	m.fpLoadExtra = 0; m.fwd = 1; m.misp = 2;  // 3-stage pipeline: flush costs pipeline-1 = 2 cycles
	return m;
}

// (b) modern x86 core (Zen 4 class; Agner Fog instruction tables / uops.info values, see report)
static LatModel latZen4(int l3 = 47) {
	LatModel m; m.name = "zen4";
	m.lat[C_ALU] = 1; m.lat[C_LEA3] = 2; m.lat[C_AG1] = 1; m.lat[C_AG2] = 1;
	m.lat[C_IMUL] = 3; m.lat[C_IMULH] = 4;
	m.lat[C_LOADI] = 0; m.lat[C_LOADF] = 0;  // load latency comes from lvl[]
	m.lat[C_STORE] = 1;
	m.lat[C_FADD] = 3; m.lat[C_FMUL] = 3; m.lat[C_FDIV] = 13; m.lat[C_FSQRT] = 20;
	m.lat[C_FLOG] = 1; m.lat[C_AES] = 4; m.lat[C_BR] = 1; m.lat[C_MXCSR] = 10; m.lat[C_PREF] = 1;
	m.lvl[M_NONE] = 0; m.lvl[M_L1] = 4; m.lvl[M_L2] = 14; m.lvl[M_L3] = l3; m.lvl[M_BLK] = l3; m.lvl[M_DS] = 4; m.lvl[M_STACK] = 4;
	m.fpLoadExtra = 6;   // FP load-to-use 7 cycles (vs 4 integer) + cvtdq2pd 3
	m.fwd = 7; m.misp = 13;
	return m;
}

// (c) optimistic specialised design: 1-cycle ALU, 2-cycle multiplier, pipelined FP, 1-2 cycle SRAM
static LatModel latAsic(int fpAdd = 2, int fpDiv = 6, int sram3 = 2) {
	LatModel m; m.name = "asic";
	m.lat[C_ALU] = 1; m.lat[C_LEA3] = 1; m.lat[C_AG1] = 1; m.lat[C_AG2] = 0;
	m.lat[C_IMUL] = 2; m.lat[C_IMULH] = 2;
	m.lat[C_LOADI] = 0; m.lat[C_LOADF] = 0;
	m.lat[C_STORE] = 1;
	m.lat[C_FADD] = fpAdd; m.lat[C_FMUL] = fpAdd; m.lat[C_FDIV] = fpDiv; m.lat[C_FSQRT] = fpDiv;
	m.lat[C_FLOG] = 0; m.lat[C_AES] = 1; m.lat[C_BR] = 0; m.lat[C_MXCSR] = 1; m.lat[C_PREF] = 0;
	m.lvl[M_NONE] = 0; m.lvl[M_L1] = 1; m.lvl[M_L2] = 1; m.lvl[M_L3] = sram3; m.lvl[M_BLK] = sram3; m.lvl[M_DS] = 1; m.lvl[M_STACK] = 0;
	m.fpLoadExtra = 0; m.fwd = 1; m.misp = 2;
	return m;
}

static inline int uopLat(const LatModel& m, const Uop& u) {
	int l = m.lat[u.cls];
	if (u.cls == C_LOADI) l += m.lvl[u.lvl];
	else if (u.cls == C_LOADF) l += m.lvl[u.lvl] + m.fpLoadExtra;
	return l;
}

// ------------------------------------------------------------------ dataflow (unbounded resources)
struct DFResult {
	double total = 0;          // completion time of the whole program (cycles)
	double rEndLast = 0;       // r_end of the last iteration
	double meanRec = 0;        // mean increment of r_end per iteration (loop-carried recurrence)
	double meanFpLag = 0;      // mean (F-store completion - r_end) per iteration
	double maxFpLag = 0;
	double uops = 0;
	double isoCP = 0;          // mean isolated per-iteration critical path (inputs ready at t=0)
	double isoRec = 0;         // mean isolated start->r_end path
	double isoFp = 0;          // mean isolated start->F-store path
	double isoUops = 0;        // mean uops per iteration
	double dsStall = 0;        // mean cycles per iteration the r_end waits for the Dataset line
	std::vector<double> perIterRec;
};

struct DFOptions {
	double Lmem = 0;           // Dataset latency in cycles (from prefetch issue)
	bool speculate = true;     // false: every branch (CBRANCH/CFROUND gate) stalls younger uops until resolved
	uint32_t zeroMask = 0;     // set the latency of these classes (bit per UClass) to zero (is it on the critical path?)
	bool keepPerIter = false;
	bool rndSerialize = false; // LDMXCSR waits for all older rounding FP ops (non-renamed rounding mode)
};

static DFResult dataflow(const ProgramTrace& tr, const LatModel& m0, const DFOptions& o) {
	LatModel m = m0;
	for (int c = 0; c < C_NCLASS; ++c) if (o.zeroMask & (1u << c)) { m.lat[c] = 0; }
	if (o.zeroMask & (1u << C_LOADF)) m.fpLoadExtra = 0;
	const size_t n = tr.u.size();
	static std::vector<double> fin, iso;
	fin.resize(n); iso.resize(n);
	DFResult r;
	double ctrl = 0;         // younger uops cannot start before this (mispredict redirect)
	double lastRnd = 0;      // completion of the latest rounding FP op (for rndSerialize)
	double prevREnd = 0;
	size_t it = 0, itStart = 0;
	double rEnd = 0, fStore = 0, isoMax = 0, isoREnd = 0, isoF = 0, dsWait = 0;
	double sumRec = 0, sumLag = 0, sumIsoCP = 0, sumIsoRec = 0, sumIsoF = 0, sumDs = 0;
	if (o.keepPerIter) r.perIterRec.reserve(tr.iterations);
	for (size_t i = 0; i < n; ++i) {
		const Uop& u = tr.u[i];
		double t = ctrl, ti = 0;
		for (int k = 0; k < 3; ++k) {
			int32_t d = u.d[k];
			if (d >= 0) {
				t = std::max(t, fin[d]);
				if ((size_t)d >= itStart) ti = std::max(ti, iso[d]);
			}
		}
		int L = uopLat(m, u);
		double fstart = t;
		if (u.md >= 0) {
			// true RAW through memory: data available fwd cycles after the store executes
			t = std::max(t, fin[u.md] - 1 + m.fwd - L);  // so that fin >= store + fwd
			if ((size_t)u.md >= itStart) ti = std::max(ti, iso[u.md] - 1 + m.fwd - L);
		}
		if (u.cls == C_MXCSR && o.rndSerialize) t = std::max(t, lastRnd);
		double f = t + L;
		if (u.lvl == M_DS && u.cls == C_LOADI) {
			// line arrives Lmem after the prefetch issued (prefetch of the first iterations: program start)
			double arrive = (u.aux >= 0 ? fin[u.aux] : 0) + o.Lmem;
			double f2 = std::max(f, arrive + m.lvl[M_DS]);
			dsWait = std::max(dsWait, f2 - f);
			f = f2;
		}
		(void)fstart;
		fin[i] = f;
		iso[i] = ti + L;
		if (u.flags & F_RND) lastRnd = std::max(lastRnd, f);
		if (u.cls == C_BR && ((u.flags & F_MISP) || (!o.speculate && u.vm != P_LOOP)))
			ctrl = std::max(ctrl, f + ((u.flags & F_MISP) ? m.misp : 0));
		isoMax = std::max(isoMax, iso[i]);
		if (u.vm == P_DSXOR) { rEnd = std::max(rEnd, f); isoREnd = std::max(isoREnd, iso[i]); }
		if (u.flags & F_FSTORE) { fStore = std::max(fStore, f); isoF = std::max(isoF, iso[i]); }
		if ((int32_t)i == tr.iterEnd[it]) {
			double rec = rEnd - prevREnd;
			sumRec += rec;
			if (o.keepPerIter) r.perIterRec.push_back(rec);
			sumLag += fStore - rEnd;
			r.maxFpLag = std::max(r.maxFpLag, fStore - rEnd);
			sumIsoCP += isoMax; sumIsoRec += isoREnd; sumIsoF += isoF; sumDs += dsWait;
			prevREnd = rEnd;
			rEnd = 0; fStore = 0; isoMax = 0; isoREnd = 0; isoF = 0; dsWait = 0;
			++it; itStart = i + 1;
		}
		r.total = std::max(r.total, f);
	}
	double I = tr.iterations;
	r.rEndLast = prevREnd;
	r.meanRec = sumRec / I; r.meanFpLag = sumLag / I; r.uops = n;
	r.isoCP = sumIsoCP / I; r.isoRec = sumIsoRec / I; r.isoFp = sumIsoF / I; r.isoUops = n / I;
	r.dsStall = sumDs / I;
	return r;
}

// ------------------------------------------------------------------ out-of-order core model
struct CpuModel {
	const char* name;
	int rob;          // ROB capacity in slots (per thread when smt)
	int dispatch;     // slots per cycle (shared between threads)
	int retire;       // slots per cycle per thread
	int nALU, nIMUL, nLOAD, nSTORE, nFADD, nFMUL, nFDIV, nFANY, nAES, nBR;
	int occFDIV, occFSQRT;
	bool aesOnFMUL;   // AES rounds use the FMUL pipes
	int mxcsrMode;    // 0: renamed (data dependency only); 1: LDMXCSR waits for older rounding ops and blocks dispatch until done; 2: full serialization (drain); 3: blocks dispatch for lat[C_MXCSR] cycles, no wait for older ops
	LatModel lat;
	double ghz;
};

static CpuModel cpuZen4() {
	CpuModel c; c.name = "zen4"; c.rob = 320; c.dispatch = 6; c.retire = 8;
	c.nALU = 4; c.nIMUL = 1; c.nLOAD = 3; c.nSTORE = 2; c.nFADD = 2; c.nFMUL = 2; c.nFDIV = 1; c.nFANY = 4; c.nAES = 2; c.nBR = 2;
	c.occFDIV = 5; c.occFSQRT = 9; c.aesOnFMUL = true; c.mxcsrMode = 1; c.lat = latZen4(47); c.ghz = 5.0;
	return c;
}
static CpuModel cpuZen5() {
	CpuModel c = cpuZen4(); c.name = "zen5"; c.rob = 448; c.dispatch = 8; c.retire = 8;
	c.nALU = 6; c.nIMUL = 3; c.nLOAD = 4; c.nSTORE = 2; c.nFADD = 2; c.nFMUL = 2; c.nFANY = 6; c.nBR = 3;
	c.lat = latZen4(55); c.lat.name = "zen5"; c.ghz = 5.2;
	return c;
}
static CpuModel cpuZen2() {
	CpuModel c = cpuZen4(); c.name = "zen2"; c.rob = 224; c.dispatch = 6; c.retire = 8;
	c.nALU = 4; c.nIMUL = 1; c.nLOAD = 2; c.nSTORE = 1; c.nFADD = 2; c.nFMUL = 2; c.nFANY = 4; c.nBR = 2;
	c.lat = latZen4(40); c.lat.name = "zen2"; c.lat.lvl[M_L2] = 12; c.lat.lat[C_FDIV] = 13; c.lat.lat[C_FSQRT] = 20;
	c.ghz = 4.2;
	return c;
}
static CpuModel cpuSkylake() {
	CpuModel c; c.name = "skx"; c.rob = 224; c.dispatch = 4; c.retire = 4;
	c.nALU = 4; c.nIMUL = 1; c.nLOAD = 2; c.nSTORE = 1; c.nFADD = 2; c.nFMUL = 2; c.nFDIV = 1; c.nFANY = 3; c.nAES = 1; c.nBR = 2;
	c.occFDIV = 4; c.occFSQRT = 6; c.aesOnFMUL = true; c.mxcsrMode = 1;
	c.lat = latZen4(70); c.lat.name = "skx";
	c.lat.lvl[M_L1] = 5; c.lat.lat[C_FADD] = 4; c.lat.lat[C_FMUL] = 4; c.lat.lat[C_FDIV] = 13; c.lat.lat[C_FSQRT] = 18;
	c.lat.lat[C_IMULH] = 4; c.lat.fpLoadExtra = 5; c.lat.lat[C_MXCSR] = 7; c.lat.misp = 16; c.lat.fwd = 5;
	c.ghz = 2.8;
	return c;
}
static CpuModel cpuGoldenCove() {
	CpuModel c = cpuSkylake(); c.name = "gcove"; c.rob = 512; c.dispatch = 6; c.retire = 8;
	c.nALU = 5; c.nLOAD = 3; c.nSTORE = 2; c.nFANY = 3; c.nAES = 2;
	c.lat.lvl[M_L2] = 15; c.lat.lvl[M_L3] = 65; c.lat.lvl[M_BLK] = 65; c.lat.lat[C_FADD] = 3; c.lat.misp = 17;
	c.ghz = 4.9;
	return c;
}

// resource calendar: capacity per cycle, ring buffer indexed by cycle
struct Calendar {
	static const int64_t SZ = 1 << 18;
	std::vector<int64_t> tag; std::vector<uint16_t> cnt; int cap;
	explicit Calendar(int c = 1) : tag(SZ, -1), cnt(SZ, 0), cap(c) {}
	void reset(int c) { std::fill(tag.begin(), tag.end(), -1); std::fill(cnt.begin(), cnt.end(), 0); cap = c; }
	inline int used(int64_t t) { size_t k = t & (SZ - 1); return tag[k] == t ? cnt[k] : 0; }
	inline void take(int64_t t) { size_t k = t & (SZ - 1); if (tag[k] != t) { tag[k] = t; cnt[k] = 0; } cnt[k]++; }
	// first cycle >= t with occ consecutive free units (occ>1 only used with cap units shared: approximate by unit 0..cap-1 count)
	int64_t reserve(int64_t t, int occ = 1) {
		for (;; ++t) {
			bool ok = true;
			for (int k = 0; k < occ; ++k) if (used(t + k) >= cap) { ok = false; break; }
			if (ok) { for (int k = 0; k < occ; ++k) take(t + k); return t; }
		}
	}
};

struct OooResult {
	double cycles = 0;          // total cycles for the program (last retire)
	double perIter = 0;
	double robFullCycles = 0;   // dispatch delayed by ROB capacity (sum of delays)
	double dsWait = 0;          // cycles uops waited for Dataset line arrival beyond their ready time (sum)
	std::vector<double> iterRetire;
};

struct ThreadCtx {
	const ProgramTrace* tr;
	size_t next = 0;
	std::vector<int64_t> fin, iss, ret;
	std::vector<int64_t> cum;  // cumulative slots up to and including uop i
	size_t robHead = 0;        // oldest uop still in the ROB
	int64_t ctrl = 0;          // dispatch blocked until
	int64_t lastRetire = 0;
	int64_t retCycle = 0; int retUsed = 0;
	int64_t lastRnd = 0;       // latest completion of a rounding FP op
	int64_t lastDisp = 0;
	OooResult res;
	size_t it = 0;
	// earliest dispatch time of the next uop given ROB capacity (advances robHead)
	int64_t robConstraint(int rob, int64_t e) {
		const Uop& u = tr->u[next];
		int64_t prevCum = next ? cum[next - 1] : 0;
		while (robHead < next) {
			int64_t headPrev = robHead ? cum[robHead - 1] : 0;
			if (prevCum - headPrev + u.slots <= rob) break;
			e = std::max(e, ret[robHead]);
			++robHead;
		}
		return e;
	}
};

// Simulate 1 or 2 threads (SMT) on one core. Each thread runs its own trace.
static void ooo(std::vector<ThreadCtx>& th, const CpuModel& c, double LmemCycles) {
	const LatModel& m = c.lat;
	static Calendar alu, imul, ld, stp, fadd, fmul, fdiv, fany, aes, br;
	alu.reset(c.nALU); imul.reset(c.nIMUL); ld.reset(c.nLOAD); stp.reset(c.nSTORE); fadd.reset(c.nFADD); fmul.reset(c.nFMUL);
	fdiv.reset(c.nFDIV); fany.reset(c.nFANY); aes.reset(c.nAES); br.reset(c.nBR);
	for (auto& t : th) {
		size_t n = t.tr->u.size();
		t.next = 0; t.fin.assign(n, 0); t.iss.assign(n, 0); t.ret.assign(n, 0); t.cum.assign(n, 0);
		int64_t cs = 0; for (size_t i = 0; i < n; ++i) { cs += t.tr->u[i].slots; t.cum[i] = cs; }
		t.robHead = 0; t.ctrl = 0; t.lastRetire = 0; t.retCycle = 0; t.retUsed = 0; t.lastRnd = 0; t.lastDisp = 0;
		t.res = OooResult(); t.it = 0; t.res.iterRetire.reserve(t.tr->iterations);
	}
	const int nth = (int)th.size();
	const int robPer = c.rob;
	int64_t dispCycle = 0; int dispUsed = 0;
	for (;;) {
		int sel = -1; int64_t best = INT64_MAX;
		for (int k = 0; k < nth; ++k) {
			ThreadCtx& t = th[k];
			if (t.next >= t.tr->u.size()) continue;
			int64_t e = t.robConstraint(robPer, std::max(t.ctrl, t.lastDisp));
			if (e < best) { best = e; sel = k; }
		}
		if (sel < 0) break;
		ThreadCtx& t = th[sel];
		const size_t i = t.next;
		const Uop& u = t.tr->u[i];
		int64_t base = std::max(t.ctrl, t.lastDisp);
		int64_t dt = best;
		if (dt > base) t.res.robFullCycles += (double)(dt - base);
		t.next++;
		// shared dispatch bandwidth
		if (u.slots > 0) {
			if (dt > dispCycle) { dispCycle = dt; dispUsed = 0; }
			if (dt < dispCycle) dt = dispCycle;
			if (dispUsed + u.slots > c.dispatch) { dispCycle++; dispUsed = 0; dt = dispCycle; }
			dispUsed += u.slots;
		}
		t.lastDisp = dt;
		// operands
		int64_t rdy = dt + 1;
		for (int k = 0; k < 3; ++k) if (u.d[k] >= 0) rdy = std::max(rdy, t.fin[u.d[k]]);
		int L = uopLat(m, u);
		if (u.md >= 0) rdy = std::max(rdy, t.fin[u.md] - 1 + m.fwd - L);
		if (u.cls == C_MXCSR && (c.mxcsrMode == 1 || c.mxcsrMode == 2)) rdy = std::max(rdy, t.lastRnd);
		if (u.cls == C_MXCSR && c.mxcsrMode == 2) rdy = std::max(rdy, t.lastRetire);
		int64_t is;
		switch (u.cls) {
		case C_ALU: case C_LEA3: case C_AG1: case C_AG2: is = alu.reserve(rdy); break;
		case C_IMUL: case C_IMULH: is = imul.reserve(rdy); break;
		case C_LOADI: case C_LOADF: case C_PREF: is = ld.reserve(rdy); break;
		case C_STORE: is = stp.reserve(rdy); break;
		case C_FADD: is = fadd.reserve(rdy); break;
		case C_FMUL: is = fmul.reserve(rdy); break;
		case C_FDIV: is = fdiv.reserve(rdy, c.occFDIV); break;
		case C_FSQRT: is = fdiv.reserve(rdy, c.occFSQRT); break;
		case C_FLOG: is = fany.reserve(rdy); break;
		case C_AES: is = c.aesOnFMUL ? fmul.reserve(rdy) : aes.reserve(rdy); break;
		case C_BR: is = br.reserve(rdy); break;
		default: is = rdy; break;
		}
		int64_t f = is + L;
		if (u.lvl == M_DS && u.cls == C_LOADI) {
			int64_t arrive = (u.aux >= 0 ? t.iss[u.aux] : 0) + (int64_t)LmemCycles;
			int64_t f2 = std::max(f, arrive + (int64_t)m.lvl[M_DS]);
			t.res.dsWait += (double)(f2 - f);
			f = f2;
		}
		if (u.lvl == M_BLK && (u.cls == C_LOADI || u.cls == C_LOADF) && u.aux >= 0) {
			// scratchpad line prefetched (prefetcht0) at the end of the previous iteration
			int extra = (u.cls == C_LOADF ? m.fpLoadExtra : 0);
			int64_t arrive = t.iss[u.aux] + m.lvl[M_BLK];
			f = std::min(f, std::max(is + (int64_t)m.lvl[M_L1] + extra, arrive + extra));
		}
		t.iss[i] = is; t.fin[i] = f;
		if (u.flags & F_RND) t.lastRnd = std::max(t.lastRnd, f);
		int64_t rt = std::max(f, t.lastRetire);
		if (u.slots > 0) {
			if (rt > t.retCycle) { t.retCycle = rt; t.retUsed = 0; }
			if (t.retUsed + u.slots > c.retire) { t.retCycle++; t.retUsed = 0; }
			rt = std::max(rt, t.retCycle);
			t.retUsed += u.slots;
		}
		t.lastRetire = rt; t.ret[i] = rt;
		if (u.cls == C_BR && (u.flags & F_MISP)) t.ctrl = std::max(t.ctrl, f + m.misp);
		if (u.cls == C_MXCSR && (c.mxcsrMode == 1 || c.mxcsrMode == 2)) t.ctrl = std::max(t.ctrl, f);
		if (u.cls == C_MXCSR && c.mxcsrMode == 3) t.ctrl = std::max(t.ctrl, dt + (int64_t)m.lat[C_MXCSR]);
		if ((int32_t)i == t.tr->iterEnd[t.it]) { t.res.iterRetire.push_back((double)rt); ++t.it; }
	}
	for (auto& t : th) {
		t.res.cycles = (double)t.lastRetire;
		t.res.perIter = t.res.cycles / t.tr->iterations;
	}
}

} // namespace ax
