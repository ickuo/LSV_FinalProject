#include "match.h"

#include "parser.h"
#include "sat_check.h"
#include "simulator.h"
#include "unate_table.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <random>
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
    switch (c) {
        case 'P': case 'N': case 'B': case 'I': return c;
        default: return 'I';
    }
}
static inline char redRel(char c) {
    c = normRel(c);
    if (c == 'P' || c == 'N') return 'U';
    return c; // B or I
}

// ------------------------
// Row feature (used for PO candidate ranking)
// ------------------------
struct RowFeat {
    int minPN = 0;
    int maxPN = 0;
    int cntB  = 0;
    int cntI  = 0;
    std::string refKey;
};

static RowFeat makeRowFeat(const UnateTable& t, int poIdx) {
    RowFeat f;
    int cntP = 0, cntN = 0;
    for (int j = 0; j < t.nPI; j++) {
        char a = normRel(t.rel[poIdx][j]);
        if (a == 'P') cntP++;
        else if (a == 'N') cntN++;
        else if (a == 'B') f.cntB++;
        else f.cntI++;
    }
    f.minPN = std::min(cntP, cntN);
    f.maxPN = std::max(cntP, cntN);
    return f;
}

static inline int rowFeatDistL1(const RowFeat& a, const RowFeat& b) {
    return std::abs(a.minPN - b.minPN) + std::abs(a.maxPN - b.maxPN) +
           std::abs(a.cntB  - b.cntB)  + std::abs(a.cntI  - b.cntI);
}

