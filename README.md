# Clause

An ahead-of-time compiler project for Erlang/OTP 29. It preprocesses, parses,
analyzes and compiles a subset of Erlang to LLVM IR, bitcode and native objects:
ordered function clauses with guards, body matches and recursive local/remote
calls with proper tail calls over atoms, arbitrary integers, floats, tuples,
lists, maps, bitstrings and tuple records. Generated code runs through native
harnesses linked with the separate runtime; a production executable launcher is
not implemented yet.

All APIs are project-internal C++23. Native generated-code execution is
validated on Windows x64; objects are inspected for seven targets. See the
[documentation index](docs/README.md), in particular [compilation](docs/compile.md),
[runtime](docs/runtime.md) and [validation status](docs/validation.md).

The [compiled-module example](examples/compile/) contains two Erlang modules
(`clau -o demo answer.erl client.erl` builds a program from them),
a C++ harness and an LLVM-free CMake runtime link recipe. Follow the
[emission, inspection and native execution commands](docs/compile.md#run-the-compiled-module-example).

## Features

- Preprocessing: macros, includes, conditional compilation and language features.
- OTP 29 syntax: expressions, patterns, records, bitstrings, types/specifications,
  control flow and comprehensions.
- Syntax checking, expanded Erlang source output and an indented syntax-tree view.
- Source diagnostics and multiple input files.
- Compilation subset checks, parameter bindings, batch call resolution and declared
  type/specification analysis; see [semantic analysis](docs/semantic.md).
- LLVM O0/O2/Os compilation, explicit per-module artifacts, IR snapshots and declared/inferred type reports.
- Native executables from positional inputs (`clau -o app a.erl b.erl`) or project
  targets (`clau --project app.toml`), linked by Clang with the runtime library; see
  [executables](docs/executables.md).
- TOML projects with named targets, source discovery, per-target frontend options,
  and annotated starter files.
- Parse transforms (`-compile({parse_transform, M})`), run on an Erlang/OTP 29
  installed on the host while compiling; see [Parse transforms](#parse-transforms).

## Build

Requirements:

- CMake 3.28+ and a C++23-capable compiler.
- On Windows, an installed Clang executable on `PATH` or under `Program Files/LLVM/bin`;
  configuration fails immediately if it is missing or cannot run.
- LLVM 23.1.x (>=23.1.1) C++ SDK for compiler builds. CMake prefers a global
  installation, then downloads the pinned 23.1.2 SDK into `thirdparty/` if missing;
  see [SDK setup and compilation contract](docs/compile.md).
- Boost 1.90+ with Boost.Multiprecision for compiler and runtime; the compiler also
  requires Boost.Parser. Multiprecision is header-only and needs no Boost binary library.
- toml++ 3.4.0 for project manifests; see [dependency setup](docs/projects.md#build-dependency).
- Normal builds and tests do not require Erlang/OTP. Tests use
  [project-owned golden fixtures](tests/fixtures/patternmatch/generated/README.md).
  Optional live audits and explicit fixture regeneration require Erlang/OTP 29+.

On macOS:

```sh
xcode-select --install
brew install cmake boost erlang tomlplusplus llvm@23
```

On Windows, install LLVM/Clang globally for the host compiler tools.
Install Visual Studio's **Desktop development with C++** workload and a Windows SDK,
plus Ninja. Use an **x64 Native Tools** command prompt or Developer PowerShell
configured for x64, with `clang-cl`, `cmake`, and `ninja` on `PATH`.
CMake checks the MSVC ABI, Windows SDK linkage, and C++23 `std::expected` support.
The LLVM command-line tools alone do not provide the required C++ SDK; CMake
downloads it separately when `LLVMConfig.cmake` and matching libraries are absent.

CMake downloads Boost 1.90.0 and toml++ 3.4.0 into the ignored `thirdparty/`
directory on first use, verifies their SHA-256 checksums, and retains both archives
and extracted sources. Subsequent configurations, including fresh build directories,
reuse those files without network access. Removing `build/` does not remove dependencies.
When CMake's own TLS stack cannot verify certificates (for example the CMake bundled
with Strawberry Perl), downloads are retried with the host `curl` and still verified.
Runtime-only builds download only Boost. Explicit dependency roots below take
precedence and allow offline setup; an invalid explicit root fails without downloading.
Linux and macOS continue to use installed dependencies or local `thirdparty/` sources.
LLVM SDK fallback downloads apply on **all three platforms**, with SHA-256-pinned
official archives for Windows x64/ARM64, Linux x64/ARM64, and macOS ARM64.
Archives and extracted SDKs stay in `thirdparty/` for offline reuse. Other host
architectures and cross-builds require an explicit matching `LLVM_DIR`.

For compiler builds, LLVM's zlib and zstd dependencies are detected from installed
libraries. On Windows, missing libraries trigger SHA-256-verified downloads of
zlib 1.3.2 and zstd 1.5.7, followed by static Debug/Release builds under `thirdparty/`.
These builds are reused across build directories and match the host compiler,
architecture and CRT. Set `ZLIB_ROOT` or `zstd_ROOT` to prefer an installed library;
`CLAUSE_DOWNLOAD_ZLIB=OFF` and `CLAUSE_DOWNLOAD_ZSTD=OFF` disable the respective
fallbacks. MSVC SDK discovery rejects cached MinGW `.a` libraries such as those
bundled with Strawberry Perl. Linux and macOS use installed development packages.

From the repository root:

```sh
cmake --preset debug
cmake --build --preset debug
```

On Windows, use the clang-cl/Ninja Multi-Config preset from the developer shell:

```powershell
cmake --preset windows
cmake --build --preset windows-debug
cmake --build --preset windows-release
```

Tests and their helper executables are opt-in. Configure explicitly for testing:

```sh
cmake --preset debug -DBUILD_TESTING=ON
cmake --build --preset debug
ctest --preset debug --no-tests=error
```

On Windows:

```powershell
cmake --preset windows -DBUILD_TESTING=ON
cmake --build --preset windows-debug
ctest --preset windows-debug --no-tests=error
cmake --build --preset windows-release
ctest --preset windows-release --no-tests=error
```

Tests have two modes, selected by `CLAUSE_TEST_MODE`:

- `fast` for development: golden corpora run two policy/driver combinations
  (O0 positional and O2 without specialization through a project) instead of all
  eight, mutation cases run once, and tests labelled `full_only` (separate CMake
  consumer projects and timing measurements) are excluded.
- `full` (the default when unset) runs every combination and every test; use it
  when a major feature is complete.

```sh
ctest --preset debug-fast            # or windows-debug-fast; tests use half the logical CPUs
ctest --preset debug -j 32           # full mode; -j N runs N/2 tests at once
make test                            # fast; make test-full or TEST_MODE=full for full
```

`make-test.bat` follows the same `TEST_MODE` (default `fast`) and `TEST_JOBS` (default
all logical CPUs) settings. Every test reserves two CTest processor slots (`PROCESSORS 2`),
so a slot count of N runs at most N/2 tests concurrently, leaving room for the compilers and
nested native builds the tests start. On a 32-thread Windows x64 host, fast mode takes about
one minute and full mode about 85 seconds with 16 jobs (729 seconds serially).

For a build directory without a preset, pass `-DBUILD_TESTING=ON` to `cmake -S . -B <dir>`
for testing. CMake caches this setting; pass `-DBUILD_TESTING=OFF` when reusing that
directory for ordinary builds. The normal presets and build wrappers set it to `OFF`.

The batch scripts mirror the Makefile's `build`, `format`, and `clean` targets:
`make-build.bat`, `make-test.bat`, `make-format.bat`, and `make-clean.bat`. The build and test
scripts configure clang-cl with Ninja Multi-Config, like the `windows` preset, entering the
Visual Studio x64 environment themselves when the shell is not a developer shell and adding
`%ProgramFiles%\LLVM\bin` to `PATH` when `clang-cl` is missing. A build directory configured
with another generator or compiler is reconfigured from scratch; set `CLAUSE_TOOLCHAIN=default`
to keep CMake's own choice (for example MSVC `cl` in a separate directory). Build defaults are
`BUILD_DIR=build/debug` (testing), `TOOL_BUILD_DIR=build/tool` (`make build`, `make-build.bat`, `clau.bat`,
`run-macos.sh`: testing off, so they never reconfigure the testing build), `BUILD_TYPE=Debug`, and native build tool
parallelism
(override with `JOBS=N`); environment variables
`CMAKE`, `CTEST`, `CMAKE_ARGS`, and `CLANG_FORMAT` also override the corresponding tools/options.
`make-test.bat` configures with testing enabled, builds all targets, then runs CTest,
matching `make test` and propagating configuration, build, or test failures.
Clean removes repository-local `build/` and `cmake-build*/` directories.

Quality checks and formatting default to changed files: everything differing from `HEAD`
in the working tree, plus untracked files.

The quality tools live in the ignored `.venv-quality/` environment, pinned by
`tools/requirements-quality.txt`:
`python -m venv .venv-quality` then `.venv-quality/Scripts/python -m pip install -r tools/requirements-quality.txt`
(`.venv-quality/bin/python` outside Windows).

- `cmake --build build/debug --target check-quality` runs Lizard on changed production
  C++ files and clang-tidy on changed translation units plus those including a changed
  header (from Ninja's recorded dependencies). Changes to `.clang-tidy`, `cmake/` or
  production `CMakeLists.txt`, or missing dependency data, check every translation unit.
  Set `CLAUSE_QUALITY_BASE` (for example `origin/master`) to compare with another base.
- `check-quality-all` (and `check-complexity-all`, `check-clang-tidy-all`) check every file.
- clang-tidy runs in batches of `4 x jobs` translation units and prints a passed/FAILED line
  after each; every batch runs, then the check fails if any batch did. To split a long run
  into shorter separate invocations, call the script directly with a shard, for example
  `cmake -DQUALITY_SCOPE=all -DQUALITY_BUILD_DIR=build/debug -DQUALITY_SHARD=1/4 -P cmake/CheckClangTidy.cmake`
  for the first quarter (`QUALITY_BATCH` and `QUALITY_JOBS` override the batch size and jobs;
  jobs default to half of the logical cores).
- `make format` / `make-format.bat` format changed C++ files; `make format-all` or
  `FORMAT_SCOPE=all make-format.bat` formats everything.

`clau.bat --help` builds first, then forwards all arguments to the selected
configuration's executable. Build failures stop execution; compiler input paths
remain relative to the caller's working directory, and its exit code is preserved.

For runtime development without downloading or using the LLVM C++ SDK, configure with
`cmake --preset windows -DCLAUSE_BUILD_COMPILER=OFF`; the same build/test presets
apply. Set the option back to `ON` when the SDK is available. MSVC `cl` is also
accepted in a separate build directory; MinGW is not supported for native Windows
development builds. Select x86 or x64 through the developer environment (or `-A`
with a Visual Studio generator), and use an LLVM SDK and runtime of that architecture.

The Windows default CRT is `/MDd` for Debug and `/MD` for other configurations,
including the static runtime library and its consumers. An explicit
`CMAKE_MSVC_RUNTIME_LIBRARY` setting is preserved; it must match the LLVM SDK and
all linked C++ libraries. Avoid mixing Debug and Release STL/CRT artifacts.
Windows project sources and CLI/path fixtures use UTF-8.

All project targets use C++23 and treat compiler warnings as errors.
The executable is `build/debug/bin/clau`. Build presets and wrappers request
parallel builds on Windows, Linux, and macOS using the native build tool's default
job count (`jobs: 0` in presets, `--parallel` in wrappers). Set an explicit limit
with `cmake --build --preset debug --parallel 8` or `JOBS=8` for the wrappers.
For a build directory without a preset, use `cmake --build <dir> --parallel`.
The Windows preset places the executable in `build/windows/bin/<Config>/clau.exe`
and the runtime in `build/windows/lib/<Config>/clause_runtime.lib`.

Alternatively, use `make build` to build only `clau` and its dependencies in `build/tool`,
or `make test` to build and run the full test suite. For a different configuration:

```sh
make test BUILD_DIR=build/release BUILD_TYPE=Release JOBS=4
```

CMake discovers installed Boost and Erlang, including Homebrew installations.
Pass these options when configuring to override defaults:

| Option                                      | Purpose                                                       |
|---------------------------------------------|---------------------------------------------------------------|
| `-DCLAUSE_BOOST_ROOT=/path/to/boost`    | Select a Boost installation or full source tree               |
| `-DCLAUSE_TOML_ROOT=/path/to/tomlplusplus-3.4.0` | Select the pinned TOML dependency |
| `-DCLAUSE_OTP_AUDITS=ON` | Enable optional live OTP/reference audits; default is OFF |
| `-DCLAUSE_ESCRIPT=/path/to/bin/escript` | Select OTP 29+ for optional audits; unused by normal tests |
| `-DCLAUSE_CLANG_EXECUTABLE=C:/path/to/clang.exe` | Select an installed Windows Clang executable |
| `-DLLVM_DIR=/prefix/lib/cmake/llvm` | Select an existing LLVM 23.1.x SDK (invalid explicit paths fail) |
| `-DCLAUSE_DOWNLOAD_LLVM=OFF` | Require an installed SDK; disable automatic LLVM downloads |
| `-DBUILD_TESTING=ON`                        | Enable tests, helper executables and their Erlang dependency (default: OFF) |
| `-DCLAUSE_BUILD_COMPILER=OFF`           | Build only the runtime library                                |
| `-DCLAUSE_BUILD_RUNTIME=OFF`            | Build only the compiler                                       |

For multi-configuration generators, add `--config Debug` when building and
`-C Debug` when testing. CMake-aware IDEs can open the repository using the
`debug` preset, or the `windows` preset with a Visual Studio development toolchain.

Boost and toml++ headers use CMake `SYSTEM` includes, keeping warnings as errors
for project code. On native macOS builds, CMake also marks Homebrew's linked include
directory as `SYSTEM` when its Boost headers resolve to the selected installation.
This keeps inherited flags such as `CXXFLAGS=-I/opt/homebrew/include` from exposing
Boost warnings, including when that flag is already cached by CMake.
For other installations, avoid adding dependency paths through global `-I` flags (including
`CXXFLAGS`): an ordinary include path can take precedence over a dependency's
system path. If CLion reports Boost warnings as errors, remove those global flags
and clear the cached value, for example:

```sh
cmake -S . -B cmake-build-debug -DCMAKE_CXX_FLAGS:STRING=
```

Then reload CMake in CLion. Use the dependency root options above to select
installations instead of adding global include flags.

## Usage

```sh
./build/debug/bin/clau --parse-check examples/project/src/main.erl
./build/debug/bin/clau --print-pp -I include -DDEBUG examples/project/src/main.erl
./build/debug/bin/clau --print-ast examples/project/src/main.erl
```

On macOS, `./run-macos.sh --parse-check examples/project/src/main.erl` builds first
and runs the latest executable, passing all arguments unchanged. It accepts `TOOL_BUILD_DIR`,
`BUILD_TYPE` and `JOBS` environment overrides.

```text
clau [options] <source.erl>...
  --project <path>        Read a TOML project instead of positional sources
  --target <name>         Select a target; repeat for more (default: all)
  --new-project <filename>  Create an annotated starter; append .toml when needed
  --preprocess-check       Check preprocessing only
  --parse-check            Preprocess and check syntax
  --print-pp               Print expanded Erlang source
  --print-ast              Print an indented syntax tree
  --print-source           Print each module as Erlang source
  --print-abstr            Print each module as OTP abstract format forms
  --parse-transform <mod>  Apply a parse transform to every module
  --transform-path <dir>   Directory of precompiled transform modules (.beam)
  --erl <path|none>        Host Erlang/OTP 29 erl that runs parse transforms
  --print-inputs           List the batch's sources, including modules added by reference; do not compile
  --print-env              Print the resolved compile environment as TOML and exit
  --print-types            Print each module as source annotated with inferred types
  --print-ir               Print verified IR with Erlang source comments before LLVM optimization
  --print-optimized-ir     Print verified IR with Erlang source comments after LLVM optimization
  --emit obj|llvm-ir|llvm-bc  Write one artifact per module
  --artifact-dir <dir>     Override the artifact root (requires --emit)
  --target-triple <triple>  Select the machine/OS/ABI
  -O0 / -O2 / -Os         Generic O0 (default) / speed / size optimization
  --no-type-specialization  Disable compiler variants at either optimization level
  -g                      Emit Erlang line tables; executables keep them for debuggers
  --lto                   Link modules as bitcode with link-time optimization (LLD targets)
  --verbose               Trace files and compilation phases to stderr
  -I, --include <dir>      Add an include directory (last supplied searched first)
  -D, --define <name[=term]>  Define a macro (default value: true)
  --app-dir <app=dir>      Set an include_lib application directory
  --enable-feature <name>  Enable a language feature
  --disable-feature <name> Disable a language feature
  -h, --help              Show all options
  --version               Show version
  --                      Treat remaining arguments as input paths
```

Quote paths containing spaces and macro values containing shell punctuation:

```sh
./build/debug/bin/clau --parse-check -I include '-DVERSION={1,0}' \
  --app-dir myapp=examples/project examples/project/src/main.erl
```

Check modes are silent on success; diagnostics go to stderr. Print modes write
to stdout and can be combined: `--print-pp --print-ast` prints source before the
tree for each input. Adding `--preprocess-check` does not disable parsing requested
by `--parse-check` or `--print-ast`. Errors may leave partial printed output.

With no check/print action, source inputs and `--project` run the complete pipeline
through verified native object buffers in memory. Positional inputs form one batch;
each project target forms its own batch. `--emit` writes artifacts under `build/aot`
or `--artifact-dir`; projects append an encoded target name and use a manifest-relative
default root. Filenames encode module identity. `-o PATH` links positional inputs, or one
selected project target, into an executable with Clang and the runtime library (entry:
`--entry MODULE[:FUNCTION]`, manifest `entry`, or the only module exporting `main/1`). Without
`-o`, project targets with an `output` or `entry` key link to their TOML `output` (default
`build/<target>`), publishing only after every selected target linked.
See [executables](docs/executables.md) for the entry, argument and exit-status contract.

```sh
clau -O2 -o demo answer.erl client.erl && ./demo
clau -O2 --emit obj answer.erl client.erl
clau --print-ir --print-optimized-ir -O2 answer.erl
clau --print-types answer.erl client.erl
```

IR inspection allows both stages together and stops before object emission. Multiple
snapshots are separate modules; use `--emit llvm-ir` for individual assembly files.
Type inspection stops before LLVM and prints each module as source with inferred
function signatures and trailing `% Type` comments on each line's outermost expression. It accepts preprocessing/project/verbosity options, but rejects other
actions, output destinations and backend policy. See [compilation options](docs/compile.md).

`--verbose` prints `[pp] <filename>` for source files and resolved preprocessor
includes, and `[parse] <filename>` when each source enters the parser. Nested and
library includes are traced as they are loaded; inactive includes are skipped.
The parser consumes expanded tokens incrementally, so its trace can precede include
traces. `[comp]` adds semantic/backend phases and bounded specialization decisions
as they start. Tracing goes to stderr in every mode, including projects.

Exit codes: **0** for success (including warnings), **1** for source/project errors,
**2** for usage errors or unknown target names.
Each input has independent preprocessing state; any source error makes the overall command fail.

Syntax checks do not validate semantics or execute parse transforms. Check/print
modes do not create output files and reject `-o`/`--output`. Default compilation
without `-o` writes no executable.

See [preprocessing](docs/preprocessor.md), [parser usage](docs/parser.md) and
[validation status](docs/validation.md) for further details.

## Parse transforms

A module that names a parse transform (`-compile({parse_transform, M})`, or
`--parse-transform M` for every module) is compiled with the transform applied,
as `erlc` would: Clause hands its forms to `M:parse_transform/2` running on an
Erlang/OTP 29 installed on the host and compiles what comes back. OTP is needed
only while compiling such modules; the executables never need it.

```sh
# erl on PATH (or the default Windows installation) is found automatically.
clau -o app src/app.erl
# Choose the host OTP, and where precompiled transform modules (.beam) live.
clau --erl /usr/local/lib/erlang/bin/erl --transform-path deps/lager/ebin -o app src/app.erl
# See what the transforms produced, as erlc +to_abstr writes it.
clau --print-abstr src/app.erl
```

OTP's own transforms (`ms_transform`, `qlc`) work as they are; transforms from
other libraries must be compiled into `.beam` files with the host OTP first.
See [parse transforms](docs/transforms.md) for lookup order, options and
messages.

## Projects

Run the included two-target example:

```sh
./build/debug/bin/clau --parse-check --project examples/project/project.toml
./build/debug/bin/clau --print-ast --project examples/project/project.toml --target app
./build/debug/bin/clau --preprocess-check --project examples/project/project.toml --target tests --target app
```

Create an annotated project in an existing directory:

```sh
mkdir -p build/project-demo
./build/debug/bin/clau --new-project build/project-demo/demo
mkdir -p build/project-demo/src
cp examples/project/src/main.erl build/project-demo/src/main.erl
./build/debug/bin/clau --parse-check --project build/project-demo/demo.toml
```

Creation writes only the requested TOML file and refuses existing destinations.
The starter contains one `app` target using `src`, with all frontend options at
their defaults. Add source files before checking it.

Manifest paths are relative to the TOML file; CLI paths are relative to the
invocation directory. All targets run by default. Repeat `--target` to choose an
ordered subset; repeat selections run once. `sources` supports literal filenames
and `*`, `?`, `**` patterns; `source_dirs` recursively discovers `.erl` files.
Source search paths only locate explicitly listed files. CLI include paths take
precedence, CLI application roots replace matching names, CLI feature settings
apply last, and duplicate macro definitions remain errors.

See [project format and workflows](docs/projects.md). Projects support every
CLI action; executable generation remains unimplemented.

The [admitted guard catalog](docs/guards.md) includes checked
`is_integer/3`, qualified calls and top-level legacy tests; process/node and native
record identities retain explicit capability diagnostics.
