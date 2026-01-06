#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <iterator>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include "parser.h"

using namespace std;
static inline string trim(const std::string& s);
static std::string stripComments(const std::string& text);
static std::vector<std::string> splitStatementsBySemicolon(const std::string& text);
static std::vector<std::string> tokenize(const std::string& s);

// ===================== ABC assign-only parser =====================

namespace {

enum class TokKind { ID, NOT, AND, OR, XOR, LP, RP, END };

struct Tok { TokKind k; std::string s; };

static bool isIdentChar(char c) {
    return std::isalnum((unsigned char)c) || c=='_' || c=='\'' || c=='[' || c==']' || c=='.';
}

static std::vector<Tok> lexExpr(const std::string& expr) {
    std::vector<Tok> v;
    for (size_t i=0;i<expr.size();) {
        char c = expr[i];
        if (std::isspace((unsigned char)c)) { i++; continue; }
        if (c=='~') { v.push_back({TokKind::NOT,"~"}); i++; continue; }
        if (c=='&') { v.push_back({TokKind::AND,"&"}); i++; continue; }
        if (c=='|') { v.push_back({TokKind::OR ,"|"}); i++; continue; }
        if (c=='^') { v.push_back({TokKind::XOR,"^"}); i++; continue; }
        if (c=='(') { v.push_back({TokKind::LP ,"("}); i++; continue; }
        if (c==')') { v.push_back({TokKind::RP ,")"}); i++; continue; }

        if (isIdentChar(c)) {
            size_t j=i;
            while (j<expr.size() && isIdentChar(expr[j])) j++;
            v.push_back({TokKind::ID, expr.substr(i,j-i)});
            i=j; continue;
        }

        // 不認得的字元：直接跳過或丟錯（建議丟錯比較早發現）
        throw std::runtime_error("ABC expr lexer: unexpected char in: " + expr);
    }
    v.push_back({TokKind::END,""});
    return v;
}

struct Node {
    TokKind op;        // ID / NOT / AND / OR / XOR
    std::string name;  // for ID
    Node* a=nullptr;
    Node* b=nullptr;
};

struct Parser {
    std::vector<Tok> t;
    size_t p=0;

    Tok& peek() { return t[p]; }
    Tok& get()  { return t[p++]; }

    // grammar (precedence):
    // expr := or
    // or   := xor ( '|' xor )*
    // xor  := and ( '^' and )*
    // and  := unary ( '&' unary )*
    // unary:= '~' unary | primary
    // primary := ID | '(' expr ')'

    Node* parseExpr() { return parseOr(); }

    Node* parseOr() {
        Node* lhs = parseXor();
        while (peek().k == TokKind::OR) {
            get();
            Node* rhs = parseXor();
            Node* n = new Node{TokKind::OR,"",lhs,rhs};
            lhs = n;
        }
        return lhs;
    }
    Node* parseXor() {
        Node* lhs = parseAnd();
        while (peek().k == TokKind::XOR) {
            get();
            Node* rhs = parseAnd();
            Node* n = new Node{TokKind::XOR,"",lhs,rhs};
            lhs = n;
        }
        return lhs;
    }
    Node* parseAnd() {
        Node* lhs = parseUnary();
        while (peek().k == TokKind::AND) {
            get();
            Node* rhs = parseUnary();
            Node* n = new Node{TokKind::AND,"",lhs,rhs};
            lhs = n;
        }
        return lhs;
    }
    Node* parseUnary() {
        if (peek().k == TokKind::NOT) {
            get();
            Node* x = parseUnary();
            return new Node{TokKind::NOT,"",x,nullptr};
        }
        return parsePrimary();
    }
    Node* parsePrimary() {
        if (peek().k == TokKind::ID) {
            auto tk = get();
            return new Node{TokKind::ID, tk.s, nullptr, nullptr};
        }
        if (peek().k == TokKind::LP) {
            get();
            Node* n = parseExpr();
            if (peek().k != TokKind::RP) throw std::runtime_error("ABC expr parser: missing ')'");
            get();
            return n;
        }
        throw std::runtime_error("ABC expr parser: bad token near: " + peek().s);
    }
};

static std::string trim2(const std::string& s) { return trim(s); }

// 產生中繼 net 名稱（避免跟原本 new_nxx 撞名）
static std::string freshTmpName(int& tmpCnt) {
    tmpCnt++;
    return "__tmp_" + std::to_string(tmpCnt);
}

// 將 AST 展開成 Gate / Net，回傳該 AST 對應的 net id
static int emitAST(Circuit& c, Node* n, int& tmpCnt) {
    if (n->op == TokKind::ID) {
        return c.getNetId(n->name);
    }

    auto newGate = [&](GateType gt, int out, const std::vector<int>& ins) {
        Gate g;
        g.id = (int)c.gates.size();
        g.type = gt;
        g.instName = gateTypeToStr(gt) + std::string("_abc_") + std::to_string(g.id);
        g.outNet = out;
        g.inNets = ins;

        c.nets[out].driverGate = g.id;
        for (int inNet : ins) c.nets[inNet].sinkGates.push_back(g.id);

        c.gates.push_back(std::move(g));
    };

    if (n->op == TokKind::NOT) {
        int in = emitAST(c, n->a, tmpCnt);
        std::string onm = freshTmpName(tmpCnt);
        int out = c.getNetId(onm);
        newGate(GateType::NOT, out, {in});
        return out;
    }

    // binary
    int a = emitAST(c, n->a, tmpCnt);
    int b = emitAST(c, n->b, tmpCnt);
    std::string onm = freshTmpName(tmpCnt);
    int out = c.getNetId(onm);

    if (n->op == TokKind::AND) newGate(GateType::AND, out, {a,b});
    else if (n->op == TokKind::OR) newGate(GateType::OR, out, {a,b});
    else if (n->op == TokKind::XOR) newGate(GateType::XOR, out, {a,b});
    else throw std::runtime_error("emitAST: unsupported op");

    return out;
}

static void freeAST(Node* n) {
    if (!n) return;
    freeAST(n->a);
    freeAST(n->b);
    delete n;
}

} // anonymous namespace