static inline std::string colReducedKey(const UnateTable& t, int piIdx) {
    int cntU = 0, cntB = 0, cntI = 0;
    for (int po = 0; po < t.nPO; po++) {
        char r = redRel(t.rel[po][piIdx]);
        if (r == 'U') cntU++;
        else if (r == 'B') cntB++;
        else cntI++;
    }
    return std::to_string(cntU) + "," + std::to_string(cntB) + "," + std::to_string(cntI);
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
    std::unordered_map<std::string, std::array<int,3>> hist;
    hist.reserve((size_t)t.nPI * 2);

    for (int pi = 0; pi < t.nPI; pi++) {
        const std::string& ck = colKeys[pi];
        char r = redRel(t.rel[poIdx][pi]);
        int idx = (r == 'U') ? 0 : (r == 'B' ? 1 : 2);
        auto it = hist.find(ck);
        if (it == hist.end()) it = hist.emplace(ck, std::array<int,3>{0,0,0}).first;
        it->second[idx]++;
    }

    std::vector<std::string> keys;
    keys.reserve(hist.size());
    for (const auto& kv : hist) keys.push_back(kv.first);
    std::sort(keys.begin(), keys.end());

    std::ostringstream oss;
    oss << base.minPN << "/" << base.maxPN << "|" << base.cntB << "|" << base.cntI << "|";
    for (const auto& k : keys) {
        const auto& a = hist[k];
        oss << k << ":" << a[0] << "," << a[1] << "," << a[2] << ";";
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
// Bus grouping & signatures (I,B,U)
// ------------------------

// ------------------------
// Influence signature (simulation-based, inversion-invariant)
// ------------------------
// Motivation: when unate tables are dominated by 'B' (binate), unate-based row/col signatures
// become nearly identical and provide little guidance. We add a simulation-based "influence spectrum"
// that is:
//   * invariant to PO inversion (uses pBal=min(p,1-p) and |P(y=1|x=1)-P(y=1|x=0)|),
//   * invariant to PI permutation (sort the per-PI influences),
//   * cheap to compute via 64-bit packed random simulation.
//
// For each PO y and each PI x, estimate influence:
//   d(x->y) = | P(y=1 | x=1) - P(y=1 | x=0) |  in [0,1].
// Then PO signature is the top-K sorted d-values (over all PIs) + pBal(y).
// PI signature can be derived similarly (top-K over all POs), but currently we mainly use PO signature
// to rank PO candidates.
static constexpr int kInfTopK = 12;

// Additional PO signature: output-output correlation profile.
// For each PO y, compute corrAbs(y, z) = |P(y==z) - 0.5| under random inputs
// and take the top-K values over all other outputs.
// This is invariant under PI permutation / PI inversion and also invariant
// under independent PO inversion (since P(y==~z)=1-P(y==z)).
static constexpr int kCorrTopK = 12;

struct InfluenceSig {
    float pBal = 0.0f; // min(P(y=1), P(y=0)) in [0,0.5]
    std::array<float, kInfTopK> top{}; // sorted desc, padded with 0
};

struct CorrSig {
    std::array<float, kCorrTopK> top{}; // sorted desc, padded with 0
};

static inline int popc64(uint64_t x) { return __builtin_popcountll(x); }

static inline double influenceDist(const InfluenceSig& a, const InfluenceSig& b) {
    double d = 2.0 * std::abs((double)a.pBal - (double)b.pBal);
    for (int i = 0; i < kInfTopK; ++i) d += std::abs((double)a.top[i] - (double)b.top[i]);
    return d;
}

static inline int influenceDistScore(const InfluenceSig& a, const InfluenceSig& b) {
    // Scale to an integer for stable ordering.
    return (int)std::llround(influenceDist(a, b) * 10000.0);
}

static void pruneAllowedPiByInfluenceSig(const std::vector<InfluenceSig>& piInf1,
                                        const std::vector<InfluenceSig>& piInf2,
                                        std::vector<std::vector<int>>& allowedPi1,
                                        int keepK,
                                        std::mt19937& rng) {
    if (keepK <= 0) return;
    if (piInf1.empty() || piInf2.empty()) return;
    const int nPI2 = (int)allowedPi1.size();
    for (int pi2 = 0; pi2 < nPI2; ++pi2) {
        auto& cand = allowedPi1[pi2];
        if ((int)cand.size() <= keepK) continue;

        std::vector<std::pair<int,int>> scored;
        scored.reserve(cand.size());
        for (int pi1 : cand) {
            if (pi1 < 0 || pi1 >= (int)piInf1.size() || pi2 >= (int)piInf2.size()) continue;
            int d = influenceDistScore(piInf1[pi1], piInf2[pi2]);
            scored.push_back({d, pi1});
        }
        if ((int)scored.size() <= keepK) {
            cand.clear();
            for (auto& p : scored) cand.push_back(p.second);
            continue;
        }

        // Break ties randomly so we keep some diversity.
        std::shuffle(scored.begin(), scored.end(), rng);

        std::nth_element(scored.begin(), scored.begin() + keepK, scored.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
        scored.resize(keepK);

        std::sort(scored.begin(), scored.end(),
                  [](const auto& a, const auto& b) {
                      if (a.first != b.first) return a.first < b.first;
                      return a.second < b.second;
                  });

        cand.clear();
        cand.reserve(keepK);
        for (auto& p : scored) cand.push_back(p.second);
    }
}


static void simulatePOsByBatchSharedMasks(const Circuit& c,
                                         const std::vector<int>& topo,
                                         const std::vector<std::vector<uint64_t>>& masksByBatch,
                                         std::vector<std::vector<uint64_t>>& outByBatch) {
    const int batches = (int)masksByBatch.size();
    const int nPO = (int)c.POs.size();
    outByBatch.assign(batches, std::vector<uint64_t>(nPO, 0));
    for (int b = 0; b < batches; ++b) {
        outByBatch[b] = simulateCircuit64(c, topo, masksByBatch[b], c.POs);
    }
}

static inline double corrDist(const CorrSig& a, const CorrSig& b) {
    double d = 0.0;
    for (int i = 0; i < kCorrTopK; ++i) d += std::abs((double)a.top[i] - (double)b.top[i]);
    return d;
}

static inline int corrDistScore(const CorrSig& a, const CorrSig& b) {
    return (int)std::llround(corrDist(a, b) * 10000.0);
}

static void computePOBaselineOutputsSharedMasks(const Circuit& c,
                                                const std::vector<int>& topo,
                                                const std::vector<std::vector<uint64_t>>& masksByBatch,
                                                std::vector<std::vector<uint64_t>>& outByBatch) {
    const int nPI = (int)c.PIs.size();
    const int nPO = (int)c.POs.size();
    const int batches = (int)masksByBatch.size();
    outByBatch.assign((size_t)batches, std::vector<uint64_t>((size_t)nPO, 0));
    if (nPI == 0 || nPO == 0 || batches == 0) return;
    for (int b = 0; b < batches; ++b) {
        const auto& piMask = masksByBatch[b];
        outByBatch[b] = simulateCircuit64(c, topo, piMask, c.POs);
    }
}

static void computePOCorrSigsFromOutputs(const std::vector<std::vector<uint64_t>>& outByBatch,
                                        std::vector<CorrSig>& corrOut) {
    const int batches = (int)outByBatch.size();
    const int nPO = batches ? (int)outByBatch[0].size() : 0;
    const int totalBits = batches * 64;
    corrOut.assign((size_t)nPO, CorrSig{});
    if (batches == 0 || nPO == 0) return;

    // Pre-pack outputs as [po][b] for cache-friendly pairwise popcounts.
    std::vector<std::vector<uint64_t>> outPO((size_t)nPO, std::vector<uint64_t>((size_t)batches, 0));
    for (int b = 0; b < batches; ++b) {
        for (int po = 0; po < nPO; ++po) outPO[po][b] = outByBatch[b][po];
    }

    for (int po = 0; po < nPO; ++po) {
        std::vector<float> vals;
        vals.reserve((size_t)std::max(0, nPO - 1));
        for (int other = 0; other < nPO; ++other) {
            if (other == po) continue;
            int ham = 0;
            for (int b = 0; b < batches; ++b) {
                ham += popc64(outPO[po][b] ^ outPO[other][b]);
            }
            int eq = totalBits - ham;
            // corrAbs = |P(eq) - 0.5|
            float corrAbs = std::abs((float)eq / (float)totalBits - 0.5f);
            vals.push_back(corrAbs);
        }
        std::sort(vals.begin(), vals.end(), std::greater<float>());
        for (int k = 0; k < kCorrTopK && k < (int)vals.size(); ++k) {
            corrOut[po].top[k] = vals[k];
        }
    }
}

static void computeInfluenceSigsSharedMasks(const Circuit& c,
                                            const std::vector<int>& topo,
                                            const std::vector<std::vector<uint64_t>>& masksByBatch,
                                            const std::vector<std::vector<uint64_t>>& outByBatch,
                                            std::vector<InfluenceSig>& poSigOut,
                                            std::vector<InfluenceSig>* piSigOut = nullptr) {
    const int nPI = (int)c.PIs.size();
    const int nPO = (int)c.POs.size();
    const int batches = (int)masksByBatch.size();
    const int totalBits = batches * 64;

    poSigOut.assign(nPO, InfluenceSig{});
    if (piSigOut) piSigOut->assign(nPI, InfluenceSig{});

    if (nPI == 0 || nPO == 0 || batches == 0) return;

    // cnt1[pi] = number of samples where x=1
    std::vector<int> cnt1(nPI, 0);
    for (int pi = 0; pi < nPI; ++pi) {
        int s1 = 0;
        for (int b = 0; b < batches; ++b) s1 += popc64(masksByBatch[b][pi]);
        cnt1[pi] = s1;
    }

    // cntPO1[po] = number of samples where y=1
    std::vector<int> cntPO1(nPO, 0);
    for (int po = 0; po < nPO; ++po) {
        int s1 = 0;
        for (int b = 0; b < batches; ++b) s1 += popc64(outByBatch[b][po]);
        cntPO1[po] = s1;
    }

    // onesXY[po][pi] = number of samples where (x=1,y=1)
    std::vector<std::vector<int>> onesXY(nPO, std::vector<int>(nPI, 0));
    for (int po = 0; po < nPO; ++po) {
        for (int pi = 0; pi < nPI; ++pi) {
            int s11 = 0;
            for (int b = 0; b < batches; ++b) {
                s11 += popc64(outByBatch[b][po] & masksByBatch[b][pi]);
            }
            onesXY[po][pi] = s11;
        }
    }

    // If we also need PI signatures, cache influence matrix [po][pi].
    std::vector<std::vector<float>> infMat;
    if (piSigOut) infMat.assign(nPO, std::vector<float>(nPI, 0.0f));

    // Build PO signatures: balance + top-K influences |P(y=1|x=1)-P(y=1|x=0)|
    for (int po = 0; po < nPO; ++po) {
        InfluenceSig sig;
        double py = (double)cntPO1[po] / (double)totalBits;
        sig.pBal = (float)std::min(py, 1.0 - py);

        std::vector<float> infl;
        infl.reserve(nPI);
        for (int pi = 0; pi < nPI; ++pi) {
            int c1 = cnt1[pi];
            int c0 = totalBits - c1;
            if (c1 == 0 || c0 == 0) { 
                if (piSigOut) infMat[po][pi] = 0.0f;
                infl.push_back(0.0f);
                continue;
            }
            int a = onesXY[po][pi];               // x=1,y=1
            int b = cntPO1[po] - a;               // x=0,y=1  (since y=1 total minus x=1,y=1)
            double p1 = (double)a / (double)c1;
            double p0 = (double)b / (double)c0;
            float v = (float)std::abs(p1 - p0);
            if (piSigOut) infMat[po][pi] = v;
            infl.push_back(v);
        }
        std::sort(infl.begin(), infl.end(), std::greater<float>());
        for (int i = 0; i < kInfTopK; ++i) sig.top[i] = (i < (int)infl.size()) ? infl[i] : 0.0f;

        poSigOut[po] = sig;
    }

    // Build PI signatures: balance + top-K influences across POs (invariant to output inversion).
    if (piSigOut) {
        piSigOut->assign(nPI, InfluenceSig{});
        for (int pi = 0; pi < nPI; ++pi) {
            InfluenceSig sig;
            double px = (double)cnt1[pi] / (double)totalBits;
            sig.pBal = (float)std::min(px, 1.0 - px);

            std::vector<float> infl;
            infl.reserve(nPO);
            for (int po = 0; po < nPO; ++po) infl.push_back(infMat[po][pi]);

            std::sort(infl.begin(), infl.end(), std::greater<float>());
            for (int i = 0; i < kInfTopK; ++i) sig.top[i] = (i < (int)infl.size()) ? infl[i] : 0.0f;

            (*piSigOut)[pi] = sig;
        }
    }
}

struct PortBusGroup {
    int busId = -1;                 // index in BusInfo.buses
    std::vector<int> portIdx;        // indices into t.{piNetIds,poNetIds}
    std::vector<std::array<int,3>> triples; // sorted (I,B,U) per member
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

static int busDistL1(const PortBusGroup& a, const PortBusGroup& b) {
    if (a.portIdx.size() != b.portIdx.size()) return 1e9;
    int d = 0;
    for (size_t i = 0; i < a.triples.size(); i++) {
        for (int k = 0; k < 3; k++) d += std::abs(a.triples[i][k] - b.triples[i][k]);
    }
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
                g.triples.push_back(isInput ? colIBU(t, idx) : rowIBU(t, idx));
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
                                                 const std::vector<std::vector<int>>& allowedPo1,
                                                 const std::vector<InfluenceSig>& poInf1,
                                                 const std::vector<InfluenceSig>& poInf2,
                                                 const std::vector<CorrSig>& poCorr1,
                                                 const std::vector<CorrSig>& poCorr2,
                                                 std::mt19937& rng) {
    vector<RowFeat> f1(t1.nPO), f2(t2.nPO);

    // Reduced column keys for rowRefKey (still useful as a weak tie-break).
    auto ck1 = computeColReducedKeys(t1);
    auto ck2 = computeColReducedKeys(t2);

    for (int i = 0; i < t1.nPO; i++) {
        f1[i] = makeRowFeat(t1, i);
        f1[i].refKey = makeRowRefKey(t1, i, ck1, f1[i]);
    }
    for (int i = 0; i < t2.nPO; i++) {
        f2[i] = makeRowFeat(t2, i);
        f2[i].refKey = makeRowRefKey(t2, i, ck2, f2[i]);
    }

    vector<vector<int>> ord(t2.nPO);

    for (int po2 = 0; po2 < t2.nPO; po2++) {
        const vector<int>* allow = nullptr;
        if (!allowedPo1.empty()) allow = &allowedPo1[po2];

        vector<std::pair<int,int>> scored;
        if (allow) scored.reserve(allow->size());
        else scored.reserve((size_t)t1.nPO);

        auto addOne = [&](int po1) {
            // Primary: simulation-based influence signature (robust under inversion and PI permutation).
            int sc = 0;
            if ((int)poInf1.size() == t1.nPO && (int)poInf2.size() == t2.nPO) {
                sc += influenceDistScore(poInf1[po1], poInf2[po2]);
            }

            // Additional: PO correlation profile (robust even when unate is mostly binate).
            if ((int)poCorr1.size() == t1.nPO && (int)poCorr2.size() == t2.nPO) {
                sc += corrDistScore(poCorr1[po1], poCorr2[po2]);
            }

            // Secondary: unate row-feat distance (often weak when binate dominates).
            sc += rowFeatDistL1(f1[po1], f2[po2]) * 200;

            // Tertiary: reduced-key row reference (treat as a *bonus*, not a hard filter).
            if (!f1[po1].refKey.empty() && f1[po1].refKey == f2[po2].refKey) {
                sc -= 50000;
            }
            scored.emplace_back(sc, po1);
        };

        if (allow) {
            for (int po1 : *allow) addOne(po1);
        } else {
            for (int po1 = 0; po1 < t1.nPO; po1++) addOne(po1);
        }

        std::sort(scored.begin(), scored.end(),
                  [](const auto& A, const auto& B) {
                      if (A.first != B.first) return A.first < B.first;
                      return A.second < B.second;
                  });

        // Shuffle exact-score ties to avoid deterministic HK repetition.
        size_t i = 0;
        while (i < scored.size()) {
            size_t j = i + 1;
            while (j < scored.size() && scored[j].first == scored[i].first) j++;
            if (j - i > 1) std::shuffle(scored.begin() + (long)i, scored.begin() + (long)j, rng);
            i = j;
        }

        ord[po2].clear();
        ord[po2].reserve(scored.size());
        for (auto& pr : scored) ord[po2].push_back(pr.second);
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
    if (!rowFlip) return r1 == r2;
    // rowFlip means c2 PO is inverted => swap P/N on the c2-side relation.
    r2 = swapPN(r2);
    return r1 == r2;
}

static inline bool relCompatibleInv(char r1, char r2, bool rowFlip, bool piFlip) {
    r1 = normRel(r1);
    r2 = normRel(r2);
    // rowFlip: c2 output inverted => swap P/N on c2 relation
    if (rowFlip) r2 = swapPN(r2);
    // piFlip: mapped PI is inverted => swap P/N on c2 relation
    if (piFlip)  r2 = swapPN(r2);
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
    const int nPI = t2.nPI;

    piCand.assign(nPI, {});
    piCandInvMask.assign(nPI, {});
    for (int pi2 = 0; pi2 < nPI; pi2++) {
        const std::vector<int>* allow = nullptr;
        if (!allowedPi1.empty()) allow = &allowedPi1[pi2];

        vector<int> cand;
        cand.reserve(allow ? allow->size() : (size_t)nPI);

        vector<uint8_t> invMask;
        invMask.reserve(allow ? allow->size() : (size_t)nPI);

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
            for (int pi1 = 0; pi1 < nPI; pi1++) {
                uint8_t m = testOne(pi1);
                if (m) { cand.push_back(pi1); invMask.push_back(m); }
            }
        }

        piCand[pi2] = std::move(cand);
        piCandInvMask[pi2] = std::move(invMask);
        if (piCand[pi2].empty()) return false;
    }
    return true;
}

static vector<char> chooseRowFlipHeuristic(const UnateTable& t1,
                                          const UnateTable& t2,
                                          const vector<int>& poMap) {
    vector<char> flip(t2.nPO, 0);
    for (int po2 = 0; po2 < t2.nPO; po2++) {
        int po1 = poMap[po2];
        if (po1 < 0) continue;

        int p1=0,n1=0,b1=0,i1=0;
        int p2=0,n2=0,b2=0,i2=0;
        for (int pi = 0; pi < t1.nPI; pi++) {
            char a = normRel(t1.rel[po1][pi]);
            if (a=='P') p1++; else if (a=='N') n1++; else if (a=='B') b1++; else i1++;
        }
        for (int pi = 0; pi < t2.nPI; pi++) {
            char a = normRel(t2.rel[po2][pi]);
            if (a=='P') p2++; else if (a=='N') n2++; else if (a=='B') b2++; else i2++;
        }

        int dNo = std::abs(p1 - p2) + std::abs(n1 - n2) + std::abs(b1 - b2) + std::abs(i1 - i2);
        int dFl = std::abs(n1 - p2) + std::abs(p1 - n2) + std::abs(b1 - b2) + std::abs(i1 - i2);
        flip[po2] = (dFl < dNo) ? 1 : 0;
    }
    return flip;
}

static vector<char> chooseRowFlipBySimOutputs(const std::vector<std::vector<uint64_t>>& outByBatch1,
                                            const std::vector<std::vector<uint64_t>>& outByBatch2,
                                            const vector<int>& poMap) {
    // Decide per-PO inversion using simulation correlation:
    // if y1 matches (~y2) better than y2, set flip=1.
    const int batches = (int)outByBatch1.size();
    const int nPO2 = (int)poMap.size();
    vector<char> flip(nPO2, 0);
    if (batches == 0) return flip;

    for (int po2 = 0; po2 < nPO2; ++po2) {
        int po1 = poMap[po2];
        if (po1 < 0) continue;

        int eq = 0;
        int neq = 0;
        for (int b = 0; b < batches; ++b) {
            uint64_t y1 = outByBatch1[b][po1];
            uint64_t y2 = outByBatch2[b][po2];
            uint64_t x  = (y1 ^ y2);
            int ne = popc64(x);
            neq += ne;
            eq  += (64 - ne);
        }
        flip[po2] = (neq > eq) ? 1 : 0;
    }
    return flip;
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
// After choosing a PI permutation (piMap), decide whether each c2 PI should be inverted.
// IMPORTANT: Many PI columns are ambiguous under reduced unate constraints; in that case
// both inverted and non-inverted can be feasible. A greedy "prefer non-inverted" choice
// can lock out the true solution and lead to satCalls=0 forever. We therefore RANDOMIZE
// the choice when both are allowed.
static bool derivePiFlipForMap(const vector<vector<int>>& piCand,
                               const vector<vector<uint8_t>>& piCandInvMask,
                               const vector<int>& piMap,
                               std::mt19937& rng,
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

        // mask bit0: ok without inversion, bit1: ok with inversion
        if ((mask & 1) && (mask & 2)) {
            // ambiguous -> explore both
            piFlip[pi2] = (char)(rng() & 1U);
        } else if (mask & 1) {
            piFlip[pi2] = 0;
        } else if (mask & 2) {
            piFlip[pi2] = 1;
        } else {
            return false;
        }
    }
    return true;
}

static MatchResult makeGuessFromMaps(const UnateTable& t1,
                                    const UnateTable& t2,
                                    const vector<int>& poMap,
                                    const vector<char>& rowFlip,
                                    const vector<int>& piMap,
                                    const vector<char>& piFlip) {
    MatchResult r;
    r.success = false;

    r.piPairs.reserve(t2.nPI);
    for (int pi2 = 0; pi2 < t2.nPI; pi2++) {
        int pi1 = piMap[pi2];
        InPortMatch ip;
        ip.c1_pi = t1.piNetIds[pi1];
        ip.c2_pi = t2.piNetIds[pi2];
        ip.c2Neg = (piFlip[pi2] != 0);
        r.piPairs.push_back(ip);
    }

    r.poPairs.reserve(t2.nPO);
    for (int po2 = 0; po2 < t2.nPO; po2++) {
        int po1 = poMap[po2];
        OutPortMatch op;
        op.c1_po = t1.poNetIds[po1];
        op.c2_po = t2.poNetIds[po2];
        op.c2Neg = (rowFlip[po2] != 0);
        r.poPairs.push_back(op);
    }

    return r;
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
        uint64_t m = piMask1[pi1];
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
            uint64_t m = piMask1[pi1];
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
                             int seed) {
    // If caller keeps the default (often 1), we can get "combo=1/1" and
    // be stuck exploring a wrong bus-to-bus mapping forever. Since the number
    // of buses is typically small, multi-sampling bus matchings is cheap and
    // significantly increases robustness.
    if (busSamples < 60) busSamples = 60;

// Speed guard: avoid burning millions of PI tries on a fixed (wrong) PO mapping.
// This cap does not affect correctness (SAT is still the final authority), it only changes exploration order.
if (piTriesPerPO > 20000) piTriesPerPO = 20000;

static bool printedBanner = false;
if (!printedBanner) {
    printedBanner = true;
    std::cerr << "[DBG][Match] UnateCEGIS"
              << " busSamples=" << busSamples
              << " poTries=" << poTries
              << " piTriesPerPO=" << piTriesPerPO
              << " maxIters=" << maxCegisIters
              << " seed=" << seed
              << "\n";
}


    MatchResult best;
    best.success = false;
    best.msg = "No solution tried yet.";

    if (c1.PIs.size() != c2.PIs.size() || c1.POs.size() != c2.POs.size()) {
        best.msg = "PI/PO count mismatch: c1 has PI=" + std::to_string(c1.PIs.size()) +
                   " PO=" + std::to_string(c1.POs.size()) + ", c2 has PI=" +
                   std::to_string(c2.PIs.size()) + " PO=" + std::to_string(c2.POs.size());
        return best;
    }

    UnateTable t1 = UnateTable::loadFromCSV(unateCsv1, c1);
    UnateTable t2 = UnateTable::loadFromCSV(unateCsv2, c2);

    // PI column reduced-key signature (P/N merged) is invariant to both PO/PI inversion.
    // Use it as a cheap hard filter to shrink candidate sets.
    const auto piKey1 = computeColReducedKeys(t1);
    const auto piKey2 = computeColReducedKeys(t2);

    const int nPO = t2.nPO;
    const int nPI = t2.nPI;

    // topo order for fast 64-bit simulation
    vector<int> topo1 = topoSortGates(c1);
    vector<int> topo2 = topoSortGates(c2);

    std::mt19937 rng((seed == 0) ? std::random_device{}() : (unsigned)seed);

    // ------------------------
    // Influence signatures (simulation-based, inversion/perm invariant)
    // ------------------------
    // Use the same random PI masks for c1/c2 to reduce sampling noise.
    const int sigBatches = 64; // 64*64 = 4096 random samples
    std::mt19937 rngSig((seed == 0) ? 0xC0FFEEu : (unsigned)(seed ^ 0x9E3779B9u));

    std::vector<std::vector<uint64_t>> sigMasks((size_t)sigBatches, std::vector<uint64_t>((size_t)nPI, 0));
    for (int b = 0; b < sigBatches; ++b) {
        for (int pi = 0; pi < nPI; ++pi) sigMasks[b][pi] = randU64(rngSig);
    }

    // Baseline PO outputs for correlation signature and for influence computation.
    std::vector<std::vector<uint64_t>> outByBatch1, outByBatch2;
    computePOBaselineOutputsSharedMasks(c1, topo1, sigMasks, outByBatch1);
    computePOBaselineOutputsSharedMasks(c2, topo2, sigMasks, outByBatch2);

    std::vector<InfluenceSig> poInf1, poInf2, piInf1, piInf2;
    computeInfluenceSigsSharedMasks(c1, topo1, sigMasks, outByBatch1, poInf1, &piInf1);
    computeInfluenceSigsSharedMasks(c2, topo2, sigMasks, outByBatch2, poInf2, &piInf2);

    // Output-output correlation signature (top-K corrAbs values) per PO.
    std::vector<CorrSig> poCorr1, poCorr2;
    computePOCorrSigsFromOutputs(outByBatch1, poCorr1);
    computePOCorrSigsFromOutputs(outByBatch2, poCorr2);

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

    // Another quick sanity: non-bus ports count must match too (otherwise strict bus constraint impossible)
    if (inNon1.size() != inNon2.size() || outNon1.size() != outNon2.size()) {
        best.msg = "Non-bus port count mismatch under strict bus constraint (inputNon: c1=" +
                   std::to_string(inNon1.size()) + " c2=" + std::to_string(inNon2.size()) +
                   ", outputNon: c1=" + std::to_string(outNon1.size()) + " c2=" +
                   std::to_string(outNon2.size()) + ")";
        return best;
    }

    // Build bus adjacency and sample matchings.
    std::vector<std::vector<int>> inAdj, outAdj;
    const int topKBus = 6; // small: keep graph sparse but flexible

    std::vector<std::vector<int>> inBusMatchings;
    if (!inB2.empty()) {
        if (!buildBusAdj(inB1, inB2, topKBus, inAdj)) {
            best.msg = "Failed to build input-bus adjacency (width/signature mismatch?)";
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
    auto tGlobalStart = std::chrono::steady_clock::now();
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

// If HK keeps returning the same PO matching, don't spend too long re-trying PI permutations for it.
std::unordered_map<uint64_t, int> poMapRepeat;
poMapRepeat.reserve(4096);

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

        // Hard prune PI candidates by inversion-invariant column signature.
        // (P/N merged into U) => safe under input negation.
        bool piKeyOk = true;
        for (int pi2 = 0; pi2 < nPI; ++pi2) {
            auto& vec = allowedPi1[pi2];
            vec.erase(std::remove_if(vec.begin(), vec.end(), [&](int pi1){
                return piKey1[pi1] != piKey2[pi2];
            }), vec.end());
            if (vec.empty()) { piKeyOk = false; break; }
        }
        if (!piKeyOk) continue;

        // Soft-prune PI candidates by inverter-invariant influence signature
        // (helps when unate table is mostly binate and provides weak guidance).
        {
            const int kKeep = 24; // keep top-K candidates per PI2
            pruneAllowedPiByInfluenceSig(piInf1, piInf2, allowedPi1, kKeep, rng);
            bool ok = true;
            for (int pi2 = 0; pi2 < nPI; ++pi2) {
                if (allowedPi1[pi2].empty()) { ok = false; break; }
            }
            if (!ok) continue;
        }


        // Build PO ordering (restricted by bus)
        vector<vector<int>> poOrd = buildPOCandidateOrder(t1, t2, allowedPo1, poInf1, poInf2, poCorr1, poCorr2, rng);

        auto tryOneGuess = [&](int Kpo) -> bool {
            vector<vector<int>> adjPO(nPO);
            for (int po2 = 0; po2 < nPO; po2++) {
                int k = std::min(Kpo, (int)poOrd[po2].size());
                adjPO[po2].assign(poOrd[po2].begin(), poOrd[po2].begin() + k);
            }

            vector<int> poMap;
            if (!hopcroftKarp(adjPO, nPO, poMap)) { hkFailPO++; return false; }

// Skip overly repeated PO maps (usually caused by near-deterministic HK on similar candidate lists).
// This forces the outer loop to reshuffle PO candidate prefixes and/or increase Kpo.
{
    uint64_t hpo = hashMatching(poMap);
    int &cnt = poMapRepeat[hpo];
    cnt++;
    if (cnt > 12) {
        return false;
    }
}

            vector<char> rowFlip = chooseRowFlipBySimOutputs(outByBatch1, outByBatch2, poMap);

            vector<vector<int>> piCand;
            vector<vector<uint8_t>> piCandInvMask;
            if (!buildPiCandidatesFromPoMap(t1, t2, poMap, rowFlip, allowedPi1, piCand, piCandInvMask)) {
                buildPiFail++; return false;
            }

            vector<int> piMapHK;
            if (!hopcroftKarp(piCand, nPI, piMapHK)) { hkFailPI++; return false; }

            // Dynamic stall window: if we keep rejecting without learning a new witness,
            // stop burning PI tries on this fixed PO-map and move on.
            size_t   stallWSize = witnesses.size();
            uint64_t stallRej   = witnessReject + simReject;

            for (int attempt = 0; attempt < piTriesPerPO; attempt++) {
                vector<int> piMap = piMapHK;
                if (!randomPerfectMatchPI(piCand, rng, piMap)) { randPiFail++; continue; }

                vector<char> piFlip;
                if (!derivePiFlipForMap(piCand, piCandInvMask, piMap, rng, piFlip)) { randPiFail++; continue; }

                MatchResult guess = makeGuessFromMaps(t1, t2, poMap, rowFlip, piMap, piFlip);
                lastTried = guess;

                triesTotal++;
                triesCombo++;
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

                // Stall guard: if we keep failing for a long time *without adding new witnesses*,
                // we are likely stuck on a wrong PO-map (or wrong polarity choice). Move on.
                if (witnesses.size() != stallWSize) {
                    stallWSize = witnesses.size();
                    stallRej = witnessReject + simReject;
                } else if ((witnessReject + simReject - stallRej) >= 30000) {
                    std::cerr << "[STALL] give-up PI tries (no new witness)"
                              << " rej=" << (witnessReject + simReject - stallRej)
                              << " w=" << witnesses.size()
                              << " poRep=" << poMapRepeat[hashMatching(poMap)]
                              << " Kpo=" << curKpo
                              << " it=" << curIt
                              << "\n";
                    break;
                }

                if (!passesAllWitnessesBatched(c1, c2, topo1, topo2, poMap, piMap, rowFlip, piFlip, witnesses)) {
                    witnessReject++;
                    continue;
                }

                // Extra cheap pruning: 64-pattern random simulation.
                // If it fails, we extract ONE failing bit as a witness (CEGIS) and skip SAT.
                if (!passesRandomSimOrAddWitness(c1, c2, topo1, topo2, poMap, piMap, rowFlip, piFlip,
                                               rng, witnesses, witnessHash, kMaxWitnesses)) {
                    simReject++;
                    continue;
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

            return false;
        };

        for (int it = 0; it < maxCegisIters; it++) {
            bool madeAttempt = false;

            int KpoStart = std::min(8, nPO);
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
                        std::cerr << "[UnateBus] success=true  PI=" << nPI << "/" << nPI
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

    std::cerr << "[UnateBus] success=false  PI=" << nPI << "/" << nPI
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
