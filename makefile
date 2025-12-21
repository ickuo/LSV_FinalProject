# ============================================================
# Compiler & flags
# ============================================================
CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Isrc -Ithird_party -Ithird_party/minisat
LDFLAGS  :=

# ============================================================
# Target
# ============================================================
TARGET := bmatch

# ============================================================
# Source files
# ============================================================
SRCDIR := src

SRCS_CPP := \
    $(SRCDIR)/parser.cpp \
    $(SRCDIR)/simulator.cpp \
    $(SRCDIR)/match.cpp \
    $(SRCDIR)/match_writer.cpp \
    $(SRCDIR)/sat_wrap.cpp \
    $(SRCDIR)/sat_check.cpp \
    $(SRCDIR)/main.cpp

# Minisat sources (.cc)
SRCS_CC := \
    third_party/minisat/core/Solver.cc \
    third_party/minisat/utils/System.cc \
    third_party/minisat/utils/Options.cc

OBJS := $(SRCS_CPP:.cpp=.o) $(SRCS_CC:.cc=.o)

# ============================================================
# Build rules
# ============================================================
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.cc
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ============================================================
# Utility
# ============================================================
.PHONY: clean
clean:
	rm -f $(OBJS) $(TARGET)
