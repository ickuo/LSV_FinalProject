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
    bool c2_neg = false;
};

// Input port binding:
// - Each Circuit-II PI appears at most once (either in piPairs or constBinds).
// - A Circuit-I PI can drive multiple Circuit-II PIs (1-to-many), which is
//   represented by multiple entries with the same c1_pi.
struct InPortMatch {
    int  c1_pi = -1;     // netId in circuit1
    int  c2_pi = -1;     // netId in circuit2
    bool c2_neg = false; // true => c2_pi is tied to NOT(c1_pi)
};

struct ConstBind {
    int  c2_pi = -1;     // netId in circuit2
    bool bind_one = false; // false => tie to 1'b0, true => tie to 1'b1
};

struct MatchResult {
    bool success = false;
    std::string msg;

    // For convenience in writer/SAT:
    // - c1_* and c2_* are netIds.
    // - Each Circuit-II PI appears at most once.
    std::vector<InPortMatch>  piPairs;
    std::vector<OutPortMatch> poPairs;

    std::vector<ConstBind> constBinds;
};

// Unate + bus-aware multi-sample solver.
// Notes:
//  - Uses bus information as a hard constraint: bus-to-bus only.
//  - For inputs we allow Circuit-II bus width to be slightly larger than Circuit-I
//    (extra bits are typically duplicates or constants). This is controlled inside
//    the solver using PI-count slack, capped to a small number.
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
                             int seed = 0,
                             int timeoutSwitchSeconds = 3200);
