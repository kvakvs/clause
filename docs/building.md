# Building Clause

How to build `clau` and `clause_runtime`, configure the build, run the tests
and use the developer tools. Test design and results are in
[validation](validation.md).

## Requirements

- CMake 3.28+ and a C++23 compiler; on Windows, Visual Studio's **Desktop
  development with C++** workload with a Windows SDK, Ninja and a global
  LLVM/Clang installation (`clang-cl`, and `clang` for linking programs).
- LLVM 23.1.x (23.1.1 or newer) C++ SDK for the compiler. A global installation
  is preferred; otherwise CMake downloads the pinned 23.1.2 SDK into
  `thirdparty/` on Windows x64/ARM64, Linux x64/ARM64 and macOS ARM64
  ([LLVM SDK](compile.md#llvm-sdk)). The LLVM command-line tools alone do not
  include the SDK.
- Boost 1.90+ (Multiprecision, Asio, and Parser for the compiler; header-only),
  toml++ 3.4.0, SQLite 3.54.0, PCRE2 10.47 and zlib 1.3.2. CMake downloads
  missing ones into `thirdparty/` with SHA-256 checks and reuses them offline,
  also for fresh build directories; on Linux and macOS, Boost and toml++ come
  from an installation or `thirdparty/`
  ([build dependency](projects.md#build-dependency)).
- Python 3 for tests and the quality, benchmark and listing tools.
- Erlang/OTP is not needed to build or test. Tests use
  [project-owned golden fixtures](../tests/fixtures/patternmatch/generated/README.md);
  only parse transforms, optional live audits, fixture regeneration and
  benchmarks need an OTP 29 installation.

On macOS (Erlang optional):

```sh
xcode-select --install
brew install cmake boost tomlplusplus llvm@23 erlang
```

## Configure and build

```sh
cmake --preset debug                    # build/debug, single configuration
cmake --build --preset debug
```

On Windows, from an x64 developer shell with `clang-cl` and `ninja` on `PATH`:

```powershell
cmake --preset windows                  # build/windows, Ninja Multi-Config
cmake --build --preset windows-debug
cmake --build --preset windows-release
```

The `debug` preset puts `clau` in `build/debug/bin`; the `windows` preset in
`build/windows/bin/<Config>/clau.exe`, with the runtime in
`build/windows/lib/<Config>/clause_runtime.lib`. `clau` finds the runtime
archive and the `library/` modules relative to its own location.

## Wrappers

Environment overrides: `BUILD_DIR`, `TOOL_BUILD_DIR`, `BUILD_TYPE`, `JOBS`,
`CMAKE`, `CTEST`, `CMAKE_ARGS`, `CLANG_FORMAT`.

| Makefile | Windows | Does |
| --- | --- | --- |
| `make build` | `make-build.bat` | Build `clau` in `build/tool` without tests (the batch script builds Release and the runtime too) |
| `make test`, `make test-full` | `make-test.bat` | Configure `build/debug` with tests, build everything, run CTest |
| `make format`, `make format-all` | `make-format.bat` | clang-format changed (or, with `FORMAT_SCOPE=all`, all) C++ files |
| `make benchmark` | `make-benchmark.bat` | Release build in `build/bench`, then compare with the host OTP |
| `make clean` | `make-clean.bat` | Remove `build/` and `cmake-build*/` |

The batch scripts enter the Visual Studio x64 environment themselves when
needed, add `%ProgramFiles%\LLVM\bin` to `PATH` when `clang-cl` is missing and
reconfigure a build directory made by another generator or compiler; set
`CLAUSE_TOOLCHAIN=default` to keep CMake's own choice. `clau.bat` (Windows) and
`run-macos.sh` build `build/tool` first, then run `clau` with all arguments,
keeping the caller's working directory and exit code.

## Configure options

| Option | Purpose |
| --- | --- |
| `-DBUILD_TESTING=ON` | Build tests and their helper programs (default OFF) |
| `-DCLAUSE_BUILD_COMPILER=OFF` | Build only the LLVM-free runtime library |
| `-DCLAUSE_BUILD_RUNTIME=OFF` | Build only the compiler |
| `-DLLVM_DIR=/prefix/lib/cmake/llvm` | Select an LLVM 23.1.x SDK (an invalid explicit path fails) |
| `-DCLAUSE_DOWNLOAD_LLVM=OFF` | Require an installed SDK |
| `-DCLAUSE_BOOST_ROOT=...`, `-DCLAUSE_TOML_ROOT=...`, `-DCLAUSE_SQLITE_ROOT=...`, `-DCLAUSE_PCRE2_ROOT=...` | Select a dependency source or installation (offline setup) |
| `-DCLAUSE_CLANG_EXECUTABLE=C:/path/to/clang.exe` | Select the Windows Clang executable |
| `-DCLAUSE_OTP_AUDITS=ON`, `-DCLAUSE_ESCRIPT=/path/to/escript` | Enable optional live OTP audits and select the OTP 29 used |
| `-DCLAUSE_TEST_DEBUG_INFO=ON` | Build test programs with debug information (to debug a crash) |
| `-DCLAUSE_OPTIMIZED_COMPILER=OFF` | Build `clau` and the compiler libraries unoptimized in Debug (to debug a `clau` crash) |

Windows notes: the default CRT is `/MDd` (Debug) or `/MD`; an explicit
`CMAKE_MSVC_RUNTIME_LIBRARY` must match the LLVM SDK. MSVC `cl` works in a
separate build directory; MinGW is not supported. Project targets treat warnings
as errors and include dependencies as `SYSTEM` headers, so avoid adding
dependency paths through global `-I` flags such as `CXXFLAGS` (in CLion, clear a
cached value with `-DCMAKE_CXX_FLAGS:STRING=`); use the root options instead.

## Tests

```sh
cmake --preset debug -DBUILD_TESTING=ON
cmake --build --preset debug
ctest --preset debug-fast                # or windows-debug-fast
ctest --preset debug -j 16               # full mode
```

`CLAUSE_TEST_MODE=fast` (the `*-fast` presets, `make test`, `make-test.bat`)
runs a reduced optimization/driver matrix and skips `full_only` tests; `full`
(the default when unset, `make test-full`, `TEST_MODE=full`) runs everything.
Each test reserves two CTest slots, so `-j N` runs N/2 tests at once.

## Quality gate

Lizard and clang-tidy, pinned in `tools/requirements-quality.txt` and installed
into `.venv-quality/` (use `.venv-quality/bin/python` outside Windows):

```sh
python -m venv .venv-quality
.venv-quality/Scripts/python -m pip install -r tools/requirements-quality.txt
cmake --build build/debug --target check-quality       # changed files
cmake --build build/debug --target check-quality-all   # whole tree
```

`check-quality` checks files changed since `HEAD` (or `CLAUSE_QUALITY_BASE`)
plus one translation unit per changed header (`check-quality-all` covers every
dependent) and needs a build with both compiler and runtime enabled;
`check-complexity-all` and `check-clang-tidy-all` run one tool.
`cmake/CheckClangTidy.cmake` accepts `QUALITY_SHARD=K/N` to split a long run.

## Benchmarks and listings

`make benchmark` / `make-benchmark.bat` build a Release `clau` in `build/bench`
and compare the `benchmarks/bench_*.erl` programs against the host OTP, and
with `--baseline REV` against another Clause commit; `benchmarks/run.py --help`
lists the options
([benchmarks](validation.md#benchmarks)). They are never part of the tests.

`benchmarks/compile_times.py` times `clau` itself: it runs a compile workload
(`--workload W1`, or `clau` arguments after `--`) several times on the
performance cores with `--no-cache --print-stage-times`, and reports the median,
minimum and spread of every phase and inference pass, the slowest inference
components, and whether the pass counters and stdout agree between runs. With
`--baseline <clau.exe>` the two builds alternate and the summary shows their
ratio. It refuses to time while builds, tests or another `clau` run, unless
given `--force-noisy`, and writes JSON to `build/bench/compile-times/`.

`decompile-debug.bat <module>` and `decompile-release.bat <module>` write the
Debug or Release assembly of one Clause C++ source to `<module>.debug.asm` /
`<module>.release.asm`, with source lines and demangled names (`tools/asm.py`,
clang-cl builds; `--function TEXT` filters).
