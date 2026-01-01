#pragma once

#include "parser.h"
#include "match.h"

#include <string>
#include <unordered_map>
#include <vector>

struct SatCheckResult {
    bool success = false;
    std::string msg;

    // If failed, we return a witness assignment for PIs of circuit1.
    // Map from PI net name -> 0/1
    std::unordered_map<std::string,int> c1PiWitness;

    // Provide one bad PO pair names (debug)
    std::string badPoC1;
    std::string badPoC2;
};

SatCheckResult checkMatchBySAT(const Circuit& c1,
                               const Circuit& c2,
                               const MatchResult& match);
