# Build with: make            (bench + tests)
#             make verify     (correctness test)
#             make bench-all  (benchmark A, B and C, 5 runs each)
# override with: make CXX=clang++
CXX      = g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -g
CXXFLAGS += $(CXXFLAGS_EXTRA)
BINDIR   ?= .

HEADERS := include/order_book.hpp include/map_book.hpp include/heap_book.hpp include/array_book.hpp src/workload.hpp

all: $(BINDIR)/bench $(BINDIR)/tests

$(BINDIR)/bench: src/bench.cpp $(HEADERS)
	@mkdir -p $(BINDIR)
	$(CXX) $(CXXFLAGS) -Iinclude src/bench.cpp -o $@

$(BINDIR)/tests: src/tests.cpp $(HEADERS)
	@mkdir -p $(BINDIR)
	$(CXX) $(CXXFLAGS) -Iinclude src/tests.cpp -o $@

test: $(BINDIR)/tests
	$(BINDIR)/tests

verify: $(BINDIR)/bench
	$(BINDIR)/bench --verify

bench-all: $(BINDIR)/bench
	$(BINDIR)/bench --impl A --repeat 5
	$(BINDIR)/bench --impl B --repeat 5
	$(BINDIR)/bench --impl C --repeat 5

clean:
	rm -f $(BINDIR)/bench $(BINDIR)/tests

.PHONY: all test verify bench-all clean
