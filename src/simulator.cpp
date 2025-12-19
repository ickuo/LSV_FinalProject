#include "simulator.h"
#include <algorithm>

using namespace std;

vector<int> topoSortGates(const Circuit& c) {
    int G = (int)c.gates.size();
    vector<int> indeg(G, 0);
    vector<vector<int>> adj(G);

    for (const auto& g : c.gates) {
        for (int inNet : g.inNets) {
            int drv = c.nets[inNet].driverGate;
            if (drv >= 0) {
                adj[drv].push_back(g.id);
                indeg[g.id]++;
            }
        }
    }

    vector<int> q;
    q.reserve(G);
    for (int i = 0; i < G; i++) if (indeg[i] == 0) q.push_back(i);

    vector<int> order;
    order.reserve(G);
    for (size_t qi = 0; qi < q.size(); qi++) {
        int u = q[qi];
        order.push_back(u);
        for (int v : adj[u]) if (--indeg[v] == 0) q.push_back(v);
    }
    return order;
}

vector<vector<uint64_t>> buildPiPatterns(int nPI) {
    int total = 1 << nPI;
    int blocks = (total + 63) / 64;
    vector<vector<uint64_t>> pat(nPI, vector<uint64_t>(blocks, 0));

    for (int assn = 0; assn < total; assn++) {
        int block = assn >> 6;
        int bit   = assn & 63;
        uint64_t mask = (1ULL << bit);
        for (int i = 0; i < nPI; i++) {
            if ((assn >> i) & 1) pat[i][block] |= mask;
        }
    }
    return pat;
}

// ---- internal helpers (keep static) ----
static inline void bitwise_not(vector<uint64_t>& a, int totalBits) {
    for (auto& x : a) x = ~x;
    int pad = ((int)a.size() * 64) - totalBits;
    if (pad > 0) {
        uint64_t keep = (~0ULL) >> pad;
        a.back() &= keep;
    }
}

static vector<uint64_t> makeConstTT(int blocks, bool one, int totalBits) {
    vector<uint64_t> v(blocks, one ? ~0ULL : 0ULL);
    if (one) {
        int pad = blocks * 64 - totalBits;
        if (pad > 0) {
            uint64_t keep = (~0ULL) >> pad;
            v.back() &= keep;
        }
    }
    return v;
}

// ---- exported ----
vector<TT> simulateCircuitTT(
    const Circuit& c,
    const vector<int>& topo,
    const vector<vector<uint64_t>>& piPatterns,
    const unordered_map<int,int>& piNetToAbstractIdx,
    const vector<int>& wantNets
) {
    int nAbsPI = (int)piPatterns.size();
    int total = 1 << nAbsPI;
    int blocks = (total + 63) / 64;

    vector<TT> netTT(c.nets.size());
    vector<char> hasTT(c.nets.size(), 0);

    for (const auto& net : c.nets) {
        if (net.isConst) {
            netTT[net.id] = makeConstTT(blocks, net.constVal == 1, total);
            hasTT[net.id] = 1;
        }
    }

    // use c.PIs directly
    for (int nid : c.PIs) {
        auto it = piNetToAbstractIdx.find(nid);
        if (it == piNetToAbstractIdx.end()) continue;
        int absIdx = it->second;
        netTT[nid] = piPatterns[absIdx];
        hasTT[nid] = 1;
    }

    for (int gid : topo) {
        const Gate& g = c.gates[gid];
        int out = g.outNet;
        TT outTT(blocks, 0);

        auto get = [&](int netId) -> const TT& {
            if (!hasTT[netId]) {
                netTT[netId] = TT(blocks, 0);
                hasTT[netId] = 1;
            }
            return netTT[netId];
        };

        if (g.type == GateType::BUF) {
            outTT = get(g.inNets[0]);
        } else if (g.type == GateType::NOT) {
            outTT = get(g.inNets[0]);
            bitwise_not(outTT, total);
        } else {
            const TT& a = get(g.inNets[0]);
            const TT& b = get(g.inNets[1]);
            for (int i = 0; i < blocks; i++) {
                uint64_t va = a[i], vb = b[i], r = 0;
                switch (g.type) {
                    case GateType::AND:  r = (va & vb); break;
                    case GateType::OR:   r = (va | vb); break;
                    case GateType::NAND: r = ~(va & vb); break;
                    case GateType::NOR:  r = ~(va | vb); break;
                    case GateType::XOR:  r = (va ^ vb); break;
                    case GateType::XNOR: r = ~(va ^ vb); break;
                    default: r = 0; break;
                }
                outTT[i] = r;
            }
            int pad = blocks * 64 - total;
            if (pad > 0) {
                uint64_t keep = (~0ULL) >> pad;
                outTT.back() &= keep;
            }
        }

        netTT[out] = std::move(outTT);
        hasTT[out] = 1;
    }

    vector<TT> res;
    res.reserve(wantNets.size());
    for (int nid : wantNets) {
        if (!hasTT[nid]) {
            netTT[nid] = TT(blocks, 0);
            hasTT[nid] = 1;
        }
        res.push_back(netTT[nid]);
    }
    return res;
}

bool sameTT(const TT& A, const TT& B) {
    if (A.size() != B.size()) return false;
    for (size_t i = 0; i < A.size(); i++) if (A[i] != B[i]) return false;
    return true;
}
