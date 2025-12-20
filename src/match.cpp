#include "match.h"
#include "simulator.h"
#include <algorithm>
#include <unordered_map>
#include <stdexcept>
#include <functional>
#include <iostream>
#include <vector>
#include <limits>
#include <algorithm>

using namespace std;

static std::vector<int> hungarianMinCost(
    const std::vector<std::vector<double>>& cost)
{
    int n = cost.size();
    int m = n ? cost[0].size() : 0;
    if (n == 0 || m == 0) return {};

    bool transposed = false;
    std::vector<std::vector<double>> a = cost;
    if (n > m) {
        transposed = true;
        std::vector<std::vector<double>> b(m, std::vector<double>(n));
        for (int i = 0; i < n; i++)
            for (int j = 0; j < m; j++)
                b[j][i] = a[i][j];
        a.swap(b);
        std::swap(n, m);
    }

    const double INF = std::numeric_limits<double>::infinity();
    std::vector<double> u(n+1), v(m+1);
    std::vector<int> p(m+1), way(m+1);

    for (int i = 1; i <= n; i++) {
        p[0] = i;
        int j0 = 0;
        std::vector<double> minv(m+1, INF);
        std::vector<char> used(m+1, false);
        do {
            used[j0] = true;
            int i0 = p[j0];
            double delta = INF;
            int j1 = 0;
            for (int j = 1; j <= m; j++) if (!used[j]) {
                double cur = a[i0-1][j-1] - u[i0] - v[j];
                if (cur < minv[j]) { minv[j] = cur; way[j] = j0; }
                if (minv[j] < delta) { delta = minv[j]; j1 = j; }
            }
            for (int j = 0; j <= m; j++) {
                if (used[j]) { u[p[j]] += delta; v[j] -= delta; }
                else minv[j] -= delta;
            }
            j0 = j1;
        } while (p[j0] != 0);

        do {
            int j1 = way[j0];
            p[j0] = p[j1];
            j0 = j1;
        } while (j0);
    }

    std::vector<int> rowToCol(n, -1);
    for (int j = 1; j <= m; j++)
        if (p[j]) rowToCol[p[j]-1] = j-1;

    if (!transposed) return rowToCol;

    std::vector<int> ans(cost.size(), -1);
    for (int j = 0; j < (int)rowToCol.size(); j++)
        if (rowToCol[j] >= 0)
            ans[rowToCol[j]] = j;
    return ans;
}


