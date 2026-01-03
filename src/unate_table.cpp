#include "unate_table.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <unordered_map>

namespace {

static inline std::string trim(const std::string& s) {
    size_t i = 0, j = s.size();
    while (i < j && std::isspace((unsigned char)s[i])) ++i;
    while (j > i && std::isspace((unsigned char)s[j - 1])) --j;
    return s.substr(i, j - i);
}

static std::vector<std::string> splitLooseCSV(const std::string& line) {
    // Very permissive splitter: treat commas/tabs as whitespace.
    // Handles the formats:
    //   po,pi,rel
    //   po pi rel
    //   po, pi, P
    std::string t = line;
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i] == ',' || t[i] == '\t') t[i] = ' ';
    }
    std::istringstream iss(t);
    std::vector<std::string> tok;
    std::string x;
    while (iss >> x) tok.push_back(x);
    return tok;
}

static inline char relCharFromToken(const std::string& tokRaw) {
    std::string tok = tokRaw;
    // strip quotes
    if (!tok.empty() && (tok.front() == '"' || tok.front() == '\'')) tok.erase(tok.begin());
    if (!tok.empty() && (tok.back()  == '"' || tok.back()  == '\'')) tok.pop_back();

    tok = trim(tok);
    if (tok.empty()) return 'X';

    // upper first alphabetic
    char c = 0;
    for (size_t i = 0; i < tok.size(); ++i) {
        char ch = tok[i];
        if (std::isalpha((unsigned char)ch)) { c = (char)std::toupper((unsigned char)ch); break; }
    }
    if (c == 'P' || c == 'N' || c == 'B' || c == 'I') return c;
    if (c == 'X') return 'X';

    // some tools write words
    std::string u;
    u.reserve(tok.size());
    for (char ch : tok) u.push_back((char)std::toupper((unsigned char)ch));
    if (u.find("POS") != std::string::npos) return 'P';
    if (u.find("NEG") != std::string::npos) return 'N';
    if (u.find("BIN") != std::string::npos) return 'B';
    if (u.find("IND") != std::string::npos) return 'I';
    if (u.find("UNK") != std::string::npos) return 'X';
    if (u.find("X")   != std::string::npos) return 'X';

    return 'X';
}

} // namespace

UnateTable UnateTable::loadFromCSV(const std::string& csvPath, const Circuit& c) {
    std::ifstream fin(csvPath.c_str());
    if (!fin) {
        throw std::runtime_error("Cannot open unate CSV: " + csvPath);
    }

    UnateTable t;
    t.poNetIds = c.POs;
    t.piNetIds = c.PIs;
    t.nPO = (int)t.poNetIds.size();
    t.nPI = (int)t.piNetIds.size();

    // Default to 'X' (unknown/unspecified) instead of assuming independent.
    t.rel.assign(t.nPO, std::vector<char>(t.nPI, 'X'));

    // netId -> poIdx/piIdx
    std::vector<int> netToPoIdx(c.nets.size(), -1);
    std::vector<int> netToPiIdx(c.nets.size(), -1);
    for (int i = 0; i < t.nPO; ++i) {
        int nid = t.poNetIds[i];
        if (nid >= 0 && nid < (int)netToPoIdx.size()) netToPoIdx[nid] = i;
    }
    for (int i = 0; i < t.nPI; ++i) {
        int nid = t.piNetIds[i];
        if (nid >= 0 && nid < (int)netToPiIdx.size()) netToPiIdx[nid] = i;
    }

    std::string line;
    int lineNo = 0;
    int filled = 0;

    while (std::getline(fin, line)) {
        ++lineNo;
        line = trim(line);
        if (line.empty()) continue;
        if (line.size() >= 1 && line[0] == '#') continue;
        if (line.size() >= 2 && line[0] == '/' && line[1] == '/') continue;

        std::vector<std::string> tok = splitLooseCSV(line);
        if (tok.size() < 3) continue;

        // Header skip (po,pi,rel or similar)
        std::string a = tok[0];
        std::string b = tok[1];
        std::string r = tok[2];
        std::string aU, bU, rU;
        for (char ch : a) aU.push_back((char)std::toupper((unsigned char)ch));
        for (char ch : b) bU.push_back((char)std::toupper((unsigned char)ch));
        for (char ch : r) rU.push_back((char)std::toupper((unsigned char)ch));
        if ((aU == "PO" || aU == "OUTPUT") && (bU == "PI" || bU == "INPUT")) {
            continue;
        }
        if (rU == "REL" || rU == "RELATION" || rU == "UNATE") {
            continue;
        }

        // map names -> net ids
        int poNet = -1;
        int piNet = -1;
        {
            std::unordered_map<std::string,int>::const_iterator it = c.netNameToId.find(a);
            if (it != c.netNameToId.end()) poNet = it->second;
        }
        {
            std::unordered_map<std::string,int>::const_iterator it = c.netNameToId.find(b);
            if (it != c.netNameToId.end()) piNet = it->second;
        }

        if (poNet < 0 || piNet < 0) {
            // Sometimes CSV might put PI/PO indexes. Try parse as integer indices.
            // Accept format: poIdx piIdx rel
            char* endp1 = 0;
            char* endp2 = 0;
            long poIdxMaybe = std::strtol(a.c_str(), &endp1, 10);
            long piIdxMaybe = std::strtol(b.c_str(), &endp2, 10);
            if (endp1 && *endp1 == 0 && endp2 && *endp2 == 0) {
                if (poIdxMaybe >= 0 && poIdxMaybe < t.nPO && piIdxMaybe >= 0 && piIdxMaybe < t.nPI) {
                    t.rel[(int)poIdxMaybe][(int)piIdxMaybe] = relCharFromToken(r);
                    ++filled;
                }
            }
            continue;
        }

        int poIdx = (poNet >= 0 && poNet < (int)netToPoIdx.size()) ? netToPoIdx[poNet] : -1;
        int piIdx = (piNet >= 0 && piNet < (int)netToPiIdx.size()) ? netToPiIdx[piNet] : -1;
        if (poIdx < 0 || piIdx < 0) continue;

        t.rel[poIdx][piIdx] = relCharFromToken(r);
        ++filled;
    }

    if (t.nPO > 0 && t.nPI > 0 && filled == 0) {
        throw std::runtime_error(
            "Unate CSV parsed but no entries matched circuit nets. "
            "Check naming (CSV uses net names?) : " + csvPath);
    }

    return t;
}
