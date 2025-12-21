CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Isrc -Ithird_party
LDFLAGS  :=

SRCDIR := src

# Minisat sources (.cc) — must exist
MINISAT_CC := \
  third_party/minisat/core/Solver.cc \
  third_party/minisat/utils/System.cc \
  third_party/minisat/utils/Options.cc

# Common project sources for bmatch
BMATCH_CPP := \
  $(SRCDIR)/parser.cpp \
  $(SRCDIR)/simulator.cpp \
  $(SRCDIR)/match.cpp \
  $(SRCDIR)/match_writer.cpp \
  $(SRCDIR)/main.cpp

# gen_miter: only needs parser + gen_miter + sat + minisat
MITER_CPP := \
  $(SRCDIR)/parser.cpp \
  $(SRCDIR)/gen_miter.cpp \
  $(SRCDIR)/sat_wrap.cpp

# unate: parser + unate + sat + minisat
UNATE_CPP := \
  $(SRCDIR)/parser.cpp \
  $(SRCDIR)/unate.cpp \
  $(SRCDIR)/sat_wrap.cpp

BMATCH_OBJS := $(patsubst %.cpp,%.o,$(BMATCH_CPP))
MITER_OBJS_CPP := $(patsubst %.cpp,%.o,$(MITER_CPP))
UNATE_OBJS_CPP := $(patsubst %.cpp,%.o,$(UNATE_CPP))
MINISAT_OBJS := $(patsubst %.cc,%.o,$(MINISAT_CC))

BMATCH := bmatch
GENMITER := gen_miter
UNATE := unate

all: $(BMATCH)

$(BMATCH): $(BMATCH_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(GENMITER): $(MITER_OBJS_CPP) $(MINISAT_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(UNATE): $(UNATE_OBJS_CPP) $(MINISAT_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.cc
	$(CXX) $(CXXFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -f $(BMATCH_OBJS) $(MITER_OBJS_CPP) $(UNATE_OBJS_CPP) $(MINISAT_OBJS) $(BMATCH) $(GENMITER) $(UNATE)
