#pragma once
#include "parser.h"
#include <string>
#include <vector>
#include <utility>

// forward declare (MatchResult 定義在 match.h)
struct MatchResult;

// SAT check result
// matched=true  => UNSAT, no counterexample exists (mapping is correct)
// matched=false => SAT, witness is one PI assignment that breaks equivalence
struct SatCheckResult {
    bool matched = false;
    std::vector<std::pair<std::string,int>> c1PiWitness; // only valid if matched==false
    std::string badPoC1; // optional
    std::string badPoC2; // optional
};

// Build CNF miter from (c1,c2,guess) and run SAT.
// If UNSAT => matched=true.
// If SAT   => matched=false + returns one witness assignment on c1 PIs.
SatCheckResult checkMatchBySAT(const Circuit& c1, const Circuit& c2, const MatchResult& guess);
