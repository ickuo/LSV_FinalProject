#pragma once

#include <string>
#include <vector>

// Forward declarations to keep headers light.
struct Circuit;
struct BusInfo;

// Output port match (PO): allow optional inversion on circuit2 side.
struct OutPortMatch {
    int c1_po = -1;
    int c2_po = -1;
    bool c2Neg = false;
};

// Input port match (PI): allow optional inversion on circuit2 side.
struct InPortMatch {
    int c1_pi = -1;
    int c2_pi = -1;
    bool c2Neg = false;
};

struct MatchResult {
    bool success = false;
    std::string msg;

    // For convenience in writer/SAT:
    // - c1_* and c2_* are netIds.
    // - Each PI/PO appears at most once.
    std::vector<InPortMatch>  piPairs;
    std::vector<OutPortMatch> poPairs;

    // (Optional) constant groups are not handled yet in this solver.
};

// Unate + bus-aware multi-sample solver.
// Notes:
//  - Uses bus information as a hard constraint: a bus must map to a bus of the same width.
//  - Within each mapped bus, does bit-level matching.
//  - If a port is not in any bus, it can only map to another non-bus port.
MatchResult solveByUnateCEGIS(const Circuit& c1,
                             const Circuit& c2,
                             const BusInfo& b1,
                             const BusInfo& b2,
                             const std::string& unateCsv1,
                             const std::string& unateCsv2,
                             int busSamples = 60,
                             int maxCegisIters = 60,
                             int poTries = 600,
                             int piTriesPerPO = 80,
                             int seed = 0);
