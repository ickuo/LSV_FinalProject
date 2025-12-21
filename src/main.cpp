#include "parser.h"
#include "match.h"
#include "match_writer.h"
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input_file> <match_file>\n";
        return 1;
    }

    try {
        ProblemInput pin = parseProblemInputFile(argv[1]);
        auto simpName = [](const std::string& path) {
            // 假設 input 是 .../circuit_1.v
            if (path.size() >= 2 && path.substr(path.size() - 2) == ".v") {
                return path.substr(0, path.size() - 2) + "_simp.v";
            }
            return path; // fallback
        };

        Circuit c1 = parseVerilogNetlist(simpName(pin.c1File));
        Circuit c2 = parseVerilogNetlist(simpName(pin.c2File));

        BusInfo b1 = attachBuses(c1, pin.c1Buses);
        BusInfo b2 = attachBuses(c2, pin.c2Buses);

        MatchResult res = solveByBusTT(c1, b1, c2, b2, 16);
        if (!res.success) {
            // PI 太多或 bus TT 跑不動，就改用 signature baseline
            std::cerr << "[DBG][Main] Using PO-signature solver\n";
            res = solveByPOSignatureBaseline(c1, b1, c2, b2,
                                     /*batches=*/16,
                                     /*K=*/10,
                                     /*threshold=*/120);
        }

        std::string inputPath  = argv[1];   // e.g. case01/input
        std::string outputName = argv[2];   // e.g. output

        std::string outPath;
        size_t pos = inputPath.find_last_of("/\\");
        if (pos == std::string::npos) {
            // input 在目前目錄
            outPath = outputName;
        } else {
            // output 放在 input 所在資料夾
            outPath = inputPath.substr(0, pos + 1) + outputName;
        }

        writeMatchFile(outPath, c1, c2, res);

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 2;
    }
}
