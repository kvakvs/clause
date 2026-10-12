# Validation

## Current baseline

Plan 11 step 1, 2026-10-03, commit `b1a471f`, Windows x64: clang-cl 23.1.2,
LLVM SDK 23.1.2 (`/MT`, `_ITERATOR_DEBUG_LEVEL=0`), Lizard 1.24.0, clang-tidy
22.1.8, OTP pin `21776803ecd1` with oracle OTP 29.1.1 / ERTS 17.1.

| Check | Result |
| --- | --- |
| Fresh combined Debug CTest | 125/125, zero skips (729 s serial; 85 s with `-j 16`) |
| Fast-mode CTest (`debug-fast`) | 122 tests, about 60 s |
| `check-quality` | Pass; Lizard CCN 10, clang-tidy over 258 production units |
| Opt-in OTP audit tests | 14/14 |
| `regenerate.py --corpus all --check` | 19/19 corpora reproduce |
| Foreign O0/O2 objects | 7 targets inspected (Linux x86/x64/ARM/AArch64, Windows x86/x64, Apple Silicon) |

## Running the gate

From an x64 Visual Studio developer shell with `C:\Program Files\LLVM\bin` on
`PATH` (automatic SDK selection reuses `thirdparty/`):

```powershell
cmake --preset debug --fresh -DBUILD_TESTING=ON -G Ninja -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl
cmake --build build/debug
ctest --preset debug-fast                  # during development
ctest --preset debug -j 16                 # full mode, at feature completion
cmake --build build/debug --target check-quality      # changed files + dependents
cmake --build build/debug --target check-quality-all  # whole tree
```

- `CLAUSE_TEST_MODE=fast` runs golden corpora at O0 positional plus O2
  specialization-off project, runs mutations once and skips `full_only` tests.
  Full mode (default) runs all driver/policy combinations.
- `CLAUSE_TEST_TIMEOUT_SCALE=N` multiplies the per-run limits of the golden
  runner (60 s) and the semantic case runner (15 s) for instrumented builds.
- `check-quality`, `make format` and `make-format.bat` cover files changed since
  `HEAD` plus untracked files; `cmake/quality_scope.py` adds translation units
  that include a changed header. Changes to `.clang-tidy`, `cmake/` or production
  CMake select everything.
- Test programs carry no debug information (no PDBs); configure with
  `-DCLAUSE_TEST_DEBUG_INFO=ON` to rebuild them for a debugger. Nested
  native consumers link the parent build's runtime through
  `ClauseRuntimeTargets.cmake` instead of compiling it again; only
  `runtime_link` builds the runtime standalone.
- Thresholds and suppressions are never raised to pass the gate. Two
  third-party exceptions are configured (plan 11 steps 63-64, user decision):
  `cmake/tidy_filter.py` drops static analyzer reports located in Boost
  headers (Boost.Multiprecision limb-storage false positives on Linux that
  rewriting the calling code only moves) and counts them in the output, and
  `bugprone-exception-escape` ignores `bad_array_new_length` (MSVC's map/set
  move constructors allocate; allocation failure is fatal anyway).
