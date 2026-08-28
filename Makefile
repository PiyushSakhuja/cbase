# CBase build file. Mirrors the original single-command g++ build and adds
# targets for the demo, benchmark and tests.
#
#   make            build the CLI (build/cbase)
#   make demo       build the guided demo (build/cbase_demo)
#   make bench      build the benchmark (build/cbase_bench)
#   make test       build and run every test suite
#   make all        CLI + demo + benchmark
#   make clean      remove build/

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic
BUILD    := build

STORAGE_SRC := storage/page.cpp storage/disk_manager.cpp \
              storage/buffer_pool.cpp storage/heap_file.cpp
STORAGE_OBJ := $(patsubst %.cpp,$(BUILD)/%.o,$(STORAGE_SRC))

TESTS := page disk_manager buffer_pool heap_file integration

.PHONY: all test demo bench clean

all: $(BUILD)/cbase $(BUILD)/cbase_demo $(BUILD)/cbase_bench

demo: $(BUILD)/cbase_demo
bench: $(BUILD)/cbase_bench

$(BUILD)/cbase: main.cpp $(STORAGE_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) main.cpp $(STORAGE_OBJ) -o $@

$(BUILD)/cbase_demo: demo.cpp $(STORAGE_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) demo.cpp $(STORAGE_OBJ) -o $@

$(BUILD)/cbase_bench: benchmark.cpp $(STORAGE_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) benchmark.cpp $(STORAGE_OBJ) -o $@

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# --- tests --------------------------------------------------------------

$(BUILD)/tests/%_test: tests/%_test.cpp $(STORAGE_OBJ) tests/test_framework.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $< $(STORAGE_OBJ) -o $@

test: $(patsubst %,$(BUILD)/tests/%_test,$(TESTS))
	@fail=0; \
	for t in $(TESTS); do \
	  echo; echo ">>> running $$t"; \
	  ./$(BUILD)/tests/$${t}_test || fail=1; \
	done; \
	if [ $$fail -ne 0 ]; then echo "TESTS FAILED"; exit 1; \
	else echo; echo "ALL TEST SUITES PASSED"; fi

clean:
	rm -rf $(BUILD)
