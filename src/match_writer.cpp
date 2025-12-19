#include "match_writer.h"
#include <fstream>
#include <stdexcept>

void writeMatchFile(
    const std::string& outPath,
    const Circuit& c1, const Circuit& c2,
    const MatchResult& res
) {
    std::ofstream fout(outPath.c_str());
    if (!fout) throw std::runtime_error("Cannot write match file: " + outPath);

    fout << "INGROUP\n";
    for (auto& pr : res.piPairs) {
        fout << "1 + " << c1.nets[pr.first].name << "\n";
        fout << "2 + " << c2.nets[pr.second].name << "\n";
    }
    fout << "END\n\n";

    fout << "OUTGROUP\n";
    for (auto& pr : res.poPairs) {
        fout << "1 + " << c1.nets[pr.c1_po].name << "\n";
        fout << "2 " << (pr.c2Neg ? '-' : '+') << " " << c2.nets[pr.c2_po].name << "\n";
    }
    fout << "END\n\n";

    fout << "CONSTGROUP\n";
    fout << "END\n\n";

}
