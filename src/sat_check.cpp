#include "sat_check.h"
#include "match.h"     // for MatchResult definition
#include "sat_wrap.h"  // for SatSolver
#include <cstdlib>
#include <stdexcept>
#include <algorithm>

using namespace std;

namespace {

struct CNF {
    int numVars = 0;
    int nextVar = 1;
    vector<vector<int>> clauses;

    explicit CNF(int initialVars = 0) {
        numVars = initialVars;
        nextVar = initialVars + 1;
    }
    int newVar() {
        int v = nextVar++;
        numVars = max(numVars, v);
        return v;
    }
    void addClause(const vector<int>& c) { clauses.push_back(c); }
    void addClause(initializer_list<int> c) { clauses.emplace_back(c); }
};

inline void addEquiv(CNF& cnf, int litA, int litB) {
    // litA == litB
    cnf.addClause({-litA, litB});
    cnf.addClause({-litB, litA});
}

inline void addXorDef(CNF& cnf, int yVar, int litA, int litB) {
    // y = litA XOR litB , where litA/litB can be negated literals
    int aVar = abs(litA), bVar = abs(litB);
    int aNeg = (litA < 0) ? 1 : 0;
    int bNeg = (litB < 0) ? 1 : 0;
    int parity = aNeg ^ bNeg;

    if (parity == 0) {
        cnf.addClause({ aVar,  bVar, -yVar});
        cnf.addClause({-aVar, -bVar, -yVar});
        cnf.addClause({ aVar, -bVar,  yVar});
        cnf.addClause({-aVar,  bVar,  yVar});
    } else {
        cnf.addClause({ aVar,  bVar,  yVar});
        cnf.addClause({-aVar, -bVar,  yVar});
        cnf.addClause({ aVar, -bVar, -yVar});
        cnf.addClause({-aVar,  bVar, -yVar});
    }
}

inline void encodeGate(CNF& cnf, GateType tp, int outVar, const vector<int>& inLits) {
    auto die = [&](const string& msg){ throw runtime_error("SAT encode error: " + msg); };

    switch (tp) {
        case GateType::NOT: {
            if (inLits.size() != 1) die("NOT expects 1 input");
            int a = inLits[0];
            cnf.addClause({ outVar,  a});
            cnf.addClause({-outVar, -a});
            break;
        }
        case GateType::BUF: {
            if (inLits.size() != 1) die("BUF expects 1 input");
            int a = inLits[0];
            cnf.addClause({-outVar,  a});
            cnf.addClause({ outVar, -a});
            break;
        }
        case GateType::AND: {
            if (inLits.size() < 2) die("AND expects >=2 inputs");
            vector<int> c;
            c.reserve(inLits.size() + 1);
            for (int a : inLits) c.push_back(-a);
            c.push_back(outVar);
            cnf.addClause(c);
            for (int a : inLits) cnf.addClause({-outVar, a});
            break;
        }
        case GateType::OR: {
            if (inLits.size() < 2) die("OR expects >=2 inputs");
            vector<int> c;
            c.reserve(inLits.size() + 1);
            c.push_back(-outVar);
            for (int a : inLits) c.push_back(a);
            cnf.addClause(c);
            for (int a : inLits) cnf.addClause({-a, outVar});
            break;
        }
        case GateType::NAND: {
            if (inLits.size() < 2) die("NAND expects >=2 inputs");
            vector<int> c;
            c.reserve(inLits.size() + 1);
            c.push_back(-outVar);
            for (int a : inLits) c.push_back(-a);
            cnf.addClause(c);
            for (int a : inLits) cnf.addClause({a, outVar});
            break;
        }
        case GateType::NOR: {
            if (inLits.size() < 2) die("NOR expects >=2 inputs");
            for (int a : inLits) cnf.addClause({-outVar, -a});
            vector<int> c;
            c.reserve(inLits.size() + 1);
            for (int a : inLits) c.push_back(a);
            c.push_back(outVar);
            cnf.addClause(c);
            break;
        }
        case GateType::XOR: {
            if (inLits.size() != 2) die("XOR expects 2 inputs");
            addXorDef(cnf, outVar, inLits[0], inLits[1]);
            break;
        }
        case GateType::XNOR: {
            if (inLits.size() != 2) die("XNOR expects 2 inputs");
            addXorDef(cnf, outVar, inLits[0], -inLits[1]); // XNOR = XOR(a,~b)
            break;
        }
        default:
            die("unknown gate type");
    }
}

inline int netLit(const Circuit& c, int base, int netId, int constVar) {
    const Net& n = c.nets[netId];
    if (n.isConst) return (n.constVal == 1) ? constVar : -constVar;
    return base + netId;
}

} // namespace

