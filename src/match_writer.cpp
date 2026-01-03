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
    // Input groups
    // Spec requirement: each group contains exactly one port from Circuit 1,
    // and one or more ports from Circuit 2.
    // =====================
    {
        // Group by c1_pi.
        std::vector<std::vector<const InPortMatch*>> grp(c1.PIs.size());
        for (const auto& pr : res.piPairs) {
            // pr.c1_pi is a netId; map it to PI index by searching c1.PIs.
            // For typical contest sizes, linear search is fine.
            int pi1Idx = -1;
            for (int i = 0; i < (int)c1.PIs.size(); ++i) {
                if (c1.PIs[i] == pr.c1_pi) { pi1Idx = i; break; }
            }
            if (pi1Idx >= 0) grp[pi1Idx].push_back(&pr);
        }

        for (int i = 0; i < (int)c1.PIs.size(); ++i) {
            if (grp[i].empty()) continue; // allow partial mapping, though we try to cover all
            fout << "INGROUP\n";
            fout << "1 " << signChar(false) << " " << c1.nets[c1.PIs[i]].name << "\n";
            for (auto* pr : grp[i]) {
                fout << "2 " << signChar(pr->c2_neg) << " " << c2.nets[pr->c2_pi].name << "\n";
            }
            fout << "END\n";
        }
    }

    // =====================
    // Output groups (one group per matched PO)
    // =====================
    for (const auto& pr : res.poPairs) {
        fout << "OUTGROUP\n";
        fout << "1 " << signChar(false) << " " << c1.nets[pr.c1_po].name << "\n";
        fout << "2 " << signChar(pr.c2_neg) << " " << c2.nets[pr.c2_po].name << "\n";
        fout << "END\n";
    }

    // =====================
    // Constant groups
    // Each line after CONSTGROUP is: "+ <name>" for 1'b0, "- <name>" for 1'b1.
    // =====================
    fout << "CONSTGROUP\n";
    for (const auto& cb : res.constBinds) {
        fout << signChar(cb.bind_one) << " " << c2.nets[cb.c2_pi].name << "\n";
    }
    fout << "END\n";
}
