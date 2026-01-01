#include "match_writer.h"

#include <fstream>
#include <stdexcept>

static inline char signChar(bool neg) { return neg ? '-' : '+'; }

void writeMatchFile(const std::string& path,
                    const Circuit& c1,
                    const Circuit& c2,
                    const MatchResult& res) {
    std::ofstream fout(path);
    if (!fout) throw std::runtime_error("Cannot open output file: " + path);

    // =====================
    // Input groups (one group per matched PI)
    // Spec requirement: each group contains exactly one port from Circuit 1.
    // =====================
    for (const auto& pr : res.piPairs) {
        fout << "INGROUP\n";
        fout << "1 " << signChar(false) << " " << c1.nets[pr.c1_pi].name << "\n";
        fout << "2 " << signChar(pr.c2Neg) << " " << c2.nets[pr.c2_pi].name << "\n";
        fout << "END\n";
    }

    // =====================
    // Output groups (one group per matched PO)
    // =====================
    for (const auto& pr : res.poPairs) {
        fout << "OUTGROUP\n";
        fout << "1 " << signChar(false) << " " << c1.nets[pr.c1_po].name << "\n";
        fout << "2 " << signChar(pr.c2Neg) << " " << c2.nets[pr.c2_po].name << "\n";
        fout << "END\n";
    }

    // =====================
    // Constant groups
    // (Not produced by our current solver; emit an empty section.)
    // =====================
    fout << "CONSTGROUP\n";
    fout << "END\n";
}
