#include "sat_wrap.h"
#include "../third_party/minisat/core/Solver.h"


struct SatSolver::Impl {
    Minisat::Solver S;
};

SatSolver::SatSolver() : p(new Impl()) {}
SatSolver::~SatSolver() { delete p; }

void SatSolver::ensureVars(int nVars) {
    while (p->S.nVars() < nVars) p->S.newVar();
}

static Minisat::Lit toLit(int dimacsLit) {
    int v = std::abs(dimacsLit) - 1; // minisat var: 0-based
    bool sign = (dimacsLit < 0);     // true means negated
    return Minisat::mkLit(v, sign);
}

void SatSolver::addClause(const std::vector<int>& lits) {
    Minisat::vec<Minisat::Lit> c;
    for (int l : lits) c.push(toLit(l));
    p->S.addClause(c);
}

SatResult SatSolver::solve(const std::vector<int>& assumptions) {
    Minisat::vec<Minisat::Lit> a;
    for (int l : assumptions) a.push(toLit(l));

    SatResult r;
    r.sat = p->S.solve(a);
    if (!r.sat) return r;

    int n = p->S.nVars();
    r.model.assign(n + 1, 0);
    for (int v = 0; v < n; v++) {
        auto mv = p->S.modelValue(v);
        r.model[v + 1] = (mv == Minisat::l_True) ? 1 : 0;
    }
    return r;
}
bool SatSolver::isSat(const std::vector<int>& assumptions) {
    Minisat::vec<Minisat::Lit> a;
    for (int l : assumptions) a.push(toLit(l));
    return p->S.solve(a);
}
