#pragma once
#include <string>
#include <vector>
#include <unordered_map>

// ====== Data Structures (把你 parser.cpp 裡的 enum/struct 全搬來) ======
enum class GateType { AND, OR, NAND, NOR, XOR, XNOR, NOT, BUF, UNKNOWN };
std::string gateTypeToStr(GateType t);

struct Gate {
    int id = -1;
    GateType type = GateType::UNKNOWN;
    std::string instName;
    int outNet = -1;
    std::vector<int> inNets;
};

struct Net {
    int id = -1;
    std::string name;
    int driverGate = -1;
    std::vector<int> sinkGates;
    bool isPI = false;
    bool isPO = false;
    bool isConst = false;
    int constVal = -1;
};

struct Circuit {
    std::string path;
    std::string moduleName;

    std::vector<Net> nets;
    std::unordered_map<std::string,int> netNameToId;

    std::vector<Gate> gates;
    std::vector<int> PIs;
    std::vector<int> POs;

    int getNetId(const std::string& name);
};

struct Bus { int id=-1; std::vector<int> members; };
struct BusInfo { std::vector<Bus> buses; std::vector<int> netToBus; };

struct ProblemInput {
    std::string c1File;
    std::vector<std::vector<std::string>> c1Buses;
    std::string c2File;
    std::vector<std::vector<std::string>> c2Buses;
};

// ====== Parser APIs ======
ProblemInput parseProblemInputFile(const std::string& path);
Circuit parseVerilogNetlist(const std::string& path);
BusInfo attachBuses(const Circuit& c, const std::vector<std::vector<std::string>>& busNames);
