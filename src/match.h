#pragma once
#include "parser.h"
#include <utility>
#include <vector>
#include <string>



struct MatchResult {
    std::vector<std::pair<int,int>> piPairs; // (c1_pi, c2_pi)
    
    struct OutPair {
        int c1_po = -1;
        int c2_po = -1;
        bool c1Neg = false; // 先保留，但 writer 目前固定輸出 '+'
        bool c2Neg = false; // true => OUTGROUP 會輸出 '-'
    };
    std::vector<OutPair> poPairs; // (c1_po, c2_po)
    bool success = false;
};

MatchResult solveByBusTT(
    const Circuit& c1, const BusInfo& b1,
    const Circuit& c2, const BusInfo& b2,
    int maxPI = 16 // TT 上限
);

// Baseline PO-signature-based solver (random simulation)
MatchResult solveByPOSignatureBaseline(
    const Circuit& c1, const BusInfo& b1,
    const Circuit& c2, const BusInfo& b2,
    int batches,
    int K,
    int threshold
);

