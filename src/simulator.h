#pragma once
#include "parser.h"
#include <cstdint>
#include <unordered_map>

using TT = std::vector<uint64_t>; // truth table blocks

std::vector<int> topoSortGates(const Circuit& c);

// 產生 abstract PI patterns（由 matcher 決定 abstract PI 的順序）
std::vector<std::vector<uint64_t>> buildPiPatterns(int nPI);

// 在給定「PI 對應到 abstract index」下，模擬 wantNets 的 TT
std::vector<TT> simulateCircuitTT(
    const Circuit& c,
    const std::vector<int>& topo,
    const std::vector<std::vector<uint64_t>>& piPatterns,
    const std::unordered_map<int,int>& piNetToAbsIdx,
    const std::vector<int>& wantNets
);

// 一次跑 64 個 patterns：每個 net 的值用 uint64_t 表示
std::vector<uint64_t> simulateCircuit64(
    const Circuit& c,
    const std::vector<int>& topo,
    const std::vector<uint64_t>& piMaskByIndex, // size = c.PIs.size()
    const std::vector<int>& wantNets            // e.g., c.POs
);
//MERRY XMAS HOHOHO!
bool sameTT(const TT& a, const TT& b);
