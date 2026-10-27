# Validation

## Current baseline

Latest recorded gates on the Windows x64 development host (clang-cl and LLVM SDK
23.1.2, `/MT`, `_ITERATOR_DEBUG_LEVEL=0`); later steps record theirs in
`.agents/00-finished.md`:

| Check | Result |
| --- | --- |
| Fresh combined Debug CTest (2026-10-14, plan 11 step 41S) | 299/300 passed; the failure is the known `codegen_measurements` IR size bound; `codegen_resource_cli` is disabled |
| `check-quality-all` (same run) | Clean; Lizard CCN 10 |
| Opt-in OTP audit tests (2026-10-14, pin `d199a49a01a2`, oracle OTP 29.1.1) | 17/17 |
| `regenerate.py --corpus all --check` (same) | 19/19 corpora reproduce |

## Running the gate

From an x64 Visual Studio developer shell with `C:\Program Files\LLVM\bin` on
`PATH` (automatic SDK selection reuses `thirdparty/`):

```powershell
cmake --preset debug --fresh -DBUILD_TESTING=ON -G Ninja -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl
cmake --build build/debug
ctest --preset debug-fast                  # during development
ctest --preset debug -j 16                 # full mode, at feature completion
cmake --build build/debug --target check-quality      # changed files, one unit per header
cmake --build build/debug --target check-quality-all  # whole tree
```

- `CLAUSE_TEST_MODE=fast` runs golden corpora at O0 positional plus O2
  specialization-off project, runs mutations once and skips `full_only` tests
  (separate consumer projects such as `runtime_generated_link`, measurements,
  the host-OTP `library_freshness` audit, and the slow workflows `linking_lto`,
  `linking_exports`, `linking_compile_cache`, `programs_compile` and
  `transforms_cache`). Full mode (default) runs all driver/policy combinations.
- Long tests driven by a Python or CMake script carry a fixed CTest `COST`:
  CTest learns a wrapper's cost from its own small CPU time and would start them
  last.
- Load-sensitive tests (`CLAUSE_LOAD_SENSITIVE_TESTS` in the top
  `CMakeLists.txt`: `frontend_cli`, `codegen_examples`, `linking_debugger`,
  `executables_applications`) only timed out or lost timing races under a full
  parallel run. They `DEPENDS` on every other test, so they start after the rest
  of the selection finishes and have the processors to themselves; a failing
  dependency does not skip them.
- `CLAUSE_TEST_TIMEOUT_SCALE=N` multiplies the per-run limits of the golden
  runner (60 s) and the semantic case runner (15 s) for instrumented builds.
- `check-quality`, `make format` and `make-format.bat` cover files changed since
  `HEAD` plus untracked files; `cmake/quality_scope.py` adds, for each changed
  header, one translation unit that includes it (its own source, else its first
  user, from Ninja's dependency data of any configuration; the header's other
  dependents are left to `check-quality-all` in the full gate) and, for
  clang-tidy, units whose compile command differs from the one the last passing
  run recorded (`build/debug/quality/tidy-commands.json`), so a build rule edit
  rechecks only the units it changes. Each passing batch merges its units into
  that record, so after a failing run the next one rechecks only what did not
  pass. Changes to `.clang-tidy`, `CheckClangTidy.cmake`, `tidy_filter.py` or
  the quality toolchain template select everything, as do build rule edits
  before the first recorded run. The scope scripts (`quality_scope.py`,
  `QualityScope.cmake`) and `CMakeLists.txt` edits that only add, remove or
  comment source files do not.
- `CLAUSE_OPTIMIZED_COMPILER` (default ON) compiles `clau` and the compiler
  libraries optimized in Debug builds too, keeping their debug information and
  the Debug inference budgets; tests spend most of their time in `clau`. When a
  compiler crash needs stepping through, configure with
  `-DCLAUSE_OPTIMIZED_COMPILER=OFF`, rebuild and rerun the failing test.
- Test programs carry no debug information (no PDBs); configure with
  `-DCLAUSE_TEST_DEBUG_INFO=ON` to rebuild them for a debugger. Nested native
  consumers link the parent build's runtime through `ClauseRuntimeTargets.cmake`
  instead of compiling it again; only `runtime_link` builds the runtime
  standalone.