void parseVerilogABC(const std::string& filename, Circuit& c) {
    std::ifstream fin(filename);
    if (!fin) throw std::runtime_error("Cannot open verilog file: " + filename);

    std::string text((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
    text = stripComments(text);
    auto stmts = splitStatementsBySemicolon(text);

    int tmpCnt = 0;

    auto parseDeclListABC = [&](const std::vector<std::string>& tks, bool isInput, bool isOutput) {
        for (size_t i=1;i<tks.size();i++) {
            std::string nm = tks[i];
            if (nm.empty()) continue;
            int nid = c.getNetId(nm);
            if (isInput && !c.nets[nid].isPI) {
                c.nets[nid].isPI = true;
                c.PIs.push_back(nid);
            }
            if (isOutput && !c.nets[nid].isPO) {
                c.nets[nid].isPO = true;
                c.POs.push_back(nid);
            }
        }
    };

    for (std::string st : stmts) {
        st = trim2(st);
        if (st.empty()) continue;

        // module/input/output/wire/assign/endmodule
        auto tks = tokenize(st);
        if (tks.empty()) continue;

        const std::string& head = tks[0];

        if (head == "module") {
            if (tks.size() >= 2) c.moduleName = tks[1];
            continue;
        }
        if (head == "input") {
            parseDeclListABC(tks, true, false);
            continue;
        }
        if (head == "output") {
            parseDeclListABC(tks, false, true);
            continue;
        }
        if (head == "wire") {
            for (size_t i=1;i<tks.size();i++) if (!tks[i].empty()) (void)c.getNetId(tks[i]);
            continue;
        }
        if (head == "endmodule") continue;

        if (head == "assign") {
            // 原句型：assign <lhs> = <expr>
            // tokenize() 會把 '=' 當成 token (它不是 delimiter) -> 你原 tokenize 不會拆 '='
            // 所以這裡直接用字串找 '=' 比較穩
            auto eqPos = st.find('=');
            if (eqPos == std::string::npos) throw std::runtime_error("Bad assign: " + st);

            std::string lhsPart = trim2(st.substr(6, eqPos - 6)); // after "assign"
            std::string rhsPart = trim2(st.substr(eqPos + 1));

            // lhsPart 可能還有空格
            lhsPart = trim2(lhsPart);

            int lhsNet = c.getNetId(lhsPart);

            // 解析 rhs expr
            auto toks = lexExpr(rhsPart);
            Parser p; p.t = std::move(toks); p.p = 0;
            Node* ast = p.parseExpr();
            if (p.peek().k != TokKind::END) {
                freeAST(ast);
                throw std::runtime_error("Bad assign expr (trailing tokens): " + st);
            }

            int rhsNet = emitAST(c, ast, tmpCnt);
            freeAST(ast);

            // 最後把 rhsNet 接到 lhsNet：
            // 用 BUF gate 表示 "lhs = rhs"
            // （或你也可以直接把 lhsNet 當 emitAST 的 output，但那會讓多重 assign 更難處理）
            {
                Gate g;
                g.id = (int)c.gates.size();
                g.type = GateType::BUF;
                g.instName = "buf_abc_" + std::to_string(g.id);
                g.outNet = lhsNet;
                g.inNets = {rhsNet};

                c.nets[lhsNet].driverGate = g.id;
                c.nets[rhsNet].sinkGates.push_back(g.id);

                c.gates.push_back(std::move(g));
            }

            continue;
        }

        // 其他 statement 忽略
    }
}

/*
  ============================================================
  Data Structures
  ============================================================
*/
std::string gateTypeToStr(GateType t) {
    switch (t) {
        case GateType::AND:  return "and";
        case GateType::OR:   return "or";
        case GateType::NAND: return "nand";
        case GateType::NOR:  return "nor";
        case GateType::XOR:  return "xor";
        case GateType::XNOR: return "xnor";
        case GateType::NOT:  return "not";
        case GateType::BUF:  return "buf";
        default: return "unknown";
    }
}

int Circuit::getNetId(const string& name) {
    auto it = netNameToId.find(name);
    if (it != netNameToId.end()) return it->second;
    int id = (int)nets.size();
    nets.push_back(Net{});
    nets.back().id = id;
    nets.back().name = name;
    netNameToId[name] = id;

    // handle constants like 1'b0 / 1'b1 (題目常見)
    if (name == "1'b0" || name == "1’b0") { // 有些檔可能用不同 apostrophe
        nets.back().isConst = true;
        nets.back().constVal = 0;
    } else if (name == "1'b1" || name == "1’b1") {
        nets.back().isConst = true;
        nets.back().constVal = 1;
    }
    return id;
}

static string getDirname(const string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == string::npos) return "";
    return path.substr(0, pos + 1); // keep trailing slash
}

static string joinPath(const string& dir, const string& file) {
    if (dir.empty()) return file;
    return dir + file;
}

/*
  ============================================================
  Helpers: string processing
  ============================================================
*/

bool looksLikeABCVerilog(const std::string& fileText) {
    // ABC 常見特徵：大量 "assign "，幾乎沒有 and/or/xor gate inst lines
    // 以及 header "written by ABC"
    if (fileText.find("written by ABC") != std::string::npos) return true;
    size_t nAssign = 0;
    size_t pos = 0;
    while ((pos = fileText.find("assign", pos)) != std::string::npos) {
        nAssign++; pos += 6;
        if (nAssign >= 3) return true;
    }
    return false;
}


static inline string trim(const string& s) {
    size_t i = 0, j = s.size();
    while (i < j && isspace((unsigned char)s[i])) i++;
    while (j > i && isspace((unsigned char)s[j-1])) j--;
    return s.substr(i, j - i);
}

static string stripComments(const string& text) {
    // remove // ... and /* ... */
    string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        if (i + 1 < text.size() && text[i] == '/' && text[i+1] == '/') {
            // line comment
            i += 2;
            while (i < text.size() && text[i] != '\n') i++;
        } else if (i + 1 < text.size() && text[i] == '/' && text[i+1] == '*') {
            // block comment
            i += 2;
            while (i + 1 < text.size() && !(text[i] == '*' && text[i+1] == '/')) i++;
            if (i + 1 < text.size()) i += 2;
        } else {
            out.push_back(text[i++]);
        }
    }
    return out;
}

