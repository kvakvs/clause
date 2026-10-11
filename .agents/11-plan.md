# Remaining compiler and runtime work — implementation plan 11

Created 2026-10-03 from [completed work](00-finished.md) and
[the feature backlog](01-todo.md). Planning only: every step is unchecked and
creating this plan starts no implementation. It replaces the earlier 51-step
draft with smaller, single-commit steps.

## How to use this plan

- Steps run in numbered order by default. Each lists its backlog owners and
  dependencies; an independent step may move earlier once its dependencies
  pass.
- Each single step ends with one focused commit titled `[plan11] <full step title with step number>`. A step that
  grows beyond one reviewable change is split into lettered sub-steps (`12a`,
  `12b`) before coding, not widened silently.
  - IMPORTANT: Do not add "co-authored by" in commit messages.
- **Decision** steps publish a short contract in `docs/` (plus a prototype where
  stated) and enable no source feature by themselves.
- Check a success or test box only with recorded evidence. Update the backlog
  checkboxes for the slice delivered.
- Optional D-items (steps 71–77) end in a recorded selection, deferral or
  omission. An omitted item is never checked as implemented.
- Phase C (steps 8A–8I) was inserted on 2026-10-04 after phase B closed; its
  steps run before step 9 and keep their letter IDs in commit titles.
