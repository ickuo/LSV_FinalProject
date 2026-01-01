#include "match.h"
#include "match_writer.h"
#include "parser.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static std::string resolveRelativeTo(const std::string& baseFile, const std::string& maybeRel) {
    fs::path p(maybeRel);
    if (p.is_absolute()) return p.string();
    fs::path baseDir = fs::path(baseFile).parent_path();
    if (baseDir.empty()) return p.string();
    fs::path cand = baseDir / p;
    if (fs::exists(cand)) return cand.string();
    return p.string();
}

static std::string pickUnateCsv(const std::string& inputFile, const std::string& fileName) {
    fs::path baseDir = fs::path(inputFile).parent_path();
    if (!baseDir.empty()) {
        fs::path cand = baseDir / fileName;
        if (fs::exists(cand)) return cand.string();
    }
    if (fs::exists(fileName)) return fileName;
    // Best guess: next to input file.
    return (baseDir / fileName).string();
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input_file> <output_match>\n";
        return 1;
    }

    const std::string inputFile = argv[1];
    const std::string outMatchFile = argv[2];

    try {
        ProblemInput in = parseProblemInputFile(inputFile);

        const std::string c1Path = resolveRelativeTo(inputFile, in.c1File);
        const std::string c2Path = resolveRelativeTo(inputFile, in.c2File);

        Circuit c1 = parseVerilogNetlist(c1Path);
        Circuit c2 = parseVerilogNetlist(c2Path);

        // Attach bus information (hard constraint: bus-to-bus only).
        BusInfo b1 = attachBuses(c1, in.c1Buses);
        BusInfo b2 = attachBuses(c2, in.c2Buses);

        const std::string unate1 = pickUnateCsv(inputFile, "unate_c1.csv");
        const std::string unate2 = pickUnateCsv(inputFile, "unate_c2.csv");

        // Defaults are tuned for case03-like (B-heavy) situations:
        // - We do randomized guessing guided by unate signatures.
        // - SAT provides witnesses to prune future guesses.
        MatchResult res = solveByUnateCEGIS(
            c1, c2,
            b1, b2,
            unate1, unate2,
            /*busSamples*/ 60,
            /*maxCegisIters*/ 120,
            /*poTries*/ 2000,
            /*piTriesPerPO*/ 250,
            /*seed*/ 0
        );

        std::cerr << "[UnateCEGIS] success=" << (res.success ? "true" : "false")
                  << "  PI=" << res.piPairs.size() << "/" << c1.PIs.size()
                  << "  PO=" << res.poPairs.size() << "/" << c1.POs.size() << "\n";
        std::cerr << "[UnateCEGIS] msg: " << res.msg << "\n";

        writeMatchFile(outMatchFile, c1, c2, res);
        return res.success ? 0 : 2;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
