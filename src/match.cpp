#include "match.h"

#include "parser.h"
#include "sat_check.h"
#include "simulator.h"
#include "unate_table.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <random>
#include <numeric>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

using std::string;
using std::vector;

// ------------------------
// Unate helpers
// ------------------------
static inline char normRel(char c) {
    // Normalize to one of {P,N,B,I,X}; treat others as unknown ('X').
    switch (c) {
        case 'P': case 'N': case 'B': case 'I': case 'X': return c;
        default: return 'X';
    }
}
static inline char redRel(char c) {
    c = normRel(c);
    if (c == 'P' || c == 'N') return 'U';
    return c; // B / I / X
}

// ------------------------
// Row feature (used for PO candidate ranking)
// ------------------------
struct RowFeat {
    int minPN = 0;
    int maxPN = 0;
    int cntB  = 0;
    int cntI  = 0;
    int cntX  = 0;
    int nPI   = 0;
    std::string refKey;
};


struct PoSig {
    uint8_t bias = 0;                 // 0..255
    std::vector<uint8_t> absCorrHist; // size=bins, each 0..255 (normalized)
};

static inline int l1HistDist(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    const size_t n = std::min(a.size(), b.size());
    int s = 0;
    for (size_t i = 0; i < n; ++i) s += std::abs((int)a[i] - (int)b[i]);
    for (size_t i = n; i < a.size(); ++i) s += (int)a[i];
    for (size_t i = n; i < b.size(); ++i) s += (int)b[i];
    return s;
}

static RowFeat makeRowFeat(const UnateTable& t, int poIdx) {
    RowFeat f;
    int cntP = 0, cntN = 0;
    for (int j = 0; j < t.nPI; j++) {
        char a = normRel(t.rel[poIdx][j]);
        if (a == 'P') cntP++;
        else if (a == 'N') cntN++;
        else if (a == 'B') f.cntB++;
        else if (a == 'I') f.cntI++;
        else f.cntX++;
    }
    f.minPN = std::min(cntP, cntN);
    f.maxPN = std::max(cntP, cntN);
    f.nPI   = t.nPI;
    return f;
}

static inline int rowFeatDistL1(const RowFeat& a, const RowFeat& b) {
    // Use normalized counts so different PI-count circuits remain comparable.
    auto norm = [](int x, int n) -> int { return n ? (int)((long long)x * 1000 / n) : x; };
    return std::abs(norm(a.minPN, a.nPI) - norm(b.minPN, b.nPI)) +
           std::abs(norm(a.maxPN, a.nPI) - norm(b.maxPN, b.nPI)) +
           std::abs(norm(a.cntB,  a.nPI) - norm(b.cntB,  b.nPI)) +
           std::abs(norm(a.cntI,  a.nPI) - norm(b.cntI,  b.nPI)) +
           std::abs(norm(a.cntX,  a.nPI) - norm(b.cntX,  b.nPI));
}

static inline std::string colReducedKey(const UnateTable& t, int piIdx) {
    int cntU = 0, cntB = 0, cntI = 0, cntX = 0;
    for (int po = 0; po < t.nPO; po++) {
        char r = redRel(t.rel[po][piIdx]);
        if (r == 'U') cntU++;
        else if (r == 'B') cntB++;
        else if (r == 'I') cntI++;
        else cntX++;
    }
    return std::to_string(cntU) + "," + std::to_string(cntB) + "," +
           std::to_string(cntI) + "," + std::to_string(cntX);
}

static inline std::vector<std::string> computeColReducedKeys(const UnateTable& t) {
    std::vector<std::string> keys(t.nPI);
    for (int pi = 0; pi < t.nPI; pi++) keys[pi] = colReducedKey(t, pi);
    return keys;
}

static inline std::string makeRowRefKey(const UnateTable& t,
                                        int poIdx,
                                        const std::vector<std::string>& colKeys,
                                        const RowFeat& base) {
    std::unordered_map<std::string, std::array<int,4>> hist;
    hist.reserve((size_t)t.nPI * 2);

    for (int pi = 0; pi < t.nPI; pi++) {
        const std::string& ck = colKeys[pi];
        char r = redRel(t.rel[poIdx][pi]);
        int idx = (r == 'U') ? 0 : (r == 'B' ? 1 : (r == 'I' ? 2 : 3));
        auto it = hist.find(ck);
        if (it == hist.end()) it = hist.emplace(ck, std::array<int,4>{0,0,0,0}).first;
        it->second[idx]++;
    }

    std::vector<std::string> keys;
    keys.reserve(hist.size());
    for (const auto& kv : hist) keys.push_back(kv.first);
    std::sort(keys.begin(), keys.end());

    std::ostringstream oss;
    oss << base.minPN << "/" << base.maxPN << "|" << base.cntB << "|" << base.cntI
        << "|" << base.cntX << "|";
    for (const auto& k : keys) {
        const auto& a = hist[k];
        oss << k << ":" << a[0] << "," << a[1] << "," << a[2] << "," << a[3] << ";";
    }
    return oss.str();
}

// ------------------------
// Hopcroft-Karp (perfect matching on L)
// ------------------------
static bool hopcroftKarp(const vector<vector<int>>& adj, int nR, vector<int>& matchL) {
    const int nL = (int)adj.size();
    vector<int> matchR(nR, -1);
    matchL.assign(nL, -1);

    vector<int> dist(nL);
    auto bfs = [&]() -> bool {
        vector<int> q;
        q.reserve(nL);
        for (int u = 0; u < nL; u++) {
            if (matchL[u] == -1) { dist[u] = 0; q.push_back(u); }
            else dist[u] = -1;
        }
        bool foundAug = false;
        for (size_t qi = 0; qi < q.size(); qi++) {
            int u = q[qi];
            for (int v : adj[u]) {
                int u2 = matchR[v];
                if (u2 >= 0 && dist[u2] == -1) {
                    dist[u2] = dist[u] + 1;
                    q.push_back(u2);
                }
                if (u2 == -1) foundAug = true;
            }
        }
        return foundAug;
    };

    std::function<bool(int)> dfs = [&](int u) -> bool {
        for (int v : adj[u]) {
            int u2 = matchR[v];
            if (u2 == -1 || (dist[u2] == dist[u] + 1 && dfs(u2))) {
                matchL[u] = v;
                matchR[v] = u;
                return true;
            }
        }
        dist[u] = -1;
        return false;
    };

    int matching = 0;
    while (bfs()) {
        for (int u = 0; u < nL; u++) {
            if (matchL[u] == -1) {
                if (dfs(u)) matching++;
            }
        }
    }
    return matching == nL;
}

// ------------------------
// Hopcroft-Karp (maximum matching)
//   - returns #matched on L (<= min(|L|,|R|))
//   - fills matchL (size=|L|, value in [0,nR) or -1)
//   - optionally fills matchR (size=nR, value in [0,|L|) or -1)
// ------------------------
static int hopcroftKarpMax(const vector<vector<int>>& adj, int nR, vector<int>& matchL, vector<int>* outMatchR = nullptr) {
    const int nL = (int)adj.size();
    vector<int> matchR(nR, -1);
    matchL.assign(nL, -1);

    vector<int> dist(nL);
    auto bfs = [&]() -> bool {
        vector<int> q;
        q.reserve(nL);
        for (int u = 0; u < nL; u++) {
            if (matchL[u] == -1) { dist[u] = 0; q.push_back(u); }
            else dist[u] = -1;
        }
        bool foundAug = false;
        for (size_t qi = 0; qi < q.size(); qi++) {
            int u = q[qi];
            for (int v : adj[u]) {
                int u2 = matchR[v];
                if (u2 >= 0 && dist[u2] == -1) {
                    dist[u2] = dist[u] + 1;
                    q.push_back(u2);
                }
                if (u2 == -1) foundAug = true;
            }
        }
        return foundAug;
    };

    std::function<bool(int)> dfs = [&](int u) -> bool {
        for (int v : adj[u]) {
            int u2 = matchR[v];
            if (u2 == -1 || (dist[u2] == dist[u] + 1 && dfs(u2))) {
                matchL[u] = v;
                matchR[v] = u;
                return true;
            }
        }
        dist[u] = -1;
        return false;
    };

    int matching = 0;
    while (bfs()) {
        for (int u = 0; u < nL; u++) {
            if (matchL[u] == -1) {
                if (dfs(u)) matching++;
            }
        }
    }

    if (outMatchR) *outMatchR = std::move(matchR);
    return matching;
}

// ------------------------
// Bus grouping & signatures (I,B,U)
// ------------------------
struct PortBusGroup {
    int busId = -1;                 // index in BusInfo.buses
    std::vector<int> portIdx;        // indices into t.{piNetIds,poNetIds}
    std::vector<std::array<int,3>> triples; // sorted (I,B,U) per member
    std::array<int,3> sum = {0,0,0};        // aggregated (I,B,U) across members
};

static std::array<int,3> colIBU(const UnateTable& t, int piIdx) {
    int cntI = 0, cntB = 0, cntU = 0;
    for (int po = 0; po < t.nPO; po++) {
        char r = redRel(t.rel[po][piIdx]);
        if (r == 'I') cntI++;
        else if (r == 'B') cntB++;
        else cntU++;
    }
    return {cntI, cntB, cntU};
}

static std::array<int,3> rowIBU(const UnateTable& t, int poIdx) {
    int cntI = 0, cntB = 0, cntU = 0;
    for (int pi = 0; pi < t.nPI; pi++) {
        char r = redRel(t.rel[poIdx][pi]);
        if (r == 'I') cntI++;
        else if (r == 'B') cntB++;
        else cntU++;
    }
    return {cntI, cntB, cntU};
}

static inline int l1Dist3(const std::array<int,3>& a, const std::array<int,3>& b) {
    return std::abs(a[0]-b[0]) + std::abs(a[1]-b[1]) + std::abs(a[2]-b[2]);
}

static std::vector<std::array<int,3>> allColIBU(const UnateTable& t) {
    std::vector<std::array<int,3>> v;
    v.reserve(t.nPI);
    for (int pi = 0; pi < t.nPI; ++pi) v.push_back(colIBU(t, pi));
    return v;
}

static int busDistL1(const PortBusGroup& a, const PortBusGroup& b) {
    if (a.portIdx.size() != b.portIdx.size()) return 1e9;
    int d = 0;
    for (size_t i = 0; i < a.triples.size(); i++) {
        for (int k = 0; k < 3; k++) d += std::abs(a.triples[i][k] - b.triples[i][k]);
    }
    return d;
}

// Flexible distance: allow b to have extra members (b.size >= a.size).
// We compare the best-aligned sorted triples on the prefix and penalize extra bits.
static int busDistL1Flex(const PortBusGroup& a, const PortBusGroup& b, int extraBitPenalty = 2000) {
    if ((int)b.portIdx.size() < (int)a.portIdx.size()) return 1e9;
    int d = 0;
    const int n = (int)a.triples.size();
    for (int i = 0; i < n; ++i) {
        for (int k = 0; k < 3; ++k) d += std::abs(a.triples[i][k] - b.triples[i][k]);
    }
    const int extra = (int)b.triples.size() - n;
    if (extra > 0) d += extra * extraBitPenalty;
    return d;
}

static void extractBusGroups(const Circuit& c,
                             const BusInfo& b,
                             const UnateTable& t,
                             bool isInput,
                             std::vector<PortBusGroup>& groups,
                             std::vector<int>& nonBusPorts) {
    // Build netId -> (piIdx or poIdx)
    std::vector<int> netToIdx(c.nets.size(), -1);
    if (isInput) {
        for (int i = 0; i < t.nPI; i++) netToIdx[t.piNetIds[i]] = i;
    } else {
        for (int i = 0; i < t.nPO; i++) netToIdx[t.poNetIds[i]] = i;
    }

    groups.clear();
    for (int busId = 0; busId < (int)b.buses.size(); busId++) {
        PortBusGroup g;
        g.busId = busId;
        for (int netId : b.buses[busId].members) {
            if (netId < 0 || netId >= (int)netToIdx.size()) continue;
            int idx = netToIdx[netId];
            if (idx >= 0) g.portIdx.push_back(idx);
        }
        if (!g.portIdx.empty()) {
            g.triples.reserve(g.portIdx.size());
            for (int idx : g.portIdx) {
                std::array<int,3> tri = isInput ? colIBU(t, idx) : rowIBU(t, idx);
                g.triples.push_back(tri);
                g.sum[0] += tri[0];
                g.sum[1] += tri[1];
                g.sum[2] += tri[2];
            }
            std::sort(g.triples.begin(), g.triples.end());
            groups.push_back(std::move(g));
        }
    }

    nonBusPorts.clear();
    if (isInput) {
        for (int pi = 0; pi < t.nPI; pi++) {
            int netId = t.piNetIds[pi];
            if (netId >= 0 && netId < (int)b.netToBus.size() && b.netToBus[netId] >= 0) continue;
            nonBusPorts.push_back(pi);
        }
    } else {
        for (int po = 0; po < t.nPO; po++) {
            int netId = t.poNetIds[po];
            if (netId >= 0 && netId < (int)b.netToBus.size() && b.netToBus[netId] >= 0) continue;
            nonBusPorts.push_back(po);
        }
    }
}

