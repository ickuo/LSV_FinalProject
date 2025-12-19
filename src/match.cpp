#include "match.h"
#include "simulator.h"
#include <algorithm>
#include <unordered_map>
#include <stdexcept>
#include <functional>

using namespace std;

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

static unordered_map<int, vector<int>> groupBusesBySize(const BusInfo& bi, const vector<int>& busIds) {
    unordered_map<int, vector<int>> mp;
    for (int id : busIds) {
        int sz = (int)bi.buses[id].members.size();
        mp[sz].push_back(id);
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

    auto in1  = groupBusesBySize(b1, inBus1);
    auto in2  = groupBusesBySize(b2, inBus2);
    auto out1 = groupBusesBySize(b1, outBus1);
    auto out2 = groupBusesBySize(b2, outBus2);

    // sanity: same size categories exist
    if (in1.size() != in2.size()) return best;
    if (out1.size() != out2.size()) return best;

    // ---- decide abstract PI order = c1 PI buses in the order they appear in inBus1 ----
    vector<int> absPiNetsC1;
    for (int bid : inBus1) {
        for (int nid : b1.buses[bid].members) absPiNetsC1.push_back(nid);
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

    vector<pair<int,int>> chosenInBusPairs; // (busId1, busId2)

    function<bool(int)> dfsInputSize = [&](int si) -> bool {
        if (si == (int)inSizes.size()) {
            // We now have bus-to-bus pairs for inputs. Enumerate within-bus permutations.

            // build per-bus member lists in matching order
            vector<vector<int>> c1BusMembers, c2BusMembers;
            c1BusMembers.reserve(chosenInBusPairs.size());
            c2BusMembers.reserve(chosenInBusPairs.size());
            for (auto& bp : chosenInBusPairs) {
                c1BusMembers.push_back(b1.buses[bp.first].members);
                c2BusMembers.push_back(b2.buses[bp.second].members);
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
                vector<pair<int,int>> chosenOutBusPairs; // (busId1, busId2)

                function<bool(int)> dfsOutSize = [&](int osi) -> bool {
                    if (osi == (int)outSizes.size()) {
                        // enumerate within-bus permutations
                        vector<vector<int>> o1m, o2m;
                        o1m.reserve(chosenOutBusPairs.size());
                        o2m.reserve(chosenOutBusPairs.size());
                        for (auto& bp : chosenOutBusPairs) {
                            o1m.push_back(b1.buses[bp.first].members);
                            o2m.push_back(b2.buses[bp.second].members);
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
                            vector<pair<int,int>> poPairs;
                            poPairs.reserve(c1.POs.size());

                            for (size_t k = 0; k < operms.size(); k++) {
                                const auto& A = o1m[k];
                                const auto& B = operms[k][oidx[k]];
                                for (size_t t = 0; t < A.size(); t++) {
                                    int po1 = A[t];
                                    int po2 = B[t];
                                    poPairs.push_back({po1, po2});

                                    const TT& T1 = tt1_pos[c1PoIdx[po1]];
                                    const TT& T2 = tt2_pos[c2PoIdx[po2]];
                                    if (!sameTT(T1, T2)) { ok = false; break; }
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
                            chosenOutBusPairs.push_back({L[i], R[i]});
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
        auto itL = in1.find(sz), itR = in2.find(sz);
        if (itL == in1.end() || itR == in2.end()) return false;
        const auto& L0 = itL->second;
        const auto& R0 = itR->second;
        if (L0.size() != R0.size()) return false;

        auto L = L0;
        auto R = R0;

        sort(R.begin(), R.end());
        do {
            int base = (int)chosenInBusPairs.size();
            for (size_t i = 0; i < L.size(); i++) {
                chosenInBusPairs.push_back({L[i], R[i]});
            }
            if (dfsInputSize(si + 1)) return true;
            chosenInBusPairs.resize(base);
        } while (next_permutation(R.begin(), R.end()));

        return false;
    };

    dfsInputSize(0);
    return best;
}
