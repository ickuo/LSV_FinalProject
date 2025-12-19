#pragma once
#include "parser.h"
#include <utility>
#include <vector>
#include <string>

struct MatchResult {
    std::vector<std::pair<int,int>> piPairs; // (c1_pi, c2_pi)
    std::vector<std::pair<int,int>> poPairs; // (c1_po, c2_po)
    bool success = false;
};

MatchResult solveByBusTT(
    const Circuit& c1, const BusInfo& b1,
    const Circuit& c2, const BusInfo& b2,
    int maxPI = 16 // TT 上限
);