// Build adjacency for bus-to-bus matching.
// Candidate edges are restricted to same width and the top-K smallest distances.
static bool buildBusAdj(const std::vector<PortBusGroup>& g1,
                        const std::vector<PortBusGroup>& g2,
                        int topK,
                        std::vector<std::vector<int>>& adj) {
    const int nL = (int)g2.size();
    const int nR = (int)g1.size();
    adj.assign(nL, {});

    if (nL != nR) return false;

    for (int i = 0; i < nL; i++) {
        std::vector<std::pair<int,int>> scored;
        scored.reserve(nR);
        for (int j = 0; j < nR; j++) {
            if (g2[i].portIdx.size() != g1[j].portIdx.size()) continue;
            scored.push_back({busDistL1(g2[i], g1[j]), j});
        }
        if (scored.empty()) return false;

        std::sort(scored.begin(), scored.end(),
                  [](const auto& a, const auto& b){ return a.first < b.first; });

        int keep = std::min(topK, (int)scored.size());
        // keep all ties at boundary
        if (keep < (int)scored.size()) {
            int bound = scored[keep - 1].first;
            while (keep < (int)scored.size() && scored[keep].first == bound) keep++;
        }

        adj[i].reserve(keep);
        for (int k = 0; k < keep; k++) adj[i].push_back(scored[k].second);
        if (adj[i].empty()) return false;
    }
    return true;
}

// Build adjacency for input bus matching allowing Circuit-II buses to be wider.
// Constraint: width(c2bus) must satisfy width(c1bus) <= width(c2bus) <= width(c1bus)+maxExtraBits.
static bool buildBusAdjFlexInput(const std::vector<PortBusGroup>& g1,
                                 const std::vector<PortBusGroup>& g2,
                                 int topK,
                                 int maxExtraBits,
                                 std::vector<std::vector<int>>& adj) {
    const int nL = (int)g2.size();
    const int nR = (int)g1.size();
    adj.assign(nL, {});

    if (nL != nR) return false;

    // Note: the (I,B,U) dominance is a *heuristic*.
    // In real benchmarks, due to unate-table noise/unknowns, strict dominance can prune away
    // the true bus mapping (e.g., case07). Therefore we treat dominance as a *soft penalty*.
    const int domPenalty = 5; // per-count penalty when g2 sum is smaller than g1 sum

    auto distFlexBoth = [](const PortBusGroup& a, const PortBusGroup& b) -> int {
        // Symmetric version of busDistL1Flex: allow either side to be wider.
        if (b.portIdx.size() >= a.portIdx.size()) return busDistL1Flex(a, b);
        return busDistL1Flex(b, a);
    };

    for (int i = 0; i < nL; i++) {
        std::vector<std::pair<int,int>> scored;
        scored.reserve(nR);

        auto pushCandidates = [&](int extraBits, bool directional) {
            for (int j = 0; j < nR; j++) {
                int w2 = (int)g2[i].portIdx.size();
                int w1 = (int)g1[j].portIdx.size();

                if (directional) {
                    // Primary rule: allow Circuit-II bus to be wider (duplicates/const), but not narrower.
                    if (w2 < w1) continue;
                    if (w2 > w1 + extraBits) continue;
                } else {
                    // Fallback: allow either side to be wider if directional yields empty candidates.
                    if (std::abs(w2 - w1) > extraBits) continue;
                }

                int d = directional ? busDistL1Flex(g1[j], g2[i]) : distFlexBoth(g1[j], g2[i]);

                // Soft dominance penalty: prefer sum(g2) >= sum(g1)
                if (g2[i].sum[0] < g1[j].sum[0]) d += (g1[j].sum[0] - g2[i].sum[0]) * domPenalty;
                if (g2[i].sum[1] < g1[j].sum[1]) d += (g1[j].sum[1] - g2[i].sum[1]) * domPenalty;
                if (g2[i].sum[2] < g1[j].sum[2]) d += (g1[j].sum[2] - g2[i].sum[2]) * domPenalty;

                scored.push_back({d, j});
            }
        };

        // First try the requested directional rule.
        pushCandidates(maxExtraBits, true);
        // If empty, relax width direction.
        if (scored.empty()) pushCandidates(maxExtraBits, false);
        // If still empty, last-resort widen slack a bit (prevents hard failure on edge cases).
        if (scored.empty()) pushCandidates(maxExtraBits + 2, false);

        if (scored.empty()) return false;

        std::sort(scored.begin(), scored.end(),
                  [](const auto& a, const auto& b){ return a.first < b.first; });

        int keep = std::min(topK, (int)scored.size());
        if (keep < (int)scored.size()) {
            int bound = scored[keep - 1].first;
            while (keep < (int)scored.size() && scored[keep].first == bound) keep++;
        }
        adj[i].reserve(keep);
        for (int k = 0; k < keep; k++) adj[i].push_back(scored[k].second);
        if (adj[i].empty()) return false;
    }
    return true;
}

static uint64_t hashMatching(const std::vector<int>& matchL) {
    uint64_t h = 1469598103934665603ULL;
    for (int v : matchL) {
        uint64_t x = (uint64_t)(v + 1000003);
        h ^= x;
        h *= 1099511628211ULL;
    }
    return h;
}

// Sample multiple bus matchings by shuffling adjacency lists and running HK.
static std::vector<std::vector<int>> sampleBusMatchings(const std::vector<std::vector<int>>& baseAdj,
                                                        int nR,
                                                        int samples,
                                                        std::mt19937& rng) {
    std::vector<std::vector<int>> out;
    std::unordered_set<uint64_t> seen;

    if (baseAdj.empty()) return out;

    for (int s = 0; s < samples; s++) {
        std::vector<std::vector<int>> adj = baseAdj;
        for (auto& v : adj) {
            if (v.size() > 1) std::shuffle(v.begin(), v.end(), rng);
        }

        std::vector<int> matchL;
        if (!hopcroftKarp(adj, nR, matchL)) break;

        uint64_t h = hashMatching(matchL);
        if (seen.insert(h).second) {
            out.push_back(std::move(matchL));
            if ((int)out.size() >= samples) break;
        }
    }

    return out;
}

// For small bus counts, we can enumerate multiple perfect matchings cheaply.
// This avoids relying on HK randomness (which can still collapse to a single matching
// in some graphs).
static std::vector<std::vector<int>> enumerateBusMatchingsSmall(const std::vector<std::vector<int>>& baseAdj,
                                                                int nR,
                                                                int limit,
                                                                std::mt19937& rng) {
    const int nL = (int)baseAdj.size();
    std::vector<std::vector<int>> out;
    if (nL == 0) return out;
    if (limit <= 0) return out;

    // Left order: smaller degree first (better pruning)
    std::vector<int> order(nL);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return baseAdj[a].size() < baseAdj[b].size();
    });

    std::vector<int> cur(nL, -1);
    std::vector<char> usedR(nR, 0);

    std::function<void(int)> dfs = [&](int idx) {
        if ((int)out.size() >= limit) return;
        if (idx == nL) {
            out.push_back(cur);
            return;
        }
        int u = order[idx];
        if (baseAdj[u].empty()) return;

        // Randomize neighbor order a bit so the first few enumerated matchings are diverse.
        std::vector<int> nbr = baseAdj[u];
        if (nbr.size() > 1) std::shuffle(nbr.begin(), nbr.end(), rng);
        for (int v : nbr) {
            if (v < 0 || v >= nR) continue;
            if (usedR[v]) continue;
            usedR[v] = 1;
            cur[u] = v;
            dfs(idx + 1);
            cur[u] = -1;
            usedR[v] = 0;
            if ((int)out.size() >= limit) return;
        }
    };

    dfs(0);
    return out;
}

// Convert a bus-group-level matching (g2Idx -> g1Idx) into busId mapping (size=b2.buses).
static std::vector<int> toBusIdMap(const BusInfo& b2,
                                   const std::vector<PortBusGroup>& g1,
                                   const std::vector<PortBusGroup>& g2,
                                   const std::vector<int>& matchBus2ToBus1) {
    std::vector<int> busMap((int)b2.buses.size(), -1);
    for (int i = 0; i < (int)g2.size(); i++) {
        int j = matchBus2ToBus1[i];
        if (j < 0 || j >= (int)g1.size()) continue;
        busMap[g2[i].busId] = g1[j].busId;
    }
    return busMap;
}

// Build allowed idx1 candidates for each idx2 based on busId mapping and busedness filter.
static bool buildAllowedByBus(const Circuit& c1,
                              const Circuit& c2,
                              const BusInfo& b1,
                              const BusInfo& b2,
                              const UnateTable& t1,
                              const UnateTable& t2,
                              bool isInput,
                              const std::vector<int>& busIdMap2to1,
                              std::vector<std::vector<int>>& allowedIdx1ForIdx2) {
    (void)c2;

    std::vector<int> netToIdx1(c1.nets.size(), -1);
    if (isInput) {
        for (int i = 0; i < t1.nPI; i++) netToIdx1[t1.piNetIds[i]] = i;
        allowedIdx1ForIdx2.assign(t2.nPI, {});

        std::vector<int> nonBus1;
        nonBus1.reserve(t1.nPI);
        for (int i = 0; i < t1.nPI; i++) {
            int netId = t1.piNetIds[i];
            if (netId >= 0 && netId < (int)b1.netToBus.size() && b1.netToBus[netId] >= 0) continue;
            nonBus1.push_back(i);
        }

        for (int pi2 = 0; pi2 < t2.nPI; pi2++) {
            int net2 = t2.piNetIds[pi2];
            int bus2 = (net2 >= 0 && net2 < (int)b2.netToBus.size()) ? b2.netToBus[net2] : -1;
            if (bus2 < 0) {
                allowedIdx1ForIdx2[pi2] = nonBus1;
                continue;
            }
            if (bus2 >= (int)busIdMap2to1.size()) return false;
            int bus1 = busIdMap2to1[bus2];
            if (bus1 < 0 || bus1 >= (int)b1.buses.size()) return false;
            std::vector<int> cand;
            for (int net1 : b1.buses[bus1].members) {
                if (net1 < 0 || net1 >= (int)netToIdx1.size()) continue;
                int idx1 = netToIdx1[net1];
                if (idx1 >= 0) cand.push_back(idx1);
            }
            allowedIdx1ForIdx2[pi2] = std::move(cand);
            if (allowedIdx1ForIdx2[pi2].empty()) return false;
        }
        return true;
    } else {
        for (int i = 0; i < t1.nPO; i++) netToIdx1[t1.poNetIds[i]] = i;
        allowedIdx1ForIdx2.assign(t2.nPO, {});

        std::vector<int> nonBus1;
        nonBus1.reserve(t1.nPO);
        for (int i = 0; i < t1.nPO; i++) {
            int netId = t1.poNetIds[i];
            if (netId >= 0 && netId < (int)b1.netToBus.size() && b1.netToBus[netId] >= 0) continue;
            nonBus1.push_back(i);
        }

        for (int po2 = 0; po2 < t2.nPO; po2++) {
            int net2 = t2.poNetIds[po2];
            int bus2 = (net2 >= 0 && net2 < (int)b2.netToBus.size()) ? b2.netToBus[net2] : -1;
            if (bus2 < 0) {
                allowedIdx1ForIdx2[po2] = nonBus1;
                continue;
            }
            if (bus2 >= (int)busIdMap2to1.size()) return false;
            int bus1 = busIdMap2to1[bus2];
            if (bus1 < 0 || bus1 >= (int)b1.buses.size()) return false;
            std::vector<int> cand;
            for (int net1 : b1.buses[bus1].members) {
                if (net1 < 0 || net1 >= (int)netToIdx1.size()) continue;
                int idx1 = netToIdx1[net1];
                if (idx1 >= 0) cand.push_back(idx1);
            }
            allowedIdx1ForIdx2[po2] = std::move(cand);
            if (allowedIdx1ForIdx2[po2].empty()) return false;
        }
        return true;
    }
}

// ------------------------
// PO candidate order (bus-aware restriction)
// ------------------------

