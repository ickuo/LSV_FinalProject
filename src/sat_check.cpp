#include "sat_check.h"

#include "parser.h"
#include "sat_wrap.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

static inline int netVar(int base, int netId) {
    // DIMACS vars are 1-based.
    return base + netId + 1;
}

static inline void addEq(SatSolver& s, int a, int b) {
    // a <-> b
    s.addClause({-a, b});
    s.addClause({a, -b});
}

static inline void addEqNeg(SatSolver& s, int a, int b) {
    // a <-> ~b
    // (a -> ~b): (-a v -b)
    // (~b -> a): (b v a)
    s.addClause({-a, -b});
    s.addClause({a, b});
}

static inline void addNot(SatSolver& s, int out, int a) {
    // out = ~a
    s.addClause({out, a});
    s.addClause({-out, -a});
}

static inline void addAnd(SatSolver& s, int out, const std::vector<int>& ins) {
    // out <-> AND(ins)
    // (out -> each in): (-out v in_i)
    for (int a : ins) s.addClause({-out, a});
    // (all in -> out): (out v -in1 v -in2 ...)
    std::vector<int> c;
    c.reserve(ins.size() + 1);
    c.push_back(out);
    for (int a : ins) c.push_back(-a);
    s.addClause(c);
}

static inline void addOr(SatSolver& s, int out, const std::vector<int>& ins) {
    // out <-> OR(ins)
    // (out -> OR): (out v -in_i) for each i
    for (int a : ins) s.addClause({out, -a});
    // (OR -> out): (-out v in1 v in2 ...)
    std::vector<int> c;
    c.reserve(ins.size() + 1);
    c.push_back(-out);
    for (int a : ins) c.push_back(a);
    s.addClause(c);
}

static inline void addNand(SatSolver& s, int out, const std::vector<int>& ins) {
    // out <-> ~(AND(ins))
    // out -> (exists i: in_i=0): (-out v -in1 v -in2 ...)
    std::vector<int> c;
    c.reserve(ins.size() + 1);
    c.push_back(-out);
    for (int a : ins) c.push_back(-a);
    s.addClause(c);

    // ~out -> (all in=1): (out v in_i) for each i
    for (int a : ins) s.addClause({out, a});
}

static inline void addNor(SatSolver& s, int out, const std::vector<int>& ins) {
    // out <-> ~(OR(ins))
    // out -> (all in=0): (-out v -in_i) for each i
    for (int a : ins) s.addClause({-out, -a});

    // ~out -> (exists i: in_i=1): (out v in1 v in2 ...)
    std::vector<int> c;
    c.reserve(ins.size() + 1);
    c.push_back(out);
    for (int a : ins) c.push_back(a);
    s.addClause(c);
}

static inline void addXor2(SatSolver& s, int out, int a, int b) {
    // out = a xor b
    // (a v b v ~out) & (~a v ~b v ~out) & (a v ~b v out) & (~a v b v out)
    s.addClause({a, b, -out});
    s.addClause({-a, -b, -out});
    s.addClause({a, -b, out});
    s.addClause({-a, b, out});
}

static inline void addXnor2(SatSolver& s, int out, int a, int b) {
    // out = ~(a xor b)
    s.addClause({a, b, out});
    s.addClause({-a, -b, out});
    s.addClause({a, -b, -out});
    s.addClause({-a, b, -out});
}

static void addGateCNF(SatSolver& s, GateType t, int out, const std::vector<int>& ins) {
    switch (t) {
    case GateType::BUF:
        assert(ins.size() == 1);
        addEq(s, out, ins[0]);
        return;
    case GateType::NOT:
        assert(ins.size() == 1);
        addNot(s, out, ins[0]);
        return;
    case GateType::AND:
        addAnd(s, out, ins);
        return;
    case GateType::OR:
        addOr(s, out, ins);
        return;
    case GateType::NAND:
        addNand(s, out, ins);
        return;
    case GateType::NOR:
        addNor(s, out, ins);
        return;
    case GateType::XOR:
        assert(ins.size() == 2);
        addXor2(s, out, ins[0], ins[1]);
        return;
    case GateType::XNOR:
        assert(ins.size() == 2);
        addXnor2(s, out, ins[0], ins[1]);
        return;
    default:
        // Unknown gate types should not happen with our parser.
        // If it happens, we do NOT add constraints (may cause false SAT).
        std::cerr << "[SAT] Warning: unknown gate type encountered; skipping CNF for this gate.\n";
        return;
    }
}

} // namespace

