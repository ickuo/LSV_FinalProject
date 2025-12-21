#pragma once
#include <vector>

struct SatResult {
    bool sat = false;
    std::vector<char> model; // model[varId] in {0,1}, varId is 1-based, model[0] unused
};

class SatSolver {
public:
    SatSolver();
    ~SatSolver();
    bool isSat(const std::vector<int>& assumptions = {});

    void ensureVars(int nVars);
    void addClause(const std::vector<int>& lits);

    // assumptions are DIMACS lits (+/-varId)
    SatResult solve(const std::vector<int>& assumptions = {});
private:
    struct Impl;
    Impl* p;
};