static vector<vector<int>> buildPOCandidateOrder(const UnateTable& t1,
                                                const UnateTable& t2,
                                                const vector<vector<int>>& allowedPo1,
                                                const vector<PoSig>& poSig1,
                                                const vector<PoSig>& poSig2,
                                                int sigTopK,
                                                std::mt19937& rng) {
    vector<RowFeat> f1(t1.nPO), f2(t2.nPO);

    const vector<string> colKeys1 = computeColReducedKeys(t1);
    const vector<string> colKeys2 = computeColReducedKeys(t2);

    for (int po = 0; po < t1.nPO; ++po) {
        f1[po] = makeRowFeat(t1, po);
        f1[po].refKey = makeRowRefKey(t1, po, colKeys1, f1[po]);
    }
    for (int po = 0; po < t2.nPO; ++po) {
        f2[po] = makeRowFeat(t2, po);
        f2[po].refKey = makeRowRefKey(t2, po, colKeys2, f2[po]);
    }

    const bool hasSig =
        ((int)poSig1.size() == t1.nPO) &&
        ((int)poSig2.size() == t2.nPO) &&
        (!poSig1.empty()) &&
        (poSig1[0].absCorrHist.size() == poSig2[0].absCorrHist.size());

    sigTopK = std::max(4, sigTopK);

    vector<vector<int>> ord(t2.nPO);
    std::uniform_int_distribution<int> tieRand(0, 0x7fff);

    for (int po2 = 0; po2 < t2.nPO; ++po2) {
        // build candidate list
        vector<int> cand;
        if (!allowedPo1.empty() && po2 < (int)allowedPo1.size() && !allowedPo1[po2].empty()) {
            cand = allowedPo1[po2];
        } else {
            cand.resize(t1.nPO);
            std::iota(cand.begin(), cand.end(), 0);
        }

        // whether we have any exact refKey among candidates
        bool hasExactRef = false;
        for (int po1 : cand) {
            if (f1[po1].refKey == f2[po2].refKey) { hasExactRef = true; break; }
        }

        // signature scores + "front" set
        vector<int> sigScoreOf(t1.nPO, 0);
        vector<char> isFront(t1.nPO, 0);
        if (hasSig) {
            vector<std::pair<int,int>> tmp;
            tmp.reserve(cand.size());
            const int b2 = (int)poSig2[po2].bias;
            for (int po1 : cand) {
                const int b1 = (int)poSig1[po1].bias;
                const int biasDiff = std::min(std::abs(b1 - b2), std::abs(b1 - (255 - b2)));
                const int histDiff = l1HistDist(poSig1[po1].absCorrHist, poSig2[po2].absCorrHist);
                const int sc = histDiff * 2 + biasDiff;
                sigScoreOf[po1] = sc;
                tmp.push_back({sc, po1});
            }
            std::sort(tmp.begin(), tmp.end());
            const int best = tmp.empty() ? 0 : tmp.front().first;
            const int slack = 32;
            int picked = 0;
            for (auto& p : tmp) {
                if (picked < sigTopK || p.first <= best + slack) {
                    isFront[p.second] = 1;
                    picked++;
                } else {
                    break;
                }
            }
        }

        vector<std::pair<long long,int>> scored;
        scored.reserve(cand.size());
        for (int po1 : cand) {
            const int rowD = rowFeatDistL1(f1[po1], f2[po2]);
            long long sc = (long long)rowD * 8192LL;

            if (hasSig) {
                sc += (long long)sigScoreOf[po1];
                if (isFront[po1]) sc -= 2000000LL; // soft front-load (HK uses first K)
            }

            if (hasExactRef && f1[po1].refKey == f2[po2].refKey) {
                sc -= (1LL << 40);
            }

            sc += (long long)tieRand(rng);
            scored.push_back({sc, po1});
        }
        std::sort(scored.begin(), scored.end());

        ord[po2].clear();
        ord[po2].reserve(scored.size());
        for (auto& p : scored) ord[po2].push_back(p.second);
    }

    return ord;
}

// ------------------------
// PI candidates from PO map (plus bus restriction)
// ------------------------
static inline char swapPN(char r) {
    r = normRel(r);
    if (r == 'P') return 'N';
    if (r == 'N') return 'P';
    return r;
}

static inline bool relCompatible(char r1, char r2, bool rowFlip) {
    r1 = normRel(r1);
    r2 = normRel(r2);
    if (rowFlip) r2 = swapPN(r2);
    return r1 == r2;
}

static inline bool relCompatibleInv(char r1, char r2, bool rowFlip, bool piFlip) {
    r1 = normRel(r1);
    r2 = normRel(r2);
    // Unknown ('X') is treated as wildcard.
    if (r1 == 'X' || r2 == 'X') return true;
    if (rowFlip ^ piFlip) r2 = swapPN(r2);
    return r1 == r2;
}

static bool buildPiCandidatesFromPoMap(const UnateTable& t1,
                                       const UnateTable& t2,
                                       const vector<int>& poMap,
                                       const vector<char>& rowFlip,
                                       const std::vector<std::vector<int>>& allowedPi1,
                                       vector<vector<int>>& piCand,
                                       vector<vector<uint8_t>>& piCandInvMask) {
    const int nPO = t2.nPO;
    const int nPI2 = t2.nPI;
    const int nPI1 = t1.nPI;

    piCand.assign(nPI2, {});
    piCandInvMask.assign(nPI2, {});
    for (int pi2 = 0; pi2 < nPI2; pi2++) {
        const std::vector<int>* allow = nullptr;
        if (!allowedPi1.empty()) allow = &allowedPi1[pi2];

        vector<int> cand;
        cand.reserve(allow ? allow->size() : (size_t)nPI1);

        vector<uint8_t> invMask;
        invMask.reserve(allow ? allow->size() : (size_t)nPI1);

        auto testOne = [&](int pi1) -> uint8_t {
            bool ok0 = true; // no PI inversion on c2
            bool ok1 = true; // PI inversion on c2
            for (int po2 = 0; po2 < nPO; po2++) {
                int po1 = poMap[po2];
                if (po1 < 0) continue;
                char a = t1.rel[po1][pi1];
                char b = t2.rel[po2][pi2];
                if (ok0 && !relCompatibleInv(a, b, rowFlip[po2], false)) ok0 = false;
                if (ok1 && !relCompatibleInv(a, b, rowFlip[po2], true))  ok1 = false;
                if (!ok0 && !ok1) return 0;
            }
            uint8_t m = 0;
            if (ok0) m |= 1;
            if (ok1) m |= 2;
            return m;
        };

        if (allow) {
            for (int pi1 : *allow) {
                uint8_t m = testOne(pi1);
                if (m) { cand.push_back(pi1); invMask.push_back(m); }
            }
        } else {
            for (int pi1 = 0; pi1 < nPI1; pi1++) {
                uint8_t m = testOne(pi1);
                if (m) { cand.push_back(pi1); invMask.push_back(m); }
            }
        }

        // If strict unate-compatibility yields no candidate, fall back to a soft score
        // (min mismatches over POs). This keeps the solver moving on cases where the
        // unate table is insufficient/noisy. SAT/witness will ultimately validate.
        if (cand.empty()) {
            struct Sc { int bestMis; int pi1; uint8_t mask; };
            std::vector<Sc> scored;
            const std::vector<int>* fallAllow = allow;
            if (fallAllow) scored.reserve(fallAllow->size());
            else scored.reserve((size_t)nPI1);

            auto scoreOne = [&](int pi1) {
                int mis0 = 0, mis1 = 0;
                for (int po2 = 0; po2 < nPO; ++po2) {
                    int po1 = poMap[po2];
                    if (po1 < 0) continue;
                    char a = t1.rel[po1][pi1];
                    char b = t2.rel[po2][pi2];
                    if (!relCompatibleInv(a, b, rowFlip[po2], false)) mis0++;
                    if (!relCompatibleInv(a, b, rowFlip[po2], true))  mis1++;
                }
                int best = std::min(mis0, mis1);
                uint8_t m = 0;
                if (mis0 <= mis1) m |= 1;
                if (mis1 <= mis0) m |= 2;
                scored.push_back({best, pi1, m});
            };

            if (fallAllow) {
                for (int pi1 : *fallAllow) scoreOne(pi1);
            } else {
                for (int pi1 = 0; pi1 < nPI1; ++pi1) scoreOne(pi1);
            }

            std::stable_sort(scored.begin(), scored.end(),
                             [](const Sc& a, const Sc& b){ return a.bestMis < b.bestMis; });

            const int topK = 8;
            int keep = std::min(topK, (int)scored.size());
            if (keep < (int)scored.size()) {
                int bound = scored[keep - 1].bestMis;
                while (keep < (int)scored.size() && scored[keep].bestMis == bound) keep++;
            }
            cand.reserve(keep);
            invMask.reserve(keep);
            for (int i = 0; i < keep; ++i) {
                cand.push_back(scored[i].pi1);
                invMask.push_back(scored[i].mask ? scored[i].mask : 3);
            }
        }

        piCand[pi2] = std::move(cand);
        piCandInvMask[pi2] = std::move(invMask);
    }
    return true;
}


static vector<char> chooseRowFlipHeuristic(const UnateTable& t1,
                                          const UnateTable& t2,
                                          const vector<int>& poMap,
                                          const vector<uint8_t>& poBias1,
                                          const vector<uint8_t>& poBias2) {
    vector<char> flip(t2.nPO, 0);
    for (int po2 = 0; po2 < t2.nPO; po2++) {
        const int po1 = poMap[po2];
        if (po1 < 0) continue;

        int p1 = 0, n1 = 0, b1 = 0, i1 = 0;
        int p2 = 0, n2 = 0, b2 = 0, i2 = 0;

        for (int pi1 = 0; pi1 < t1.nPI; pi1++) {
            const char r = normRel(t1.rel[po1][pi1]);
            if (r == 'P') p1++;
            else if (r == 'N') n1++;
            else if (r == 'B') b1++;
            else i1++;
        }
        for (int pi2 = 0; pi2 < t2.nPI; pi2++) {
            const char r = normRel(t2.rel[po2][pi2]);
            if (r == 'P') p2++;
            else if (r == 'N') n2++;
            else if (r == 'B') b2++;
            else i2++;
        }

        const int dNo = abs(p1 - p2) + abs(n1 - n2) + abs(b1 - b2) + abs(i1 - i2);
        const int dFl = abs(p1 - n2) + abs(n1 - p2) + abs(b1 - b2) + abs(i1 - i2);

        int biasNo = 0, biasFl = 0;
        if (!poBias1.empty() && !poBias2.empty()) {
            const int bb1 = (int)poBias1[po1];
            const int bb2 = (int)poBias2[po2];
            biasNo = abs(bb1 - bb2);
            biasFl = abs(bb1 - (255 - bb2));
        }

        const int scoreNo = dNo * 16 + biasNo;
        const int scoreFl = dFl * 16 + biasFl;
        flip[po2] = (scoreFl < scoreNo) ? 1 : 0;
    }
    return flip;
}

// Build a PI mapping using maximum matching (one-to-one) as a backbone, then
// assign remaining PIs (if |PI2| > |PI1|) by allowing duplicates.
//
// piCand[pi2] is assumed to be ordered from best to worst.
static bool buildPiMapUsingHK(const vector<vector<int>>& piCand,
                              const vector<vector<uint8_t>>& piCandInvMask,
                              int nPI1,
                              std::mt19937& rng,
                              vector<int>& piMap,
                              vector<char>& piFlip) {
    const int nPI2 = (int)piCand.size();
    piMap.assign(nPI2, -1);
    piFlip.assign(nPI2, 0);

    // Build adjacency for HK (cap per PI2 to keep it light).
    const int kAdjCap = 16;
    vector<vector<int>> adj(nPI2);
    adj.reserve(nPI2);
    for (int pi2 = 0; pi2 < nPI2; ++pi2) {
        const auto& c = piCand[pi2];
        int take = std::min((int)c.size(), kAdjCap);
        adj[pi2].assign(c.begin(), c.begin() + take);
        // Shuffle within the cap to explore different max matchings.
        std::shuffle(adj[pi2].begin(), adj[pi2].end(), rng);
    }

    vector<int> matchL, matchR;
    (void)hopcroftKarpMax(adj, nPI1, matchL, &matchR);

    // Use the HK matching as the backbone.
    for (int pi2 = 0; pi2 < nPI2; ++pi2) {
        if (matchL[pi2] >= 0) piMap[pi2] = matchL[pi2];
    }

    // Fill the rest with best candidates (duplicates allowed).
    for (int pi2 = 0; pi2 < nPI2; ++pi2) {
        if (piMap[pi2] >= 0) continue;
        if (!piCand[pi2].empty()) {
            piMap[pi2] = piCand[pi2][0];
        } else {
            piMap[pi2] = -1; // treated as const 0 unless piConstOne is set
        }
    }

    // Decide PI inversion per PI2.
    for (int pi2 = 0; pi2 < nPI2; ++pi2) {
        const int pi1 = piMap[pi2];
        if (pi1 < 0) {
            piFlip[pi2] = 0;
            continue;
        }
        uint8_t m = 0;
        const auto& c = piCand[pi2];
        const auto& im = piCandInvMask[pi2];
        for (int k = 0; k < (int)c.size() && k < (int)im.size(); ++k) {
            if (c[k] == pi1) { m = im[k]; break; }
        }
        if (m == 0) {
            piFlip[pi2] = 0;
        } else if (m == 1) {
            piFlip[pi2] = 0;
        } else if (m == 2) {
            piFlip[pi2] = 1;
        } else {
            // both ok
            piFlip[pi2] = (char)(rng() & 1);
        }
    }
    return true;
}

