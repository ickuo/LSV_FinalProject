#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <stdexcept>

#include "parser.h"
#include "sat_wrap.h"

using namespace std;

// ---------------- CNF ----------------
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

static inline bool isConst1Name(const string& s) { return (s == "1'b1" || s == "1"); }
static inline bool isConst0Name(const string& s) { return (s == "1'b0" || s == "0"); }

// lit for net in copy with base; constLit is +/-1 for const nets, 0 for non-const
static inline int litNet(int base, int constVar, int constSign, int netId) {
    if (constSign == +1) return  constVar;
    if (constSign == -1) return -constVar;
    return base + netId;
}

static inline void addEquivVarVar(CNF& cnf, int a, int b) {
    cnf.addClause({-a, b});
    cnf.addClause({-b, a});
}

static void addXorDef(CNF& cnf, int yVar, int litA, int litB) {
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

static void encodeGate(CNF& cnf, GateType tp, int outVar, const vector<int>& inLits) {
    auto die = [&](const string& msg){ throw runtime_error("Gate encode error: " + msg); };

    switch (tp) {
        case GateType::NOT: {
            if (inLits.size() != 1) die("NOT expects 1 input");
            int a = inLits[0];
            cnf.addClause({ outVar, a});
            cnf.addClause({-outVar, -a});
            break;
        }
        case GateType::BUF: {
            if (inLits.size() != 1) die("BUF expects 1 input");
            int a = inLits[0];
            cnf.addClause({-outVar, a});
            cnf.addClause({ outVar, -a});
            break;
        }
        case GateType::AND: {
            if (inLits.size() < 2) die("AND expects >=2 inputs");
            vector<int> c;
            for (int a : inLits) c.push_back(-a);
            c.push_back(outVar);
            cnf.addClause(c);
            for (int a : inLits) cnf.addClause({-outVar, a});
            break;
        }
        case GateType::OR: {
            if (inLits.size() < 2) die("OR expects >=2 inputs");
            vector<int> c;
            c.push_back(-outVar);
            for (int a : inLits) c.push_back(a);
            cnf.addClause(c);
            for (int a : inLits) cnf.addClause({-a, outVar});
            break;
        }
        case GateType::NAND: {
            if (inLits.size() < 2) die("NAND expects >=2 inputs");
            vector<int> c;
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
            addXorDef(cnf, outVar, inLits[0], -inLits[1]);
            break;
        }
        default:
            die("unknown gate type");
    }
}

// ite constraint: v = (s ? a : p)
// clauses:
// (¬s ∨ ¬a ∨ v)
// (¬s ∨  a ∨ ¬v)
// ( s ∨ ¬p ∨ v)
// ( s ∨  p ∨ ¬v)
static inline void addIteEq(CNF& cnf, int v, int s, int a, int p) {
    cnf.addClause({-s, -a,  v});
    cnf.addClause({-s,  a, -v});
    cnf.addClause({ s, -p,  v});
    cnf.addClause({ s,  p, -v});
}

// Sinz sequential at-most-one encoding for selectors sVars
static void addAtMostOneSequential(CNF& cnf, const vector<int>& sVars) {
    int n = (int)sVars.size();
    if (n <= 1) return;

    vector<int> r(n - 1);
    for (int i = 0; i < n - 1; i++) r[i] = cnf.newVar();

    cnf.addClause({-sVars[0], r[0]});
    for (int i = 1; i <= n - 2; i++) {
        cnf.addClause({-sVars[i], r[i]});
        cnf.addClause({-r[i-1], r[i]});
        cnf.addClause({-sVars[i], -r[i-1]});
    }
    cnf.addClause({-sVars[n-1], -r[n-2]});
}

struct UnateSatPack {
    SatSolver solver;
    int constVar = 1;
    int base0 = 0, base1 = 0;
    vector<int> piNetIds;      // selected PI net ids (non-const)
    vector<string> piNames;
    vector<int> sVar, pVar, a0Var, a1Var;
    vector<int> netConstSign;  // per net: +1 const1, -1 const0, 0 normal
};

static void encodeCircuitCopy(CNF& cnf, const Circuit& c, int base, int constVar, const vector<int>& netConstSign) {
    for (const auto& g : c.gates) {
        int outVar = base + g.outNet;
        vector<int> inLits;
        inLits.reserve(g.inNets.size());
        for (int nid : g.inNets) {
            int lit = litNet(base, constVar, netConstSign[nid], nid);
            inLits.push_back(lit);
        }
        encodeGate(cnf, g.type, outVar, inLits);
    }
}

static UnateSatPack buildOneSolverAllPI(const Circuit& c, int maxPi) {
    UnateSatPack pack;
    const int nNets = (int)c.nets.size();

    pack.netConstSign.assign(nNets, 0);
    for (int nid = 0; nid < nNets; nid++) {
        const string& nm = c.nets[nid].name;
        if (isConst1Name(nm)) pack.netConstSign[nid] = +1;
        else if (isConst0Name(nm)) pack.netConstSign[nid] = -1;
    }

    // Choose PIs (exclude const nets)
    for (int pid : c.PIs) {
        if (pack.netConstSign[pid] != 0) continue;
        pack.piNetIds.push_back(pid);
        pack.piNames.push_back(c.nets[pid].name);
        if (maxPi >= 0 && (int)pack.piNetIds.size() >= maxPi) break;
    }

    // Layout variables:
    // constVar=1
    // copy0 net vars: base0..base0+nNets-1
    // copy1 net vars: base1..base1+nNets-1
    pack.base0 = 2;
    pack.base1 = pack.base0 + nNets;
    int initialVars = pack.base1 + nNets - 1;

    CNF cnf(initialVars);

    // constVar = True
    cnf.addClause({pack.constVar});

    // Encode gates for both copies
    encodeCircuitCopy(cnf, c, pack.base0, pack.constVar, pack.netConstSign);
    encodeCircuitCopy(cnf, c, pack.base1, pack.constVar, pack.netConstSign);

    // Allocate selector/mux vars for each PI
    int m = (int)pack.piNetIds.size();
    pack.sVar.resize(m);
    pack.pVar.resize(m);
    pack.a0Var.resize(m);
    pack.a1Var.resize(m);

    for (int i = 0; i < m; i++) {
        pack.sVar[i]  = cnf.newVar(); // selector: this PI is the "changed" one
        pack.pVar[i]  = cnf.newVar(); // shared value for both copies when not selected
        pack.a0Var[i] = cnf.newVar(); // value used in copy0 when selected
        pack.a1Var[i] = cnf.newVar(); // value used in copy1 when selected
    }

    // At-most-one on selectors (so only one PI can differ between copies)
    addAtMostOneSequential(cnf, pack.sVar);

    // Connect each PI net var in each copy to ITE(selector, a?, p)
    for (int i = 0; i < m; i++) {
        int pid = pack.piNetIds[i];
        int v0 = pack.base0 + pid;
        int v1 = pack.base1 + pid;

        // v0 = (s ? a0 : p)
        addIteEq(cnf, v0, pack.sVar[i], pack.a0Var[i], pack.pVar[i]);
        // v1 = (s ? a1 : p)
        addIteEq(cnf, v1, pack.sVar[i], pack.a1Var[i], pack.pVar[i]);
    }

    // Load into Minisat
    pack.solver.ensureVars(cnf.numVars);
    for (auto& cl : cnf.clauses) pack.solver.addClause(cl);

    return pack;
}

static void usage(const char* prog) {
    cerr << "Usage:\n"
         << "  " << prog << " <case_input> <circuit_id:1|2> <out.csv> [--max-pi N] [--max-po N] [--quiet]\n";
}

int main(int argc, char** argv) {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    if (argc < 4) {
        usage(argv[0]);
        return 1;
    }

    string caseInput = argv[1];
    int cirId = stoi(argv[2]);
    string outCsv = argv[3];

    int maxPi = -1;
    int maxPo = -1;
    bool quiet = false;

    for (int i = 4; i < argc; i++) {
        string a = argv[i];
        if (a == "--max-pi" && i + 1 < argc) maxPi = stoi(argv[++i]);
        else if (a == "--max-po" && i + 1 < argc) maxPo = stoi(argv[++i]);
        else if (a == "--quiet") quiet = true;
        else cerr << "[WARN] unknown arg: " << a << "\n";
    }

    try {
        ProblemInput in = parseProblemInputFile(caseInput);
        string vPath = (cirId == 1) ? in.c1File : in.c2File;
        Circuit c = parseVerilogNetlist(vPath);

        // Build ONE solver that can answer all PIs using assumptions
        UnateSatPack pack = buildOneSolverAllPI(c, maxPi);

        // Prepare PO list
        vector<int> POs = c.POs;
        if (maxPo >= 0 && (int)POs.size() > maxPo) POs.resize(maxPo);

        vector<string> poNames;
        poNames.reserve(POs.size());
        for (int yid : POs) poNames.push_back(c.nets[yid].name);

        ofstream fout(outCsv);
        if (!fout) throw runtime_error("Cannot open output: " + outCsv);
        fout << "po,pi,rel\n";

        vector<int> baseAssum;
        baseAssum.reserve(3);

        vector<int> ass;
        ass.reserve(5);

        int m = (int)pack.piNetIds.size();
        for (int i = 0; i < m; i++) {
            if (!quiet) cerr << "[PI " << (i+1) << "/" << m << "] " << pack.piNames[i] << "\n";

            // select this PI and force a0=0, a1=1
            baseAssum.clear();
            baseAssum.push_back( pack.sVar[i]);     // s_i = 1
            baseAssum.push_back(-pack.a0Var[i]);    // a0_i = 0
            baseAssum.push_back( pack.a1Var[i]);    // a1_i = 1

            for (int k = 0; k < (int)POs.size(); k++) {
                int yid = POs[k];

                int f0 = litNet(pack.base0, pack.constVar, pack.netConstSign[yid], yid);
                int f1 = litNet(pack.base1, pack.constVar, pack.netConstSign[yid], yid);

                // pos-viol: f0=1 & f1=0
                ass = baseAssum;
                ass.push_back( f0);
                ass.push_back(-f1);
                bool posViol = pack.solver.isSat(ass);

                // neg-viol: f0=0 & f1=1
                ass = baseAssum;
                ass.push_back(-f0);
                ass.push_back( f1);
                bool negViol = pack.solver.isSat(ass);

                char rel;
                if (!posViol && !negViol) rel = 'I';
                else if (!posViol &&  negViol) rel = 'P';
                else if ( posViol && !negViol) rel = 'N';
                else rel = 'B';

                fout << poNames[k] << "," << pack.piNames[i] << "," << rel << "\n";
            }
        }

        if (!quiet) cerr << "[OK] wrote " << outCsv << "\n";
    } catch (const exception& e) {
        cerr << "[ERROR] " << e.what() << "\n";
        return 2;
    }

    return 0;
}
