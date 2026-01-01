#pragma once

#include "parser.h"
#include <string>
#include <vector>

// Unate relation per (PO, PI)
//   I: independent
//   P: positive unate
//   N: negative unate
//   B: binate
// Missing/unknown entries are represented as 'I' (safe default).
struct UnateTable {
    int nPO = 0;
    int nPI = 0;

    // Matrix indexed by (poIdx, piIdx) where:
    //   poIdx is index into Circuit::POs
    //   piIdx is index into Circuit::PIs
    std::vector<std::vector<char>> rel; // size [nPO][nPI]

    // convenience: map index -> netId
    std::vector<int> poNetIds; // size nPO, equals c.POs
    std::vector<int> piNetIds; // size nPI, equals c.PIs

    char at(int poIdx, int piIdx) const {
        return rel[poIdx][piIdx];
    }

    static UnateTable loadFromCSV(const std::string& csvPath, const Circuit& c);
};