static bool randomPerfectMatchPI(const vector<vector<int>>& cand,
                                 std::mt19937& rng,
                                 vector<int>& matchPi2ToPi1) {
    const int nPI = (int)cand.size();
    matchPi2ToPi1.assign(nPI, -1);

    vector<vector<int>> shuffled = cand;
    for (int i = 0; i < nPI; i++) std::shuffle(shuffled[i].begin(), shuffled[i].end(), rng);

    vector<int> order(nPI);
    for (int i = 0; i < nPI; i++) order[i] = i;
    std::shuffle(order.begin(), order.end(), rng);

    vector<int> matchR(nPI, -1);

    std::function<bool(int, vector<char>&)> dfs = [&](int u, vector<char>& vis) -> bool {
        for (int v : shuffled[u]) {
            if (vis[v]) continue;
            vis[v] = 1;
            if (matchR[v] == -1 || dfs(matchR[v], vis)) {
                matchR[v] = u;
                matchPi2ToPi1[u] = v;
                return true;
            }
        }
        return false;
    };

    for (int u : order) {
        vector<char> vis(nPI, 0);
        if (!dfs(u, vis)) return false;
    }
    return true;
}

// After choosing a PI permutation (piMap), decide whether each c2 PI should be inverted.
// piCandInvMask[pi2][k] is a bitmask for candidate pi1=piCand[pi2][k]:
//   bit0: ok with no PI inversion, bit1: ok with PI inversion.
static bool derivePiFlipForMap(const vector<vector<int>>& piCand,
                               const vector<vector<uint8_t>>& piCandInvMask,
                               const vector<int>& piMap,
                               vector<char>& piFlip) {
    const int nPI = (int)piMap.size();
    piFlip.assign(nPI, 0);
    for (int pi2 = 0; pi2 < nPI; ++pi2) {
        int pi1 = piMap[pi2];
        uint8_t mask = 0;
        const auto& cand = piCand[pi2];
        const auto& msk  = piCandInvMask[pi2];
        for (size_t i = 0; i < cand.size(); ++i) {
            if (cand[i] == pi1) { mask = msk[i]; break; }
        }
        if (mask == 0) return false;
        if (mask & 1) piFlip[pi2] = 0;        // prefer non-inverted if possible
        else if (mask & 2) piFlip[pi2] = 1;
        else return false;
    }
    return true;
}

static MatchResult makeGuessFromMaps(const UnateTable& t1,
                                    const UnateTable& t2,
                                    const vector<int>& poMap,
                                    const vector<char>& rowFlip,
                                    const vector<int>& piMap,
                                    const vector<char>& piFlip,
                                    const vector<char>& piConstOne) {
    MatchResult r;
    r.success = false;

    r.piPairs.reserve(t2.nPI);
    r.constBinds.reserve(8);
    for (int pi2 = 0; pi2 < t2.nPI; pi2++) {
        int pi1 = piMap[pi2];
        if (pi1 >= 0) {
            InPortMatch ip;
            ip.c1_pi = t1.piNetIds[pi1];
            ip.c2_pi = t2.piNetIds[pi2];
            ip.c2_neg = (piFlip[pi2] != 0);
            r.piPairs.push_back(ip);
        } else {
            ConstBind cb;
            cb.c2_pi = t2.piNetIds[pi2];
            // Input inversion flips constants too.
            bool v = (piConstOne[pi2] != 0);
            if (piFlip[pi2]) v = !v;
            cb.bind_one = v;
            r.constBinds.push_back(cb);
        }
    }

    r.poPairs.reserve(t2.nPO);
    for (int po2 = 0; po2 < t2.nPO; po2++) {
        int po1 = poMap[po2];
        OutPortMatch op;
        op.c1_po = t1.poNetIds[po1];
        op.c2_po = t2.poNetIds[po2];
        op.c2_neg = (rowFlip[po2] != 0);
        r.poPairs.push_back(op);
    }
return r;
}


    



// ------------------------
// Small-case brute force + SAT
//   For tiny benchmarks (e.g., case1), enumerate ALL legal bus-consistent mappings
//   (including bit permutations and per-bit inversions), then SAT-check.
//   This is meant to be a *simple* and *complete* solver for small instances.
// ------------------------
static inline uint64_t cappedMul(uint64_t a, uint64_t b, uint64_t cap) {
    if (a == 0 || b == 0) return 0;
    if (a > cap / b) return cap;
    return a * b;
}

static inline uint64_t factU64(int n, uint64_t cap) {
    uint64_t v = 1;
    for (int i = 2; i <= n; ++i) {
        v = cappedMul(v, (uint64_t)i, cap);
        if (v >= cap) return cap;
    }
    return v;
}

static inline uint64_t pow2U64(int k, uint64_t cap) {
    if (k <= 0) return 1;
    uint64_t v = 1;
    for (int i = 0; i < k; ++i) {
        v = cappedMul(v, 2, cap);
        if (v >= cap) return cap;
    }
    return v;
}

static bool isBruteforceSmallCaseFeasible(const UnateTable& t1,
                                         const UnateTable& t2,
                                         const std::vector<PortBusGroup>& inB1,
                                         const std::vector<PortBusGroup>& inB2,
                                         const std::vector<int>& inNon1,
                                         const std::vector<int>& inNon2,
                                         const std::vector<PortBusGroup>& outB1,
                                         const std::vector<PortBusGroup>& outB2,
                                         const std::vector<int>& outNon1,
                                         const std::vector<int>& outNon2,
                                         uint64_t& estCombos) {
    // Hard requirement for completeness with our brute strategy:
    // - same number of PIs and POs
    // - same number of non-bus ports (so we can enumerate bijections)
    // - bus counts already matched before calling this
    if (t1.nPO != t2.nPO) return false;
    if (t1.nPI != t2.nPI) return false;
    if (inNon1.size() != inNon2.size()) return false;
    if (outNon1.size() != outNon2.size()) return false;

    // Require width multisets to match for input/output buses.
    auto widths = [](const std::vector<PortBusGroup>& g) {
        std::vector<int> w;
        w.reserve(g.size());
        for (const auto& x : g) w.push_back((int)x.portIdx.size());
        std::sort(w.begin(), w.end());
        return w;
    };
    if (widths(inB1) != widths(inB2)) return false;
    if (widths(outB1) != widths(outB2)) return false;

    // Estimate total combos (cap at kCap+1).
    const uint64_t kCap = 300000; // keeps SAT calls bounded
    uint64_t cap = kCap + 1;

    auto busPart = [&](const std::vector<PortBusGroup>& g) -> uint64_t {
        uint64_t ways = factU64((int)g.size(), cap);
        for (const auto& bg : g) {
            const int w = (int)bg.portIdx.size();
            ways = cappedMul(ways, factU64(w, cap), cap);
            ways = cappedMul(ways, pow2U64(w, cap), cap);
            if (ways >= cap) return cap;
        }
        return ways;
    };

    uint64_t inWays  = busPart(inB2);
    inWays = cappedMul(inWays, factU64((int)inNon2.size(), cap), cap);
    inWays = cappedMul(inWays, pow2U64((int)inNon2.size(), cap), cap);

    uint64_t outWays = busPart(outB2);
    outWays = cappedMul(outWays, factU64((int)outNon2.size(), cap), cap);
    outWays = cappedMul(outWays, pow2U64((int)outNon2.size(), cap), cap);

    estCombos = cappedMul(inWays, outWays, cap);
    if (estCombos > kCap) return false;
    return true;
}

static MatchResult solveSmallCaseBruteforceSAT(const Circuit& c1,
                                              const Circuit& c2,
                                              const UnateTable& t1,
                                              const UnateTable& t2,
                                              const std::vector<PortBusGroup>& inB1,
                                              const std::vector<PortBusGroup>& inB2,
                                              const std::vector<int>& inNon1,
                                              const std::vector<int>& inNon2,
                                              const std::vector<PortBusGroup>& outB1,
                                              const std::vector<PortBusGroup>& outB2,
                                              const std::vector<int>& outNon1,
                                              const std::vector<int>& outNon2,
                                              uint64_t estCombos) {
    MatchResult best;
    best.success = false;
    best.msg = "Small-case brute: exhausted all mappings (no SAT-UNSAT found).";

    std::vector<int>  piMap(t2.nPI, -1);
    std::vector<char> piFlip(t2.nPI, 0);
    std::vector<char> piConstOne(t2.nPI, 0);

    std::vector<int>  poMap(t2.nPO, -1);
    std::vector<char> rowFlip(t2.nPO, 0);

    uint64_t tried = 0;
    const uint64_t kLogEvery = 2000;

    auto trySAT = [&]() -> bool {
        MatchResult g = makeGuessFromMaps(t1, t2, poMap, rowFlip, piMap, piFlip, piConstOne);
        SatCheckResult satRes = checkMatchBySAT(c1, c2, g);
        tried++;
        if (tried % kLogEvery == 0) {
            std::cerr << "[SmallCase] tried=" << tried;
            if (estCombos) std::cerr << "/" << estCombos;
            std::cerr << " lastBadPo=(" << satRes.badPoC1 << "," << satRes.badPoC2 << ")\n";
        }
        if (satRes.success) {
            g.success = true;
            g.msg = "Small-case brute SAT success (tried=" + std::to_string(tried) + ")";
            best = g;
            return true;
        }
        return false;
    };

    // ---- Enumerate OUTPUT mappings, given fixed INPUT mapping
    auto enumOutputAll = [&](auto&& self) -> bool {
        const int nBus = (int)outB2.size();
        std::vector<int> permBus(nBus);
        std::iota(permBus.begin(), permBus.end(), 0);
        if (!permBus.empty()) std::sort(permBus.begin(), permBus.end());

        do {
            bool widthOk = true;
            for (int i = 0; i < nBus; ++i) {
                if (outB2[i].portIdx.size() != outB1[permBus[i]].portIdx.size()) { widthOk = false; break; }
            }
            if (!widthOk) continue;

            // recurse bus-by-bus for bit permutations / inversions
            std::function<bool(int)> recBus;
            recBus = [&](int bi) -> bool {
                if (bi == nBus) {
                    // non-bus outputs: bijection + inversion
                    std::vector<int> p1 = outNon1;
                    std::sort(p1.begin(), p1.end());
                    do {
                        const int nN = (int)outNon2.size();
                        const uint64_t mlim = (nN >= 63) ? 0 : (1ULL << nN);
                        const uint64_t masks = (nN == 0) ? 1ULL : mlim;
                        for (uint64_t m = 0; m < masks; ++m) {
                            for (int k = 0; k < nN; ++k) {
                                int po2 = outNon2[k];
                                int po1 = p1[k];
                                poMap[po2] = po1;
                                rowFlip[po2] = (char)((m >> k) & 1ULL);
                            }
                            if (trySAT()) return true;
                        }
                    } while (std::next_permutation(p1.begin(), p1.end()));
                    // cleanup non-bus
                    for (int po2 : outNon2) { poMap[po2] = -1; rowFlip[po2] = 0; }
                    return false;
                }

                const auto& g2 = outB2[bi];
                const auto& g1 = outB1[permBus[bi]];
                const int w = (int)g2.portIdx.size();

                std::vector<int> bits1 = g1.portIdx;
                std::sort(bits1.begin(), bits1.end());
                do {
                    const uint64_t masks = (w == 0) ? 1ULL : (1ULL << w);
                    for (uint64_t m = 0; m < masks; ++m) {
                        for (int k = 0; k < w; ++k) {
                            int po2 = g2.portIdx[k];
                            int po1 = bits1[k];
                            poMap[po2] = po1;
                            rowFlip[po2] = (char)((m >> k) & 1ULL);
                        }
                        if (recBus(bi + 1)) return true;
                    }
                } while (std::next_permutation(bits1.begin(), bits1.end()));

                // cleanup this bus
                for (int po2 : g2.portIdx) { poMap[po2] = -1; rowFlip[po2] = 0; }
                return false;
            };

            if (recBus(0)) return true;
        } while (std::next_permutation(permBus.begin(), permBus.end()));

        return false;
    };

    // ---- Enumerate INPUT mappings
    const int nBusIn = (int)inB2.size();
    std::vector<int> permIn(nBusIn);
    std::iota(permIn.begin(), permIn.end(), 0);
    if (!permIn.empty()) std::sort(permIn.begin(), permIn.end());

    do {
        bool widthOk = true;
        for (int i = 0; i < nBusIn; ++i) {
            if (inB2[i].portIdx.size() != inB1[permIn[i]].portIdx.size()) { widthOk = false; break; }
        }
        if (!widthOk) continue;

        std::function<bool(int)> recInBus;
        recInBus = [&](int bi) -> bool {
            if (bi == nBusIn) {
                // non-bus inputs: bijection + inversion
                std::vector<int> p1 = inNon1;
                std::sort(p1.begin(), p1.end());
                do {
                    const int nN = (int)inNon2.size();
                    const uint64_t masks = (nN == 0) ? 1ULL : (1ULL << nN);
                    for (uint64_t m = 0; m < masks; ++m) {
                        for (int k = 0; k < nN; ++k) {
                            int pi2 = inNon2[k];
                            int pi1 = p1[k];
                            piMap[pi2] = pi1;
                            piFlip[pi2] = (char)((m >> k) & 1ULL);
                        }
                        if (enumOutputAll(enumOutputAll)) return true;
                    }
                } while (std::next_permutation(p1.begin(), p1.end()));
                // cleanup non-bus
                for (int pi2 : inNon2) { piMap[pi2] = -1; piFlip[pi2] = 0; }
                return false;
            }

            const auto& g2 = inB2[bi];
            const auto& g1 = inB1[permIn[bi]];
            const int w = (int)g2.portIdx.size();

            std::vector<int> bits1 = g1.portIdx;
            std::sort(bits1.begin(), bits1.end());
            do {
                const uint64_t masks = (w == 0) ? 1ULL : (1ULL << w);
                for (uint64_t m = 0; m < masks; ++m) {
                    for (int k = 0; k < w; ++k) {
                        int pi2 = g2.portIdx[k];
                        int pi1 = bits1[k];
                        piMap[pi2] = pi1;
                        piFlip[pi2] = (char)((m >> k) & 1ULL);
                    }
                    if (recInBus(bi + 1)) return true;
                }
            } while (std::next_permutation(bits1.begin(), bits1.end()));

            // cleanup this bus
            for (int pi2 : g2.portIdx) { piMap[pi2] = -1; piFlip[pi2] = 0; }
            return false;
        };

        if (recInBus(0)) return best; // found
    } while (std::next_permutation(permIn.begin(), permIn.end()));

    // If we reach here, nothing worked.
    best.success = false;
    best.msg = "Small-case brute: exhausted all mappings (tried=" + std::to_string(tried) + ")";
    return best;
}