- clang-tidy checks project headers on every host: the header filter accepts
  `/` and `\` separators (Windows paths never matched before step 63).

### Linux

The same gate runs on Linux with the pinned LLVM 23.1.2 Linux SDK as host
compiler (`CC`/`CXX` from its `bin/`), the system libstdc++, Ninja and CMake
3.28 or newer. Boost 1.90 and toml++ 3.4.0 come from installed packages or
are extracted into `thirdparty/` (`boost_1_90_0/`, `tomlplusplus-3.4.0/`);
only Windows downloads them. The release archive's `ld.lld` links ICU 70
(Ubuntu 22.04) and its `lldb` links Python 3.14; on hosts without them
`linking_lto` needs ICU 70 on `LD_LIBRARY_PATH`, and `linking_debugger` uses
GDB.

```sh
cmake --preset debug --fresh -G Ninja -DBUILD_TESTING=ON
cmake --build build/debug
ctest --preset debug -j 16
```

`check-quality` (Lizard, clang-tidy) runs on the Windows development host
only; other hosts build and run the tests. Step 63 ran it once on Linux, which
found what the Windows run could not see (POSIX-only sources, libstdc++
`std::optional`), and left it clean.

### Other targets

32-bit and ARM targets are checked without a compiler built for them (no LLVM
SDK exists for those hosts):

- A runtime-only build (`-DCLAUSE_BUILD_COMPILER=OFF -DBUILD_TESTING=ON`) for
  the target runs the runtime and ABI tests there: natively, in a target
  userland (chroot), or through the host's emulator (qemu binfmt).
- `tests/compiler/executables/cross.py` runs every executable golden and
  program fixture with this host's `clau`, `--target-triple`, that build's
  runtime archive and a `--linker` that reaches the target's libraries
  (`run.py --target-option` passes them through).

## Fixtures and provenance

- Normal builds and tests need neither OTP nor its source checkout. Goldens were
  generated once from OTP and are committed with hashes; hash checks run before
  any fixture is used.
- Nineteen pattern/guard corpora hold 67,748 native expected values/errors and
  106 semantic acceptance rows. Each native corpus runs positional and project
  drivers at O0/O2 with specialization on/off, local and remote calls.
- Committed Erlang inputs are locally authored
  (`tests/fixtures/patternmatch/fragments/`, preprocessor `semantic/headers/`).
  A 2026-10-03 audit removed all copied OTP files; 60-token window comparison
  against 4,150 OTP Erlang and 1,190 C/C++ files found no remaining overlap
  besides a generated integer tuple. `fixture_sources` enforces isolation.
- Six end-goal program fixtures (`tests/fixtures/programs/`, plan 11 step 2)
  hold OTP stdout/exit-status goldens and today's compile diagnostics;
  `programs_compile` checks them OTP-free and
  `tests/compiler/programs/regenerate.py --check` reproduces them under OTP
  ([fixture map](../tests/fixtures/programs/README.md)).
- Term printing goldens (`tests/fixtures/printing/`, plan 11 step 4) hold OTP
  `~w` and `erlang:display/1` text for 9,542 values plus OTP stdout of compiled
  display calls; `tests/compiler/printing/regenerate.py --check` reproduces them
  ([fixture notes](../tests/fixtures/printing/README.md)).
- Executable golden cases (`tests/fixtures/executables/`, plan 11 step 8) are
  source directories plus one `golden.json` with OTP stdout/exit status and an
  authored stderr pattern; `tests/compiler/executables/run.py` links and runs
  each under the policy/driver matrix (CTest `executables_<case>`), and
  `regenerate.py --check` reproduces them under OTP
  ([case notes](../tests/fixtures/executables/README.md)).
- Regeneration and live audits are explicit:
  `-DCLAUSE_OTP_AUDITS=ON` and `tests/compiler/patternmatch/regenerate.py`
  ([instructions](../tests/fixtures/patternmatch/generated/README.md)).
  Nothing refreshes goldens or the pin silently.

## Test design

Behavior is tested through the real CLI, emitted objects, linked native consumers
and OTP goldens. Focused unit tests remain only where source cannot reach the
state, each with its purpose stated in the test:

- Injected budgets and allocation/IO faults (`project_limits`,
  `project_creation_failure`, `codegen_limits`, `codegen_write_failure`,
  `runtime_lifecycle_failure`, generated-call fault seams).
- Private ownership, invalid/stale handle and rollback invariants in the parser,
  semantic type graph and backend.
- Raw word validation and 32/64-bit term boundaries (`runtime_immediate`,
  `runtime_term_tag`, `abi_integers`).
- Diagnostic sink failures and the stable feature ID snapshot.

Filesystem capability cases (links, case aliases) report per-case skips; they
never stand in for a whole-test pass.

## Platform and sanitizer status

- Native generated-code execution: Windows x64; Linux x86-64 (plan 11
  step 63). 32-bit and ARM results are in the history.
- Apple Silicon: pending (step 66).
- Compiler and runtime ASan, UBSan and LeakSanitizer (plan 11 step 67): Linux
  x86-64, one combined Debug configuration
  (`-fsanitize=address,undefined -fno-sanitize=vptr -fno-sanitize-recover=undefined`),
  full CTest with `CLAUSE_TEST_TIMEOUT_SCALE=10` (instrumented code runs
  several times slower): no sanitizer report. The compiler, the runtime and
  the test programs are instrumented; programs `clau` links take the sanitizer
  runtime from a `clang++` wrapper on `PATH`, their generated code is not
  instrumented. The four injected host-refusal tests (`codegen_failure_*`,
  `runtime_lifecycle_failure`, `runtime_memory`) are outside ASan's scope:
  its `operator new` aborts on an impossible size instead of throwing.
- ThreadSanitizer (plan 11 step 68): Linux x86-64, the same instrumentation
  scope with `-fsanitize=thread`; full CTest and the concurrency tests five
  times each report no race after the `Executor::clear` lock-order fix. Out of
  scope as well: the two tests that replace `operator new`.
- On Windows the prebuilt LLVM SDK still conflicts with instrumented code
  (`annotate_string` 0 vs 1).
- Runtime-only ASan passes on Windows with Release probes,
  `/EHsc /fsanitize=address`, `/MT`, Clang's ASan import library and static
  runtime thunk, and the ASan DLL on `PATH`.
- Historical macOS arm64 runs (2026-09-19/20) passed full Debug, C++26,
  ASan+UBSan, compiler-only and runtime-only builds for the parser and project
  stages; they predate the backend.

## Benchmarks

Manual performance comparison with Erlang/OTP (plan 11 step 69A). It is never
part of CTest, the commit gate or `check-quality`, and only it needs a host
Erlang/OTP; results are host-specific evidence, not gate results.

```powershell
make-benchmark.bat                         # Windows; set JOBS=N to cap parallel builds
make-benchmark.bat --repeat 9 --only bench_gc,bench_msg
make benchmark BENCH_ARGS="--baseline HEAD~5"   # other hosts
```

- Both configure and build a Release `clau` and runtime in `build/bench`, then
  run `benchmarks/run.py` (`--help` lists its options).
- Programs: `benchmarks/bench_*.erl` with `bench_util.erl`, built unchanged by
  `erlc` and `clau -O2`. Each times its kernels with
  `erlang:monotonic_time/1` after one warm-up call and prints
  `BENCH <name> <microseconds>`: data generation, data processing, garbage
  collection, message passing, file I/O (in an empty temporary working
  directory) and loopback sockets.
- Systems, on the same host in one run, interleaved: OTP with its default
  flavor (JIT), OTP `-emu_flavor emu` when installed (the Windows installer
  ships only the JIT, so those columns read `n/a`), Clause HEAD at O2 and
  with `--lto`, and a previous Clause commit (default: the newest commit at
  least one day older than HEAD; `--baseline REV`, `--no-baseline`) built in a
  cached worktree under `build/bench-base/<hash>` that shares `thirdparty/`.
- Report: per kernel the median of `--repeat` runs (default 5) with spread,
  the change of Clause O2 against each system (`+N%`: Clause faster, the
  other takes N% longer; `-N%`: Clause takes N% longer; `(Fx)` the time
  ratio) with a geometric mean, `--lto` against OTP and O2, program wall
  times including startup, failures and timeouts (`--timeout`, 600 s per run).
  Iteration counts keep a default run under 10 minutes; Clause, the slowest
  system, sets the sizes, so some OTP kernels last only a few milliseconds.
- History: `benchmarks/history/<host>.json` keeps one record per run day
  (run date, HEAD hash and commit date, Clause, OTP and baseline medians),
  `benchmarks/history/reports/<host>/<day>-<hash>.txt` the text reports; a
  rerun on the same day replaces both. Records and reports run more than
  183 days before the newest are pruned; a run with uncommitted source
  changes is reported but not recorded. The history directory is
  gitignored: it stays on the host that ran it.

## History

Condensed from the former per-step records (originals in Git history up to
commit `2777c98`). Unless noted: Windows x64, LLVM 23.1.2, pin `21776803ecd1`,
oracle OTP 29.1.1 / ERTS 17.1. Test counts are full CTest passes with zero skips.

| Date | Milestone | Tests | Quality units | Notes |
| --- | --- | ---: | ---: | --- |
| 2026-09-19 | Parser phase VI (macOS arm64, OTP 29.1 `751f87b7`, oracle 29.0.5) | 46 | full | 344/344 productions witnessed; 10-file corpus |
| 2026-09-20 | Projects (macOS arm64) | 64 | full | Debug, compiler-only, ASan+UBSan |
| 2026-09-28 | Test migration to CLI workflows | 74/75 | — | Baseline 78/93; `parser_hardening` stack overflow later fixed with 8 MiB stack |
| 2026-09-28 | Windows gate repair | 75 | full | Lizard + clang-tidy clean |
| 2026-09-29 | Compiler milestone steps 1–46 | 103 | 182 | Compiler-only 80, runtime-only 16, runtime ASan 16 |
| 2026-10-01 | PG1 semantic matrix and evidence | 104 | — | Source hashes pinned |
| 2026-10-01 | PG2 failure channel | 108 | — | ABI rev 2 |
| 2026-10-01 | PG3 atoms | 109 | 189 | ABI rev 3 |
| 2026-10-01 | PG4 scoped bindings | 111 | 191 | 26 legality cases |
| 2026-10-02 | PG5 pattern semantics | 113 | 196 | 92 legality modules |
| 2026-10-02 | PG6 immediate matching | 114 | 199 | 34 calls |
| 2026-10-02 | PG7 immediate guards | 119 | 205 | 1,689 calls |
| 2026-10-02 | PG8 guard control flow | 120 | 207 | 2,075 calls |
| 2026-10-02 | PG9 ordered clauses | 121 | 208 | 1,020 calls |
| 2026-10-02 | PG10 body matches | 122 | 209 | 1,666 calls |
| 2026-10-02 | PG11 stable heap and roots | 123 | 213 | ABI rev 4 |
| 2026-10-02 | PG12 tuples/lists/strings | 125 | 221 | 4,801 calls |
| 2026-10-02 | PG13 arbitrary integers | 127 | 231 | 16,065 calls |
| 2026-10-02 | PG14 floats | 129 | 238 | 14,436 calls |
| 2026-10-02 | PG15 maps | 131 | 244 | 8,010 calls |
| 2026-10-02 | PG15a OTP-free goldens | 118 | 244 | 14 corpora, 49,959 values; audits opt-in |
| 2026-10-02 | PG16 bitstrings | 120 | 253 | 8,826 calls |
| 2026-10-03 | PG17 tuple records | 121 | 257 | 1,025 outcomes, 29 semantic cases |
| 2026-10-03 | PG18 guard catalog | 122 | 257 | 81 rows, 5,033 outcomes |
| 2026-10-03 | PG19 binding facts | 123 | 258 | 822 outcomes, 976 dominance checks |
| 2026-10-03 | PG20 closure | 124 | 258 | 19 corpora, 67,634 values, 1,969 seeded outcomes |
| 2026-10-03 | OTP source audit | 125 | 258 | Copied OTP files replaced by local fragments |
| 2026-10-03 | Plan 11 step 1 baseline | 125 | 258 | See current baseline |
| 2026-10-03 | Plan 11 step 2 program fixtures | 126 | 258 | Six OTP goldens; fast mode 123 tests; full `-j 16` 83 s |
| 2026-10-03 | Plan 11 step 4 term printing | 128 fast | 265 | 9,542 `~w`/display goldens; 154 compiled display calls in all policies; clang-tidy run with one job (concurrent runs crashed the tool on unchanged units) |
| 2026-10-04 | Plan 11 step 8 executable runner (phase B closed) | 138 (135 fast) | 272 | Cases `demo`, `exits` under eight policy/driver combinations; full `-j 16` 235 s |
| 2026-10-04 | Plan 11 step 8I classic heap (phase C closed) | 144 (140 fast) | 276 | Full `-j 16` 370 s: 143/144, `codegen_dependency` timed out at 120 s under load and passed alone in 29 s; Lizard-all 0 warnings; tidy-all passed with one job after a silent two-job tool exit |
| 2026-10-04 | Plan 11 step 9 `case` and `begin` | 146 (142 fast) | 114 changed | Fast 142/142; affected tests 7/7 in full mode; Lizard 0 warnings; tidy passed with one job after a silent two-job exit |
| 2026-10-05 | Plan 11 step 10 `if` | 147 (143 fast) | 114 changed | Fast 143/143; affected tests 8/8 in full mode; Lizard 0 warnings; tidy passed |
| 2026-10-05 | Plan 11 step 11 source raises | 148 (144 fast) | 124 changed | Fast 144/144; affected tests 22/22 in full mode; Lizard 0 warnings; tidy passed |
| 2026-10-05 | Plan 11 step 12 `catch Expr` | 149 (145 fast) | 277 | Fast 145/145; affected tests 25/25 in full mode; Lizard 0 warnings; tidy passed |
| 2026-10-05 | Plan 11 step 13 `try ... of ... catch` | 150 (146 fast) | 189 changed | Fast 146/146; Lizard 0 warnings; tidy passed |
| 2026-10-05 | Plan 11 step 14 `try ... after` | 153 (149 fast) | 2 changed | Fast 149/149; Lizard 0 warnings; tidy passed |
| 2026-10-05 | Plan 11 step 15 stack traces and `raise/3` | 154 (150 fast) | 129 changed | Fast 150/150; Lizard 0 warnings; tidy passed |
| 2026-10-05 | Plan 11 step 16 `maybe` (phase D closed) | 155 (151 fast) | 277 | Fast 151/151; full `-j 16` 155/155 in 259 s; Lizard-all 0 warnings; tidy-all passed |
| 2026-10-05 | Plan 11 step 18 recursive call graphs | 156 (152 fast) | 91 changed | Fast 152/152; affected tests 13/13 in full mode; Lizard 0 warnings; tidy passed |
| 2026-10-05 | Plan 11 step 19 explicit frames and tail calls | 157 (153 fast) | 278 | Fast and full CTest pass (with the step-20 case: 154/154, 158/158); clang-cl configure; Lizard 0 warnings; tidy passed |
| 2026-10-05 | Plan 11 step 20 deep body recursion and stack budget | 158 (154 fast) | 278 | Fast 154/154; full 158/158; Lizard 0 warnings; tidy passed |
| 2026-10-06 | Plan 11 step 21 list comprehensions | 159 (155 fast) | changed | Fast 155/155; affected tests full mode; Lizard 0 warnings; tidy passed |
| 2026-10-06 | Plan 11 step 22 binary and map comprehensions (phase E closed) | 160 (156 fast) | all | Fast 156/156; full `-j 12` 160/160 in 275 s; Lizard-all 0 warnings; tidy-all passed after fixing four new-code findings |
| 2026-10-06 | Plan 11 step 23 root inventory, `SafePoint`, live registers | 160 (156 fast) | 50 changed | Fast 156/156; Lizard 0 warnings; tidy passed |
| 2026-10-06 | Plan 11 step 26 collection from generated code | 161 (157 fast) | 63 changed | Fast 157/157; full `-j 12` 161/161 in 107 s; Lizard 0 warnings; tidy passed |
| 2026-10-06 | Plan 11 step 27 heap exhaustion after collection | 162 (158 fast) | 50 changed | Fast 158/158; full `-j 12` 162/162 in 108 s; Lizard 0 warnings; tidy passed |
| 2026-10-06 | Plan 11 step 27 correction: no default memory caps | 162 (158 fast) | 50 changed | Full `-j 12` 158/162 in 120 s, 4 `codegen_failure_*` fixed by expecting `out_of_memory`, then 10/10 affected; Lizard 0 warnings; tidy passed |
| 2026-10-06 | Plan 11 step 27 follow-up: no binary or process-count caps, `--max-atoms` | 164 (160 fast) | 281 | Fast 160/160; full `-j 12` 164/164 in 105 s; Lizard 0 warnings; tidy-all passed |
| 2026-10-06 | Plan 11 step 28 graph copies between heaps, shared off-heap buffers | 166 (162 fast) | 71 changed | Fast 162/162; full `-j 12` 166/166 in 130 s; Lizard 0 warnings; tidy passed |
| 2026-10-07 | Plan 11 step 29 record updates | 167 (163 fast) | 38 changed | Fast 163/163; Lizard 0 warnings; tidy passed; records corpus reproduces under OTP |
| 2026-10-07 | Plan 11 step 30 `record_info/2` | 168 (164 fast) | 42 changed | Fast 164/164; full `-j 12` 168/168 in 119 s; Lizard 0 warnings; tidy passed |
| 2026-10-07 | Plan 11 step 31B native record cells and `CLAUSE_record_v1` (ABI 6) | 169 (165 fast) | 283 changed | Fast 165/165; full `-j 12` 169/169 in 122 s; Lizard 0 warnings; tidy passed after two fixes |
| 2026-10-07 | Plan 11 step 31C local native records | 170 (166 fast) | 285 changed | Fast 166/166; full `-j 12` 170/170 in 146 s; Lizard and tidy passed after complexity fixes |
| 2026-10-07 | Plan 11 step 31D qualified and imported native records | 171 (167 fast) | 124 changed | Fast 167/167; full `-j 12` 171/171 in 129 s; Lizard 0 warnings; tidy passed after three fixes |
| 2026-10-07 | Plan 11 step 31E anonymous native records (step 31 closed) | 172 (168 fast) | 33 changed | Fast 168/168; full `-j 12` 172/172 in 124 s; Lizard 0 warnings; tidy passed after one fix |
| 2026-10-07 | Plan 11 step 32 function values `fun F/A`, `fun M:F/A` (ABI 7) | 173 (169 fast) | 289 changed | Fast 169/169; full `-j 12` 173/173 in 122 s; Lizard and tidy passed after complexity fixes |
| 2026-10-07 | Plan 11 step 33 closures with captured variables | 175 (171 fast) | 134 changed | Fast 171/171; full `-j 12` 175/175 in 126 s; Lizard and tidy passed after complexity fixes |
| 2026-10-07 | Plan 11 step 34 named funs | 176 (172 fast) | 132 changed | Fast 172/172; full `-j 12` 176/176 in 152 s; Lizard 0 warnings; tidy passed |
| 2026-10-07 | Plan 11 step 35 dynamic calls `M:F(Args)`, `apply/2,3`, runtime `fun M:F/A` (ABI 8) | 177 (173 fast) | 290 (analyzer config changed) | Fast 173/173; full `-j 12` 177/177 in 142 s; Lizard 0 warnings and tidy passed after one complexity and three tidy fixes |
| 2026-10-09 | Plan 11 step 63 Linux x86-64 (WSL2 Ubuntu 25.04, glibc 2.41, LLVM 23.1.2 SDK clang, libstdc++ 14, GDB 16.2) | 235 | 338 | Full CTest 235/235 in 161 s; Lizard and clang-tidy clean (7 analyzer reports in Boost headers ignored); Windows gate 235/235 with headers checked |
| 2026-10-09 | Plan 11 step 64 32-bit: Windows x86 (WoW64, clang-cl) and Linux i386 (Debian 13 chroot, clang 19) | 42 + 65 goldens | — | Runtime/ABI tests 42/42 natively on both; `cross.py` goldens 65/65 on both (`integer_limit` 64-bit-only runs skipped) |
| 2026-10-09 | Plan 11 step 65 Linux arm64 and armhf (Debian 13 sysroots, qemu-user 9.2) | 42 + 65 goldens | — | Runtime/ABI tests 42/42 each; goldens 64/65 each (`port_spawn` `enoent`: qemu `posix_spawn` gap), run limit 900 s |
| 2026-10-10 | Plan 11 step 67 ASan + UBSan + LeakSanitizer (Linux x86-64) | 235 | — | No sanitizer report; four injected host-refusal tests outside ASan's scope; `CLAUSE_TEST_TIMEOUT_SCALE=10` |
| 2026-10-10 | Plan 11 step 68 ThreadSanitizer (Linux x86-64) | 233 | — | One lock-order inversion fixed (`Executor::clear`); then no report in full CTest and 15 concurrency tests x5 |
| 2026-10-10 | Plan 11 step 65A `-behaviour`/`-behavior` and generated `behaviour_info/1` | 234 (fast) | 342 | Fresh configure; fast 234/234; Lizard 0 warnings; tidy passed after one fix (throwing static init); new `lint_diagnostics` (8 OTP-generated cases) and `executables_behaviours`; OTP `--check` reproduces 60 executable, 8 lint goldens; program goldens regenerated for the changed oracle hash only |
| 2026-10-10 | Plan 11 step 65B `module_info/0,1` and informational attributes | 235 (fast) | 344 | Fresh configure; fast 235/235; Lizard 0 warnings; tidy passed after splitting `parse_and_print`; new `executables_module_info` and lint cases `module_info_defined`, `exports_repeated` reproduce under OTP 29.1.1 |
| 2026-10-10 | Plan 11 step 65C imports, compile hints, named attribute rejections | 236 (fast) | — | See plan 65C evidence; `executables_imports` and three import lint cases reproduce under OTP 29.1.1 |
| 2026-10-06 | Plan 11 step 27E ERTS big integer limit, `error:system_limit` | 165 (161 fast) | 157 changed | Fast 161/161; full `-j 12` 165/165 in 130 s; Lizard 0 warnings; tidy passed; 21 executable goldens reproduce under OTP |
| 2026-10-06 | Plan 11 step 27D no map size or key-work caps | 164 (160 fast) | 13 changed | Fast 160/160; full `-j 12` 164/164 in 115 s; after a tidy fix in `bit_order`, 35 affected tests pass; Lizard 0 warnings; tidy passed |
| 2026-10-06 | Plan 11 step 27C tuple arity limit 16,777,215 | 164 (160 fast) | 51 changed | Fast 160/160; full `-j 12` 164/164 in 121 s; Lizard 0 warnings; tidy passed |
| 2026-10-06 | Plan 11 step 27B no list length or comparison work caps | 164 (160 fast) | 4 changed | Fast 160/160; full `-j 12` 164/164 in 125 s; Lizard 0 warnings; tidy passed |
| 2026-10-06 | Plan 11 step 27A runtime-wide memory limit and `--max-heap/stack/memory` | 164 (160 fast) | 49 changed | Fast 160/160; full `-j 12` 164/164 in 118 s; after a tidy fix in `ProcessStack::grow`, 42 affected tests pass; Lizard 0 warnings; tidy passed |

PG = pattern/guard plan step (archived in `.agents/00-finished.md`).
