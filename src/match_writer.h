#pragma once
#include "parser.h"
#include <string>
#include <utility>
#include <vector>

void writeMatchFile(
    const std::string& outPath,
    const Circuit& c1, const Circuit& c2,
    const std::vector<std::pair<int,int>>& piPairs, // (c1_pi_net, c2_pi_net)
    const std::vector<std::pair<int,int>>& poPairs  // (c1_po_net, c2_po_net)
);