// ------------------------
// Timeout fallback: build a *very fast* guess without witness/SAT.
// Uses bus-only allowed sets + (I,B,U) nearest-neighbor style scoring.
// ------------------------
static MatchResult makeIBUQuickGuessFromAllowed(const UnateTable& t1,
                                               const UnateTable& t2,
                                               const std::vector<std::vector<int>>& allowedPo1,
                                               const std::vector<std::vector<int>>& allowedPi1,
                                               std::mt19937& rng) {
    const int nPO = t2.nPO;
    const int nPI2 = t2.nPI;

    // (1) PO mapping: perfect matching with candidates ordered by row (I,B,U).
    std::vector<std::array<int,3>> poIBU1(t1.nPO), poIBU2(t2.nPO);
    for (int i = 0; i < t1.nPO; ++i) poIBU1[i] = rowIBU(t1, i);
    for (int i = 0; i < t2.nPO; ++i) poIBU2[i] = rowIBU(t2, i);

    std::vector<int> poMap(nPO, -1);
    std::vector<char> rowFlip(nPO, 0); // keep polarity neutral in fallback

    auto buildAdjPO = [&](int cap, std::vector<std::vector<int>>& adj) {
        adj.assign(nPO, {});
        std::uniform_int_distribution<int> tie(0, 0x7fffffff);
        for (int po2 = 0; po2 < nPO; ++po2) {
            const auto& cand = allowedPo1[po2];
            std::vector<std::pair<int,int>> scored;
            scored.reserve(cand.size());
            for (int po1 : cand) {
                int d = l1Dist3(poIBU1[po1], poIBU2[po2]);
                d = d * 1024 + (tie(rng) & 1023); // random tie-break
                scored.push_back({d, po1});
            }
            if (scored.empty()) continue;
            if (cap > 0 && (int)scored.size() > cap) {
                std::nth_element(scored.begin(), scored.begin() + cap, scored.end(),
                                 [](const auto& a, const auto& b){ return a.first < b.first; });
                scored.resize(cap);
            }
            std::sort(scored.begin(), scored.end(),
                      [](const auto& a, const auto& b){ return a.first < b.first; });
            adj[po2].reserve(scored.size());
            for (auto& it : scored) adj[po2].push_back(it.second);
        }
    };

    std::vector<std::vector<int>> adjPO;
    bool okPO = false;
    const int caps[] = {4, 8, 16, 32, 64, 0}; // 0 => full
    for (int cap0 : caps) {
        int cap = cap0;
        if (cap == 0) cap = 1000000000; // effectively no cap
        buildAdjPO(cap, adjPO);
        if (hopcroftKarp(adjPO, t1.nPO, poMap)) { okPO = true; break; }
    }

    if (!okPO) {
        // Last resort: greedy permutation (still attempts to respect allowed sets).
        std::vector<char> used(t1.nPO, 0);
        for (int po2 = 0; po2 < nPO; ++po2) {
            int pick = -1;
            int bestD = 1000000000;
            for (int po1 : allowedPo1[po2]) {
                if (used[po1]) continue;
                int d = l1Dist3(poIBU1[po1], poIBU2[po2]);
                if (d < bestD) { bestD = d; pick = po1; }
            }
            if (pick < 0) {
                for (int po1 = 0; po1 < t1.nPO; ++po1) if (!used[po1]) { pick = po1; break; }
            }
            if (pick < 0) pick = 0;
            used[pick] = 1;
            poMap[po2] = pick;
        }
    }

    // (2) PI mapping (many-to-one allowed): nearest by column (I,B,U) within allowed set.
    const auto piIBU1 = allColIBU(t1);
    const auto piIBU2 = allColIBU(t2);

    std::vector<int>  piMap(nPI2, -1);
    std::vector<char> piFlip(nPI2, 0);
    std::vector<char> piConstOne(nPI2, 0);

    for (int pi2 = 0; pi2 < nPI2; ++pi2) {
        const auto& cand = allowedPi1[pi2];
        int bestPi1 = -1;
        int bestD = 1000000000;
        for (int pi1 : cand) {
            int d = l1Dist3(piIBU1[pi1], piIBU2[pi2]);
            if (d < bestD) { bestD = d; bestPi1 = pi1; }
        }
        piMap[pi2] = bestPi1;
        // Random polarity: sometimes helps by chance; format legality unaffected.
        piFlip[pi2] = (char)(rng() & 1);
        if (bestPi1 < 0) {
            // Shouldn't happen with bus-only allowed sets; tie to constant 0.
            piFlip[pi2] = 0;
            piConstOne[pi2] = 0;
        }
    }

    return makeGuessFromMaps(t1, t2, poMap, rowFlip, piMap, piFlip, piConstOne);
}

// ------------------------
// Witness pruning (CEGIS)
// ------------------------
struct WitnessBits {
    // One counterexample assignment on c1 PIs (0/1 per PI).
    std::vector<uint8_t> piVal1;
};

static inline uint64_t hashWitnessBits(const WitnessBits& w) {
    // FNV-1a on the PI bitvector.
    uint64_t h = 1469598103934665603ULL;
    for (uint8_t b : w.piVal1) {
        h ^= (uint64_t)(b & 1U);
        h *= 1099511628211ULL;
    }
    return h;
}

static WitnessBits buildWitnessFromSat(const Circuit& c1,
                                      const std::unordered_map<std::string,int>& c1PiWitness) {
    WitnessBits w;
    const int nPI = (int)c1.PIs.size();
    w.piVal1.assign(nPI, 0);

    std::unordered_map<std::string,int> piIdxByName;
    piIdxByName.reserve((size_t)nPI * 2);
    for (int i = 0; i < nPI; ++i) {
        int netId = c1.PIs[i];
        piIdxByName[c1.nets[netId].name] = i;
    }

    for (const auto& kv : c1PiWitness) {
        auto it = piIdxByName.find(kv.first);
        if (it == piIdxByName.end()) continue;
        w.piVal1[it->second] = (uint8_t)(kv.second ? 1 : 0);
    }
    return w;
}

static inline uint64_t randU64(std::mt19937& rng) {
    // mt19937 is 32-bit output; combine two draws.
    uint64_t hi = (uint64_t)rng();
    uint64_t lo = (uint64_t)rng();
    return (hi << 32) | lo;
}

// ------------------------
// Influence signatures (PI->PO correlation)
// ------------------------
// For each PI and each PO, compute an inversion-invariant correlation score:
//   score = | P(PO=1 | PI=1) - P(PO=1 | PI=0) |
// This is invariant under:
//   * PO inversion (PO -> !PO): probabilities complement, difference unchanged.
//   * PI inversion (PI -> !PI): swaps conditions, absolute value unchanged.
// It is also insensitive to PO ordering if we only use permutation-invariant
// summaries (coarse filter).
static std::vector<std::vector<uint8_t>> computeInfluenceQuant(const Circuit& c,
                                                               const std::vector<int>& topo,
                                                               std::mt19937& rng,
                                                               int batches) {
    const int nPI = (int)c.PIs.size();
    const int nPO = (int)c.POs.size();
    std::vector<std::vector<uint32_t>> cnt11(nPI, std::vector<uint32_t>(nPO, 0));
    std::vector<std::vector<uint32_t>> cnt01(nPI, std::vector<uint32_t>(nPO, 0));
    std::vector<uint32_t> cntPi1(nPI, 0);
    std::vector<uint32_t> cntPi0(nPI, 0);

    for (int b = 0; b < batches; ++b) {
        std::vector<uint64_t> piMask(nPI);
        for (int i = 0; i < nPI; ++i) piMask[i] = randU64(rng);
        const std::vector<uint64_t> out = simulateCircuit64(c, topo, piMask, c.POs);
        for (int i = 0; i < nPI; ++i) {
            const uint64_t m1 = piMask[i];
            const uint64_t m0 = ~m1;
            cntPi1[i] += (uint32_t)__builtin_popcountll(m1);
            cntPi0[i] += (uint32_t)__builtin_popcountll(m0);
            for (int po = 0; po < nPO; ++po) {
                const uint64_t o = out[po];
                cnt11[i][po] += (uint32_t)__builtin_popcountll(o & m1);
                cnt01[i][po] += (uint32_t)__builtin_popcountll(o & m0);
            }
        }
    }

    // Quantize to 0..255 per (pi,po)
    std::vector<std::vector<uint8_t>> q(nPI, std::vector<uint8_t>(nPO, 0));
    for (int i = 0; i < nPI; ++i) {
        const double d1 = std::max(1u, cntPi1[i]);
        const double d0 = std::max(1u, cntPi0[i]);
        for (int po = 0; po < nPO; ++po) {
            const uint32_t c11 = cnt11[i][po];
            const uint32_t c01 = cnt01[i][po];
            const double p1 = (double)c11 / d1;
            const double p0 = (double)c01 / d0;
            const double diff = std::abs(p1 - p0);
            int v = (int)std::lround(diff * 255.0);
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            q[i][po] = (uint8_t)v;
        }
    }
    return q;
}

struct InflCoarseFeat {
    uint16_t top4[4];
    uint16_t sum;
    uint16_t nz;
    uint16_t hist8[8];
};

static InflCoarseFeat makeInflCoarseFeat(const std::vector<uint8_t>& v) {
    InflCoarseFeat f{};
    f.sum = 0;
    f.nz = 0;
    for (int b = 0; b < 8; ++b) f.hist8[b] = 0;

    // top4 selection (small nPO so simple sort is fine)
    std::vector<uint16_t> tmp;
    tmp.reserve(v.size());
    for (uint8_t x : v) {
        f.sum = (uint16_t)(f.sum + x);
        if (x) f.nz++;
        f.hist8[x >> 5]++; // 0..7
        tmp.push_back((uint16_t)x);
    }
    std::sort(tmp.begin(), tmp.end(), std::greater<uint16_t>());
    for (int i = 0; i < 4; ++i) f.top4[i] = (i < (int)tmp.size()) ? tmp[i] : 0;
    return f;
}

