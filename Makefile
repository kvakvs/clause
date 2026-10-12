# Override paths and configuration on the command line, e.g. BUILD_TYPE=Release.
CMAKE ?= cmake
CTEST ?= ctest
BUILD_DIR ?= build/debug
# Builds without tests (make build, run-macos.sh) use their own directory, so they never reconfigure the testing
# build with BUILD_TESTING=OFF.
TOOL_BUILD_DIR ?= build/tool
BUILD_TYPE ?= Debug
# An empty count requests the native build tool's default parallelism.
JOBS ?=
CMAKE_ARGS ?=
# Development tests default to fast mode; TEST_MODE=full (or make test-full) runs every combination.
TEST_MODE ?= fast
# CTest slot count; the default is every logical CPU. Each test takes two slots, so half run concurrently.
TEST_JOBS ?= $(shell getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
# Manual benchmarks against Erlang/OTP (plan step 69A) use their own Release build; BENCH_ARGS go to the driver.
BENCH_BUILD_DIR ?= build/bench
BENCH_ARGS ?=
PYTHON ?= python3
CLANG_FORMAT ?= $(shell command -v clang-format 2>/dev/null || xcrun --find clang-format 2>/dev/null)

.PHONY: build build_test test test-full benchmark format fmt format-all clean

# Reconfigure each time; let CMake control jobs independently of outer make flags.
# Build only the compiler executable and its dependencies for manual runs.
build:
	$(CMAKE) -S . -B "$(TOOL_BUILD_DIR)" $(CMAKE_ARGS) \
		-DCMAKE_BUILD_TYPE="$(BUILD_TYPE)" -DBUILD_TESTING=OFF \
		-DCLAUSE_BUILD_COMPILER=ON -DCLAUSE_BUILD_RUNTIME=ON
	+MAKEFLAGS= $(CMAKE) --build "$(TOOL_BUILD_DIR)" --config "$(BUILD_TYPE)" --target clau --parallel $(if $(strip $(JOBS)),"$(JOBS)")

# Build all executables including those for testing
build_test:
	$(CMAKE) -S . -B "$(BUILD_DIR)" $(CMAKE_ARGS) \
		-DCMAKE_BUILD_TYPE="$(BUILD_TYPE)" -DBUILD_TESTING=ON \
		-DCLAUSE_BUILD_COMPILER=ON -DCLAUSE_BUILD_RUNTIME=ON
	+MAKEFLAGS= $(CMAKE) --build "$(BUILD_DIR)" --config "$(BUILD_TYPE)" --parallel $(if $(strip $(JOBS)),"$(JOBS)")

# Build first, propagate failures, and show diagnostics for failing tests.
test: build_test
	CLAUSE_TEST_MODE="$(TEST_MODE)" $(CTEST) --test-dir "$(BUILD_DIR)" -C "$(BUILD_TYPE)" \
		--output-on-failure --no-tests=error --parallel "$(TEST_JOBS)" $(if $(filter fast,$(TEST_MODE)),-LE full_only)

# Run every policy/driver combination and full-only tests, e.g. at feature completion.
test-full:
	+$(MAKE) test TEST_MODE=full

# Manual only, never part of the test gate: build Release clau and the runtime, then compare with OTP.
benchmark:
	$(CMAKE) -S . -B "$(BENCH_BUILD_DIR)" $(CMAKE_ARGS) \
		-DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
		-DCLAUSE_BUILD_COMPILER=ON -DCLAUSE_BUILD_RUNTIME=ON
	+MAKEFLAGS= $(CMAKE) --build "$(BENCH_BUILD_DIR)" --config Release --target clau clause_runtime --parallel $(if $(strip $(JOBS)),"$(JOBS)")
	$(PYTHON) benchmarks/run.py --build-dir "$(BENCH_BUILD_DIR)" --config Release $(if $(strip $(JOBS)),--jobs "$(JOBS)") $(BENCH_ARGS)

# Format project C++ files changed since HEAD (including untracked files).
format fmt:
	@test -n "$(CLANG_FORMAT)" || { echo "clang-format is required" >&2; exit 1; }
	@{ git diff --name-only HEAD --; git ls-files --others --exclude-standard; } | sort -u | \
		grep -E '^(compiler|runtime|abi|tests)/.*\.(cpp|cc|cxx|hpp|h|hh|hxx)$$' | \
		while IFS= read -r file; do [ ! -f "$$file" ] || "$(CLANG_FORMAT)" -i --style=file "$$file" || exit 1; done

# Format every project C++ file, excluding generated and vendored build trees.
format-all:
	@test -n "$(CLANG_FORMAT)" || { echo "clang-format is required" >&2; exit 1; }
	@find compiler runtime abi tests -type f \( -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' -o -name '*.hpp' -o -name '*.h' -o -name '*.hh' -o -name '*.hxx' \) -print0 | xargs -0 "$(CLANG_FORMAT)" -i --style=file

clean:
	rm -rf build/ cmake-build*/
