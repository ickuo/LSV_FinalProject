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

        Circuit c1 = parseVerilogNetlist(pin.c1File);
        Circuit c2 = parseVerilogNetlist(pin.c2File);

        BusInfo b1 = attachBuses(c1, pin.c1Buses);
        BusInfo b2 = attachBuses(c2, pin.c2Buses);

        MatchResult res = solveByBusTT(c1, b1, c2, b2, 16);
        if (!res.success) {
            std::cerr << "No mapping found.\n";
            return 2;
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

        writeMatchFile(outPath, c1, c2, res.piPairs, res.poPairs);

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 2;
    }
}