static inline int inflCoarseDist(const InflCoarseFeat& a, const InflCoarseFeat& b) {
    int d = 0;
    d += std::abs((int)a.sum - (int)b.sum);
    d += std::abs((int)a.nz  - (int)b.nz) * 8;
    for (int i = 0; i < 4; ++i) d += std::abs((int)a.top4[i] - (int)b.top4[i]);
    for (int i = 0; i < 8; ++i) d += std::abs((int)a.hist8[i] - (int)b.hist8[i]) * 4;
    return d;
}

// Coarse prune (PO-permutation invariant): keep only topK PI1 candidates per PI2.
static void coarsePrunePiAllowed(const std::vector<InflCoarseFeat>& feat1,
                                const std::vector<InflCoarseFeat>& feat2,
                                int topK,
                                std::vector<std::vector<int>>& allowedPi1) {
    if (topK <= 0) return;
    for (int pi2 = 0; pi2 < (int)allowedPi1.size(); ++pi2) {
        auto& cand = allowedPi1[pi2];
        if ((int)cand.size() <= topK) continue;
        std::vector<std::pair<int,int>> scored;
        scored.reserve(cand.size());
        for (int pi1 : cand) {
            scored.push_back({inflCoarseDist(feat1[pi1], feat2[pi2]), pi1});
        }
        std::sort(scored.begin(), scored.end(), [](const auto& x, const auto& y){
            if (x.first != y.first) return x.first < y.first;
            return x.second < y.second;
        });
        int keep = std::min(topK, (int)scored.size());
        if (keep < (int)scored.size()) {
            int bound = scored[keep-1].first;
            while (keep < (int)scored.size() && scored[keep].first == bound) keep++;
        }
        cand.clear();
        cand.reserve(keep);
        for (int i = 0; i < keep; ++i) cand.push_back(scored[i].second);
    }
}

// Fine prune (PO-aligned): keep topK by aligned L1 distance under a PO mapping.
static void finePrunePiCandByInfluence(const std::vector<std::vector<uint8_t>>& infl1,
                                      const std::vector<std::vector<uint8_t>>& infl2,
                                      const std::vector<int>& poMap,
                                      int topK,
                                      std::vector<std::vector<int>>& piCand,
                                      std::vector<std::vector<uint8_t>>& piCandInvMask) {
    if (topK <= 0) return;
    const int nPO2 = (int)poMap.size();
    for (int pi2 = 0; pi2 < (int)piCand.size(); ++pi2) {
        auto& cand = piCand[pi2];
        auto& msk  = piCandInvMask[pi2];
        if ((int)cand.size() <= topK) continue;
        std::vector<std::tuple<int,int,uint8_t>> scored; // dist, pi1, mask
        scored.reserve(cand.size());
        for (int k = 0; k < (int)cand.size(); ++k) {
            const int pi1 = cand[k];
            int d = 0;
            for (int po2 = 0; po2 < nPO2; ++po2) {
                const int po1 = poMap[po2];
                if (po1 < 0) continue;
                d += std::abs((int)infl1[pi1][po1] - (int)infl2[pi2][po2]);
            }
            scored.emplace_back(d, pi1, msk[k]);
        }
        std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b){
            if (std::get<0>(a) != std::get<0>(b)) return std::get<0>(a) < std::get<0>(b);
            return std::get<1>(a) < std::get<1>(b);
        });
        int keep = std::min(topK, (int)scored.size());
        if (keep < (int)scored.size()) {
            int bound = std::get<0>(scored[keep-1]);
            while (keep < (int)scored.size() && std::get<0>(scored[keep]) == bound) keep++;
        }
        cand.clear();
        msk.clear();
        cand.reserve(keep);
        msk.reserve(keep);
        for (int i = 0; i < keep; ++i) {
            cand.push_back(std::get<1>(scored[i]));
            msk.push_back(std::get<2>(scored[i]));
        }
    }
}


static vector<uint8_t> computePoBiasQuant(const Circuit& c,
                                         const vector<int>& topo,
                                         std::mt19937& rng,
                                         int batches) {
    const int nPI = (int)c.PIs.size();
    const int nPO = (int)c.POs.size();
    vector<uint64_t> ones(nPO, 0);

    for (int b = 0; b < batches; b++) {
        vector<uint64_t> piMask(nPI);
        for (int i = 0; i < nPI; i++) piMask[i] = randU64(rng);
        const vector<uint64_t> out = simulateCircuit64(c, topo, piMask, c.POs);
        for (int po = 0; po < nPO; po++) ones[po] += (uint64_t)__builtin_popcountll(out[po]);
    }

    const double denom = 64.0 * (double)batches;
    vector<uint8_t> q(nPO, 0);
    for (int po = 0; po < nPO; po++) {
        const double p1 = (denom > 0.0) ? ((double)ones[po] / denom) : 0.0;
        int qi = (int)llround(p1 * 255.0);
        if (qi < 0) qi = 0;
        if (qi > 255) qi = 255;
        q[po] = (uint8_t)qi;
    }
    return q;
}


// Compute a PO signature: (bias + abs-correlation histogram vs. other POs).
// abs-corr is estimated from random simulation: |P(eq) - P(neq)| = |1 - 2*P(neq)|.
static std::vector<PoSig> computePoSigQuant(const Circuit& c,
                                            const vector<int>& topo,
                                            std::mt19937& rng,
                                            int batches,
                                            int bins) {
    const int nPO = (int)c.POs.size();
    const int nPI = (int)c.PIs.size();
    if (nPO == 0) return {};

    batches = std::max(1, batches);
    bins = std::max(4, bins);

    std::vector<std::vector<uint64_t>> outBits(nPO, std::vector<uint64_t>(batches, 0));
    std::vector<uint32_t> ones(nPO, 0);

    std::uniform_int_distribution<uint64_t> dist(0, ~0ull);
    std::vector<uint64_t> piMask(nPI);

    for (int b = 0; b < batches; ++b) {
        for (int i = 0; i < nPI; ++i) piMask[i] = dist(rng);
        std::vector<uint64_t> out = simulateCircuit64(c, topo, piMask, c.POs);
        for (int po = 0; po < nPO; ++po) {
            outBits[po][b] = out[po];
            ones[po] += (uint32_t)__builtin_popcountll(out[po]);
        }
    }

    const int totalBits = 64 * batches;

    std::vector<PoSig> sig(nPO);
    for (int po = 0; po < nPO; ++po) {
        int q = (int)(((uint64_t)ones[po] * 255ull + (uint64_t)totalBits / 2ull) / (uint64_t)totalBits);
        if (q < 0) q = 0;
        if (q > 255) q = 255;
        sig[po].bias = (uint8_t)q;
        sig[po].absCorrHist.assign((size_t)bins, 0);
    }

    if (nPO <= 1) return sig;

    std::vector<std::vector<int>> histCnt(nPO, std::vector<int>(bins, 0));
    for (int i = 0; i < nPO; ++i) {
        for (int j = 0; j < nPO; ++j) {
            if (i == j) continue;
            int diff = 0;
            for (int b = 0; b < batches; ++b) {
                diff += __builtin_popcountll(outBits[i][b] ^ outBits[j][b]);
            }
            long long d = (long long)totalBits - 2LL * (long long)diff;
            if (d < 0) d = -d;
            int absQ = (int)((d * 255LL) / (long long)totalBits);
            if (absQ < 0) absQ = 0;
            if (absQ > 255) absQ = 255;
            int bin = (absQ * bins) >> 8; // /256
            if (bin >= bins) bin = bins - 1;
            histCnt[i][bin] += 1;
        }
    }

    for (int i = 0; i < nPO; ++i) {
        for (int k = 0; k < bins; ++k) {
            int q = (int)(((long long)histCnt[i][k] * 255LL + (long long)(nPO - 1) / 2LL) / (long long)(nPO - 1));
            if (q < 0) q = 0;
            if (q > 255) q = 255;
            sig[i].absCorrHist[k] = (uint8_t)q;
        }
    }

    return sig;
}



 

// Fast randomized simulation filter:
// - Simulate 64 random patterns at once.
// - If any PO mismatch under the proposed (poMap,piMap,rowFlip), extract ONE failing bit
//   and add it as a new witness (single assignment) to prune future guesses.
// Returns true if the mapping passes this random simulation batch.
static bool passesRandomSimOrAddWitness(const Circuit& c1,
                                       const Circuit& c2,
                                       const vector<int>& topo1,
                                       const vector<int>& topo2,
                                       const vector<int>& poMap,
                                       const vector<int>& piMap,
                                       const vector<char>& rowFlip,
                                       const vector<char>& piFlip,
                                       const vector<char>& piConstOne,
                                       std::mt19937& rng,
                                       vector<WitnessBits>& witnesses,
                                       std::unordered_set<uint64_t>& witnessHash,
                                       int maxWitnesses) {
    const int nPI1 = (int)c1.PIs.size();
    const int nPI2 = (int)c2.PIs.size();
    const int nPO2 = (int)c2.POs.size();

    // Random 64-pattern PI masks on circuit1.
    vector<uint64_t> piMask1(nPI1, 0);
    for (int i = 0; i < nPI1; ++i) piMask1[i] = randU64(rng);

    // Derive circuit2 PI masks by PI mapping.
    vector<uint64_t> piMask2(nPI2, 0);
    for (int pi2 = 0; pi2 < nPI2; ++pi2) {
        int pi1 = piMap[pi2];
        uint64_t m = 0;
        if (pi1 >= 0) {
            m = piMask1[pi1];
        } else {
            m = piConstOne[pi2] ? ~0ULL : 0ULL;
        }
        if (piFlip[pi2]) m = ~m;
        piMask2[pi2] = m;
    }

    vector<uint64_t> out1 = simulateCircuit64(c1, topo1, piMask1, c1.POs);
    vector<uint64_t> out2 = simulateCircuit64(c2, topo2, piMask2, c2.POs);

    for (int po2 = 0; po2 < nPO2; ++po2) {
        int po1 = poMap[po2];
        uint64_t v1 = out1[po1];
        uint64_t v2 = out2[po2];
        if (rowFlip[po2]) v2 = ~v2;
        uint64_t diff = (v1 ^ v2);
        if (diff == 0) continue;

        // Extract one failing bit position.
        int bit = __builtin_ctzll(diff);
        WitnessBits w;
        w.piVal1.assign(nPI1, 0);
        for (int i = 0; i < nPI1; ++i) {
            w.piVal1[i] = (uint8_t)((piMask1[i] >> bit) & 1ULL);
        }

        if ((int)witnesses.size() < maxWitnesses) {
            uint64_t hw = hashWitnessBits(w);
            if (witnessHash.insert(hw).second) {
                witnesses.push_back(std::move(w));
                std::cerr << "[UnateBus] add SIM-witness#" << witnesses.size()
                          << " badPo=(" << c1.nets[c1.POs[po1]].name
                          << "," << c2.nets[c2.POs[po2]].name << ")\n";
            }
        }
        return false;
    }
    return true;
}

static bool passesAllWitnessesBatched(const Circuit& c1,
                                      const Circuit& c2,
                                      const vector<int>& topo1,
                                      const vector<int>& topo2,
                                      const vector<int>& poMap,
                                      const vector<int>& piMap,
                                      const vector<char>& rowFlip,
                                      const vector<char>& piFlip,
                                      const vector<char>& piConstOne,
                                      const vector<WitnessBits>& witnesses) {
    if (witnesses.empty()) return true;

    const int nPO  = (int)c2.POs.size();
    const int nPI1 = (int)c1.PIs.size();
    const int nPI2 = (int)c2.PIs.size();

    // Pack up to 64 witnesses into one 64-bit simulation batch.
    for (size_t base = 0; base < witnesses.size(); base += 64) {
        const size_t cnt = std::min<size_t>(64, witnesses.size() - base);
        const uint64_t activeMask = (cnt == 64) ? ~0ULL : ((1ULL << cnt) - 1ULL);

        vector<uint64_t> piMask1(nPI1, 0);
        for (int pi1 = 0; pi1 < nPI1; ++pi1) {
            uint64_t m = 0;
            for (size_t k = 0; k < cnt; ++k) {
                if (witnesses[base + k].piVal1[pi1] & 1U) m |= (1ULL << k);
            }
            piMask1[pi1] = m;
        }

        vector<uint64_t> piMask2(nPI2, 0);
        for (int pi2 = 0; pi2 < nPI2; ++pi2) {
            int pi1 = piMap[pi2];
            uint64_t m = 0;
            if (pi1 >= 0) {
                m = piMask1[pi1];
            } else {
                m = piConstOne[pi2] ? activeMask : 0ULL;
            }
            if (piFlip[pi2]) m = ~m;
            piMask2[pi2] = m;
        }

        vector<uint64_t> out1 = simulateCircuit64(c1, topo1, piMask1, c1.POs);
        vector<uint64_t> out2 = simulateCircuit64(c2, topo2, piMask2, c2.POs);

        for (int po2 = 0; po2 < nPO; ++po2) {
            int po1 = poMap[po2];
            uint64_t v1 = out1[po1];
            uint64_t v2 = out2[po2];
            if (rowFlip[po2]) v2 = ~v2;
            uint64_t diff = (v1 ^ v2) & activeMask;
            if (diff != 0) return false;
        }
    }

    return true;
}

} // namespace

