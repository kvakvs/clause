# Clause

An ahead-of-time compiler for Erlang/OTP 29. `clau` preprocesses, parses and
analyzes Erlang sources, lowers them through LLVM and links them with the
separately built C++ runtime library `clause_runtime` into native executables.
The goal is to build existing pure Erlang projects into runnable programs that
keep Erlang's semantics: isolated process heaps, preemptive scheduling, message
passing and garbage collection.

All APIs are project-internal C++23. The [documentation index](docs/README.md)
lists every reference note; [differences from Erlang/OTP](docs/differences.md)
records observable behavior that differs.

## Status

What works today:

- **Language:** the OTP 29 grammar, preprocessor (macros, includes,
  conditionals, features), function clauses with guards, all pattern contexts,
  `case`/`if`/`maybe`, `try`/`catch` with stack traces, list/binary/map
  comprehensions, funs and closures, dynamic calls and `apply`, tuple and native
  records, maps, bitstrings, arbitrary-size integers and floats, proper tail
  calls, `-import`, `-behaviour`, `module_info/0,1` and `#!` escripts. See
  [compilation](docs/compile.md), [patterns](docs/patterns.md),
  [guards](docs/guards.md), [funs](docs/funs.md) and
  [native records](docs/native-records.md).
- **Processes:** `spawn` and friends, scheduler worker threads (one per logical
  processor by default), reduction-based preemption, `!`, selective `receive`
  with `after` timeouts, links, `trap_exit`, monitors, aliases, registered
  names, timers and the process dictionary ([processes](docs/processes.md)).
- **Memory:** per-process heaps with a copying garbage collector, shared
  reference-counted binaries, optional heap/stack/memory caps
  ([runtime heap](docs/runtime-heap.md)).
- **I/O:** standard input/output, files, subprocesses (`open_port`, `os:cmd/1`)
  and TCP/UDP sockets through `gen_tcp`, `gen_udp` and `inet`
  ([ports](docs/ports.md), [console output](docs/io.md)).
- **Library:** every `erlang` export is implemented or a named stub; Clause
  ships `lists`, `maps`, `binary`, `math`, `unicode`, `re` (PCRE2), `zlib`,
  `persistent_term`, `atomics`, `counters`, `io`, `file`, `os`, `code` and
  runtime stand-ins for ERTS-preloaded modules. Other OTP `kernel`/`stdlib`
  modules compile from an OTP source tree listed in a project's
  `source_search_paths` ([library](docs/library.md),
  [builtins](docs/builtins.md)).
- **Toolchain:** O0/O2/Os, type inference and O2 type specialization
  ([semantic analysis](docs/semantic.md),
  [specialization](docs/specialization.md)), link-time optimization (`--lto`),
  debug line tables for native debuggers (`-g`, [debugging](docs/debugging.md)),
  an opt-in per-function profiler ([profiling](docs/profiling.md)), parse
  transforms run on a host OTP ([parse transforms](docs/transforms.md)), TOML
  projects with SQLite-backed build caches ([projects](docs/projects.md)), and
  IR, assembly and inferred-type listings.

Platforms: native execution is validated on Windows x64 and Linux x86-64; the
32-bit x86 runtime and Linux AArch64/armhf (under qemu-user) have historical
results. macOS on Apple Silicon and native ARM hardware are not yet validated.
See [validation](docs/validation.md).

Not supported (by design or not yet):

- Distribution: a program is one non-distributed node, `nonode@nohost`.
- Dynamic code loading and hot code upgrades: executables are static and modules
  are never unloaded.
- NIFs and `-on_load`; `-compile(export_all)` and other behavior-changing
  compile options are rejected with a diagnostic.
- ETS tables, and the `socket`/`net` NIF modules (use `gen_tcp`, `gen_udp` and
  `inet`).
- Library functions not implemented yet raise `{not_implemented, F, A}` when
  called; deferred features report a single `notimpl` diagnostic
  ([features](docs/features.md)).

## Build

Requirements:

- CMake 3.28+ and a C++23 compiler; on Windows, Visual Studio's **Desktop
  development with C++** workload with a Windows SDK, Ninja and a global
  LLVM/Clang installation (`clang-cl`, and `clang` for linking programs).