- Phase N2 (steps 73A–73H) was inserted on 2026-10-11 (user direction). It
  selects and implements the parse-transform slice of step 73 and depends on
  [65C](00-finished.md#step-65c), not on step 73, so it may run before steps
  66–72.
- Phase H2 (steps 41A–41H) was inserted on 2026-10-11 (user direction) after
  the luerl project build showed the missing `erlang` builtins. Its steps
  follow 41 numerically but run after the open phase M/N2 work unless
  reordered.
- Phase L3 (step 62D, as 62D1–62D4, and step 62E) was inserted on 2026-10-11
  (user direction). It adds the `build_dir` option, build caches and an
  in-memory index of searched directories, selecting the cache item of D07
  ahead of decision step 77. 62E is independent and may run before 62D.

## Common gate and rules (apply to every step)

- **Gate:** freshly configure `build/debug` with compiler, runtime and
  `BUILD_TESTING=ON`; build; run fast-mode CTest (`ctest --preset debug-fast`);
  run `cmake --build build/debug --target check-quality` (changed files and
  header dependents). When a phase or major feature completes, run full-mode
  CTest (`ctest --preset debug -j <N>`; each test takes two slots, so N/2 run
  at once) instead of fast mode, never both (full covers every fast run), and
  `check-quality-all`. Lizard and clang-tidy pass
  without raised thresholds or suppressions. Code is clang-formatted; new and
  changed `.erl`/terms files pass erlfmt.
- **Code:** project-internal C++23; document field and function intent in 1–2
  lines; keep cyclomatic complexity low; runtime stays LLVM-free.
- **OTP:** at the start of OTP-dependent work follow
  [otp-reference.md](../docs/otp-reference.md) to check `maint-29`. OTP sources
  and copies never enter git; commit only owned sources, observations and
  golden results. Normal builds and tests require neither OTP nor its checkout;
  goldens change only by explicit, reviewed regeneration.
- **Tests:** prefer real `.erl` sources through the CLI, linked executables and
  OTP-derived goldens. Where applicable cover positional and project drivers,
  O0/O2, specialization on/off and local/remote calls. Add focused unit tests
  only for budgets, ownership, invalid handles and injected faults that source
  cannot reach. Do not remove a useful test before equivalent behavioral
  coverage exists.
- **Representations:** a new term kind needs construction, comparison,
  printing, tracing, copying, destruction and error ownership rules before
  source code may produce it. Forged tagged words are never accepted as valid
  identities.
- **Failures:** keep legality, availability and runtime failure distinct.
  Invalid source is diagnosed even when unreachable; missing services stay
  explicit unavailable-capability diagnostics.
- **Docs:** update affected contracts, examples, `00-finished.md`, `arch.md`,
  `files.md` and `aimemory.md`. Unavailable hosts and tools are recorded as
  gaps, never as passes.

<a id="completed-patternmatch"></a>

## Retained context from the completed pattern/guard plan

Old plan 10 steps 1–20 and added 15a finished on 2026-10-03; their numbers are
historical and unrelated to the steps below. Per-step records are in
[the archive](00-finished.md#completed-patternmatch) and condensed in
[validation history](../docs/validation.md#history).

Executable baseline: ordered function clauses with guards, body
matches/sequences, acyclic local and exported remote calls. Admitted terms:
atoms/booleans, arbitrary integers, finite binary64 floats, tuples,
proper/improper lists, strings, exact-key maps, bitstrings and ordinary tuple
records (declarations, defaults, construction, access, patterns, tests).
Descriptors use ABI revision 4; the checked first-error channel is revision 2.

Invariants that later steps must keep:

- Candidate bindings publish only after head and guard success; `_` binds
  nothing; repeated names use exact equality; body matches evaluate the RHS once
  and chains right-to-left.
- Map keys read only incoming bindings; binary sizes also read preceding
  segments of the same binary.
- Guard legality comes from the pinned catalog and lint rules, not from runtime
  registration. Semantic guard errors reject the alternative; infrastructure
  errors stop execution.
- Every fallible call is checked before its output is used; errors are owned,
  first failure wins, outer cleanup runs, and retry in the same context works.
- Shape proofs dominate extraction; specs never authorize runtime access or
  narrow representation. Specialization limits: 3 variants per function, 32
  per module, 128 per target, 2x generic IR growth, with generic fallback.
- Failed compilation preserves earlier valid outputs; multi-file replacement is
  not atomic.

| Retained contract | Documents |
| --- | --- |
| Patterns, clauses, body matches | [Patterns](../docs/patterns.md) |
| Guards and catalog | [Guards](../docs/guards.md) |
| Bindings, types, inference facts | [Semantic analysis](../docs/semantic.md) |
| Failure channel, atoms, roots, registration | [ABI](../docs/abi.md), [terms](../docs/terms.md#atoms) |
| Representations | [Terms](../docs/terms.md), [runtime memory](../docs/runtime.md#process-memory) |
| Owned fixtures and history | [Validation](../docs/validation.md), [fixture instructions](../tests/fixtures/patternmatch/generated/README.md) |

Last reviewed `maint-29` pin: `21776803ecd11f5fa948732c0ec66b8f325dedfc`;
oracle OTP 29.1.1 / ERTS 17.1. Latest combined Windows x64 Debug gate (steps 63-65, 2026-10-09):
235 full-mode CTests; `check-quality-all` clean (338 units, headers now checked); Linux x86-64 235/235.

## Step overview

Completed steps link to [the plan 11 record](00-finished.md#plan-11-step-commits).

| Phase | Steps | Backlog owners | State |
| --- | --- | --- | --- |
| A. Baseline and fixtures | [1](00-finished.md#step-1)–[2](00-finished.md#step-2) | V03 | done |
| B. Production executables | [3](00-finished.md#step-3)–[8](00-finished.md#step-8) | F01, F26, V04 | done |
| C. Classic process heap | [8A](00-finished.md#step-8a)–[8I](00-finished.md#step-8i) | F02–F05, F09 | done |
| D. Control flow and exceptions | [9](00-finished.md#step-9)–[16](00-finished.md#step-16) | F13, F14, F16, F20 | done |
| E. Execution model, recursion, comprehensions | [17](00-finished.md#step-17)–[22](00-finished.md#step-22) | F02, F13, F16, F21, F22 | done |
| F. Memory management | [23](00-finished.md#step-23)–[28](00-finished.md#step-28) | F02–F05, F08–F11 | done |
| G. Records, function values, dynamic calls | [29](00-finished.md#step-29)–[35](00-finished.md#step-35) | F03, F12, F14, F17–F19, F21 | done |
| H. Builtins and libraries | [36](00-finished.md#step-36)–[41](00-finished.md#step-41) | F26, F27 | done |
| I. Processes and messaging | [42](00-finished.md#step-42)–[53](00-finished.md#step-53), [43A](00-finished.md#step-43a) | F02, F04, F05, F07, F14, F22, F24–F26 | done |
| J. Multi-worker scheduling | [54](00-finished.md#step-54)–[57](00-finished.md#step-57) | F06, F23, F25, F28 | done |
| J2. Ports and port I/O | [57A](00-finished.md#step-57a)–[57G3](00-finished.md#step-57g3) | F07, F23, F26, F35 | done |
| K. End-to-end projects | [58](00-finished.md#step-58) | F01, V03 | done |
| L. Optimization and tooling | [58A](00-finished.md#step-58a)–[58N3](00-finished.md#step-58n3), [59](00-finished.md#step-59)–[62B](00-finished.md#step-62b) | F23, F25, F29–F34 | done |
| H2. `erlang` module builtins | [41A](#step-41a)–[41H](#step-41h) (41G as 41G1–41G3) | F36 | done (41F TSan stress with V02) |
| L2. Export liveness | [62C](#step-62c) | F32, F36 | done |
| L3. Build directory and caches | [62D](#step-62d) (62D1–62D4), [62E](#step-62e) | D07 | open |
| M. Validation closure | [63](00-finished.md#step-63)–[70](#step-70), [65A](00-finished.md#step-65a), [65B](00-finished.md#step-65b), [65C](00-finished.md#step-65c) | V01–V04, D03 | open: [66](#step-66), [69](#step-69), [70](#step-70) |
| N. Optional scope decisions | [71](#step-71)–[77](#step-77) | D01–D07 | open |
| N2. Parse transforms | [73A](00-finished.md#step-73a)–[73H](#step-73h) | D03 | open: [73H](#step-73h) |
| O. Final closure | [78A](#step-78a), [78B](#step-78b), [78](#step-78) | all | open |

---

## Completed steps

Moved to [00-finished.md](00-finished.md#plan-11-step-commits) (step, title and
commit for 1–68, 65A–65C and 73A–73G); full step text and evidence are in Git
at those commits. The anchors below keep older links to this file working.

<a id="completed-steps"></a><a id="step-1"></a><a id="step-1a"></a><a id="step-1b"></a><a id="step-1c"></a>
<a id="step-2"></a><a id="step-3"></a><a id="step-3a"></a><a id="step-4"></a><a id="step-5"></a><a id="step-6"></a>
<a id="step-6a"></a><a id="step-7"></a><a id="step-8"></a><a id="step-8a"></a><a id="step-8b"></a><a id="step-8c"></a>
<a id="step-8d"></a><a id="step-8e"></a><a id="step-8f"></a><a id="step-8g"></a><a id="step-8h"></a><a id="step-8i"></a>
<a id="step-9"></a><a id="step-10"></a><a id="step-11"></a><a id="step-12"></a><a id="step-13"></a><a id="step-14"></a>
<a id="step-15"></a><a id="step-16"></a><a id="step-17"></a><a id="step-18"></a><a id="step-19"></a><a id="step-20"></a>
<a id="step-21"></a><a id="step-22"></a><a id="step-23"></a><a id="step-24"></a><a id="step-25"></a><a id="step-26"></a>
<a id="step-27"></a><a id="step-27a"></a><a id="step-27b"></a><a id="step-27c"></a><a id="step-27d"></a><a id="step-27e"></a>
<a id="step-28"></a><a id="step-29"></a><a id="step-30"></a><a id="step-31"></a><a id="step-31a"></a><a id="step-31b"></a>
<a id="step-31c"></a><a id="step-31d"></a><a id="step-31e"></a><a id="step-32"></a><a id="step-33"></a><a id="step-34"></a>
<a id="step-35"></a><a id="step-36"></a><a id="step-37"></a><a id="step-38"></a><a id="step-39"></a><a id="step-40"></a>
<a id="step-41"></a><a id="step-42"></a><a id="step-43"></a><a id="step-43a"></a><a id="step-44"></a><a id="step-45"></a>
<a id="step-46"></a><a id="step-47"></a><a id="step-48"></a><a id="step-49"></a><a id="step-50"></a><a id="step-51"></a>
<a id="step-52"></a><a id="step-53"></a><a id="step-54"></a><a id="step-55"></a><a id="step-56"></a><a id="step-57"></a>
<a id="step-57a"></a><a id="step-57b"></a><a id="step-57c"></a><a id="step-57d"></a><a id="step-57e"></a><a id="step-57f"></a>
<a id="step-57g"></a><a id="step-57g1"></a><a id="step-57g2"></a><a id="step-57g3"></a><a id="step-58"></a><a id="step-58a"></a>
<a id="step-58b"></a><a id="step-58c"></a><a id="step-58d"></a><a id="step-58e"></a><a id="step-58f"></a><a id="step-58f1"></a>
<a id="step-58g"></a><a id="step-58h"></a><a id="step-58h1"></a><a id="step-58i"></a><a id="step-58i1"></a><a id="step-58i2"></a>
<a id="step-58j"></a><a id="step-58j1"></a><a id="step-58k"></a><a id="step-58l"></a><a id="step-58m"></a><a id="step-58n1"></a>
<a id="step-58n2"></a><a id="step-58n3"></a><a id="step-59"></a><a id="step-60"></a><a id="step-61"></a><a id="step-62"></a>
<a id="step-62a"></a><a id="step-62b"></a><a id="step-63"></a><a id="step-64"></a><a id="step-65"></a><a id="step-65a"></a>
<a id="step-65b"></a><a id="step-65c"></a><a id="step-67"></a><a id="step-68"></a><a id="step-73a"></a><a id="step-73b"></a>
<a id="step-73c"></a><a id="step-73d"></a><a id="step-73e"></a><a id="step-73f"></a><a id="step-73g"></a>

### Notes from completed steps that guide remaining work

- Gate baseline: since commit `33481a7` raised the module limit to 16384,
  `codegen_resource_cli` still expects 1025 inputs to fail; every 73B–73G
  gate ran with that one known failure.
- 60 (for 66): Mach-O debug info needs `dsymutil`, not run yet
  (`docs/debugging.md`).
- 63: clang-tidy runs on the Windows host only (user); `cmake/tidy_filter.py`
  ignores Boost `cpp_int` analyzer reports (user decision).
- 65 (for 66): ARM ran under qemu-user only; qemu's `posix_spawn` never reports
  a missing program (`port_spawn` `enoent` fails there). Native ARM is open.
- 65A–65C (for 73): still rejected are `on_load`, `nifs`, `export_all`,
  `{d, ...}`, `warnings_as_errors` and unknown `-compile` options; each names
  itself in the diagnostic (imports of `erlang` functions reach the library
  module since 41B).
- 73A–73G (for 73H): OTP-requiring tests carry label `host_otp` and skip when
  CMake finds no `erl`; host OTP lookup is `--erl`, `PATH`, then
  `%ProgramFiles%\Erlang OTP`; `--erl none` simulates a missing host OTP.

## H2. `erlang` module builtins

Real projects call many `erlang` functions outside the bridge catalog
([builtins](../docs/builtins.md#which-builtins-exist)). Evidence, 2026-10-11:
`test-luerl.toml` (luerl plus the OTP `kernel`/`stdlib` modules it pulls in)
stops with 590 `unknown module erlang` errors and hundreds of
`undefined function M:get/1`-style errors for auto-imported BIFs. Most frequent:
`get/1` 147, `put/2` 138, `nif_error` 175, `erase/1` 45, `binary_to_term/1` 45,
`iolist_size/1` 34, `monotonic_time` 34, `fun_info/2` 34, `term_to_binary/1,2`
34, `nodes/0,1,2` 29, `group_leader/0,2` 27, `process_info/1,2` 21,
`monitor_node/2` 19, `convert_time_unit/3` 18, `binary_to_integer/1,2` 15,
`atom_to_binary/1,2` 17, timers (`start_timer`, `send_after`,
`cancel_timer`) 29, `system_info/1` 12, `split_binary/2` 10. Each step from
41C on appends catalog entries (indices never change), implements them in the runtime
with OTP's error terms in its own translation unit under
`runtime/src/builtins/erlang/` (41A), replacing the matching `erlang.erl`
stub from 41B, documents them in `docs/builtins.md` and proves them
with OTP-goldened executables; Lua/luerl progress is recorded, not a gate.

<a id="step-41a"></a>

### 41A. Organize `erlang` builtins in their own source directory

Backlog: F36. Depends on: [41](00-finished.md#step-41).

A large builtin module is implemented over several C++ translation units, one
per family, under a subdirectory named after the module. This step only moves
and splits existing code; no builtin changes behavior.

- Success criteria
  - [x] `runtime/src/builtins/erlang/` holds every translation unit whose
    builtins belong to module `erlang`: today's `erlang.cpp` (operators,
    type tests, errors), `term_access.cpp`, the `erlang` part of
    `lists.cpp` (`length/1`, `'++'/2`, `'--'/2`), `conversions.cpp`,
    `processes.cpp` and the port builtins of `ports.cpp`; helpers shared
    only by them live in `erlang/support.hpp`. Other modules (`io`, `os`,
    `lists` library helpers) and the bridge infrastructure (`bridge`,
    `registry`, `invocation`, `portions`, `typed`, `text`) stay in
    `runtime/src/builtins/`.
  - [x] Each family TU defines its own `*_builtins()` span, listed in
    `production_builtins()`; a new family (41C–41H) adds a new TU and span
    instead of growing an existing file. A TU past about 400 lines splits
    by family before it grows further.
  - [x] The catalog `abi::v1::bridge_builtins` stays one append-only array
    (indices are the ABI); its entries are grouped by family comments that
    name the implementing TU.
  - [x] Same rule for later large modules (`binary`, `unicode`, `ets`):
    `runtime/src/builtins/<module>/`.
  - [x] `runtime/CMakeLists.txt` lists the moved files; `files.md`,
    `arch.md` and `docs/builtins.md` name the new layout.
- Tests
  - [x] Unchanged full-mode CTest results and `check-quality-all`; the
    standalone `runtime_link` build still succeeds.
- Done 2026-10-11: `production_builtins()` moved to `registry.cpp` so new families never touch
  `core.cpp`. Gate: fast CTest 242/243 (known `codegen_resource_cli`), `check-quality` clean.

<a id="step-41b"></a>

### 41B. Declare `erlang` exports through a library stub

Backlog: F36. Depends on: [41A](#step-41a).

Builtins stay defined in the runtime (`runtime/src/builtins/`); the compiler
never reads runtime sources. It learns that a builtin exists from the shared
catalog `abi::v1::bridge_builtins` (`abi/include/clause/abi/builtins.hpp`) and
that an `erlang` function exists from a project-owned library module, as OTP's
`erlang.erl` declares BIFs whose bodies ERTS replaces.

- Success criteria
  - [x] An owned observation of OTP 29's `erlang` exports and auto-imports
    (name/arity, guard/auto-import flags; generated from the pinned
    checkout, no copied source) classifies every export as implemented,
    planned (41C–41H) or unavailable with a reason (distribution, tracing,
    code loading pending step 71, NIF loading).
  - [x] `library/stdlib/erlang.erl` (Clause's own source, not an OTP copy)
    exports exactly that inventory. A function without an implementation
    has a stub body that raises `error(undef)` with `erlang:F/A` on top of
    the stack, as OTP does for a missing function; a catalog builtin is
    exported and stubbed too, and the runtime's builtin replaces the stub
    at registration. Functions OTP writes in Erlang may get real Erlang
    bodies here, as `io.erl` mixes library code with runtime builtins.
  - [x] Compiler resolution of `erlang:F/A`, `fun erlang:F/A`, `apply` with
    literal names and auto-imported calls: a catalog builtin goes through
    the bridge; any other export of `erlang.erl` calls the library module
    (pulled in like `lists`); a name the stub does not export is
    `undefined function erlang:F/A`. Auto-imports follow OTP's list, so
    `get(K)` no longer reports `undefined function M:get/1`; a module's own
    definition and `-compile({no_auto_import, ...})` still win.
  - [x] Runtime: registering library module `erlang` next to the builtin
    module keeps builtins first for catalog entries and serves the stubs
    for dynamic calls; no duplicate-module failure.
  - [x] `erlang:nif_error/1,2` raises `error(Reason)` like OTP's stub, so
    NIF stub modules compile and fail only when called.
  - [x] Later steps (41C–41H) add catalog entries and runtime builtins only;
    the stub module changes only where a body moves into Erlang.
    `docs/builtins.md`, `docs/library.md` and `docs/differences.md`
    (stubs raise `undef` at run time where OTP has the BIF) describe it.
- Tests
  - [x] Consistency check without OTP: every `erlang` catalog entry is
    exported by `erlang.erl`, and every `erlang.erl` export is in the owned
    inventory; inventory freshness against the pin when OTP is present.
  - [x] Executables: a builtin reached directly, as a fun and by dynamic
    call; a stubbed function compiles and raises `undef` only when called;
    an auto-imported call resolves; a local definition shadows it.
- Done 2026-10-11: 351 exports (133 implemented, 119 planned for 41C–41H, 99 unavailable). Literal
  funs of builtins bind the builtin (`module_registration`), compiler-handled `erlang:` calls
  (`is_record/3`) need no library module. Gate: fast CTest (known `codegen_resource_cli`),
  `check-quality` clean.

<a id="step-41c"></a>

### 41C. Process dictionary

Backlog: F36. Depends on: [41B](#step-41b).

- Success criteria
  - [x] `put/2`, `get/0,1`, `get_keys/0,1`, `erase/0,1` with OTP semantics
    and ordering-independent results; values live on the process heap,
    are collection roots and are dropped at exit.
  - [x] `process_info(Pid, dictionary)` deferred to 41G is noted.
- Tests
  - [x] Executables with large dictionaries across collections and spawned
    processes (isolation); OTP goldens.
- Done 2026-10-11: `executables_process_dictionary` (OTP golden). The hand-kept auto-import set became
  OTP's table intersected with the catalog, so later steps only append catalog entries. `garbage_collect/0`
  is not implemented yet (41G); the fixture forces collections by allocation.

<a id="step-41d"></a>

### 41D. Binary, atom and number conversions

Backlog: F36. Depends on: [41B](#step-41b).

- Success criteria
  - [x] `atom_to_binary/1,2`, `binary_to_atom/1,2`,
    `binary_to_existing_atom/1,2`, `list_to_existing_atom/1`,
    `integer_to_binary/1,2`, `binary_to_integer/1,2`, `float_to_binary/1,2`,
    `binary_to_float/1`, `list_to_float/1`, `binary_to_list/3`,
    `bitstring_to_list/1`, `list_to_bitstring/1`, `split_binary/2`,
    `iolist_size/1`, `list_to_pid/1`, `list_to_ref/1`; encodings and
    `badarg` cases as OTP.
- Tests
  - [x] OTP-goldened executable covering edge values (big integers, bases
    2–36, Unicode atoms, latin1 limits, improper iolists).
- Done 2026-10-11: `executables_binary_conversions` (OTP golden, 70 cases plus direct calls). Added
  `AtomStorage::existing` and `TermFactory::reference_number`; `Term::bit_slice` was declared but
  never defined, so slices use `BitAccess::slice`. `list_to_pid/1` admits only issued pids
  (`docs/differences.md`).

<a id="step-41e"></a>

### 41E. External term format

Backlog: F36, F05. Depends on: [41D](#step-41d).

- Success criteria
  - [x] `term_to_binary/1,2` (minor version, for `compressed` we need to use
    system-available zlib or install a local copy of it), `binary_to_term/1,2` 
    (`safe`, `used`), `external_size/1,2` reuse
    `abi/include/clause/abi/external_term.hpp`; pids, references, ports and
    funs encode as OTP for the local node or are rejected explicitly.
  - [x] Bounds and atom-table limits checked before allocation; malformed
    input is `badarg`.
- Tests
  - [x] Byte-exact goldens from OTP for every term kind; round trips; fuzzed
    malformed input under the sanitizers.
- Done 2026-10-11: OTP bytes matched on the first run (`executables_external_terms`); the fuzz test
  found two decoder bugs (invalid atom UTF-8, NaN floats) before commit. Not run under the sanitizers
  in this step (they run with V02 phase gates). `compressed` writes uncompressed bytes; differences
  recorded in `docs/differences.md`.

<a id="step-41f"></a>

### 41F. Time, unique values and timers

Backlog: F36, F23. Depends on: [41B](#step-41b).

- Success criteria
  - [x] `monotonic_time/0,1`, `system_time/0,1`, `time_offset/0,1`,
    `convert_time_unit/3`, `timestamp/0`, `unique_integer/0,1`,
    `localtime/0`, `universaltime/0` and their conversions.
  - [x] `start_timer/3,4`, `send_after/3,4`, `cancel_timer/1,2`,
    `read_timer/1,2` on the existing timer wheel; messages and results as
    OTP, timers of exited processes released.
- Tests
  - [ ] Executables with deterministic ordering assertions (no wall-clock
    flakiness); timer stress under TSan.
- Done 2026-10-11: `executables_time_timers` (OTP golden). Timer messages live in a
  `MessageBuffer` (runtime term format, user direction: no ETF re-encoding), so local funs travel too.
  Open: the TSan timer stress run belongs to the next Linux sanitizer gate (V02).

<a id="step-41g"></a>

### 41G. Process introspection and spawn options

Backlog: F36, F22. Depends on: [41C](#step-41c). Split on 2026-10-11 before
coding into 41G1–41G3 (about 40 functions in three unrelated families); each
sub-step is one commit. Results that legitimately differ from OTP (memory
sizes, counts, hash values) are checked by shape and recorded in
`docs/differences.md`.

<a id="step-41g1"></a>

#### 41G1. Process and system introspection

- Success criteria
  - [x] `process_info/1,2` (selected items; an OTP item Clause does not keep is
    named in the error), `processes/0`, `group_leader/0,2` (inherited by
    spawned processes), `garbage_collect/0,1,2` (a collection at the next
    safepoint), `yield/0`, `statistics/1`, `system_info/1` and `memory/0,1`
    for the items OTP programs commonly read; other items `badarg`.
- Tests
  - [x] OTP-goldened executable (shapes and relations for values that vary).
- Done 2026-10-11: `executables_introspection` (OTP golden, shapes and relations).

<a id="step-41g2"></a>

#### 41G2. Spawn options, spawn requests, aliases and hibernation

- Success criteria
  - [x] `spawn_opt/2..5` (link, monitor, priority and heap tuning options;
    another node is `badarg`), `spawn_request/1..5` with its reply message,
    `spawn_request_abandon/1`, `alias/0,1`, `unalias/1`, sends to an alias,
    `monitor/3` (`{alias, ...}`, `{tag, ...}`) and `hibernate/3`.
- Tests
  - [x] OTP-goldened executable.
- Done 2026-10-11: `executables_spawning` (OTP golden). A repeat run under parallel load found that a
  reply alias was consumed before a blocked send retried; `Executor::send_alias` now consumes it on
  delivery.

<a id="step-41g3"></a>

#### 41G3. Fun information, hashing and MD5

- Success criteria
  - [x] `fun_info/1,2`, `fun_to_list/1`, `is_builtin/3`, `phash/2`,
    `phash2/1,2` (equal terms hash equally within the range), `md5/1` and
    `md5_init/0`, `md5_update/2`, `md5_final/1` (RFC 1321, OTP's digests).
- Tests
  - [x] OTP-goldened executable; MD5 digests byte-exact.
- Done 2026-10-11: `executables_fun_hashing` (OTP golden; MD5 byte-exact on the first run).

<a id="step-41h"></a>

### 41H. Single-node distribution and code-loading answers

Backlog: F36, D01. Depends on: [41B](#step-41b), [71](#step-71).

- Success criteria
  - [x] Single-node answers as a non-distributed OTP node: `node/0,1`,
    `nodes/0,1,2` (`[]`), `is_alive/0` (`false`), `monitor_node/2`,
    `disconnect_node/1`; `module_loaded/1`, `pre_loaded/0`,
    `delete_module/1`, `prepare_loading/2`, `finish_loading/1` per the
    step 71 decision.
  - [x] Rerun `test-luerl.toml`; record which `erlang` functions remain
    and plan or decline each.
- Tests
  - [x] Executables against OTP goldens taken from a non-distributed node.
- Done 2026-10-11: `executables_node_code` (OTP golden). Phase gate: full-mode CTest 258/259 (known
  `codegen_resource_cli`), `check-quality-all` clean. Step 71 is still open, so code loading answers for
  linked code (`delete_module/1` notsup, `prepare_loading/2` badfile). `test-luerl.toml` rerun: the 590
  `unknown module erlang` errors and the auto-import errors are gone; no `erlang` function is missing. What
  remains (626 lines) is other modules, planned or declined in `01-todo.md` F36:
  - OTP's runtime modules without Clause counterparts: `prim_socket` (115), `prim_inet` (73), `prim_net`
    (21), `socket_registry` (16), `prim_file` (8) — declined: Clause's sockets and files are its own drivers
    (`clause_socket`, `file`);
  - boot and code server internals: `init` (36), `erts_internal` (29), `erl_prim_loader` (29),
    `erts_code_purger` (3), `prim_eval` (1) — declined with step 71;
  - NIF and BIF modules worth a runtime implementation: `persistent_term` (54), `unicode_util` (101),
    `zlib` (11) — open backlog;
  - parsers generated from `.yrl`/`.xrl` (`erl_parse`, `core_parse`, `luerl_scan`, `luerl_parse`) — open:
    leex/yecc generation through the host OTP, like parse transforms;
  - 3 calls of `erlang:get_stacktrace/0`, removed in OTP 23: OTP rejects them too.

## L2. Export liveness

Inserted 2026-10-11 (user direction). Every exported function is reachable
from its module descriptor's export table, so `-Os` dead-stripping and
`--lto` can never drop one, even when nothing calls it; large library modules
(the 41B `erlang.erl` stub, `lists`, OTP modules pulled in from search paths)
keep every export alive.

<a id="step-62c"></a>

### 62C. Drop unreachable exports before emission

Backlog: F32, F36. Depends on: [62](00-finished.md#step-62),
[41B](#step-41b).

- Success criteria
  - [x] A whole-batch export liveness analysis runs before LLVM IR emission.
    Roots: the executable entry, `-on_load`-free startup, every exported
    function named statically elsewhere in the batch (`M:F(...)`,
    `-import`, `fun M:F/A`, `apply`/`spawn` families with literal module
    and function), behaviour callbacks of a module whose name escapes as a
    value, and everything a live function calls.
  - [x] Escape rules stay conservative: a module whose name atom is used as a
    value (passed, stored, sent, `?MODULE`), or any dynamic call whose module
    is not a literal while `list_to_atom`/`binary_to_atom` or atom-producing
    builtins are reachable, keeps all exports of the affected modules;
    the analysis reports why a module stayed fully live (`--verbose`).
  - [x] A dead export leaves the module's export table and gets internal
    linkage (or is not emitted), so linker dead-stripping and LTO remove it;
    `module_info(exports)` and `function_exported/3` answer as if it were
    still exported (from the descriptor's name list, not frames), and a
    dynamic call that reaches it raises `undef`, recorded in
    `docs/differences.md`.
  - [x] Opt-out `--keep-exports` (all exports live, today's behavior) and a
    per-target project option; the default applies the analysis only to
    executables, never to `--emit` artifacts meant for other programs.
  - [x] Weak references are evaluated as an alternative (weak export-table
    entries resolved by the linker): kept only if all targets (COFF, ELF,
    Mach-O) support them; the decision is recorded in `docs/executables.md`.
- Tests
  - [x] Executable sizes with `-Os` and `--lto` for a program using a few
    `lists`/`erlang` functions shrink measurably; recorded, not a gate.
  - [x] Behavior of all program fixtures and executable goldens unchanged
    with and without `--keep-exports`; dynamic calls, `spawn/3`, behaviours
    and `apply` on escaped module names still work.
- Done 2026-10-11: `semantic/liveness` walks from the entry (only live functions' bodies count), so an export
  named only in unreachable code is dropped too; dead functions and their lambdas get no code at all, their
  export entries keep the name only. `linking_exports` checks the fixture (undef only for a module named through
  a stack trace), `keep_exports`, `--verbose` reasons and 14 goldens with `--keep-exports`; all other executable
  goldens run with liveness on. Sizes (sizes.erl: `lists:map/seq/foldl/reverse`): `-Os` 2,760,704 bytes vs
  2,802,688 kept; `-O2 --lto` 2,760,192 vs 2,802,688 (the runtime archive dominates). Weak references rejected
  (`docs/executables.md#export-liveness`).

## L3. Build directory and caches

Inserted 2026-10-11 (user direction). Selects the cache item of D07 ahead of
step 77. Today `clau` keeps nothing between runs. Every build re-reads,
preprocesses, parses, transforms and compiles every module. Temporary files
are spread around: transform workspaces go to the system temp directory, link
staging (`.clause-link*`) sits beside the output, and `--emit` artifacts go to
`build/aot`.

Measured on the luerl project (188 modules, Release `clau`,
`--print-stage-times`, 2026-10-11):

- planning, including `**` glob expansion over `references/otp/lib`: 0.09 s;
- preprocess 0.5 s, parse 0.5 s, parse transforms 0.9 s (3 modules), compile
  1.1 s; whole run 3.6 s.

So directory scans are cheap at this size. Host `erl` transform runs and
whole-batch compilation are the costs worth caching.

<a id="step-62d"></a>

### 62D. Build directory and cached build results

Backlog: D07 (cache). Depends on: [58](00-finished.md#step-58),
[73G](00-finished.md#step-73g), [62C](#step-62c) (compile-cache keys must
include export liveness). Split into 62D1–62D4. Each sub-step is one commit
titled `[plan11] 62Dn. <title>`.

Shared design rules for all sub-steps:

- **Storage.** One project-internal cache interface:
  `get`/`put` by (input directory, file, kind, key), plus `invalidate` and
  `prune`. Callers never see paths. The first backend stores files under
  `<build_dir>/cache/v<format>/`, grouped by input directory. Each source
  directory gets one subdirectory named by a readable tail of the path plus a
  hash of the normalized absolute path (`src-3f2a91c0`). That subdirectory
  holds one index file plus pack files per result kind, not one file per
  module and kind. A later SQLite (or similar) backend must replace this
  without changing any caller; record that boundary in `docs/projects.md`.
- **Keys.** Never trust an entry by time stamp alone. Size and mtime are only a
  fast pre-check, and the content hash decides. Every key includes:
  - the `clau` version and the cache format version;
  - the effective options of that stage: include directories, defines,
    features and application directories for the frontend; triple,
    optimization, `-g`, specialization and LTO for compilation; transform
    names, `transform_paths` `.beam` hashes and the host OTP release for
    transforms.
- **Dependencies.** A frontend entry records every file it read, with its
  hash: includes, `include_lib` results, and transform sources. It also
  records every path probed and found missing. A header added earlier on the
  include path must invalidate the entry.
- **Safety.** Writes go to a temporary file and are renamed into place. A
  truncated, corrupt or foreign-version entry is ignored and rebuilt, never
  an error. Two `clau` processes sharing one build directory must not corrupt
  it: use either a lock or append-only packs plus an atomic index swap.
  Diagnostics come out the same with and without a cache hit, so warnings are
  stored with the entry.
- **Observability.** `--print-stage-times` marks reused stages
  (`[time] parse a.erl: 0.012 ms (cached)`), and `--verbose` says why an
  entry missed. `--no-cache` disables reads and writes.

#### 62D1. Build directory option and temporary files

- Success criteria
  - [x] Project option `build_dir` (manifest-relative, per target) and CLI
    `--build-dir DIR` (overrides it for every selected target). The default
    is `<manifest-dir>/build` for projects. Positional inputs get no build
    directory unless `--build-dir` is given: today's "check/print modes
    create no files" contract stays true without it.
  - [x] Clause-owned temporary files move under `<build_dir>/tmp`, starting
    with the transform loader workspaces. Link staging stays beside the
    output, because the final rename must stay on the same volume; record
    that reason in `docs/executables.md`.
  - [x] `--print-env` reports the build directory, and `--help`,
    `docs/projects.md`, `docs/compile.md` and the `--new-project` template
    name it. The contract states that check/print modes may write only
    inside the build directory.
- Tests
  - [x] A project run creates temporary files only under `build_dir`, and
    nothing is left there after success or failure. `--build-dir` overrides
    the manifest, and a positional check run without it writes nothing.
- Done 2026-10-11: `build_dir` is a target key (beside `output`), resolved in planning
  (`PlannedTarget::build_directory_`); `FrontendRequest::build_directory_` sets
  `TransformSettings::temporary_directory_` (`<build_dir>/tmp`), which the loader's workspaces and the sticky
  query use. `transforms_build_dir` (host OTP) reads the workspace a transform prints from the loader's plain
  argument: default, manifest, `--build-dir`, a failing transform, positional `--build-dir`, and a positional
  check that writes nothing; the system temp stays empty.

#### 62D2. Cache store and frontend results

- Success criteria
  - [x] The storage interface and its file backend from the shared rules
    exist, with `--no-cache`.
  - [x] Parse transform results are cached first, because they are the
    largest frontend cost: the imported forms as ETF, plus the transform's
    messages and output. A warm run starts no host `erl` for unchanged
    modules.
  - [x] Preprocess and parse results are cached only if a warm-cache load
    (ETF of the abstract forms through `transforms/import`, or a dedicated
    AST format) beats reparsing on the luerl fixture. Record the measurement
    either way.
  - [x] Header, define and feature changes invalidate exactly the modules
    that depend on them. That includes a new header that would shadow an
    included one.
- Tests
  - [x] Black-box project runs: a cold build then a warm build give identical
    diagnostics and outputs, with stage times showing `(cached)`. Then edit a
    header, a define, a transform source and a `.beam` on `transform_paths`;
    each edit rebuilds only the modules it affects.
  - [x] Corrupt, truncated and old-format entries are rebuilt silently. A
    deleted build directory is recreated.
- Done 2026-10-11: `compiler/src/cache/store` (interface, file backend, `KeyBuilder`, xxh3-128 keys) and
  `driver/build_cache` (`BuildCaches` per invocation, `TransformCache` as the loader's `transforms::HostCache`).
  The transform key is computed after preprocessing from the request bytes, so header/define/feature
  dependencies need no recorded file lists; transform sources add their includes (recorded by
  `TransformSourceCache::includes_`), code paths add all their `.beam` files (coarse: a `.beam` change misses
  every module using that path). Preprocess/parse not cached: on luerl with an identity transform on 169
  modules, importing the forms alone took 1,063 ms vs 819 ms preprocess+parse (export for the key 727 ms);
  cold transforms 46.2 s, warm 3.0 s. `transforms_cache` (host OTP) covers cold/warm, the four edits,
  damaged/truncated/old-format entries, a deleted build directory and `--no-cache`.

#### 62D3. Directory scan cache

- Success criteria
  - [x] Measure the expansion of `sources`, `source_dirs`,
    `source_search_paths`, `include_dirs` and `ignore_dirs` cold and warm on
    a large tree (all of `references/otp/lib` with `**` patterns), plus
    include-path probing during preprocessing.
  - [x] Measure with the in-memory index of [62E](#step-62e) in place; a scan
    cache persists that index between runs.
  - [x] Implement a scan cache only if planning or probing costs more than
    100 ms or 5% of a warm build. It would store expansion results per
    pattern root and invalidate them by directory mtimes (entries added or
    removed) and the manifest hash. Otherwise record the numbers and close
    the step without code.
- Tests
  - [ ] (not applicable) If implemented: adding, removing or renaming a file or directory
    that matches a pattern is picked up on the next run, as is a change to
    `ignore_dirs`.
- Done 2026-10-11 without a scan cache; `--print-stage-times` gained `[time] plan <manifest>` to measure
  planning. luerl: plan 78 ms of a 2.77 s warm build (2.8%), include probing answered by the index. All of
  `references/otp/lib` (1,319 directories): plan about 150 ms warm (sources glob 74, source_dirs 140, search and
  include patterns 35 each, ignore_dirs 0.4 ms), 10-20 s on first touch. That exceeds 100 ms, but listing the
  whole tree costs about 28 ms against about 13 ms to stat its directories (what a mtime-validated cache would
  do), and the remainder is per-source validation (open + identity) the cache cannot skip, so it would save
  about 15 ms per walk; recorded in `docs/projects.md#build-caches`, no code.

#### 62D4. Compile result cache

- Success criteria
  - [ ] Decide the reuse unit and record it in `docs/compile.md`.
    Analysis and type inference cover the whole batch, so a module's object
    is reusable only when its own syntax and every cross-module fact it
    consumed are unchanged. Those facts are callee signatures, inferred
    types, records, behaviours, export liveness and the entry/startup
    module. The cache key hashes exactly those facts.
  - [ ] If per-module keys cannot be made sound, fall back to a whole-batch
    key, or defer with the reason recorded.
  - [ ] Cached objects (or bitcode under `--lto`) feed linking and `--emit`
    exactly like fresh ones. Inspection modes (`--print-ir`,
    `--print-types`) always recompute.
- Tests
  - [ ] Executable goldens are identical with and without a warm cache.
  - [ ] Editing one module's body that other modules' facts do not depend on
    recompiles only that module. Changing an exported function's inferred
    type recompiles its callers. Changing the triple or optimization level
    misses the cache.
  - [ ] Warm luerl build time recorded before and after 62D2–62D4, not a
    gate.

<a id="step-62e"></a>

### 62E. In-memory index of searched directories

Backlog: D07 (cache). Depends on: [58](00-finished.md#step-58). Independent
of 62D, so it may run first; [62D3](#step-62d) then measures with this index
in place and may persist it.

Today every lookup probes every candidate directory, one at a time, through
the filesystem. Most probes miss. The lookups are:

- the referenced-module search over `source_search_paths` and the library
  (`module_source` in `driver/frontend.cpp`);
- transform sources (`locate` in `driver/transform_sources.cpp`);
- literal `sources` found through `source_search_paths`
  (`literal_source` in `project/paths.cpp`);
- include resolution (`candidates` in `preprocessor/includes.cpp`). This
  tries to open the include name in the including file's directory, the
  working directory, every include directory and the `include_lib`
  applications, for every include of every module. With patterns such as
  `references/otp/lib/**/include`, that is dozens of failed opens per
  include.

- Success criteria
  - [x] Measure first: the number of filesystem probes, and the time spent in
    them, for the luerl project before the change. Count them in a debug
    build or under `--verbose`, never in normal output.
  - [x] One index per invocation, shared by all lookups and all targets.
    It holds a list of scanned directories (by id) and a bucket map from
    file name to the ids of every directory where that name was seen. A
    name seen in several directories keeps all of them.
  - [x] Each directory is listed once, non-recursively, the first time a
    lookup needs it. The recursive walks that already expand `**` in
    `sources`, `source_dirs`, `source_search_paths` and `include_dirs` add
    the entries they visit, so those directories are never listed twice.
  - [x] A lookup takes the name and the ordered candidate directories, and
    returns the first directory in that order that has the name. Precedence
    is therefore exactly what probing gives today: the including file's
    directory, then the working directory, then include directories in order,
    then `include_lib` applications for includes; inputs before search
    directories for modules. `ignore_dirs` and `ignore_modules` are applied
    to the result, as they are now.
  - [x] An include name with directory parts (`sub/x.hrl`, `../inc/x.hrl`) is
    looked up by its last component. The rest of the name must then match
    below the candidate directory; anything the index cannot answer
    correctly falls back to probing. Absolute names are probed as they are
    today.
  - [x] File name keys fold case where the volume is case-insensitive
    (Windows, default macOS), so `Foo.hrl` is still found for `foo.hrl`.
    Exact keys are used elsewhere. Record the rule and its reason in
    `docs/projects.md`.
  - [x] The preprocessor stays independent of the filesystem. It gets the
    index through a `PreprocessorOptions` lookup hook, next to the existing
    `read_file`. Without a hook it probes as before (library users, unit
    tests).
  - [x] Within one invocation the index is a snapshot. Files that appear
    during the run, such as link staging or outputs, are never search inputs.
    The contract says so.
- Tests
  - [x] Black-box precedence:
    - the same module name in two search directories picks the first, and
      passes over an ignored copy;
    - the same header name in the including directory, the working
      directory and two include directories picks the first in OTP's order;
    - `include_lib` still resolves through application directories;
    - a header that differs only in case resolves on Windows.
  - [x] The probe count and time for luerl are recorded after the change,
    alongside the planning and preprocess stage times.
- Done 2026-10-11: `project/directory_index` answers "may exist" from listings; callers still probe a hit, so
  precedence and diagnostics are unchanged (luerl `--print-inputs` output identical). Before (Release, luerl
  `--print-inputs`, temporary counters): 15,215 probes (13,675 module search, 1,538 include opens with 165 hits,
  2 literal), about 105 ms in them; whole run 2.57-2.68 s, preprocess 452-464 ms. After: `[index] listed=66
  walked=1380 lookups=15215 misses=14909 probes=0`; whole run 2.54-2.60 s, preprocess 443-446 ms.
  `project_directory_index` covers precedence, include_lib, case folding and the --verbose line.

## M. Validation closure

<a id="step-66"></a>

### 66. Validate macOS Apple Silicon

Backlog: V01. Depends on: [63](00-finished.md#step-63).

- Success criteria
  - [ ] Same as step 63 on macOS arm64.
- Tests
  - [ ] Full gate plus fixture goldens.

<a id="step-69"></a>

### 69. Broaden OTP compatibility evidence

Backlog: V03. Depends on: [58](00-finished.md#step-58).

- Success criteria
  - [ ] Selected applicable upstream Common Test suites run against a matching
    built OTP, kept separate from Clause differential comparisons.
  - [ ] Differential goldens cover every feature enabled by this plan;
    exclusions are published.
- Tests
  - [ ] Opt-in upstream run with recorded commands; normal CTest stays OTP-free.

<a id="step-70"></a>

### 70. Close the test migration ledger

Backlog: V04. Depends on: [58](00-finished.md#step-58).

- Success criteria
  - [ ] Every remaining adapter or synthetic success test either has equivalent
    executable coverage and is removed, or has a recorded reason to stay.
- Tests
  - [ ] Full gate after removals; ledger dispositions cite the replacing tests.

## N. Optional scope decisions

Each step records a decision in its contract document. If an item is selected,
write its own small implementation plan before coding.

<a id="step-71"></a>

### 71. Decide dynamic modules and code upgrades

Backlog: D01. Depends on: [55](00-finished.md#step-55). **Decision.**

- Success criteria
  - [ ] Static-only, native dynamic libraries, or an upgrade model chosen;
    static-only failures for `code:load_*` documented if omitted.
- Tests
  - [ ] Golden check of the documented boundary behavior.

<a id="step-72"></a>

### 72. Decide atom collection

Backlog: D02. Depends on: [54](00-finished.md#step-54). **Decision.**

- Success criteria
  - [ ] Bounded permanent atoms kept, or collection roots and policy defined.
- Tests
  - [ ] Atom-limit golden for the chosen behavior.

<a id="step-73"></a>

### 73. Decide behavior-changing attributes and transforms

Backlog: D03. Depends on: [58](00-finished.md#step-58). **Decision.**

- Success criteria
  - [ ] Per-attribute decision (`compile` options, `on_load`, others;
    parse transforms are selected and implemented by phase N2,
    [73A](00-finished.md#step-73a)–[73H](#step-73h) (applied since 73F);
    `behaviour` is implemented by [65A](00-finished.md#step-65a),
    `module_info` and informational attributes by
    [65B](00-finished.md#step-65b), imports and compile hints by
    [65C](00-finished.md#step-65c)); rejected ones keep diagnostics.
- Tests
  - [ ] CLI diagnostics for each rejected attribute.

<a id="step-74"></a>

### 74. Decide public stage interchange

Backlog: D04. Depends on: [58](00-finished.md#step-58). **Decision.**

- Success criteria
  - [ ] A concrete consumer named, or deferral recorded; inspection dumps stay
    non-stable.
- Tests
  - [ ] None beyond the gate unless selected.

<a id="step-75"></a>

### 75. Confirm intermediate-stage reader reservations

Backlog: D05. Depends on: [74](#step-74). **Decision.**

- Success criteria
  - [ ] Directory reservations remain the only artifact per AGENTS.md, unless
    a D04 consumer requires a reader.
- Tests
  - [ ] None beyond the gate.

<a id="step-76"></a>

### 76. Decide C/FFI interoperability

Backlog: D06. Depends on: [58](00-finished.md#step-58). **Decision.**

- Success criteria
  - [ ] An external caller and minimal API named, or deferral recorded.
- Tests
  - [ ] None beyond the gate unless selected.

<a id="step-77"></a>

### 77. Decide project schema extensions

Backlog: D07. Depends on: [58](00-finished.md#step-58). **Decision.**

- Success criteria
  - [ ] Demand recorded for each extension (profiles, dependencies, exclusions,
    packages, watch/cache, parallel builds); selected items get their own plan.
    The build directory and caches are already selected as [62D](#step-62d).
- Tests
  - [ ] None beyond the gate unless selected.

## N2. Parse transforms

Added 2026-10-11 (user direction). Backlog: D03 (selected slice). Clause
applies `{parse_transform, M}` the way `erlc` does, using Erlang/OTP on the
host only to run the transform module:

1. export the preprocessed and parsed module as OTP abstract format forms;
2. run `M:parse_transform(Forms, Options)` through a project-owned loader
   adapter on the host OTP;
3. import the returned forms, replace the module's AST with them and continue
   compilation (lint, semantic analysis, codegen) as if parsed from source.

All new code lives in `compiler/src/transforms/` (reserved for D03 in
`files.md`), tests in `tests/compiler/transforms/`, fixtures in
`tests/fixtures/transforms/`; the rest of the compiler only calls the stage's
entry point from the driver. Normal builds and tests stay OTP-free: format
coverage uses OTP-generated goldens; tests that run a transform need host OTP
and are labeled and skipped (recorded as a gap) without it.

Steps 73A–73G are done ([record](00-finished.md#plan-11-step-commits)). OTP's
transform pass (order, options, return shapes, messages, column stripping,
forms) and Clause's contract are in [transforms](../docs/transforms.md).

<a id="step-73h"></a>

### 73H. Close parse transforms

Backlog: D03. Depends on: [73G](00-finished.md#step-73g).

- Success criteria
  - [ ] Real projects with parse transforms (for example a `lager`- or
    `qlc`-style user) compile past the transform stage; result recorded,
    not a gate.
  - [ ] User documentation (a "Parse transforms" section in `README.md`
    linking a user guide in `docs/transforms.md`, plus `docs/compile.md` and
    `docs/projects.md`) explains:
    - what is required: Erlang/OTP 29 installed on the host only for modules
      that use transforms; how `clau` finds it (`--erl`, `PATH`, the
      Windows default installation);
    - how to invoke a transform: `-compile({parse_transform, M})` in source,
      `include_lib` headers such as `ms_transform.hrl`, and the CLI and
      project options equivalent to `erlc +'{parse_transform,M}'`;
    - how to configure code paths for precompiled `.beam` files, on the CLI
      and in the project file, with a worked example;
    - when precompiling is required: always for transforms from dependency
      libraries; for project transforms when host `erlc` cannot compile them
      with project settings; how to precompile with the host OTP
      (`erlc -o DIR ...`) and that the `.beam` must match the host OTP
      major;
    - the lookup order, the cost of compiling project transforms on every
      run, and the
      diagnostics a user can meet with their remedies.
  - [ ] Every command and example in that documentation executed as written
    with host OTP; the CLI `--help` text names the new options.
  - [ ] `docs/transforms.md`, `docs/differences.md`, `features.md`,
    `01-todo.md` D03, step 73, `00-finished.md`, `arch.md`, `files.md` and
    `aimemory.md` describe exactly what is supported.
- Tests
  - [ ] Full-mode CTest and `check-quality-all`, once with host OTP and once
    without (OTP-requiring tests skipped and reported).

## O. Final closure

<a id="step-78a"></a>

### 78A. Reset every versioned ABI name to v1

Backlog: all. Depends on: steps 1–70 and any selected optional work. Inserted
2026-10-05.

The project has never been released, so no earlier generated-code contract has
to stay loadable. Collapse every version marker to v1: runtime service symbols
(`CLAUSE_roots_enter_v5`, `CLAUSE_roots_leave_v4`,
`CLAUSE_raise_v2`, `CLAUSE_call_failed_v2`,
`CLAUSE_register_module_v4`, `CLAUSE_atom_v3`,
`CLAUSE_exception_v2`, `CLAUSE_reraise_v2` and the rest), C++
namespaces such as `abi::v1`/`v2`, `abi::v1::version` and descriptor revision
numbers, the generated `clausev1_` prefixes if any other revision exists, and the
revision tables in the docs.

- Success criteria
  - [ ] No `_v2`-or-later suffix, versioned namespace or revision number above
    1 remains in sources, generated IR, tests, fixtures or docs; the ABI
    document describes one revision 1 without a revision history.
  - [ ] `runtime_symbols.hpp` aliases mirror the renamed `abi/include`
    declarations; no mangled spelling is hardcoded elsewhere.
- Tests
  - [ ] `tests/compiler/codegen/mangling.cpp` spellings, cross-target import
    checks, native consumers and every test calling a service directly use the
    v1 names, checked against Clang for every target ABI and width.
  - [ ] Fresh full gate and `check-quality-all` pass.

<a id="step-78b"></a>

### 78B. Rename runtime builtin modules to builtin_<name>

Backlog: all. Depends on: [78A](#step-78a). Added 2026-10-09 (user
direction).

Rename the C++ sources and headers of `runtime/src/builtins/` to
`builtin_<name>.cpp` / `builtin_<name>.hpp` (`erlang.cpp` to
`builtin_erlang.cpp`, `io_format.hpp` to `builtin_io_format.hpp`, ...) and
update includes, CMake lists, `files.md`, `arch.md` and every document
naming them.

- Success criteria
  - [ ] Every file of `runtime/src/builtins/` is named `builtin_<name>`; no
    reference to an old name remains.
- Tests
  - [ ] Fresh full gate and `check-quality-all` pass.

<a id="step-78"></a>

### 78. Publish the final implementation and validation boundary

Backlog: all. Depends on: steps 1–70, [78A](#step-78a), [78B](#step-78b) and any selected optional work.

- Success criteria
  - [ ] `00-finished.md`, `01-todo.md`, `arch.md`, `files.md`, contracts and
    examples reflect exactly what is implemented, validated, deferred or
    omitted.
  - [ ] README shows building and running a multi-process Erlang program as an
    executable.
- Tests
  - [ ] Fresh full gate on every available host; README commands executed as
    written.