SatCheckResult checkMatchBySAT(const Circuit& c1, const Circuit& c2, const MatchResult& m) {
    SatCheckResult out;

    const int n1 = (int)c1.nets.size();
    const int n2 = (int)c2.nets.size();
    const int base1 = 0;
    const int base2 = n1;

    int numVars = n1 + n2;

    SatSolver solver;
    solver.ensureVars(numVars);

    // ---- Encode constants
    for (const auto& net : c1.nets) {
        if (net.isConst) {
            int v = netVar(base1, net.id);
            solver.addClause({ net.constVal ? v : -v });
        }
    }
    for (const auto& net : c2.nets) {
        if (net.isConst) {
            int v = netVar(base2, net.id);
            solver.addClause({ net.constVal ? v : -v });
        }
    }

    // ---- Encode gates
    for (const auto& g : c1.gates) {
        int o = netVar(base1, g.outNet);
        std::vector<int> ins;
        ins.reserve(g.inNets.size());
        for (int inNet : g.inNets) ins.push_back(netVar(base1, inNet));
        addGateCNF(solver, g.type, o, ins);
    }
    for (const auto& g : c2.gates) {
        int o = netVar(base2, g.outNet);
        std::vector<int> ins;
        ins.reserve(g.inNets.size());
        for (int inNet : g.inNets) ins.push_back(netVar(base2, inNet));
        addGateCNF(solver, g.type, o, ins);
    }

    // ---- Tie matched PIs (allow optional inversion on c2 side)
    for (const auto& pr : m.piPairs) {
        int v1 = netVar(base1, pr.c1_pi);
        int v2 = netVar(base2, pr.c2_pi);
        if (!pr.c2Neg) addEq(solver, v1, v2);
        else           addEqNeg(solver, v1, v2);
    }

    // ---- Miter on matched POs
    std::vector<int> diffVars;
    diffVars.reserve(m.poPairs.size());

    for (const auto& pr : m.poPairs) {
        int o1 = netVar(base1, pr.c1_po);
        int o2 = netVar(base2, pr.c2_po);

        int d = ++numVars;
        solver.ensureVars(numVars);

        // If pr.c2Neg==0: d = XOR(o1,o2)
        // If pr.c2Neg==1: compare o1 vs ~o2 => d = XOR(o1,~o2) = XNOR(o1,o2)
        if (!pr.c2Neg) addXor2(solver, d, o1, o2);
        else           addXnor2(solver, d, o1, o2);

        diffVars.push_back(d);
    }

    if (!diffVars.empty()) {
        solver.addClause(diffVars); // at least one diff must be true
    } else {
        // Degenerate: no POs (shouldn't happen). treat as equivalent.
        out.success = true;
        out.msg = "No POs in mapping; treat as equivalent.";
        return out;
    }

    SatResult r = solver.solve();
    if (!r.sat) {
        out.success = true;
        out.msg = "UNSAT miter: equivalent.";
        return out;
    }

    // SAT => counterexample
    out.success = false;
    out.msg = "SAT miter: found counterexample.";

    // Record a bad PO pair
    int badIdx = -1;
    for (int i = 0; i < (int)diffVars.size(); i++) {
        int dv = diffVars[i];
        if (dv >= 1 && dv < (int)r.model.size() && r.model[dv]) { badIdx = i; break; }
    }
    if (badIdx < 0) badIdx = 0;

    if (badIdx >= 0 && badIdx < (int)m.poPairs.size()) {
        const auto& pr = m.poPairs[badIdx];
        out.badPoC1 = c1.nets[pr.c1_po].name;
        out.badPoC2 = c2.nets[pr.c2_po].name;
    }

    // Record PI assignment on circuit1 PIs
    out.c1PiWitness.clear();
    out.c1PiWitness.reserve(c1.PIs.size() * 2);
    for (int piNet : c1.PIs) {
        int v = netVar(base1, piNet);
        int val = (v >= 1 && v < (int)r.model.size() && r.model[v]) ? 1 : 0;
        out.c1PiWitness[c1.nets[piNet].name] = val;
    }

    return out;
}
