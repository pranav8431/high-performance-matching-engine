# Dependency-free build (CMake is the primary build; this is a convenience).
#   make            build everything into build/bin
#   make test       build and run all tests
#   make bench      build and run all benchmarks
#   make NATIVE=1   add -march=native
#   make SANITIZE=1 build with ASan + UBSan

CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -Wshadow -Wconversion
CXXFLAGS += -Iinclude
ifeq ($(NATIVE),1)
  CXXFLAGS += -march=native
endif
ifeq ($(SANITIZE),1)
  CXXFLAGS := $(filter-out -O3 -DNDEBUG,$(CXXFLAGS)) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
  LDFLAGS  += -fsanitize=address,undefined
endif

BIN := build/bin
APPS   := demo replay gen_workload
TESTS  := order_index_test order_book_test matching_engine_test differential_test
BENCHS := throughput_benchmark latency_benchmark cancel_benchmark order_index_benchmark
HEADERS := $(wildcard include/lob/*.hpp)

all: $(addprefix $(BIN)/,$(APPS) $(TESTS) $(BENCHS))

$(BIN):
	@mkdir -p $(BIN)

$(BIN)/demo: src/demo.cpp $(HEADERS) | $(BIN)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)
$(BIN)/replay: src/replay.cpp $(HEADERS) | $(BIN)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)
$(BIN)/gen_workload: tools/gen_workload.cpp $(HEADERS) | $(BIN)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

$(BIN)/%_test: tests/%_test.cpp tests/test_framework.hpp tests/reference_book.hpp $(HEADERS) | $(BIN)
	$(CXX) $(CXXFLAGS) -Itests $< -o $@ $(LDFLAGS)

$(BIN)/%_benchmark: benchmarks/%_benchmark.cpp benchmarks/bench_common.hpp $(HEADERS) | $(BIN)
	$(CXX) $(CXXFLAGS) -Ibenchmarks $< -o $@ $(LDFLAGS)

test: $(addprefix $(BIN)/,$(TESTS))
	@for t in $(TESTS); do echo "== $$t"; $(BIN)/$$t || exit 1; done

bench: $(addprefix $(BIN)/,$(BENCHS))
	@for b in $(BENCHS); do echo "== $$b"; $(BIN)/$$b; echo; done

clean:
	rm -rf build

.PHONY: all test bench clean
