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

// NEW: prefer *_simp.v if exists
static std::string preferSimpVerilog(const std::string& pathStr) {
    fs::path p(pathStr);

    // already simp
    std::string fn = p.filename().string();
    if (fn.size() >= 7 && fn.find("_simp.v") != std::string::npos) return pathStr;

    // only handle .v
    if (p.extension() != ".v") return pathStr;

    // try "<stem>_simp.v"
    fs::path simp = p.parent_path() / (p.stem().string() + "_simp.v");
    if (fs::exists(simp)) return simp.string();

    // otherwise keep original
    return pathStr;
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
    // const std::string outMatchFile = argv[2];

    try {
        ProblemInput in = parseProblemInputFile(inputFile);

        std::string c1Path = resolveRelativeTo(inputFile, in.c1File);
        std::string c2Path = resolveRelativeTo(inputFile, in.c2File);

        // NEW: auto switch to *_simp.v if present
        c1Path = preferSimpVerilog(c1Path);
        c2Path = preferSimpVerilog(c2Path);

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

        fs::path inputPath = fs::path(inputFile);
        fs::path outPath;

        // 如果 argv[2] 本身是絕對路徑，就尊重它
        fs::path userOut(argv[2]);
        if (userOut.is_absolute()) {
            outPath = userOut;
        } else {
            // 一律寫到 input 所在資料夾
            outPath = inputPath.parent_path() / userOut;
        }

        writeMatchFile(outPath.string(), c1, c2, res);

        std::cerr << "[INFO] output written to: " << outPath << "\n";

        return res.success ? 0 : 2;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
