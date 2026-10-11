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
  `{d, ...}`, `warnings_as_errors`, unknown `-compile` options and imports of
  `erlang` functions that are no builtins; each names itself in the diagnostic.
- 73A–73G (for 73H): OTP-requiring tests carry label `host_otp` and skip when
  CMake finds no `erl`; host OTP lookup is `--erl`, `PATH`, then
  `%ProgramFiles%\Erlang OTP`; `--erl none` simulates a missing host OTP.

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
