#pragma once

#include "parser.h"
#include "match.h"

#include <string>

void writeMatchFile(const std::string& path,
                    const Circuit& c1,
                    const Circuit& c2,
                    const MatchResult& res);