- LLVM 23.1.x (23.1.1 or newer) C++ SDK for the compiler. A global installation
  is preferred; otherwise CMake downloads the pinned 23.1.2 SDK into
  `thirdparty/` on Windows x64/ARM64, Linux x64/ARM64 and macOS ARM64
  ([LLVM SDK](docs/compile.md#llvm-sdk)). The LLVM command-line tools alone do
  not include the SDK.
- Boost 1.90+ (Multiprecision, Asio, and Parser for the compiler; header-only),
  toml++ 3.4.0, SQLite 3.54.0, PCRE2 10.47 and zlib 1.3.2. CMake downloads
  missing ones into `thirdparty/` with SHA-256 checks and reuses them offline,
  also for fresh build directories; on Linux and macOS, Boost and toml++ come
  from an installation or `thirdparty/`
  ([build dependency](docs/projects.md#build-dependency)).
- Python 3 for tests and the quality, benchmark and listing tools.
- Erlang/OTP is not needed to build or test. Tests use
  [project-owned golden fixtures](tests/fixtures/patternmatch/generated/README.md);
  only parse transforms, optional live audits, fixture regeneration and
  benchmarks need an OTP 29 installation.

On macOS (Erlang optional):

```sh
xcode-select --install
brew install cmake boost tomlplusplus llvm@23 erlang
```

Configure and build from the repository root:

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

Wrappers (environment overrides: `BUILD_DIR`, `TOOL_BUILD_DIR`, `BUILD_TYPE`,
`JOBS`, `CMAKE`, `CTEST`, `CMAKE_ARGS`, `CLANG_FORMAT`):

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

Configure options:

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

Windows notes: the default CRT is `/MDd` (Debug) or `/MD`; an explicit
`CMAKE_MSVC_RUNTIME_LIBRARY` must match the LLVM SDK. MSVC `cl` works in a
separate build directory; MinGW is not supported. Project targets treat warnings
as errors and include dependencies as `SYSTEM` headers, so avoid adding
dependency paths through global `-I` flags such as `CXXFLAGS` (in CLion, clear a
cached value with `-DCMAKE_CXX_FLAGS:STRING=`); use the root options instead.

## Usage

Build a program from sources; the entry is the only module exporting `main/1`,
which receives the command-line arguments as strings:

```sh
clau -O2 -o demo examples/compile/answer.erl examples/compile/client.erl
./demo
clau -o app --entry app:start src/*.erl     # app:start/1, else app:start/0
```

On Windows `-o` adds `.exe` when the path has no extension. Without `-o`,
positional inputs compile in memory and report diagnostics only. See
[executables](docs/executables.md) for entry selection, arguments, exit status
and linking, and [the compiled-module example](examples/compile/) for a C++ host
that links emitted objects itself.

Projects describe targets in TOML; each target with an `output` or `entry` key
links to its output (default `build/<target>`), and outputs are replaced only
after every selected target succeeded:

```sh
clau --new-project myapp                        # writes an annotated myapp.toml
clau --project myapp.toml                       # build every target
clau --project examples/project/project.toml --target app --parse-check
```

See [projects](docs/projects.md) for the manifest, source discovery, ignored
sources, build directories and caches.

Common options (`clau --help` lists all):

| Option | Purpose |
| --- | --- |
| `-o, --output PATH`, `--entry M[:F[/A]]` | Link an executable and choose its entry |
| `--project PATH`, `--target NAME` | Build a TOML project, or selected targets |
| `-O0`, `-O2`, `-Os`, `--no-type-specialization` | Optimization (O0 default; O2 adds type specialization) |
| `-g`, `--lto`, `--keep-exports` | Debug line tables, link-time optimization, keep unreachable exports |
| `-I DIR`, `-D NAME[=TERM]`, `--app-dir APP=DIR` | Include paths, macros, `include_lib` roots |
| `--enable-feature F`, `--disable-feature F` | OTP language features |
| `--parse-transform M`, `--transform-path DIR`, `--erl PATH\|none` | Parse transforms and the host OTP running them |
| `--emit obj\|llvm-ir\|llvm-bc`, `--artifact-dir DIR` | Write one artifact per module (default root `build/aot`) |
| `--target-triple TRIPLE`, `--linker PATH`, `--runtime-library PATH` | Target, link driver and runtime archive |
| `--build-dir DIR`, `--no-cache`, `--cache-inference-in-dirs PAT` | Temporary files and build caches |
| `--verbose`, `--print-stage-times` | Trace phases; time each stage on stderr |

Inspection actions print to stdout and do not compile executables:

| Option | Prints |
| --- | --- |
| `--preprocess-check`, `--parse-check` | Nothing; diagnostics only |
| `--print-pp`, `--print-ast`, `--print-source`, `--print-abstr` | Preprocessed source, syntax tree, source, OTP abstract format |
| `--print-inputs` | Every source of the batch, including library modules added by reference |
| `--print-env` | The resolved compile environment as TOML |
| `--print-types` | Source annotated with inferred types |
| `--print-ir`, `--print-optimized-ir`, `--print-asm` | IR before/after LLVM optimization, assembly with IR and Erlang comments |

`clau` exits with 0 on success (warnings included), 1 for source or project
errors and 2 for usage errors. Details: [compilation](docs/compile.md),
[preprocessing](docs/preprocessor.md), [parser](docs/parser.md).

Parse transforms (`-compile({parse_transform, M})`) run on an Erlang/OTP 29
installed on the host (`erl` on `PATH`, or `--erl`), as `erlc` would; only the
compile needs OTP, never the executable. Transforms other than OTP's own must be
compiled to `.beam` first ([parse transforms](docs/transforms.md)).

### Runtime options

Programs read runtime options from the `CLAUSE_FLAGS` environment variable and
from leading command-line arguments, up to the first other argument or `--`:

```sh
./demo --schedulers 4 --max-heap 100000000 -- arg1 arg2
```

`--schedulers N`, `--max-atoms N`, `--max-heap BYTES`, `--max-stack BYTES`,
`--max-memory BYTES` and `--profile FILE` are described in
[runtime options](docs/executables.md#runtime-options).

## Testing

Tests are opt-in at configure time:

```sh
cmake --preset debug -DBUILD_TESTING=ON
cmake --build --preset debug
ctest --preset debug-fast                # or windows-debug-fast
ctest --preset debug -j 16               # full mode
```

`CLAUSE_TEST_MODE=fast` (the `*-fast` presets, `make test`, `make-test.bat`)
runs a reduced optimization/driver matrix and skips `full_only` tests; `full`
(the default when unset, `make test-full`, `TEST_MODE=full`) runs everything.
Each test reserves two CTest slots, so `-j N` runs N/2 tests at once. Tests
compile real Erlang sources through the CLI and compare executables with
OTP-generated goldens; see [validation](docs/validation.md) for the baseline,
fixtures and test design.

Quality gate (Lizard and clang-tidy, tools pinned in
`tools/requirements-quality.txt` and installed into `.venv-quality/`; use
`.venv-quality/bin/python` outside Windows):

```sh
python -m venv .venv-quality
.venv-quality/Scripts/python -m pip install -r tools/requirements-quality.txt
cmake --build build/debug --target check-quality       # changed files
cmake --build build/debug --target check-quality-all   # whole tree
```

`check-quality` checks files changed since `HEAD` (or `CLAUSE_QUALITY_BASE`)
plus translation units including a changed header, and needs a build with both
compiler and runtime enabled; `check-complexity-all` and `check-clang-tidy-all`
run one tool. `cmake/CheckClangTidy.cmake` accepts
`QUALITY_SHARD=K/N` to split a long run.

## Benchmarks and listings

`make benchmark` / `make-benchmark.bat` build a Release `clau` in `build/bench`
and compare the `benchmarks/bench_*.erl` programs against the host OTP and an
older Clause commit; `benchmarks/run.py --help` lists the options
([benchmarks](docs/validation.md#benchmarks)). They are never part of the tests.

`decompile-debug.bat <module>` and `decompile-release.bat <module>` write the
Debug or Release assembly of one Clause C++ source to `<module>.debug.asm` /
`<module>.release.asm`, with source lines and demangled names (`tools/asm.py`,
clang-cl builds; `--function TEXT` filters).