- Thresholds and suppressions are never raised to pass the gate. Two third-party
  exceptions are configured (user decision): `cmake/tidy_filter.py` drops static
  analyzer reports located in Boost headers (Boost.Multiprecision limb-storage
  false positives on Linux that rewriting the calling code only moves) and
  counts them in the output, and `bugprone-exception-escape` ignores
  `bad_array_new_length` (MSVC's map/set move constructors allocate; allocation
  failure is fatal anyway).
- clang-tidy checks project headers on every host: the header filter accepts `/`
  and `\` separators.

### Linux

The same gate runs on Linux with the pinned LLVM 23.1.2 Linux SDK as host
compiler (`CC`/`CXX` from its `bin/`), the system libstdc++, Ninja and CMake
3.28 or newer. Boost 1.90 and toml++ 3.4.0 come from installed packages or are
extracted into `thirdparty/` (`boost_1_90_0/`, `tomlplusplus-3.4.0/`); only
Windows downloads them. The release archive's `ld.lld` links ICU 70 (Ubuntu
22.04) and its `lldb` links Python 3.14; on hosts without them `linking_lto`
needs ICU 70 on `LD_LIBRARY_PATH`, and `linking_debugger` uses GDB.

```sh
cmake --preset debug --fresh -G Ninja -DBUILD_TESTING=ON
cmake --build build/debug
ctest --preset debug -j 16
```

`check-quality` (Lizard, clang-tidy) runs on the Windows development host only;
other hosts build and run the tests (it was clean on Linux once, see
[history](#history)).

### Other targets

32-bit and ARM targets are checked without a compiler built for them (no LLVM
SDK exists for those hosts):

- A runtime-only build (`-DCLAUSE_BUILD_COMPILER=OFF -DBUILD_TESTING=ON`) for
  the target runs the runtime and ABI tests there: natively, in a target
  userland (chroot), or through the host's emulator (qemu binfmt).
- `tests/compiler/executables/cross.py` runs every executable golden and program
  fixture with this host's `clau`, `--target-triple`, that build's runtime
  archive and a `--linker` that reaches the target's libraries
  (`run.py --target-option` passes them through).

## Fixtures and provenance

- Normal builds and tests need neither OTP nor its source checkout. Goldens were
  generated once from OTP and are committed with hashes; hash checks run before
  any fixture is used.
- Nineteen pattern/guard corpora hold 67,748 native expected values/errors and
  106 semantic acceptance rows. Each native corpus runs positional and project
  drivers at O0/O2 with specialization on/off, local and remote calls.
- Committed Erlang inputs are locally authored
  (`tests/fixtures/patternmatch/fragments/`, preprocessor `semantic/headers/`);
  `fixture_sources` enforces isolation from OTP sources.
- Six end-goal program fixtures (`tests/fixtures/programs/`) hold OTP
  stdout/exit-status goldens and their compile diagnostics; `programs_compile`
  checks them OTP-free and `tests/compiler/programs/regenerate.py --check`
  reproduces them under OTP
  ([fixture map](../tests/fixtures/programs/README.md)).
- Term printing goldens (`tests/fixtures/printing/`) hold OTP `~w` and
  `erlang:display/1` text for 9,542 values plus OTP stdout of compiled display
  calls; `tests/compiler/printing/regenerate.py --check` reproduces them
  ([fixture notes](../tests/fixtures/printing/README.md)).
- Executable golden cases (`tests/fixtures/executables/`) are source directories
  plus one `golden.json` with OTP stdout/exit status and an authored stderr
  pattern; `tests/compiler/executables/run.py` links and runs each under the
  policy/driver matrix (CTest `executables_<case>`), and `regenerate.py --check`
  reproduces them under OTP
  ([case notes](../tests/fixtures/executables/README.md)).
- Regeneration and live audits are explicit: `-DCLAUSE_OTP_AUDITS=ON` and
  `tests/compiler/patternmatch/regenerate.py`
  ([instructions](../tests/fixtures/patternmatch/generated/README.md)). Nothing
  refreshes goldens or the pin silently.

## Test design

Behavior is tested through the real CLI, emitted objects, linked native
consumers and OTP goldens. Focused unit tests remain only where source cannot
reach the state, each with its purpose stated in the test:

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

- Native generated-code execution: Windows x64; Linux x86-64. 32-bit and ARM
  results (runtime tests and cross-linked goldens) are in the history.
- Apple Silicon: not validated since the backend exists (plan 11 step 66).
- Compiler and runtime ASan, UBSan and LeakSanitizer: Linux x86-64, one combined
  Debug configuration
  (`-fsanitize=address,undefined -fno-sanitize=vptr -fno-sanitize-recover=undefined`),
  full CTest with `CLAUSE_TEST_TIMEOUT_SCALE=10` (instrumented code runs several
  times slower): no sanitizer report. The compiler, the runtime and the test
  programs are instrumented; programs `clau` links take the sanitizer runtime
  from a `clang++` wrapper on `PATH`, their generated code is not instrumented.
  The four injected host-refusal tests (`codegen_failure_*`,
  `runtime_lifecycle_failure`, `runtime_memory`) are outside ASan's scope: its
  `operator new` aborts on an impossible size instead of throwing.
- ThreadSanitizer: Linux x86-64, the same instrumentation scope with
  `-fsanitize=thread`; full CTest and the concurrency tests five times each
  report no race. Out of scope as well: the two tests that replace
  `operator new`.
- On Windows the prebuilt LLVM SDK still conflicts with instrumented code
  (`annotate_string` 0 vs 1).
- Runtime-only ASan passes on Windows with Release probes,
  `/EHsc /fsanitize=address`, `/MT`, Clang's ASan import library and static
  runtime thunk, and the ASan DLL on `PATH`.
- Historical macOS arm64 runs (2026-09-19/20) passed full Debug, C++26,
  ASan+UBSan, compiler-only and runtime-only builds for the parser and project
  stages; they predate the backend.

## Benchmarks

Manual performance comparison with Erlang/OTP. It is never part of CTest, the
commit gate or `check-quality`, and only it needs a host Erlang/OTP; results are
host-specific evidence, not gate results.

```powershell
make-benchmark.bat                         # Windows; set JOBS=N to cap parallel builds
make-benchmark.bat --repeat 9 --only bench_gc,bench_msg
make-benchmark.bat --cpus all                 # unpinned set; default --cpus performance
make benchmark BENCH_ARGS="--baseline HEAD~5"   # other hosts
```

- Both configure and build a Release `clau` and runtime in `build/bench`, then
  run `benchmarks/run.py` (`--help` lists its options).
- Programs: `benchmarks/bench_*.erl` with `bench_util.erl`, built unchanged by
  `erlc` and `clau -O2`. Each times its kernels with `erlang:monotonic_time/1`
  after one warm-up call and prints `BENCH <name> <nanoseconds>`: data
  generation, data processing, garbage collection, message passing, file I/O (in
  an empty temporary working directory under `build/bench/runs`, inside the
  repository so that a repository antivirus exclusion covers it), loopback
  sockets and libraries (`libraries.qlc`: `qlc_pt` transforms of queries and
  modules shaped like `qlc_SUITE` cases, timed together). Iteration counts make every kernel last 1-2 s on OTP, so timer
  resolution and host noise stay small against it; socket kernels grow the work
  per connection, not the number of sockets. Untimed, the warm-up and the timed
  calls each wait until the processes they spawned have exited (`processes_left`
  error after 10 s). Each kernel runs in its own fresh process, which exits
  before the next kernel starts, so no kernel pays for heaps an earlier one
  grew.
- Every program run is its own Windows job object or POSIX session; the next run
  starts once all its processes are gone, and leftovers still alive 30 s after
  exit are killed and reported as a failure.
- CPUs: every process of a run, OTP's and Clause's alike, runs on the `--cpus`
  set: `performance` (default: the cores of the highest Windows
  `EfficiencyClass`, Linux `/sys/devices/cpu_core/cpus` or largest
  `cpu_capacity`, so the P-cores of a hybrid CPU and every CPU otherwise),
  `all`, a list such as `0-15` or `2,4,6`, or a hex mask such as `0x5554`. On
  Windows the run's job object carries the affinity limit before the program
  resumes, on Linux the program sets it before `exec`; macOS cannot pin and
  runs unpinned. Both OTP and Clause start one online scheduler per CPU of the
  set, unbound to single CPUs. The report header and each history record
  (`cpus`) name the set; ratios of runs on different sets are not comparable.
- Systems, on the same host in one run, interleaved: OTP with its default flavor
  (JIT), OTP `-emu_flavor emu` when installed (the Windows installer ships only
  the JIT, so those columns read `n/a`), Clause HEAD at O2 and with `--lto`, and
  a previous Clause commit (default: the newest commit at least one day older
  than HEAD; `--baseline REV`, `--no-baseline`) built in a cached worktree under
  `build/bench-base/<hash>` that shares `thirdparty/`.
- Report: per kernel the median of `--repeat` runs (default 3) in milliseconds
  with six decimals and its spread, the change of Clause O2 against each system
  (`+N%`: Clause faster, the other takes N% longer; `-N%`: Clause takes N%
  longer; `(Fx)` the time ratio) with a geometric mean, `--lto` against OTP and
  O2, program wall times including startup, executable sizes, failures and
  timeouts (`--timeout`, 600 s per run). A default run measures for about 15
  minutes.
- Executable sizes, in bytes: every program at `-O2`, with `--lto` and at `-O0`
  (an extra build only measured, never run; both skipped by `--no-lto`) and the
  baseline at `-O2`, with a total row and the changes O2 vs O0, `--lto` vs O2,
  `--lto` vs O0 and O2 vs baseline (`-N%` smaller); and `clau` at HEAD and the
  baseline. The history keeps them as `sizes_bytes` (`programs`: program →
  column → bytes, `clau`: `head`/`baseline` → bytes; `null` where a build
  failed).
- History: `benchmarks/history/<host>.json` keeps one record per run day (run
  date, HEAD hash and commit date, Clause, OTP and baseline medians, sizes, and
  Clause's time ratios `otp_ratio` and `baseline.ratio` per kernel),
  `benchmarks/history/reports/<host>/<day>-<hash>.txt` the text reports; a rerun
  on the same day replaces both. Records and reports run more than 183 days
  before the newest are pruned; a run with uncommitted source changes is
  reported but not recorded. The report's history tables show the ratios of the
  last 8 run days, which stay comparable when kernels change their work; records
  from before the 1-2 s rescaling keep only their ratios. The history directory
  is gitignored: it stays on the host that ran it.
- Listings: with `--listings`, before measuring, a run writes each program's
  [`--print-asm`](compile.md#assembly-listings) and `--print-ir` listings (with
  `bench_util`, `--entry <program>` so unreachable library code is left out as
  in the executable) beside the reports as `<program>.debug.asm` /
  `<program>.debug.ll` (`-O0`) and `<program>.release.asm` /
  `<program>.release.ll` (`-O2`, the code the `clause-o2` column runs),
  replacing the previous run's listings, `--no-history` and uncommitted changes
  included. `--print-ir` is the IR before the LLVM pipeline; the assembly
  comments quote the IR after it. A program that fails to compile loses its
  stale listings.

## History

Condensed from the former per-step records (originals in Git history up to
commit `2777c98`). Unless noted: Windows x64, LLVM 23.1.2, pin `21776803ecd1`,
oracle OTP 29.1.1 / ERTS 17.1. Test counts are full CTest passes with zero
skips.

| Date | Milestone | Tests | Quality units | Notes |
| --- | --- | ---: | ---: | --- |
| 2026-09-19 | Parser phase VI (macOS arm64, OTP 29.1 `751f87b7`, oracle 29.0.5) | 46 | full | 344/344 productions witnessed; 10-file corpus |
| 2026-09-20 | Projects (macOS arm64) | 64 | full | Debug, compiler-only, ASan+UBSan |
| 2026-09-28 | Test migration to CLI workflows; Windows gate repair | 75 | full | `parser_hardening` stack overflow fixed with an 8 MiB stack; Lizard + clang-tidy clean |
| 2026-09-29 | Compiler milestone steps 1–46 | 103 | 182 | Compiler-only 80, runtime-only 16, runtime ASan 16 |
| 2026-10-01 – 10-03 | PG1–PG20 pattern/guard plan | 104–131 | 189–258 | ABI revisions 2–4; PG15a made goldens OTP-free (audits opt-in, 131 → 118 tests); closed at 19 corpora, 67,634 values, 1,969 seeded outcomes |
| 2026-10-03 | OTP source audit | 125 | 258 | Copied OTP files replaced by local fragments; 60-token window comparison against 4,150 OTP Erlang and 1,190 C/C++ files found no overlap besides a generated integer tuple |
| 2026-10-03 | Plan 11 step 1 baseline (commit `b1a471f`, pin `21776803ecd1`, Lizard 1.24.0, clang-tidy 22.1.8) | 125 (122 fast) | 258 | Full 729 s serial, 85 s `-j 16`; 14/14 audits; 19/19 corpora; foreign O0/O2 objects inspected for 7 targets |
| 2026-10-03 | Steps 2, 4: program fixtures, term printing | 126–128 | 265 | Six OTP program goldens; 9,542 `~w`/display goldens |
| 2026-10-04 | Steps 8, 8I: executable runner, classic heap (phases B, C closed) | 138–144 | 272–276 | Full `-j 16` 235–370 s |
| 2026-10-04 – 10-05 | Steps 9–20: `case`, `if`, raises, `catch`, `try`, stack traces, `maybe` (phase D closed), recursion, frames and tail calls | 146–158 | 277–278 | Fast and affected full-mode tests per step; full `-j 16` 155/155 in 259 s at phase D |
| 2026-10-06 | Steps 21–28: comprehensions (phase E closed), roots and collection, memory and term limits (27A–27E), graph copies | 159–166 | 278–281 | Full `-j 12` 105–275 s; step 27 removed default memory caps |
| 2026-10-07 | Steps 29–35: records, native records (ABI 6), funs (ABI 7), closures, named funs, dynamic calls (ABI 8) | 167–177 | 283–290 | Full `-j 12` 119–152 s |
| 2026-10-09 | Step 63 Linux x86-64 (WSL2 Ubuntu 25.04, glibc 2.41, LLVM 23.1.2 SDK clang, libstdc++ 14, GDB 16.2) | 235 | 338 | Full CTest 235/235 in 161 s; Lizard and clang-tidy clean on Linux (7 analyzer reports in Boost headers ignored); Windows gate 235/235 with headers checked |
| 2026-10-09 | Step 64 32-bit: Windows x86 (WoW64, clang-cl) and Linux i386 (Debian 13 chroot, clang 19) | 42 + 65 goldens | — | Runtime/ABI tests 42/42 natively on both; `cross.py` goldens 65/65 on both (`integer_limit` 64-bit-only runs skipped) |
| 2026-10-09 | Step 65 Linux arm64 and armhf (Debian 13 sysroots, qemu-user 9.2) | 42 + 65 goldens | — | Runtime/ABI tests 42/42 each; goldens 64/65 each (`port_spawn` `enoent`: qemu `posix_spawn` gap), run limit 900 s |
| 2026-10-10 | Step 67 ASan + UBSan + LeakSanitizer (Linux x86-64) | 235 | — | No sanitizer report; four injected host-refusal tests outside ASan's scope; `CLAUSE_TEST_TIMEOUT_SCALE=10` |
| 2026-10-10 | Step 68 ThreadSanitizer (Linux x86-64) | 233 | — | One lock-order inversion fixed (`Executor::clear`); then no report in full CTest and 15 concurrency tests x5 |
| 2026-10-10 | Steps 65A–65C: behaviours, `module_info/0,1`, imports | 234–236 fast | 342–344 | Lint and executable goldens reproduce under OTP 29.1.1 |
| 2026-10-11 | Steps 73A–73G parse transforms | 242/243 fast | — | Known `codegen_resource_cli` failure, since disabled |
| 2026-10-12 | Steps 62D1–62E build directory and caches | 263/264 | all | Full CTest, known `codegen_resource_cli` failure; `check-quality-all` clean |
| 2026-10-13 | Steps 58U–58W inference cache | 278/279 | all | Full CTest, known `codegen_measurements` failure; `check-quality-all` clean |
| 2026-10-14 | Step 41S `prim_eval` and the luerl rerun (H3 closed) | 299/300 | all | See current baseline |
| 2026-10-17 | Plan 20 OPTM.2 (one-word Term, persistent terms in place and reclaimed) and OPTM.3 (unchecked accessors) | 330/332 | all | Full CTest: known `codegen_measurements` failure, and `library_freshness` (host OTP) still expects the `not_implemented` stub bodies that `b0e62a38` replaced; `check-quality-all` clean |
| 2026-10-19 | Plan 21 LIB10F ThreadSanitizer (Linux x86-64): ETS and digraph goldens, `runtime_concurrency` | 10 | — | No report; `ets_portions` on one worker showed a delete freed in two portions, fixed by costing a freed object as a walked one |

PG = pattern/guard plan step; plan 11 step records are archived in
`.agents/00-finished.md`.