MatchResult solveByUnateCEGIS(const Circuit& c1,
                             const Circuit& c2,
                             const BusInfo& b1,
                             const BusInfo& b2,
                             const std::string& unateCsv1,
                             const std::string& unateCsv2,
                             int busSamples,
                             int maxCegisIters,
                             int poTries,
                             int piTriesPerPO,
                             int seed,
                             int timeoutSwitchSeconds) {
    // If caller keeps the default (often 1), we can get "combo=1/1" and
    // be stuck exploring a wrong bus-to-bus mapping forever. Since the number
    // of buses is typically small, multi-sampling bus matchings is cheap and
    // significantly increases robustness.
    if (busSamples < 60) busSamples = 60;
    const auto tStartAll = std::chrono::steady_clock::now();
    auto elapsedSec = [&]() -> double {
        using namespace std::chrono;
        return duration_cast<duration<double>>(steady_clock::now() - tStartAll).count();
    };
    auto timeoutReached = [&]() -> bool {
        if (timeoutSwitchSeconds <= 0) return false;
        return elapsedSec() >= (double)timeoutSwitchSeconds;
    };
    // Avoid starting SAT too close to the deadline (SAT may run long).
    const int kSatSafetyMarginSec = 60;
    auto timeoutPreSat = [&]() -> bool {
        if (timeoutSwitchSeconds <= 0) return false;
        return elapsedSec() >= (double)(timeoutSwitchSeconds - kSatSafetyMarginSec);
    };
    bool timeoutTriggered = false;

    MatchResult best;
    best.success = false;
    best.msg = "No solution tried yet.";

    // This contest format allows Circuit-II inputs to be many-to-one mapped to Circuit-I inputs,
    // and also to be tied to constants. Therefore PI counts may legitimately differ.
    // (We still assume the benchmark has the same number of outputs in both circuits.)
    if (c1.POs.size() != c2.POs.size()) {
        best.msg = "PO count mismatch: c1 has PO=" + std::to_string(c1.POs.size()) +
                   ", c2 has PO=" + std::to_string(c2.POs.size());
        return best;
    }

    UnateTable t1 = UnateTable::loadFromCSV(unateCsv1, c1);
    UnateTable t2 = UnateTable::loadFromCSV(unateCsv2, c2);

    // PI column reduced-key signature (P/N merged) is invariant to both PO/PI inversion.
    // Use it as a cheap hard filter to shrink candidate sets.
    const auto piKey1 = computeColReducedKeys(t1);
    const auto piKey2 = computeColReducedKeys(t2);

    // Lightweight column signatures (I,B,U counts) for scoring PI candidates.
    // This is invariant to PI inversion (because P/N are both counted as U).
    const auto piIBU1 = allColIBU(t1);
    const auto piIBU2 = allColIBU(t2);

    const int nPO  = t2.nPO;
    const int nPI1 = t1.nPI;
    const int nPI2 = t2.nPI;

    // topo order for fast 64-bit simulation
    vector<int> topo1 = topoSortGates(c1);
    vector<int> topo2 = topoSortGates(c2);

    std::mt19937 rng((seed == 0) ? std::random_device{}() : (unsigned)seed);

std::mt19937 rngBias = rng;

// PO signature (bias + absCorr histogram) for pruning / tie-break.
std::mt19937 rngPoSig = rng;
const int kPoSigBatches = 4;   // 4*64=256 random patterns
const int kPoSigBins    = 16;
const vector<PoSig> poSig1 = computePoSigQuant(c1, topo1, rngPoSig, kPoSigBatches, kPoSigBins);
const vector<PoSig> poSig2 = computePoSigQuant(c2, topo2, rngPoSig, kPoSigBatches, kPoSigBins);

vector<uint8_t> poBias1, poBias2;
poBias1.reserve((size_t)nPO);
poBias2.reserve((size_t)nPO);
for (int i = 0; i < nPO; ++i) {
    poBias1.push_back(i < (int)poSig1.size() ? poSig1[i].bias : 0);
    poBias2.push_back(i < (int)poSig2.size() ? poSig2[i].bias : 0);
}

    // Influence signatures: coarse (T=256) for PO-permutation-invariant pruning,
    // and fine (T=512) for PO-aligned pruning after we obtain a PO mapping.
    const int kInfluenceTopK = 6;
    std::mt19937 rngInfl = rng;
    auto infl1_coarse = computeInfluenceQuant(c1, topo1, rngInfl, 4); // 4*64 = 256
    auto infl2_coarse = computeInfluenceQuant(c2, topo2, rngInfl, 4);
    auto infl1_fine   = computeInfluenceQuant(c1, topo1, rngInfl, 8); // 8*64 = 512
    auto infl2_fine   = computeInfluenceQuant(c2, topo2, rngInfl, 8);

    std::vector<InflCoarseFeat> inflFeat1(nPI1), inflFeat2(nPI2);
    for (int i = 0; i < nPI1; ++i) inflFeat1[i] = makeInflCoarseFeat(infl1_coarse[i]);
    for (int i = 0; i < nPI2; ++i) inflFeat2[i] = makeInflCoarseFeat(infl2_coarse[i]);

    // ------------------------
    // (1) Build bus groups & bus mapping samples (input buses and output buses separately)
    // ------------------------
    std::vector<PortBusGroup> inB1, inB2, outB1, outB2;
    std::vector<int> inNon1, inNon2, outNon1, outNon2;

    extractBusGroups(c1, b1, t1, true,  inB1,  inNon1);
    extractBusGroups(c2, b2, t2, true,  inB2,  inNon2);
    extractBusGroups(c1, b1, t1, false, outB1, outNon1);
    extractBusGroups(c2, b2, t2, false, outB2, outNon2);

    std::cerr << "[DBG][Bus] detected buses: in(c1,c2)=(" << inB1.size() << "," << inB2.size()
              << ") out(c1,c2)=(" << outB1.size() << "," << outB2.size()
              << ") nonBus in(c1,c2)=(" << inNon1.size() << "," << inNon2.size()
              << ") out(c1,c2)=(" << outNon1.size() << "," << outNon2.size() << ")\n";

    // Hard constraint: bused ports must map to bused ports, and buses must map 1-1.
    if (inB1.size() != inB2.size() || outB1.size() != outB2.size()) {
        best.msg = "Bus group count mismatch (inputBus: c1=" + std::to_string(inB1.size()) +
                   " c2=" + std::to_string(inB2.size()) + ", outputBus: c1=" +
                   std::to_string(outB1.size()) + " c2=" + std::to_string(outB2.size()) + ")";
        return best;
    }


    // ------------------------
    // Small-case fast path: brute-force ALL legal mappings then SAT-check.
    // Intended for very small benchmarks (e.g., case1). No witness/reject logic.
    // ------------------------
    {
        uint64_t estCombos = 0;
        if (isBruteforceSmallCaseFeasible(t1, t2, inB1, inB2, inNon1, inNon2, outB1, outB2, outNon1, outNon2, estCombos)) {
            std::cerr << "[SmallCase] enabled: estimated combos=" << estCombos << " (<=300000)\n";
            MatchResult r = solveSmallCaseBruteforceSAT(c1, c2, t1, t2, inB1, inB2, inNon1, inNon2, outB1, outB2, outNon1, outNon2, estCombos);
            return r;
        }
    }


    // Note: PI counts can differ. Extra circuit-II inputs are allowed and can be mapped
    // many-to-one onto circuit-I inputs (or even constants). Therefore, we do NOT require
    // the number of non-bus ports to match.

    // Build bus adjacency and sample matchings.
    std::vector<std::vector<int>> inAdj, outAdj;
    const int topKBus = 6; // small: keep graph sparse but flexible

    std::vector<std::vector<int>> inBusMatchings;
    if (!inB2.empty()) {
        // Allow input-bus width mismatch up to the PI-count delta (common when c2
        // contains duplicated / expanded input buses). Also apply aggregated
        // (I,B,U) dominance constraints.
        const int piSlack = std::abs((int)c2.PIs.size() - (int)c1.PIs.size());
        // buildBusAdjFlexInput args: (topK, maxExtraBits)
        if (!buildBusAdjFlexInput(inB1, inB2, topKBus, piSlack, inAdj)) {
            best.msg = "Failed to build input-bus adjacency (constraints too tight?)";
            return best;
        }
        // If bus count is small, enumerate multiple matchings directly.
        // This avoids collapsing to a single matching when only HK is used.
        if ((int)inB2.size() <= 8) {
            inBusMatchings = enumerateBusMatchingsSmall(inAdj, (int)inB1.size(), std::max(1, busSamples), rng);
        } else {
            inBusMatchings = sampleBusMatchings(inAdj, (int)inB1.size(), std::max(1, busSamples), rng);
        }
        if (inBusMatchings.empty()) {
            best.msg = "No perfect input-bus matching found under constraints.";
            return best;
        }
    } else {
        inBusMatchings.push_back(std::vector<int>());
    }

    std::vector<std::vector<int>> outBusMatchings;
    if (!outB2.empty()) {
        if (!buildBusAdj(outB1, outB2, topKBus, outAdj)) {
            best.msg = "Failed to build output-bus adjacency (width/signature mismatch?)";
            return best;
        }
        // If bus count is small, enumerate multiple matchings directly.
        if ((int)outB2.size() <= 8) {
            outBusMatchings = enumerateBusMatchingsSmall(outAdj, (int)outB1.size(), std::max(1, busSamples), rng);
        } else {
            outBusMatchings = sampleBusMatchings(outAdj, (int)outB1.size(), std::max(1, busSamples), rng);
        }
        if (outBusMatchings.empty()) {
            best.msg = "No perfect output-bus matching found under constraints.";
            return best;
        }
    } else {
        outBusMatchings.push_back(std::vector<int>());
    }

    std::cerr << "[DBG][Bus] busMatchings sizes: in=" << inBusMatchings.size()
              << " out=" << outBusMatchings.size() << " (effective busSamples=" << busSamples << ")\n";

    // Build a list of (inIdx, outIdx) combos, then try up to busSamples combos.
    std::vector<std::pair<int,int>> combos;
    for (int i = 0; i < (int)inBusMatchings.size(); i++) {
        for (int j = 0; j < (int)outBusMatchings.size(); j++) {
            combos.push_back({i,j});
        }
    }
    std::shuffle(combos.begin(), combos.end(), rng);
    if ((int)combos.size() > busSamples) combos.resize(busSamples);

    // ------------------------
    // CEGIS state
    // ------------------------
    const int kMaxWitnesses = 256;
    vector<WitnessBits> witnesses;
    witnesses.reserve((size_t)kMaxWitnesses);
    std::unordered_set<uint64_t> witnessHash;
    witnessHash.reserve(512);

    uint64_t satCalls = 0;
    uint64_t simReject = 0;
    uint64_t witnessReject = 0;
    uint64_t hkFailPO = 0;
    uint64_t hkFailPI = 0;
    uint64_t buildPiFail = 0;
    uint64_t randPiFail = 0;
    uint64_t triesTotal = 0;
    auto tGlobalStart = tStartAll;
    auto tLastLog = tGlobalStart;

    MatchResult lastTried;
    SatCheckResult lastSat;

    // ------------------------
    // (2) For each sampled bus mapping, run the usual PO/PI matching + SAT CEGIS
    // ------------------------
    for (int comboIdx = 0; comboIdx < (int)combos.size(); comboIdx++) {
        const auto [iIn, iOut] = combos[comboIdx];

        uint64_t triesCombo = 0;
        int curIt = -1;
        int curKpo = -1;

        // busId maps (size = b2.buses.size())
        std::vector<int> inBusIdMap2to1;
        if (!inB2.empty()) {
            inBusIdMap2to1 = toBusIdMap(b2, inB1, inB2, inBusMatchings[iIn]);
        } else {
            inBusIdMap2to1.assign((int)b2.buses.size(), -1);
        }

        std::vector<int> outBusIdMap2to1;
        if (!outB2.empty()) {
            outBusIdMap2to1 = toBusIdMap(b2, outB1, outB2, outBusMatchings[iOut]);
        } else {
            outBusIdMap2to1.assign((int)b2.buses.size(), -1);
        }

        // allowed candidates per port (idx-based)
        std::vector<std::vector<int>> allowedPo1, allowedPi1;
        if (!buildAllowedByBus(c1, c2, b1, b2, t1, t2, false, outBusIdMap2to1, allowedPo1)) {
            continue;
        }
        if (!buildAllowedByBus(c1, c2, b1, b2, t1, t2, true,  inBusIdMap2to1, allowedPi1)) {
            continue;
        }

        // Keep a bus-only copy for timeout fallback (no extra pruning).
        auto allowedPiBusOnly = allowedPi1;

        if (timeoutReached()) {
            MatchResult r = makeIBUQuickGuessFromAllowed(t1, t2, allowedPo1, allowedPiBusOnly, rng);
            r.msg = "[TIMEOUT] switch@" + std::to_string(timeoutSwitchSeconds) + "s: output quick IBU guess (no witness/SAT).";
            return r;
        }

        // Hard prune PI candidates by inversion-invariant column signature.
        // (P/N merged into U) => safe under input negation.
        for (int pi2 = 0; pi2 < nPI2; ++pi2) {
            // Keep the original candidate set as a fallback: sometimes the reduced-key
            // signature is too strict when PI counts mismatch or the unate table is noisy.
            auto orig = allowedPi1[pi2];
            auto& vec = allowedPi1[pi2];
            vec.erase(std::remove_if(vec.begin(), vec.end(), [&](int pi1){
                return piKey1[pi1] != piKey2[pi2];
            }), vec.end());
            if (vec.empty()) vec = std::move(orig);
        }

        // Coarse influence pruning (PO-permutation invariant): keep only top-K candidates
        // based on coarse influence summaries. This dramatically reduces HK search space
        // when unate tables are not discriminative.
        coarsePrunePiAllowed(inflFeat1, inflFeat2, kInfluenceTopK, allowedPi1);

        // Build PO ordering (restricted by bus)
        vector<vector<int>> poOrd = buildPOCandidateOrder(t1, t2, allowedPo1, poSig1, poSig2, 16, rng);

        auto tryOneGuess = [&](int Kpo) -> bool {
            if (timeoutReached()) {
                timeoutTriggered = true;
                MatchResult r = makeIBUQuickGuessFromAllowed(t1, t2, allowedPo1, allowedPiBusOnly, rng);
                r.msg = "[TIMEOUT] switch@" + std::to_string(timeoutSwitchSeconds) + "s: output quick IBU guess (no witness/SAT).";
                best = r;
                return true;
            }
            vector<vector<int>> adjPO(nPO);
            for (int po2 = 0; po2 < nPO; po2++) {
                int k = std::min(Kpo, (int)poOrd[po2].size());
                adjPO[po2].assign(poOrd[po2].begin(), poOrd[po2].begin() + k);
            }

	            vector<int> poMap;
	            if (!hopcroftKarp(adjPO, nPO, poMap)) { hkFailPO++; return false; }

	            // -------------------------------------------------
	            // Row-flip handling
	            //
	            // NOTE: rowFlip affects PI candidate construction.
	            // For small-PO cases (common in early contest cases),
	            // the heuristic rowFlip may be wrong and will cause the
	            // witness filter to reject almost everything (satCalls=0).
	            //
	            // To make small cases robust, we enumerate ALL rowFlip
	            // assignments when nPO is small, prioritizing those close
	            // to the heuristic.
	            // -------------------------------------------------
	            vector<char> rowFlipHeu = chooseRowFlipHeuristic(t1, t2, poMap, poBias1, poBias2);
	            uint32_t heuMask = 0;
	            for (int po2 = 0; po2 < nPO && po2 < 32; ++po2) {
	                if (rowFlipHeu[po2]) heuMask |= (1u << po2);
	            }
	            const int kMaxEnumPO = 10; // 2^10 = 1024, cheap
	            std::vector<uint32_t> flipMasks;
	            if (nPO <= kMaxEnumPO) {
	                const uint32_t full = (nPO == 32 ? 0xFFFFFFFFu : ((1u << nPO) - 1u));
	                flipMasks.reserve((size_t)1u << nPO);
	                for (uint32_t m = 0; m <= full; ++m) flipMasks.push_back(m);
	                std::sort(flipMasks.begin(), flipMasks.end(), [&](uint32_t a, uint32_t b) {
	                    return __builtin_popcount((a ^ heuMask) & full) < __builtin_popcount((b ^ heuMask) & full);
	                });
	            } else {
	                flipMasks.push_back(heuMask);
	            }

	            for (uint32_t mFlip : flipMasks) {
	                vector<char> rowFlip(nPO, 0);
	                for (int po2 = 0; po2 < nPO && po2 < 32; ++po2) rowFlip[po2] = ((mFlip >> po2) & 1u) ? 1 : 0;

	                vector<vector<int>> piCand;
	                vector<vector<uint8_t>> piCandInvMask;
	                buildPiCandidatesFromPoMap(t1, t2, poMap, rowFlip, allowedPi1, piCand, piCandInvMask);

                // Fine influence pruning (PO-aligned): further trim each PI's candidate list
                // using aligned influence distance under the current PO mapping.
                finePrunePiCandByInfluence(infl1_fine, infl2_fine, poMap, kInfluenceTopK, piCand, piCandInvMask);

	                const int piAttempts = std::max(1, std::min(piTriesPerPO, 24));
	                for (int attempt = 0; attempt < piAttempts; attempt++) {
	                    std::vector<int>  piMap;
	                    std::vector<char> piFlip;
	                    std::vector<char> piConstOne(nPI2, 0);
	                    const bool ok = buildPiMapUsingHK(
	                        piCand, piCandInvMask, nPI1,
	                        rng, piMap, piFlip);
	                    if (!ok) { hkFailPI++; randPiFail++; continue; }

	                    MatchResult guess = makeGuessFromMaps(t1, t2, poMap, rowFlip, piMap, piFlip, piConstOne);
	                    lastTried = guess;
                        triesTotal++;
                        triesCombo++;
                        if (timeoutReached()) {
                            timeoutTriggered = true;
                            MatchResult r = makeIBUQuickGuessFromAllowed(t1, t2, allowedPo1, allowedPiBusOnly, rng);
                            r.msg = "[TIMEOUT] switch@" + std::to_string(timeoutSwitchSeconds) + "s: output quick IBU guess (no witness/SAT).";
                            best = r;
                            return true;
                        }
                {
                    auto now = std::chrono::steady_clock::now();
                    if (std::chrono::duration_cast<std::chrono::seconds>(now - tLastLog).count() >= 1) {
                        tLastLog = now;
                        using namespace std::chrono;
                        auto sec = duration_cast<duration<double>>(now - tGlobalStart).count();
                        std::cerr << "[PROG] combo=" << comboIdx+1 << "/" << combos.size()
                                  << " it=" << curIt << " Kpo=" << curKpo
                                  << " triesCombo=" << triesCombo
                                  << " triesTotal=" << triesTotal
                                  << " satCalls=" << satCalls
                                  << " simReject=" << simReject
                                  << " witReject=" << witnessReject
                                  << " hkPO=" << hkFailPO
                                  << " hkPI=" << hkFailPI
                                  << " buildPiFail=" << buildPiFail
                                  << " randPiFail=" << randPiFail
                                  << " witnesses=" << witnesses.size()
                                  << " t=" << sec << "s\n";
                    }
                }

	                    if (!passesAllWitnessesBatched(c1, c2, topo1, topo2, poMap, piMap, rowFlip, piFlip, piConstOne, witnesses)) {
	                        witnessReject++;
	                        continue;
	                    }

                // Extra cheap pruning: 64-pattern random simulation.
                // If it fails, we extract ONE failing bit as a witness (CEGIS) and skip SAT.
	                    if (!passesRandomSimOrAddWitness(c1, c2, topo1, topo2, poMap, piMap, rowFlip, piFlip, piConstOne,
	                                                   rng, witnesses, witnessHash, kMaxWitnesses)) {
	                        simReject++;
	                        continue;
	                    }
                        if (timeoutPreSat()) {
                            timeoutTriggered = true;
                            MatchResult r = makeIBUQuickGuessFromAllowed(t1, t2, allowedPo1, allowedPiBusOnly, rng);
                            r.msg = "[TIMEOUT] nearing deadline: skip SAT and output quick IBU guess.";
                            best = r;
                            return true;
                        }

                        satCalls++;
                {
                    using namespace std::chrono;
                    auto now = steady_clock::now();
                    auto sec = duration_cast<duration<double>>(now - tGlobalStart).count();
                    std::cerr << "[SAT] start call=" << satCalls
                              << " witnesses=" << witnesses.size()
                              << " simReject=" << simReject
                              << " t=" << sec << "s\n";
                }

	                    auto tSat0 = std::chrono::steady_clock::now();
	                    SatCheckResult satRes = checkMatchBySAT(c1, c2, guess);
	                    auto tSat1 = std::chrono::steady_clock::now();
                {
                    using namespace std::chrono;
                    auto ms = duration_cast<milliseconds>(tSat1 - tSat0).count();
                    std::cerr << "[SAT] end   call=" << satCalls
                              << " ok=" << (satRes.success ? 1 : 0)
                              << " ms=" << ms
                              << " badPo=(" << satRes.badPoC1 << "," << satRes.badPoC2 << ")\n";
                }
	                    lastSat = satRes;
	                    if (satRes.success) {
	                        guess.success = true;
	                        guess.msg = "SAT UNSAT miter: equivalent mapping found.";
	                        best = guess;
	                        return true;
	                    }

                // de-dup witnesses
	                    {
	                        WitnessBits w = buildWitnessFromSat(c1, satRes.c1PiWitness);
	                        uint64_t h = hashWitnessBits(w);
	                        if ((int)witnesses.size() < kMaxWitnesses && witnessHash.insert(h).second) {
	                            witnesses.push_back(std::move(w));
	                            std::cerr << "[UnateBus] add SAT-witness#" << witnesses.size()
	                                      << " badPo=(" << satRes.badPoC1 << "," << satRes.badPoC2 << ")\n";
	                        }
	                    }
	                }
	            }

	            return false;
        };

        for (int it = 0; it < maxCegisIters; it++) {
            bool madeAttempt = false;

            int KpoStart = std::min(16, nPO);
            int KpoMax = nPO;

            for (int Kpo = KpoStart; ; ) {
                for (int t = 0; t < poTries; t++) {
                    for (int po2 = 0; po2 < nPO; po2++) {
                        int k = std::min(Kpo, (int)poOrd[po2].size());
                        if (k > 2) std::shuffle(poOrd[po2].begin(), poOrd[po2].begin() + k, rng);
                    }

                    curIt = it;
                    curKpo = Kpo;
                    if (tryOneGuess(Kpo)) {
                        if (timeoutTriggered) {
                            std::cerr << "[TIMEOUT] quick-guess emitted at t=" << elapsedSec() << "s\n";
                            return best;
                        }
                        std::cerr << "[UnateBus] success=true  PI=" << nPI2 << "/" << nPI2
                                  << " (c1PI=" << nPI1 << ")"
                                  << "  PO=" << nPO << "/" << nPO
                                  << " combo=" << comboIdx << " it=" << it << "\n";
                        return best;
                    }

                    madeAttempt = true;
                }
                if (Kpo >= KpoMax) break;
                Kpo = std::min(KpoMax, Kpo * 2);

            }

            if (!madeAttempt) break;
        }
    }

    std::cerr << "[UnateBus] success=false  PI=" << nPI2 << "/" << nPI2
              << " (c1PI=" << nPI1 << ")"
              << "  PO=" << nPO << "/" << nPO << "\n";

    if (!lastTried.piPairs.empty() && !lastTried.poPairs.empty()) {
        best = lastTried;
        best.success = false;
        best.msg = "Failed after bus-aware CEGIS. witnesses=" + std::to_string(witnesses.size()) +
                   " lastBadPo=(" + lastSat.badPoC1 + "," + lastSat.badPoC2 + ")";
    } else {
        best.msg = "Failed: could not build any valid PO/PI mapping under current bus+unate constraints.";
    }

    return best;
}