static vector<string> splitStatementsBySemicolon(const string& text) {
    // accumulate until ';' -> one statement
    vector<string> stmts;
    string cur;
    cur.reserve(256);
    for (char c : text) {
        if (c == ';') {
            string s = trim(cur);
            if (!s.empty()) stmts.push_back(s);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    // leftover
    string s = trim(cur);
    if (!s.empty()) stmts.push_back(s);
    return stmts;
}

// tokenize: split by whitespace, '(', ')', ','
// keep tokens like "1'b0" intact
static vector<string> tokenize(const string& s) {
    vector<string> tks;
    string cur;
    auto flush = [&]() {
        if (!cur.empty()) { tks.push_back(cur); cur.clear(); }
    };
    for (char c : s) {
        if (isspace((unsigned char)c) || c == '(' || c == ')' || c == ',' ) {
            flush();
        } else {
            cur.push_back(c);
        }
    }
    flush();
    return tks;
}

static GateType parseGateType(const string& kw) {
    if (kw == "and") return GateType::AND;
    if (kw == "or") return GateType::OR;
    if (kw == "nand") return GateType::NAND;
    if (kw == "nor") return GateType::NOR;
    if (kw == "xor") return GateType::XOR;
    if (kw == "xnor") return GateType::XNOR;
    if (kw == "not") return GateType::NOT;
    if (kw == "buf") return GateType::BUF;
    return GateType::UNKNOWN;
}

/*
  ============================================================
  Parse <input> problem file (bus grouping file)
  Format:
    circuit_1.v
    N
    M name1 name2 ... nameM
    ... (N lines)
    circuit_2.v
    N
    ...
  ============================================================
*/

ProblemInput parseProblemInputFile(const string& path) {
    ifstream fin(path);
    string baseDir = getDirname(path);
    if (!fin) throw runtime_error("Cannot open problem input file: " + path);

    ProblemInput in;

    auto readBuses = [&](string& cirFile, vector<vector<string>>& buses) {
        if (!(fin >> cirFile)) throw runtime_error("Bad format: missing circuit filename");
        cirFile = joinPath(baseDir, cirFile);
        int N;
        if (!(fin >> N)) throw runtime_error("Bad format: missing bus count after " + cirFile);
        buses.clear();
        buses.reserve(N);
        for (int i = 0; i < N; i++) {
            int M;
            if (!(fin >> M)) throw runtime_error("Bad format: missing bus size");
            vector<string> members;
            members.reserve(M);
            for (int j = 0; j < M; j++) {
                string name;
                if (!(fin >> name)) throw runtime_error("Bad format: missing bus member name");
                members.push_back(name);
            }
            buses.push_back(std::move(members));
        }
    };

    readBuses(in.c1File, in.c1Buses);
    readBuses(in.c2File, in.c2Buses);

    return in;
}

/*
  ============================================================
  Parse Verilog (restricted primitive gate netlist)
  Handles:
    module top ( ... )
    input a0, a1, ...
    output h0, ...
    wire n1, ...
    <gate> <inst> ( out , in1 , in2 ... )
  ============================================================
*/

Circuit parseVerilogNetlist(const string& path) {
    ifstream fin(path);
    if (!fin) throw runtime_error("Cannot open verilog file: " + path);

    string text((istreambuf_iterator<char>(fin)), istreambuf_iterator<char>());
    text = stripComments(text);
    auto stmts = splitStatementsBySemicolon(text);
    Circuit cir;
    cir.path = path;

    if (looksLikeABCVerilog(text)) {
        parseVerilogABC(path, cir);
        // optional de-dup PIs/POs
        sort(cir.PIs.begin(), cir.PIs.end());
        cir.PIs.erase(unique(cir.PIs.begin(), cir.PIs.end()), cir.PIs.end());
        sort(cir.POs.begin(), cir.POs.end());
        cir.POs.erase(unique(cir.POs.begin(), cir.POs.end()), cir.POs.end());
        return cir;
    }

    auto parseDeclList = [&](Circuit& c, const vector<string>& tks, bool isInput, bool isOutput) {
        // tks: ["input", "a0", "a1", ...] already tokenized by (), comma, whitespace
        for (size_t i = 1; i < tks.size(); i++) {
            string nm = tks[i];
            if (nm.empty()) continue;
            int nid = c.getNetId(nm);
            if (isInput) {
                if (!c.nets[nid].isPI) {
                    c.nets[nid].isPI = true;
                    c.PIs.push_back(nid);
                }
            }
            if (isOutput) {
                if (!c.nets[nid].isPO) {
                    c.nets[nid].isPO = true;
                    c.POs.push_back(nid);
                }
            }
        }
    };

    for (string st : stmts) {
        st = trim(st);
        if (st.empty()) continue;

        // normalize: sometimes "endmodule" appears without ';' in some netlists; we ignore unknowns safely.
        auto tks = tokenize(st);
        if (tks.empty()) continue;

        const string& head = tks[0];

        if (head == "module") {
            // module top ( a0 , a1 , ... )
            // tokens: module top a0 a1 ...
            if (tks.size() >= 2) cir.moduleName = tks[1];
            continue;
        }
        if (head == "input") {
            parseDeclList(cir, tks, true, false);
            continue;
        }
        if (head == "output") {
            parseDeclList(cir, tks, false, true);
            continue;
        }
        if (head == "wire") {
            // we still create net ids, but no PI/PO flags
            for (size_t i = 1; i < tks.size(); i++) {
                if (!tks[i].empty()) (void)cir.getNetId(tks[i]);
            }
            continue;
        }
        if (head == "endmodule") {
            continue;
        }

        // gate instantiation
        GateType gt = parseGateType(head);
        if (gt != GateType::UNKNOWN) {
            // expected tokens: <gate> <inst> <out> <in1> <in2> ...
            // because tokenize removed parentheses and commas
            if (tks.size() < 4) {
                throw runtime_error("Bad gate line (too few tokens): " + st);
            }
            Gate g;
            g.id = (int)cir.gates.size();
            g.type = gt;
            g.instName = tks[1];

            // arguments start at tks[2]
            g.outNet = cir.getNetId(tks[2]);
            for (size_t i = 3; i < tks.size(); i++) {
                g.inNets.push_back(cir.getNetId(tks[i]));
            }

            // connect driver/sinks
            // output net driven by this gate
            if (cir.nets[g.outNet].driverGate != -1) {
                // in these netlists it should not happen; but keep last and warn optionally
                // cerr << "Warning: net " << cir.nets[g.outNet].name << " has multiple drivers\n";
            }
            cir.nets[g.outNet].driverGate = g.id;

            // each input net sinks this gate
            for (int inNet : g.inNets) {
                cir.nets[inNet].sinkGates.push_back(g.id);
            }

            cir.gates.push_back(std::move(g));
            continue;
        }

        // If there are other constructs, you can extend here (e.g., assign).
        // For contest A 2023, primitive gates are typical, so ignoring unknown statement is OK.
        // cerr << "Ignore statement: " << st << "\n";
    }

    // optional: de-dup PIs/POs (if declared twice)
    auto uniqVec = [&](vector<int>& v) {
        sort(v.begin(), v.end());
        v.erase(unique(v.begin(), v.end()), v.end());
    };
    uniqVec(cir.PIs);
    uniqVec(cir.POs);

    return cir;
}

/*
  ============================================================
  Attach buses to a parsed circuit
  - For each bus member name, map to net id
  - Provide netToBus mapping (-1 if not assigned)
  ============================================================
*/
BusInfo attachBuses(const Circuit& c, const vector<vector<string>>& busNames) {
    BusInfo bi;
    bi.netToBus.assign(c.nets.size(), -1);
    bi.buses.reserve(busNames.size());

    for (int i = 0; i < (int)busNames.size(); i++) {
        Bus b;
        b.id = i;
        for (const string& nm : busNames[i]) {
            auto it = c.netNameToId.find(nm);
            if (it == c.netNameToId.end()) {
                // bus name not found in circuit netlist -> usually means input file mismatch
                throw runtime_error("Bus member '" + nm + "' not found in circuit nets.");
            }
            int nid = it->second;
            b.members.push_back(nid);

            if (bi.netToBus[nid] != -1 && bi.netToBus[nid] != i) {
                // 只禁止：同一個 net 出現在不同 bus
                throw runtime_error("Net '" + nm + "' appears in multiple buses.");
            }
            bi.netToBus[nid] = i;
        }
        bi.buses.push_back(std::move(b));
    }
    return bi;
}

/*
  ============================================================
  Debug printer (optional)
  ============================================================
*/
static void printCircuitSummary(const Circuit& c) {
    cout << "Circuit: " << c.path << "\n";
    cout << "  module: " << c.moduleName << "\n";
    cout << "  nets: " << c.nets.size() << ", gates: " << c.gates.size() << "\n";
    cout << "  PIs: " << c.PIs.size() << ", POs: " << c.POs.size() << "\n";
    cout << "  PI names: ";
    for (int nid : c.PIs) cout << c.nets[nid].name << " ";
    cout << "\n";
    cout << "  PO names: ";
    for (int nid : c.POs) cout << c.nets[nid].name << " ";
    cout << "\n";
}