static inline uint64_t splitmix64(uint64_t& x) {
    uint64_t z = (x += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}


// Invert a TT while keeping the padding bits (beyond totalBits) masked to 0.
static TT invertTT(const TT& in, int totalBits) {
    TT out = in;
    for (auto& x : out) x = ~x;
    int pad = ((int)out.size() * 64) - totalBits;
    if (pad > 0 && !out.empty()) {
        uint64_t keep = (~0ULL) >> pad;
        out.back() &= keep;
    }
    return out;
}
//---------------------------

static vector<vector<int>> buildGroups(
    const Circuit& c,
    const BusInfo& bi,
    const vector<int>& busIds,
    const vector<int>& netsOfInterest
) {
    vector<vector<int>> groups;
    groups.reserve(busIds.size() + netsOfInterest.size());

    vector<char> covered((int)c.nets.size(), 0);

    // existing bus groups
    for (int bid : busIds) {
        const auto& mem = bi.buses[bid].members;
        groups.push_back(mem);
        for (int nid : mem) {
            if (nid >= 0 && nid < (int)covered.size()) covered[nid] = 1;
        }
    }

    // singleton groups for nets not in any bus
    for (int nid : netsOfInterest) {
        if (nid >= 0 && nid < (int)covered.size() && !covered[nid]) {
            groups.push_back(vector<int>{nid});
            covered[nid] = 1;
        }
    }
    return groups;
}

static vector<int> filterBusIdsByType(const Circuit& c, const BusInfo& bi, bool wantPIbus, bool wantPObus) {
    vector<int> ids;
    for (const auto& b : bi.buses) {
        bool allPI = true, allPO = true;
        for (int nid : b.members) {
            allPI = allPI && c.nets[nid].isPI;
            allPO = allPO && c.nets[nid].isPO;
        }
        if (wantPIbus && allPI) ids.push_back(b.id);
        else if (wantPObus && allPO) ids.push_back(b.id);
    }
    return ids;
}

static unordered_map<int, vector<int>> groupBusesBySize(const vector<vector<int>>& groups) {
    unordered_map<int, vector<int>> mp;
    for (int gid = 0; gid < (int)groups.size(); gid++) {
        mp[(int)groups[gid].size()].push_back(gid);
    }
    return mp;
}

static vector<int> keysSorted(const unordered_map<int, vector<int>>& mp) {
    vector<int> keys;
    keys.reserve(mp.size());
    for (auto& kv : mp) keys.push_back(kv.first);
    sort(keys.begin(), keys.end());
    return keys;
}

MatchResult solveByBusTT(
    const Circuit& c1, const BusInfo& b1,
    const Circuit& c2, const BusInfo& b2,
    int maxPI
) {
    MatchResult best;

    // ---- collect PI buses / PO buses ----
    vector<int> inBus1  = filterBusIdsByType(c1, b1, true,  false);
    vector<int> outBus1 = filterBusIdsByType(c1, b1, false, true);
    vector<int> inBus2  = filterBusIdsByType(c2, b2, true,  false);
    vector<int> outBus2 = filterBusIdsByType(c2, b2, false, true);

    auto inGroups1  = buildGroups(c1, b1, inBus1,  c1.PIs);
    auto inGroups2  = buildGroups(c2, b2, inBus2,  c2.PIs);
    auto outGroups1 = buildGroups(c1, b1, outBus1, c1.POs);
    auto outGroups2 = buildGroups(c2, b2, outBus2, c2.POs);

    auto in1  = groupBusesBySize(inGroups1);
    auto in2  = groupBusesBySize(inGroups2);
    auto out1 = groupBusesBySize(outGroups1);
    auto out2 = groupBusesBySize(outGroups2);


    // sanity: same size categories exist
    // if (in1.size() != in2.size()) return best;
    // if (out1.size() != out2.size()) return best;

    // ---- decide abstract PI order = c1 PI buses in the order they appear in inBus1 ----
    vector<int> absPiNetsC1;
    for (const auto& g : inGroups1) {
        for (int nid : g) absPiNetsC1.push_back(nid);
    }

    int nAbsPI = (int)absPiNetsC1.size();
    if (nAbsPI > maxPI) return best;

    // abstract idx map for c1
    unordered_map<int,int> c1PiNetToAbs;
    for (int i = 0; i < nAbsPI; i++) c1PiNetToAbs[absPiNetsC1[i]] = i;

    // precompute PI patterns and topo
    auto piPatterns = buildPiPatterns(nAbsPI);
    auto topo1 = topoSortGates(c1);
    auto topo2 = topoSortGates(c2);

    // precompute c1 PO truth tables once
    auto tt1_pos = simulateCircuitTT(c1, topo1, piPatterns, c1PiNetToAbs, c1.POs);

    // helper: map PO net -> index
    unordered_map<int,int> c1PoIdx, c2PoIdx;
    for (int i = 0; i < (int)c1.POs.size(); i++) c1PoIdx[c1.POs[i]] = i;
    for (int i = 0; i < (int)c2.POs.size(); i++) c2PoIdx[c2.POs[i]] = i;

    // ---- enumerate input bus bijection by size ----
    vector<int> inSizes = keysSorted(in1);

    vector<pair<int,int>> chosenInBusPairs; // (groupId1, groupId2)

    function<bool(int)> dfsInputSize = [&](int si) -> bool {
        if (si == (int)inSizes.size()) {
            // We now have bus-to-bus pairs for inputs. Enumerate within-bus permutations.

            // build per-bus member lists in matching order
            vector<vector<int>> c1BusMembers, c2BusMembers;
            c1BusMembers.reserve(chosenInBusPairs.size());
            c2BusMembers.reserve(chosenInBusPairs.size());
            for (auto& bp : chosenInBusPairs) {
                c1BusMembers.push_back(inGroups1[bp.first]);
                c2BusMembers.push_back(inGroups2[bp.second]);
            }

            // build list of permutations for each c2 bus
            vector<vector<vector<int>>> perms;
            perms.reserve(c2BusMembers.size());
            for (auto mem : c2BusMembers) {
                sort(mem.begin(), mem.end());
                vector<vector<int>> ps;
                do { ps.push_back(mem); } while (next_permutation(mem.begin(), mem.end()));
                perms.push_back(ps);
            }

            vector<int> idx(perms.size(), 0);
            while (true) {
                // build c2 PI net -> abstract idx according to current permutation selection
                unordered_map<int,int> c2PiNetToAbs;
                vector<pair<int,int>> piPairs;
                piPairs.reserve(nAbsPI);

                for (size_t k = 0; k < perms.size(); k++) {
                    const auto& A = c1BusMembers[k];
                    const auto& B = perms[k][idx[k]];
                    for (size_t t = 0; t < A.size(); t++) {
                        int pi1 = A[t];
                        int pi2 = B[t];
                        c2PiNetToAbs[pi2] = c1PiNetToAbs[pi1];
                        piPairs.push_back({pi1, pi2});
                    }
                }

                // simulate c2 POs with this PI mapping
                auto tt2_pos = simulateCircuitTT(c2, topo2, piPatterns, c2PiNetToAbs, c2.POs);

                // ---- enumerate output bus bijection + within-bus permutations, and test all PO pairs ----
                vector<int> outSizes = keysSorted(out1);
                vector<pair<int,int>> chosenOutBusPairs; // (groupId1, groupId2)

                function<bool(int)> dfsOutSize = [&](int osi) -> bool {
                    if (osi == (int)outSizes.size()) {
                        // enumerate within-bus permutations
                        vector<vector<int>> o1m, o2m;
                        o1m.reserve(chosenOutBusPairs.size());
                        o2m.reserve(chosenOutBusPairs.size());
                        for (auto& bp : chosenOutBusPairs) {
                            o1m.push_back(outGroups1[bp.first]);
                            o2m.push_back(outGroups2[bp.second]);
                        }

                        vector<vector<vector<int>>> operms;
                        operms.reserve(o2m.size());
                        for (auto mem : o2m) {
                            sort(mem.begin(), mem.end());
                            vector<vector<int>> ps;
                            do { ps.push_back(mem); } while (next_permutation(mem.begin(), mem.end()));
                            operms.push_back(ps);
                        }

                        vector<int> oidx(operms.size(), 0);
                        while (true) {
                            bool ok = true;
                            vector<MatchResult::OutPair> poPairs;
                            poPairs.reserve(c1.POs.size());

                            // 這個 totalBits 是 2^nAbsPI
                            const int totalBits = (int)piPatterns.size();

                            for (size_t k = 0; k < operms.size(); k++) {
                                const auto& A = o1m[k];
                                const auto& B = operms[k][oidx[k]];
                                for (size_t t = 0; t < A.size(); t++) {
                                    int po1 = A[t];
                                    int po2 = B[t];
                                    // poPairs.push_back({po1, po2});

                                    const TT& T1 = tt1_pos[c1PoIdx[po1]];
                                    const TT& T2 = tt2_pos[c2PoIdx[po2]];
                                    MatchResult::OutPair op;
                                    op.c1_po = po1;
                                    op.c2_po = po2;
                                    op.c1Neg = false;
                                    op.c2Neg = false;

                                    if (sameTT(T1, T2)) {
                                        op.c2Neg = false;
                                    } else {
                                        TT invT2 = invertTT(T2, totalBits);
                                        if (sameTT(T1, invT2)) {
                                            op.c2Neg = true;
                                        } else {
                                            ok = false;
                                            break;
                                        }
                                    }

                                    poPairs.push_back(op);
                                }
                                if (!ok) break;
                            }

                            if (ok) {
                                best.piPairs = piPairs;
                                best.poPairs = poPairs;
                                best.success = true;
                                return true;
                            }

                            // next oidx
                            int p = (int)oidx.size() - 1;
                            while (p >= 0) {
                                oidx[p]++;
                                if (oidx[p] < (int)operms[p].size()) break;
                                oidx[p] = 0;
                                p--;
                            }
                            if (p < 0) break;
                        }
                        return false;
                    }

                    int sz = outSizes[osi];
                    auto L = out1.at(sz);
                    auto R = out2.at(sz);

                    sort(R.begin(), R.end());
                    do {
                        int base = (int)chosenOutBusPairs.size();
                        for (size_t i = 0; i < L.size(); i++) {
                            chosenOutBusPairs.push_back(std::make_pair(L[i], R[i]));
                        }
                        if (dfsOutSize(osi + 1)) return true;
                        chosenOutBusPairs.resize(base);
                    } while (next_permutation(R.begin(), R.end()));

                    return false;
                };

                if (dfsOutSize(0)) return true;

                // next input-perm selection idx
                int p = (int)idx.size() - 1;
                while (p >= 0) {
                    idx[p]++;
                    if (idx[p] < (int)perms[p].size()) break;
                    idx[p] = 0;
                    p--;
                }
                if (p < 0) break;
            }

            return false;
        }

        int sz = inSizes[si];

        // need both sides have same number of buses for this size
        auto itL = in1.find(sz); 
        auto itR = in2.find(sz);
        if (itL == in1.end() || itR == in2.end()) return false;
        const auto& L0 = itL->second;
        const auto& R0 = itR->second;
        // if (L0.size() != R0.size()) return false;

        auto L = L0;
        auto R = R0;

        sort(R.begin(), R.end());
        do {
            int base = (int)chosenInBusPairs.size();
            for (size_t i = 0; i < L.size(); i++) {
                chosenInBusPairs.push_back(std::make_pair(L[i], R[i]));
            }
            if (dfsInputSize(si + 1)) return true;
            chosenInBusPairs.resize(base);
        } while (next_permutation(R.begin(), R.end()));

        return false;
    };

    dfsInputSize(0);
    return best;
}


//------------------------------
// big case
//------------------------------
// step 1: build signatures
struct POSig {
    // sig[poIndex][batch] = uint64_t mask
    std::vector<std::vector<uint8_t>> ones;
};

static POSig buildPOSignature(const Circuit& c, const std::vector<int>& topo,
                              int batches, uint64_t seed) {
    POSig out;
    out.ones.assign(c.POs.size(), std::vector<uint8_t>());

    std::vector<uint64_t> piMasks(c.PIs.size(), 0);
    for (int b = 0; b < batches; b++) {
        uint64_t s = seed + 0x1000003ULL * (uint64_t)b;

        // 每個 PI 一個 64-bit mask（64 個 patterns）
        for (size_t i = 0; i < c.PIs.size(); i++) {
            piMasks[i] = splitmix64(s);
        }

        // 一次模擬拿到「所有 PO」的 64-bit 輸出
        std::vector<uint64_t> poMasks = simulateCircuit64(c, topo, piMasks, c.POs);

        for (size_t po = 0; po < poMasks.size(); po++) {
            uint8_t cnt = (uint8_t)__builtin_popcountll(poMasks[po]);
            out.ones[po].push_back(cnt);   // <<< 就是這一行
        }


        // debug
        // if (b == 0) {
        //     uint64_t m = poMasks[0];
        //     std::cerr << "[DBG][Sig] first PO=" << c.nets[c.POs[0]].name
        //             << " popcnt=" << __builtin_popcountll(m) << "/64"
        //             << " mask=" << std::hex << m << std::dec << "\n";
        // }
        // debug end
    }

    // debug
    std::cerr << "[DBG][Sig] batches=" << batches
              << " PO count=" << out.ones.size()
              << " siglen=" << (out.ones.empty() ? 0 : out.ones[0].size())
              << "\n";
    for (int i = 0; i < (int)out.ones.size() && i < 3; i++) {
        std::cerr << "  PO[" << i << "] ones: ";
        for (auto v : out.ones[i]) std::cerr << (int)v << " ";
        std::cerr << "\n";
    }
    // debug end

    return out;
}

// step 2: build candidates
// p-vector distance (L1) with optional inversion
static void distancePO_pvec(const std::vector<uint8_t>& o1,
                            const std::vector<uint8_t>& o2,
                            int& dSame, int& dInv) {
    dSame = 0; dInv = 0;
    for (size_t i = 0; i < o1.size(); i++) {
        int a = (int)o1[i];
        int b = (int)o2[i];
        dSame += std::abs(a - b);
        dInv  += std::abs(a - (64 - b));
    }
}

struct POCand { int po2; bool inv; int score; };

static std::vector<std::vector<POCand>>
buildPOCandidates(const Circuit& c1, const POSig& s1,
                  const Circuit& c2, const POSig& s2,
                  int K, int threshold) {
    std::vector<std::vector<POCand>> cands(c1.POs.size());

    for (size_t i = 0; i < c1.POs.size(); i++) {
        std::vector<POCand> tmp;
        tmp.reserve(c2.POs.size());

        int bestSame = 1e9, bestInv = 1e9, bestMin = 1e9;
        int bestJ_same = -1, bestJ_inv = -1, bestJ_min = -1;
        bool bestMinInv = false;

        for (size_t j = 0; j < c2.POs.size(); j++) {
            int dSame, dInv;
            distancePO_pvec(s1.ones[i], s2.ones[j], dSame, dInv);
            int best = std::min(dSame, dInv);
            bool inv = (dInv < dSame);

            // 先用 threshold 過濾（你也可以先暫時不濾，方便 debug）
            if (best <= threshold) tmp.push_back({(int)j, inv, best});

            // debug: track global best for this po1
            if (dSame < bestSame) { bestSame = dSame; bestJ_same = (int)j; }
            if (dInv  < bestInv)  { bestInv  = dInv;  bestJ_inv  = (int)j; }
            if (best  < bestMin)  { bestMin  = best;  bestJ_min  = (int)j; bestMinInv = inv; }
        }

        std::sort(tmp.begin(), tmp.end(),
                  [](const POCand& a, const POCand& b){ return a.score < b.score; });
        if ((int)tmp.size() > K) tmp.resize(K);
        cands[i] = std::move(tmp);

        // debug only for first po1
        if (i == 0) {
            std::cerr << "[DBG][POCand] po1=" << c1.nets[c1.POs[i]].name << "\n";
            std::cerr << "  bestSame=" << bestSame
                      << " with po2=" << (bestJ_same>=0 ? c2.nets[c2.POs[bestJ_same]].name : "NA") << "\n";
            std::cerr << "  bestInv =" << bestInv
                      << " with po2=" << (bestJ_inv>=0 ? c2.nets[c2.POs[bestJ_inv]].name : "NA") << "\n";
            std::cerr << "  bestMin =" << bestMin
                      << " inv=" << bestMinInv
                      << " with po2=" << (bestJ_min>=0 ? c2.nets[c2.POs[bestJ_min]].name : "NA") << "\n";
            std::cerr << "  candidates kept=" << cands[i].size() << "\n";
        }
        if (i == 0) {
            std::cerr << "[DBG][POCand-ALL] po1="
                    << c1.nets[c1.POs[i]].name << "\n";

            for (size_t j = 0; j < c2.POs.size(); j++) {
                int dSame, dInv;
                distancePO_pvec(s1.ones[i], s2.ones[j], dSame, dInv);
                int best = std::min(dSame, dInv);
                bool inv = (dInv < dSame);

                std::cerr << "  po2="
                        << c2.nets[c2.POs[j]].name
                        << "  dSame=" << dSame
                        << "  dInv=" << dInv
                        << "  best=" << best
                        << "  inv=" << inv
                        << "\n";
            }
        }
        // debug end
    }

    return cands;
}


// step 3: solve by candidates
// MatchResult solveByPOSignatureBaseline(
//     const Circuit& c1, const Circuit& c2,
//     int batches, int K, int threshold
// ) {
//     MatchResult res;
//     auto topo1 = topoSortGates(c1);
//     auto topo2 = topoSortGates(c2);

//     POSig sig1 = buildPOSignature(c1, topo1, batches, 12345);
//     POSig sig2 = buildPOSignature(c2, topo2, batches, 98765);

//     auto cands = buildPOCandidates(c1, sig1, c2, sig2, K, threshold);

//     std::vector<char> po2_taken(c2.POs.size(), 0);

//     // 先簡單：每個 po1 試它的候選，找到第一個沒被占用的
//     for (size_t i = 0; i < c1.POs.size(); i++) {
//         for (auto& cd : cands[i]) {
//             if (po2_taken[cd.po2]) continue;
//             po2_taken[cd.po2] = 1;

//             MatchResult::OutPair op;
//             op.c1_po = c1.POs[i];
//             op.c2_po = c2.POs[cd.po2];
//             op.c2Neg = cd.inv; // 反相就輸出 '-'
//             res.poPairs.push_back(op);
//             // debug
//             std::cerr << "  match "
//                     << c1.nets[op.c1_po].name
//                     << " <-> "
//                     << (op.c2Neg ? "-" : "+")
//                     << c2.nets[op.c2_po].name
//                     << "\n";
//             // debug end    
//             break;
//         }
//     }

//     res.success = !res.poPairs.empty();
//     // debug
//     std::cerr << "[DBG][SolveBaseline] matched pairs = "
//           << res.poPairs.size() << "\n";
//     // debug end
//     return res;
// }
MatchResult solveByPOSignatureBaseline(
    const Circuit& c1, const BusInfo& b1,
    const Circuit& c2, const BusInfo& b2,
    int batches, int K, int threshold)
{
    (void)K; (void)threshold; // 這版不靠候選剪枝，先讓流程穩

    MatchResult res;

    // 0) topo + signatures (只做一次，不要在 cost loop 裡重算)
    auto topo1 = topoSortGates(c1);
    auto topo2 = topoSortGates(c2);
    POSig sig1 = buildPOSignature(c1, topo1, batches, 12345);
    POSig sig2 = buildPOSignature(c2, topo2, batches, 98765);

    // PO net id -> PO index (for sig.ones indexing)
    unordered_map<int,int> poIdx1, poIdx2;
    for (int i = 0; i < (int)c1.POs.size(); i++) poIdx1[c1.POs[i]] = i;
    for (int i = 0; i < (int)c2.POs.size(); i++) poIdx2[c2.POs[i]] = i;

    // ---- precompute full PO cost matrix (score + inv) ----
    int N1 = (int)c1.POs.size();
    int N2 = (int)c2.POs.size();

    std::vector<std::vector<int>> score(N1, std::vector<int>(N2, 0));
    std::vector<std::vector<char>> invMat(N1, std::vector<char>(N2, 0));

    for (int i = 0; i < N1; i++) {
        for (int j = 0; j < N2; j++) {
            int dSame, dInv;
            distancePO_pvec(sig1.ones[i], sig2.ones[j], dSame, dInv);
            score[i][j] = std::min(dSame, dInv);
            invMat[i][j] = (dInv < dSame) ? 1 : 0;
        }
    }


    auto poCost = [&](int poNet1, int poNet2, bool &inv) -> int {
        auto it1 = poIdx1.find(poNet1);
        auto it2 = poIdx2.find(poNet2);
        if (it1 == poIdx1.end() || it2 == poIdx2.end()) {
            inv = false;
            return 1000000000;
        }
        int i = it1->second;
        int j = it2->second;
        inv = (invMat[i][j] != 0);
        return score[i][j];
    };


    // 1) 建立 PO bus-only groups（size>=2），singleton 不要進 bus-level Hungarian
    std::vector<std::vector<int>> bus1, bus2;

    std::vector<char> covered1((int)c1.nets.size(), 0);
    std::vector<char> covered2((int)c2.nets.size(), 0);

    // 取出「全是 PO」的 bus id
    std::vector<int> outBus1 = filterBusIdsByType(c1, b1, /*wantPIbus=*/false, /*wantPObus=*/true);
    std::vector<int> outBus2 = filterBusIdsByType(c2, b2, /*wantPIbus=*/false, /*wantPObus=*/true);

    // 收集真正 bus (size>=2) 並標記 covered
    for (int bid : outBus1) {
        const auto& mem = b1.buses[bid].members;  // 這條 bus 的成員 net ids
        if ((int)mem.size() >= 2) {
            bus1.push_back(mem);
            for (int nid : mem) if (nid >= 0 && nid < (int)covered1.size()) covered1[nid] = 1;
        }
    }
    for (int bid : outBus2) {
        const auto& mem = b2.buses[bid].members;
        if ((int)mem.size() >= 2) {
            bus2.push_back(mem);
            for (int nid : mem) if (nid >= 0 && nid < (int)covered2.size()) covered2[nid] = 1;
        }
    }


    // 這些是「非 bus」的 PO（或 bus 抽不到的 PO），之後會走 leftover（可以自由配到任何 c2 port）
    std::vector<int> single1, single2;
    single1.reserve(c1.POs.size());
    single2.reserve(c2.POs.size());
    for (int po : c1.POs) if (po >= 0 && po < (int)covered1.size() && !covered1[po]) single1.push_back(po);
    for (int po : c2.POs) if (po >= 0 && po < (int)covered2.size() && !covered2[po]) single2.push_back(po);

    // 用 bus-only 取代原本的 g1/g2
    const auto& g1 = bus1;
    const auto& g2 = bus2;

    int G1 = (int)g1.size();
    int G2 = (int)g2.size();

    // 如果沒有真正的 bus，就不要做 bus-level Hungarian；直接讓全部都去 leftover（你原本的 greedy 會看所有 po2 候選）
    if (G1 == 0 || G2 == 0) {
        std::vector<char> used2((int)c2.POs.size(), 0);

        // 這裡你可以選擇先處理「bus cover 的那些 PO」或先處理 singleton；
    //  但因為 greedy 會掃所有剩下 po2，所以順序不影響正確性，只影響結果品質。
    //  我先維持最簡單：照 c1.POs 順序全做一遍。
        for (int i = 0; i < (int)c1.POs.size(); i++) {
            int po1 = c1.POs[i];
            int bestScore = 1000000000, bestJ = -1;
            bool bestInv = false;

            for (int j = 0; j < (int)c2.POs.size(); j++) if (!used2[j]) {
                bool inv;
                int s = poCost(po1, c2.POs[j], inv);
                if (s < bestScore) { bestScore = s; bestJ = j; bestInv = inv; }
            }
            if (bestJ >= 0) {
                MatchResult::OutPair op;
                op.c1_po = po1;
                op.c2_po = c2.POs[bestJ];
                op.c1Neg = false;
                op.c2Neg = bestInv;
                res.poPairs.push_back(op);
                used2[bestJ] = 1;
            }
        }
        res.success = !res.poPairs.empty();
        return res;
    }


    // 2) bus-level cost + Hungarian
    const double WIDTH_PENALTY = 1000.0;
    vector<vector<double>> costG(G1, vector<double>(G2, 0.0));

    for (int i = 0; i < G1; i++) {
        for (int j = 0; j < G2; j++) {
            int w1 = (int)g1[i].size();
            int w2 = (int)g2[j].size();
            double base = std::abs(w1 - w2) * WIDTH_PENALTY;

            // rough estimate: group 內最佳 pair 的 cost（越小越像）
            int best = 1e9;
            for (int a : g1[i]) for (int b : g2[j]) {
                bool invDummy;
                int s = poCost(a, b, invDummy);
                if (s < best) best = s;
            }
            costG[i][j] = base + (double)best;
        }
    }

    vector<int> gAssign = hungarianMinCost(costG);

    // 3) bus 內 Hungarian
    // used2: 用「PO index」標記是否已被配走
    vector<char> used2((int)c2.POs.size(), 0);

    // 也記一下 c1 哪些 PO 已經配過，避免 leftover 重複
    unordered_map<int,char> used1Net;

    for (int i = 0; i < G1; i++) {
        int j = gAssign[i];
        if (j < 0 || j >= G2) continue;

        const auto& A = g1[i]; // PO net ids (c1 side)
        const auto& B = g2[j]; // PO net ids (c2 side)

        int n = (int)A.size();
        int m = (int)B.size();
        if (n == 0 || m == 0) continue;

        vector<vector<double>> cost(n, vector<double>(m, 0.0));
        vector<vector<char>> invFlag(n, vector<char>(m, 0));

        for (int x = 0; x < n; x++) {
            for (int y = 0; y < m; y++) {
                bool inv;
                int s = poCost(A[x], B[y], inv);
                cost[x][y] = (double)s;
                invFlag[x][y] = inv ? 1 : 0;
            }
        }

        vector<int> assign = hungarianMinCost(cost); // rows(A)->col(B)
        for (int x = 0; x < n; x++) {
            int y = assign[x];
            if (y < 0 || y >= m) continue;

            // B[y] 是 PO net id；要轉成 PO index 才能 used2
            auto it2 = poIdx2.find(B[y]);
            if (it2 == poIdx2.end()) continue;
            int po2Index = it2->second;
            if (used2[po2Index]) continue; // 避免重複（理論上 Hungarian 不會，但保險）

            MatchResult::OutPair op;
            op.c1_po = A[x];
            op.c2_po = B[y];
            op.c1Neg = false;
            op.c2Neg = (invFlag[x][y] != 0);

            res.poPairs.push_back(op);
            used2[po2Index] = 1;
            used1Net[A[x]] = 1;
        }
    }

    // 4) leftover fallback（先用 greedy；你若要全域 Hungarian 我也能再改）
    for (int i = 0; i < (int)c1.POs.size(); i++) {
        int po1 = c1.POs[i];
        if (used1Net.find(po1) != used1Net.end()) continue;

        int bestScore = 1e9;
        int bestJ = -1;
        bool bestInv = false;

        for (int j = 0; j < (int)c2.POs.size(); j++) {
            if (used2[j]) continue;
            bool inv;
            int s = poCost(po1, c2.POs[j], inv);
            if (s < bestScore) {
                bestScore = s;
                bestJ = j;
                bestInv = inv;
            }
        }

        if (bestJ >= 0) {
            MatchResult::OutPair op;
            op.c1_po = po1;
            op.c2_po = c2.POs[bestJ];
            op.c1Neg = false;
            op.c2Neg = bestInv;
            res.poPairs.push_back(op);
            used2[bestJ] = 1;
        }
    }

    res.success = !res.poPairs.empty();
    cerr << "\n========== DEBUG: res.poPairs (with ones distance) ==========\n";

    for (const auto& pp : res.poPairs) {
        int net1 = pp.c1_po;      // net id in c1
        int net2 = pp.c2_po;      // net id in c2 (no sign here, you store sign separately)
        bool invChosen = pp.c2Neg;

        // 防呆
        if (net1 < 0 || net1 >= (int)c1.nets.size()) continue;
        if (net2 < 0 || net2 >= (int)c2.nets.size()) continue;

        // 轉成 PO index（給 sig.ones 用）
        auto it1 = poIdx1.find(net1);
        auto it2 = poIdx2.find(net2);
        if (it1 == poIdx1.end() || it2 == poIdx2.end()) continue;

        int i = it1->second;
        int j = it2->second;

        int dSame, dInv;
        distancePO_pvec(sig1.ones[i], sig2.ones[j], dSame, dInv);

        cerr << "C1_PO " << c1.nets[net1].name
            << " <-> C2_PO "
            << (invChosen ? "-" : "+")
            << c2.nets[net2].name
            << " | dSame=" << dSame
            << " dInv=" << dInv
            << " | chosen=" << (invChosen ? "INV" : "SAME")
            << "\n";
    }

    cerr << "============================================================\n\n";


    return res;
}


