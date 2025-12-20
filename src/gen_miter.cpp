#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <filesystem>

#include <filesystem>
using namespace std;

// 你現有的 parser (必須提供這兩個函式 + Circuit/GateType/Gate/Net/ProblemInput 定義)
#include "parser.h"

// ------------------------------------------------------------
// Small utils
// ------------------------------------------------------------
static inline string trim(string s) {
    auto notSpace = [](int ch){ return !isspace(ch); };
    s.erase(s.begin(), find_if(s.begin(), s.end(), notSpace));
    s.erase(find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

static inline vector<string> split_ws(const string& s) {
    vector<string> t;
    string cur;
    for (char c : s) {
        if (isspace((unsigned char)c)) {
            if (!cur.empty()) { t.push_back(cur); cur.clear(); }
        } else cur.push_back(c);
    }
    if (!cur.empty()) t.push_back(cur);
    return t;
}

static inline string strip_comment(string s) {
    // allow "# ..." or "// ..."
    auto p1 = s.find('#');
    auto p2 = s.find("//");
    size_t p = string::npos;
    if (p1 != string::npos) p = p1;
    if (p2 != string::npos) p = min(p, p2);
    if (p != string::npos) s = s.substr(0, p);
    return s;
}

// ------------------------------------------------------------
// Match file parsing (your output.match style + also supports END keyword)
// Group rule:
//   - INGROUP/OUTGROUP: each group has exactly one Circuit I port ("1 ...")
//     and one or more Circuit II ports ("2 ...").
//   - Groups can be terminated by "END" (spec) OR inferred when a new "1 ..." starts.
//   - CONSTGROUP: lines are "+ name" or "- name" (optionally may have leading "2").
//     '+' means bind to 0, '-' means bind to 1.
// ------------------------------------------------------------
struct PortLit {
    int cir = 0;        // 1 or 2
    bool neg = false;   // '+' => false, '-' => true
    string name;
};

struct MatchGroup {
    bool hasC1 = false;
    PortLit c1;
    vector<PortLit> c2s;
};

struct MatchFile {
    vector<MatchGroup> ingroups;
    vector<MatchGroup> outgroups;
    // name -> value (0/1)
    vector<pair<string,int>> consts;
};

static MatchFile parseMatchFile(const string& path) {
    ifstream fin(path);
    if (!fin) throw runtime_error("Cannot open match file: " + path);

    enum Sec { NONE, IN, OUT, CST } sec = NONE;

    MatchFile mf;
    MatchGroup cur;

    auto flushCur = [&](){
        if (!cur.hasC1 && cur.c2s.empty()) return;
        if (!cur.hasC1) throw runtime_error("Bad match group: missing Circuit-I (1 ...) line");
        if (cur.c2s.empty()) throw runtime_error("Bad match group: missing Circuit-II (2 ...) lines");
        if (sec == IN) mf.ingroups.push_back(cur);
        else if (sec == OUT) mf.outgroups.push_back(cur);
        cur = MatchGroup{};
    };

    string line;
    while (getline(fin, line)) {
        line = strip_comment(line);
        line = trim(line);
        if (line.empty()) continue;

        if (line == "INGROUP") { flushCur(); sec = IN; continue; }
        if (line == "OUTGROUP") { flushCur(); sec = OUT; continue; }
        if (line == "CONSTGROUP") { flushCur(); sec = CST; continue; }
        if (line == "END") { flushCur(); continue; }

        auto tk = split_ws(line);
        if (sec == CST) {
            // format: [+|-] name   OR   2 [+|-] name
            if (tk.size() == 2) {
                string sgn = tk[0], name = tk[1];
                if (sgn != "+" && sgn != "-") throw runtime_error("Bad CONSTGROUP line: " + line);
                int val = (sgn == "+") ? 0 : 1;
                mf.consts.push_back({name, val});
            } else if (tk.size() == 3) {
                // ignore leading circuit id if present
                string sgn = tk[1], name = tk[2];
                if (sgn != "+" && sgn != "-") throw runtime_error("Bad CONSTGROUP line: " + line);
                int val = (sgn == "+") ? 0 : 1;
                mf.consts.push_back({name, val});
            } else {
                throw runtime_error("Bad CONSTGROUP line: " + line);
            }
            continue;
        }

        if (sec != IN && sec != OUT) {
            throw runtime_error("Line appears before any group keyword: " + line);
        }

        // IN/OUT: format: <1|2> <+|-> <name>
        if (tk.size() != 3) throw runtime_error("Bad IN/OUT line: " + line);

        int cir = stoi(tk[0]);
        string sgn = tk[1];
        string name = tk[2];
        if (cir != 1 && cir != 2) throw runtime_error("Bad circuit id in line: " + line);
        if (sgn != "+" && sgn != "-") throw runtime_error("Bad sign in line: " + line);

        PortLit pl;
        pl.cir = cir;
        pl.neg = (sgn == "-");
        pl.name = name;

        // If no END is used, infer new group when we see another "1 ..." after already having one.
        if (cir == 1) {
            if (cur.hasC1) flushCur();
            cur.hasC1 = true;
            cur.c1 = pl;
        } else {
            if (!cur.hasC1) throw runtime_error("Circuit-II line appears before Circuit-I line: " + line);
            cur.c2s.push_back(pl);
        }
    }
    flushCur();
    return mf;
}

// ------------------------------------------------------------
// CNF builder (Tseitin encoding)
// We create one CNF variable for every net in circuit1 and circuit2.
// Additionally constVar=1 is fixed to True, and we use (+1) as 1 and (-1) as 0.
// ------------------------------------------------------------
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

static inline bool isConst1Name(const string& s) {
    return (s == "1'b1" || s == "1");
}
static inline bool isConst0Name(const string& s) {
    return (s == "1'b0" || s == "0");
}

struct VarCtx {
    int constVar = 1;
    int base1 = 2;
    int base2 = 0;
};

static int findNetIdOrDie(const Circuit& cir, const string& name, const string& who) {
    auto it = cir.netNameToId.find(name);
    if (it == cir.netNameToId.end()) {
        throw runtime_error("Cannot find net/port '" + name + "' in " + who);
    }
    return it->second;
}

static int netLitById(const Circuit& cir, const VarCtx& vc, int cirIdx, int netId) {
    // return a literal (may be const1/-const1)
    const string& nm = cir.nets[netId].name;
    if (isConst1Name(nm)) return vc.constVar;
    if (isConst0Name(nm)) return -vc.constVar;
    if (cirIdx == 1) return vc.base1 + netId;
    return vc.base2 + netId;
}

static int netVarByNameOrDie(const Circuit& cir, const VarCtx& vc, int cirIdx, const string& name, const string& who) {
    int id = findNetIdOrDie(cir, name, who);
    const string& nm = cir.nets[id].name;
    if (isConst1Name(nm) || isConst0Name(nm)) {
        throw runtime_error("Port '" + name + "' in " + who + " is parsed as a constant net; unsupported as a port");
    }
    return (cirIdx == 1) ? (vc.base1 + id) : (vc.base2 + id);
}

static void addEquiv(CNF& cnf, int litA, int litB) {
    // litA == litB
    cnf.addClause({-litA, litB});
    cnf.addClause({-litB, litA});
}

static void addXorDef(CNF& cnf, int yVar, int litA, int litB) {
    // y = litA XOR litB
    int aVar = abs(litA), bVar = abs(litB);
    int aNeg = (litA < 0) ? 1 : 0;
    int bNeg = (litB < 0) ? 1 : 0;
    int parity = aNeg ^ bNeg;

    // parity==0: y = a XOR b
    // parity==1: y = XNOR(a,b)
    if (parity == 0) {
        // ( a ∨  b ∨ ¬y)
        // (¬a ∨ ¬b ∨ ¬y)
        // ( a ∨ ¬b ∨  y)
        // (¬a ∨  b ∨  y)
        cnf.addClause({ aVar,  bVar, -yVar});
        cnf.addClause({-aVar, -bVar, -yVar});
        cnf.addClause({ aVar, -bVar,  yVar});
        cnf.addClause({-aVar,  bVar,  yVar});
    } else {
        // y = XNOR(a,b)  <=> y = ¬(a XOR b)
        // ( a ∨  b ∨  y)
        // (¬a ∨ ¬b ∨  y)
        // ( a ∨ ¬b ∨ ¬y)
        // (¬a ∨  b ∨ ¬y)
        cnf.addClause({ aVar,  bVar,  yVar});
        cnf.addClause({-aVar, -bVar,  yVar});
        cnf.addClause({ aVar, -bVar, -yVar});
        cnf.addClause({-aVar,  bVar, -yVar});
    }
}

static void encodeGate(CNF& cnf, GateType tp, int outVar, const vector<int>& inLits) {
    // outVar is variable id (>0), inLits are literals
    auto die = [&](const string& msg){ throw runtime_error("Gate encode error: " + msg); };

    switch (tp) {
        case GateType::NOT: {
            if (inLits.size() != 1) die("NOT expects 1 input");
            int a = inLits[0];
            // y <-> ~a : (y or a) and (~y or ~a)
            cnf.addClause({ outVar, a});
            cnf.addClause({-outVar, -a});
            break;
        }
        case GateType::BUF: {
            if (inLits.size() != 1) die("BUF expects 1 input");
            int a = inLits[0];
            // y <-> a : (~y or a) and (y or ~a)
            cnf.addClause({-outVar, a});
            cnf.addClause({ outVar, -a});
            break;
        }
        case GateType::AND: {
            if (inLits.size() < 2) die("AND expects >=2 inputs");
            // (¬a1 ∨ ¬a2 ∨ ... ∨ y)
            vector<int> c;
            for (int a : inLits) c.push_back(-a);
            c.push_back(outVar);
            cnf.addClause(c);
            // for each ai: (¬y ∨ ai)
            for (int a : inLits) cnf.addClause({-outVar, a});
            break;
        }
        case GateType::OR: {
            if (inLits.size() < 2) die("OR expects >=2 inputs");
            // (¬y ∨ a1 ∨ a2 ∨ ...)
            vector<int> c;
            c.push_back(-outVar);
            for (int a : inLits) c.push_back(a);
            cnf.addClause(c);
            // for each ai: (¬ai ∨ y)
            for (int a : inLits) cnf.addClause({-a, outVar});
            break;
        }
        case GateType::NAND: {
            if (inLits.size() < 2) die("NAND expects >=2 inputs");
            // y = ~(AND in)
            // (¬y ∨ ¬a1 ∨ ¬a2 ∨ ...)
            vector<int> c;
            c.push_back(-outVar);
            for (int a : inLits) c.push_back(-a);
            cnf.addClause(c);
            // for each ai: (ai ∨ y)
            for (int a : inLits) cnf.addClause({a, outVar});
            break;
        }
        case GateType::NOR: {
            if (inLits.size() < 2) die("NOR expects >=2 inputs");
            // y = ~(OR in)
            // for each ai: (¬y ∨ ¬ai)
            for (int a : inLits) cnf.addClause({-outVar, -a});
            // (a1 ∨ a2 ∨ ... ∨ y)
            vector<int> c;
            for (int a : inLits) c.push_back(a);
            c.push_back(outVar);
            cnf.addClause(c);
            break;
        }
        case GateType::XOR: {
            if (inLits.size() != 2) die("XOR expects 2 inputs");
            // y = a XOR b
            addXorDef(cnf, outVar, inLits[0], inLits[1]);
            break;
        }
        case GateType::XNOR: {
            if (inLits.size() != 2) die("XNOR expects 2 inputs");
            // y = XNOR(a,b) == NOT(XOR)
            // Use XOR def with one input negated to flip parity:
            addXorDef(cnf, outVar, inLits[0], -inLits[1]);
            break;
        }
        default:
            die("unknown gate type");
    }
}

static CNF buildBaseCNF(const Circuit& c1, const Circuit& c2, const MatchFile& mf, VarCtx& vcOut) {
    VarCtx vc;
    vc.base2 = vc.base1 + (int)c1.nets.size();
    vcOut = vc;

    int initialVars = vc.base2 + (int)c2.nets.size() - 1; // last used var id
    CNF cnf(initialVars);

    // constVar = True (so +constVar means 1, -constVar means 0)
    cnf.addClause({vc.constVar});

    // Encode gates of c1
    for (const auto& g : c1.gates) {
        int outVar = vc.base1 + g.outNet;
        vector<int> inLits;
        inLits.reserve(g.inNets.size());
        for (int nid : g.inNets) inLits.push_back(netLitById(c1, vc, 1, nid));
        encodeGate(cnf, g.type, outVar, inLits);
    }

    // Encode gates of c2
    for (const auto& g : c2.gates) {
        int outVar = vc.base2 + g.outNet;
        vector<int> inLits;
        inLits.reserve(g.inNets.size());
        for (int nid : g.inNets) inLits.push_back(netLitById(c2, vc, 2, nid));
        encodeGate(cnf, g.type, outVar, inLits);
    }

    // Apply INGROUP equalities
    for (const auto& grp : mf.ingroups) {
        int v1 = netVarByNameOrDie(c1, vc, 1, grp.c1.name, "Circuit I (INGROUP)");
        int lit1 = grp.c1.neg ? -v1 : v1;
        for (const auto& p2 : grp.c2s) {
            int v2 = netVarByNameOrDie(c2, vc, 2, p2.name, "Circuit II (INGROUP)");
            int lit2 = p2.neg ? -v2 : v2;
            addEquiv(cnf, lit1, lit2);
        }
    }

    // Apply CONSTGROUP bindings (Circuit II inputs only): '+'=>0, '-' =>1
    for (auto [name, val] : mf.consts) {
        int v = netVarByNameOrDie(c2, vc, 2, name, "Circuit II (CONSTGROUP)");
        cnf.addClause({ val ? v : -v });
    }

    return cnf;
}

static void addOutGroupMiter(CNF& cnf, const Circuit& c1, const Circuit& c2, const VarCtx& vc, const MatchGroup& grp) {
    int v1 = netVarByNameOrDie(c1, vc, 1, grp.c1.name, "Circuit I (OUTGROUP)");
    int lit1 = grp.c1.neg ? -v1 : v1;

    vector<int> diffs;
    diffs.reserve(grp.c2s.size());

    for (const auto& p2 : grp.c2s) {
        int v2 = netVarByNameOrDie(c2, vc, 2, p2.name, "Circuit II (OUTGROUP)");
        int lit2 = p2.neg ? -v2 : v2;

        int d = cnf.newVar();              // d = (lit1 XOR lit2)
        addXorDef(cnf, d, lit1, lit2);
        diffs.push_back(d);
    }

    // Force "there exists a mismatch"
    cnf.addClause(diffs);
}

static void writeDimacs(const string& path, const CNF& cnf) {
    ofstream fout(path);
    if (!fout) throw runtime_error("Cannot write CNF: " + path);
    fout << "p cnf " << cnf.numVars << " " << cnf.clauses.size() << "\n";
    for (auto &c : cnf.clauses) {
        for (int lit : c) fout << lit << " ";
        fout << "0\n";
    }
}

int main(int argc, char** argv) {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    if (argc < 4) {
        cerr << "Usage:\n"
             << "  " << argv[0] << " <case_input> <output.match> <out_dir> [--mode both|all|per]\n\n"
             << "Meaning:\n"
             << "  - mode=per : one CNF per OUTGROUP (recommended)\n"
             << "  - mode=all : one CNF combining all OUTGROUPs\n"
             << "  - mode=both: generate both\n";
        return 1;
    }

    string caseInput = argv[1];
    string matchPath = argv[2];
    string outDir = argv[3];
    string mode = "both";
    for (int i = 4; i < argc; i++) {
        string a = argv[i];
        if (a == "--mode" && i + 1 < argc) mode = argv[++i];
    }

    try {
        std::filesystem::create_directories(outDir);

        // Parse case input -> get circuit paths
        ProblemInput pi = parseProblemInputFile(caseInput);

        // Parse circuits
        Circuit c1 = parseVerilogNetlist(pi.c1File);
        Circuit c2 = parseVerilogNetlist(pi.c2File);

        // Parse your output.match
        MatchFile mf = parseMatchFile(matchPath);

        // Base CNF: two circuits + INGROUP + CONSTGROUP
        VarCtx vc;
        CNF base = buildBaseCNF(c1, c2, mf, vc);

        // Generate CNFs
        if (mode == "all" || mode == "both") {
            CNF cnf = base;
            for (const auto& og : mf.outgroups) addOutGroupMiter(cnf, c1, c2, vc, og);
            writeDimacs(outDir + "/miter_all.cnf", cnf);
            cerr << "[OK] wrote " << outDir << "/miter_all.cnf\n";
        }

        if (mode == "per" || mode == "both") {
            for (int i = 0; i < (int)mf.outgroups.size(); i++) {
                CNF cnf = base;
                addOutGroupMiter(cnf, c1, c2, vc, mf.outgroups[i]);
                writeDimacs(outDir + "/miter_outgroup_" + to_string(i) + ".cnf", cnf);
                cerr << "[OK] wrote " << outDir << "/miter_outgroup_" << i << ".cnf\n";
            }
        }

    } catch (const exception& e) {
        cerr << "[ERROR] " << e.what() << "\n";
        return 2;
    }

    return 0;
}