SatCheckResult checkMatchBySAT(const Circuit& c1, const Circuit& c2, const MatchResult& guess) {
    SatCheckResult out;

    // var layout
    const int constVar = 1;
    const int base1 = 2;
    const int base2 = base1 + (int)c1.nets.size();
    const int initialVars = base2 + (int)c2.nets.size() - 1;

    CNF cnf(initialVars);
    cnf.addClause({constVar}); // constVar = True

    // Encode circuit 1
    for (const auto& g : c1.gates) {
        int outVar = base1 + g.outNet;
        vector<int> inLits;
        inLits.reserve(g.inNets.size());
        for (int nid : g.inNets) inLits.push_back(netLit(c1, base1, nid, constVar));
        encodeGate(cnf, g.type, outVar, inLits);
    }

    // Encode circuit 2
    for (const auto& g : c2.gates) {
        int outVar = base2 + g.outNet;
        vector<int> inLits;
        inLits.reserve(g.inNets.size());
        for (int nid : g.inNets) inLits.push_back(netLit(c2, base2, nid, constVar));
        encodeGate(cnf, g.type, outVar, inLits);
    }

    // Apply PI mapping constraints: (c1_pi == c2_pi)
    for (const auto& pr : guess.piPairs) {
        int lit1 = base1 + pr.first;
        int lit2 = base2 + pr.second;
        addEquiv(cnf, lit1, lit2);
    }

    // Miter over matched PO pairs: assert existence of mismatch
    vector<int> diffs;
    diffs.reserve(guess.poPairs.size());

    for (const auto& op : guess.poPairs) {
        int v1 = base1 + op.c1_po;
        int v2 = base2 + op.c2_po;
        int lit1 = op.c1Neg ? -v1 : v1;
        int lit2 = op.c2Neg ? -v2 : v2;

        int d = cnf.newVar();
        addXorDef(cnf, d, lit1, lit2);
        diffs.push_back(d);
    }

    // If no outputs to compare, treat as matched (nothing to disprove)
    if (diffs.empty()) {
        out.matched = true;
        return out;
    }

    // OR(diffs) = 1  (exists a mismatch)
    cnf.addClause(diffs);

    // Solve
    SatSolver solver;
    solver.ensureVars(cnf.numVars);
    for (auto& cl : cnf.clauses) solver.addClause(cl);

    SatResult r = solver.solve();
    if (!r.sat) {
        out.matched = true; // UNSAT => no counterexample
        return out;
    }

    out.matched = false;

    // Witness: c1 PI values
    out.c1PiWitness.reserve(c1.PIs.size());
    for (int nid : c1.PIs) {
        if (nid < 0 || nid >= (int)c1.nets.size()) continue;
        if (c1.nets[nid].isConst) continue;

        int var = base1 + nid;
        int val = 0;
        if (var >= 1 && var < (int)r.model.size()) val = (int)r.model[var];
        out.c1PiWitness.push_back({c1.nets[nid].name, val});
    }

    // Optional: find one mismatched PO pair
    for (size_t i = 0; i < diffs.size(); i++) {
        int dvar = diffs[i];
        if (dvar >= 1 && dvar < (int)r.model.size() && r.model[dvar] == 1) {
            out.badPoC1 = c1.nets[guess.poPairs[i].c1_po].name;
            out.badPoC2 = c2.nets[guess.poPairs[i].c2_po].name;
            break;
        }
    }

    return out;
}
