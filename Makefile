# Atom Reloading Simulator
# Makefile

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -I./include -I$(OBJ_DIR)
# Both SCIP and GLPK libraries are required because both MIP implementations are built.
LDFLAGS = -lscip -lglpk

# Directories
SRC_DIR = src
SOLUTION_DIR = $(SRC_DIR)/solution
INC_DIR = include
OBJ_DIR = obj
OBJ_SOLUTION_DIR = $(OBJ_DIR)/solution
BIN_DIR = .

# Shared source files (excluding main and simple_run).
COMMON_SOURCES = $(filter-out $(SRC_DIR)/main.cpp $(SRC_DIR)/simple_run.cpp, $(wildcard $(SRC_DIR)/*.cpp)) \
                 $(wildcard $(SOLUTION_DIR)/*.cpp)
COMMON_OBJECTS = $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(COMMON_SOURCES))

# Benchmark target.
TARGET_BENCHMARK = $(BIN_DIR)/atom_reloading
BENCHMARK_OBJECTS = $(COMMON_OBJECTS) $(OBJ_DIR)/main.o

# Simple-run target.
TARGET_SIMPLE = $(BIN_DIR)/atom_reloading_simple
SIMPLE_OBJECTS = $(COMMON_OBJECTS) $(OBJ_DIR)/simple_run.o

# Default target: build both programs.
.PHONY: all
all: $(TARGET_BENCHMARK) $(TARGET_SIMPLE)

# Generate obj/git_commit.h with the HEAD commit ID at build time.
# FORCE checks on every build; update the file only when its contents change,
# so an unchanged commit ID does not trigger recompilation.
# Use "unknown" without Git; append "-dirty" if tracked files have uncommitted changes.
GIT_COMMIT_HEADER = $(OBJ_DIR)/git_commit.h

.PHONY: FORCE
FORCE:

$(GIT_COMMIT_HEADER): FORCE | $(OBJ_DIR)
	@commit_id=$$(git rev-parse HEAD 2>/dev/null || echo unknown); \
	if [ -n "$$(git status --porcelain --untracked-files=no 2>/dev/null)" ]; then \
		commit_id="$$commit_id-dirty"; \
	fi; \
	printf '#ifndef GIT_COMMIT_H\n#define GIT_COMMIT_H\n#define GIT_COMMIT_ID "%s"\n#endif\n' "$$commit_id" > $@.tmp; \
	cmp -s $@.tmp $@ && rm -f $@.tmp || mv -f $@.tmp $@

# Validate algorithms against an independent exhaustive oracle on small instances.
TARGET_TEST = $(OBJ_DIR)/tests/algorithms_test
TARGET_BENCHMARK_TEST = $(OBJ_DIR)/tests/benchmark_runner_test
.PHONY: test
test: $(TARGET_TEST) $(TARGET_BENCHMARK_TEST) $(TARGET_BENCHMARK)
	./$(TARGET_TEST)
	./$(TARGET_BENCHMARK_TEST)
	python3 tests/benchmark_cli_test.py

$(TARGET_TEST): tests/algorithms_test.cpp $(COMMON_OBJECTS) $(wildcard $(INC_DIR)/*.h)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ tests/algorithms_test.cpp $(COMMON_OBJECTS) $(LDFLAGS)

$(TARGET_BENCHMARK_TEST): tests/benchmark_runner_test.cpp $(COMMON_OBJECTS) $(wildcard $(INC_DIR)/*.h)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ tests/benchmark_runner_test.cpp $(COMMON_OBJECTS) $(LDFLAGS)

# Link the benchmark executable.
$(TARGET_BENCHMARK): $(BENCHMARK_OBJECTS)
	@echo "Linking: $@"
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Build complete: $@"

# Link the simple-run executable.
$(TARGET_SIMPLE): $(SIMPLE_OBJECTS)
	@echo "Linking: $@"
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Build complete: $@"

# Compile object files.
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	@echo "Compiling: $<"
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Compile object files under src/solution.
$(OBJ_SOLUTION_DIR)/%.o: $(SOLUTION_DIR)/%.cpp | $(OBJ_SOLUTION_DIR)
	@echo "Compiling: $<"
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Create the obj directory.
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Create the obj/solution directory.
$(OBJ_SOLUTION_DIR):
	mkdir -p $(OBJ_SOLUTION_DIR)

# Cleanup
.PHONY: clean
clean:
	rm -rf $(OBJ_DIR) $(TARGET_BENCHMARK) $(TARGET_SIMPLE)
	@echo "Cleanup complete"

# Run the benchmark.
.PHONY: run
run: $(TARGET_BENCHMARK)
	./$(TARGET_BENCHMARK)

# Run the simple-run program (example target).
.PHONY: run-simple
run-simple: $(TARGET_SIMPLE)
	./$(TARGET_SIMPLE)

# Analysis and plotting through the uv package in analysis/.
ANALYSIS_DIR = analysis
UV = uv

.PHONY: figures
figures:
	$(UV) run --project $(ANALYSIS_DIR) plot-metrics
	$(UV) run --project $(ANALYSIS_DIR) plot-filling-rate

.PHONY: stats
stats:
	$(UV) run --project $(ANALYSIS_DIR) trial-stats

# Debug build.
.PHONY: debug
debug: CXXFLAGS += -g -DDEBUG
debug: clean all

# Help
.PHONY: help
help:
	@echo "Available targets:"
	@echo "  make                       - Build both programs, including SCIP and GLPK MIP"
	@echo "  make test                  - Run exhaustive checks on small instances and regression tests"
	@echo "  make run                   - Build and run the benchmark program"
	@echo "  make run-simple            - Build and run the simple-run program"
	@echo "  make figures               - Regenerate figures/ from results/ (requires uv)"
	@echo "  make stats                 - Show per-trial statistics and Welch's t-tests (requires uv)"
	@echo "  make clean                 - Remove build artifacts"
	@echo "  make debug                 - Build with debug information"
	@echo "  make help                  - Show this help"
	@echo ""
	@echo "Simple-run program options:"
	@echo "  ./atom_reloading_simple -n 30 -a 1 -p_idle 0.004 -p_reload 0.004 -i 100"
	@echo "    -n:       Scale factor n; storage 12n x 30n, preparation 4n x 15n (default: 15)"
	@echo "    -a:       Algorithm (see ./atom_reloading_simple -h for IDs)"
	@echo "    -p_idle:  Environmental noise probability (default: 0.004)"
	@echo "    -p_reload: Transport failure probability during reloading (default: 0.004)"
	@echo "    -i:       Number of iterations (default: 100)"
	@echo "    -t:       Number of trials (default: 100)"
	@echo "    -s:       Base random seed (default: 998244353)"
	@echo "    -o:       Output JSON file (default: execution_result.json)"
	@echo ""
	@echo "Directory structure:"
	@echo "  results/  - Experiment JSON files (default benchmark output)"
	@echo "  figures/  - Figures generated by the plotting scripts"
	@echo "  analysis/ - Python analysis and plotting package (managed with uv)"

# Dependencies
# Rebuild all affected objects, including the registry and Greedy, after planning API changes.
$(COMMON_OBJECTS) $(OBJ_DIR)/main.o $(OBJ_DIR)/simple_run.o: $(wildcard $(INC_DIR)/*.h)
# Embed the commit ID in JSON metadata for both benchmark and simple-run modes.
$(OBJ_DIR)/main.o $(OBJ_DIR)/simple_run.o: $(GIT_COMMIT_HEADER)
$(OBJ_DIR)/main.o: $(INC_DIR)/types.h $(INC_DIR)/simulator.h
$(OBJ_DIR)/simple_run.o: $(INC_DIR)/types.h $(INC_DIR)/simulator.h
$(OBJ_DIR)/types.o: $(INC_DIR)/types.h
$(OBJ_DIR)/grid.o: $(INC_DIR)/grid.h $(INC_DIR)/types.h
$(OBJ_SOLUTION_DIR)/baseline.o: $(INC_DIR)/algorithms.h $(INC_DIR)/types.h
$(OBJ_SOLUTION_DIR)/proposed.o: $(INC_DIR)/algorithms.h $(INC_DIR)/types.h
$(OBJ_SOLUTION_DIR)/mip_scip.o: $(INC_DIR)/algorithms.h $(INC_DIR)/types.h
$(OBJ_SOLUTION_DIR)/mip_glpk.o: $(INC_DIR)/algorithms.h $(INC_DIR)/types.h
$(OBJ_DIR)/simulator.o: $(INC_DIR)/simulator.h $(INC_DIR)/grid.h $(INC_DIR)/algorithms.h $(INC_DIR)/types.h
