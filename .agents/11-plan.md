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
- Phases H2 (`erlang` builtins, 41A–41H), L2 (export liveness, 62C) and L3
  (build directory and caches, 62D1–62D5 and 62E; the cache item of D07
  selected ahead of step 77) were inserted on 2026-10-11/12 (user direction)
  and are done.
- Phase H3 (steps 41I–41S) was inserted on 2026-10-12 (user direction):
  replacements for the ERTS-preloaded and build-generated modules OTP's
  `kernel`/`stdlib` call, driven by the luerl build. Its steps follow 41H
  numerically and grow as the luerl ledger advances.
- Step 69A (manual benchmarks against OTP, backlog V05) was inserted on
  2026-10-12 (user direction); it is never part of the gate.
- Phase I2 (steps 58O–58S, inference convergence and cost) was inserted on
  2026-10-13 (user direction), driven by the luerl build's 330 s inference.

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
| I2. Inference convergence and cost | [58O](#step-58o)–[58S](#step-58s) | F29, V05 | open: [58R](#step-58r), [58S](#step-58s) |
| H3. Drop-in replacements for OTP runtime modules | [41I](#step-41i)–[41S](#step-41s) (41K1, 41K2 added) | F36, D01 | open: [41L](#step-41l) |
| M. Validation closure | [63](00-finished.md#step-63)–[70](#step-70), [65A](00-finished.md#step-65a), [65B](00-finished.md#step-65b), [65C](00-finished.md#step-65c), [69A](#step-69a) | V01–V05, D03 | open: [66](#step-66), [69](#step-69), [69A](#step-69a), [70](#step-70) |
| N. Optional scope decisions | [71](#step-71)–[77](#step-77) | D01–D07 | open |
| N2. Parse transforms | [73A](00-finished.md#step-73a)–[73H](#step-73h) | D03 | open: [73H](#step-73h) |
| O. Final closure | [78A](#step-78a), [78B](#step-78b), [78](#step-78) | all | open |

---

## Completed steps

Moved to [00-finished.md](00-finished.md#plan-11-step-commits) (step, title and
commit for 1–68, 41A–41H, 62C–62E, 65A–65C and 73A–73G); full step text and
evidence are in Git at those commits. The anchors below keep older links to
this file working.

### Notes from completed steps that guide remaining work

- Gate baseline: since commit `33481a7` raised the module limit to 16384,
  `codegen_resource_cli` still expects 1025 inputs to fail; every gate from 73B
  to 41K ran with that one known failure. Disabled on 2026-10-12 (user) until
  it is rewritten for the new limit (16385 inputs need a project, not a
  command line).
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
- 41F (for V02): the timer stress run under TSan belongs to the next Linux
  sanitizer gate.
- 41H (for 71): code loading answers for linked code (`delete_module/1`
  `notsup`, `prepare_loading/2` `badfile`) until step 71 decides.
- 62D (for 77): cache layout and limits in
  [build caches](../docs/projects.md#build-caches); per-module compile reuse
  and a directory scan cache stay open in D07.
- 73A–73G (for 73H): OTP-requiring tests carry label `host_otp` and skip when
  CMake finds no `erl`; host OTP lookup is `--erl`, `PATH`, then
  `%ProgramFiles%\Erlang OTP`; `--erl none` simulates a missing host OTP.

## I2. Inference convergence and cost

Added 2026-10-13 (user direction). Evidence: `clau.bat --project test-luerl --entry luerl:init
--print-stage-times` (188 modules) spends 330 s of a 399 s compile in inference: 17 passes, the 8M-step pass
budget spent from pass 3 on. The cost is in recursive groups (strongly connected components), which `solve`
iterates for up to `8 + 4 × members` rounds, re-walking every member each round:

| Group | Members | Round cap | Solves capped | Walks that changed a summary | Last solve (inputs `term()`) | Time |
| --- | --- | --- | --- | --- | --- | --- |
| `erl_scan:scan1/5` | 47 | 196 | 13 / 17 | 12% | converged in 8 rounds | 167 s |
| `luerl_comp_vars:stmts/3` | 29 | 124 | 14 / 17 | 56% | capped, ~19 members change every round | 17 s |
| `luerl_comp_normalise:stmts/2` | 30 | 128 | 15 / 17 | 46% | capped, flat 16 per round | 15 s |
| `luerl_parse:yeccpars1/5` | 220 | 888 | 0 / 17 | 21% | converged in 8 rounds | 15 s |
| `io_lib:write/5` | 15 | 68 | 14 / 17 | 46% | capped, flat 9 per round | 8 s |

No group is stopped by the pass budget (only `v3_core:clauses/2`, 13 of 17 solves); they stop at the round cap
and widen every member to `term()`. Several never settle even with `term()` inputs, which points at
non-monotone round merging (`next_round` replaces a member's function types when their inputs differ;
`refine` overwrites `Summary::inputs`). The lattice is of finite height (`FactLimits`), so monotone rounds
always end; the steps stop repeating known failures, make rounds monotone, and skip walks that cannot change
anything. Each step reruns the luerl command and records inference time, passes and the `inference rounds`
lines; `--print-types` output of the existing fixtures may change only where a step says so.

<a id="step-58o"></a>

### 58O. Measure the fixed-point rounds of recursive groups

Backlog: F29, V05. Depends on: [58](00-finished.md#step-58).

- Success criteria
  - [x] `--print-stage-times` prints `inference rounds <group>` for the 10 slowest recursive groups: solves over
    all passes, rounds run against the per-solve cap, solves that reached the cap or ended with the pass
    budget spent, member walks and the share that changed a summary, and the members changed per round of the
    last solve in ten groups of rounds (`docs/compile.md`).
- Tests
  - [x] None beyond the gate (timing output; recorded luerl run above).
- Done 2026-10-13: `SolveTrace` from `solve`, `InferenceProfile::Cost::rounds_`, `print_recursive`; the
  luerl evidence above is this step's run. Committed without the gate (user direction).

<a id="step-58p"></a>

### 58P. Remember groups that failed to converge

Backlog: F29. Depends on: [58O](#step-58o).

- Success criteria
  - [x] A recursive group whose solve reaches its round cap (every member widened to `term()`) is recorded as
    failed; every later pass gives its members `term()` summaries directly and runs one round to record their
    expression facts, instead of iterating up to the cap again. The result equals what a capped solve
    produces (sound: `term()` is top), and the choice is deterministic.
  - [x] A group whose solve spends more than its share of the pass budget (decided in this step, for example
    a fixed fraction of the budget, or a cap that grows with the group's size) is also recorded as failed, so
    one group no longer starves every function after it in callee-first order.
  - [x] `--print-stage-times` shows remembered groups (`inference rounds ... N remembered`), and
    `docs/semantic.md` (inference passes and budgets) describes the rule.
  - [x] luerl rerun: inference time and passes recorded against 58O; `erl_scan` and the capped `luerl_comp_*`,
    `io_lib` groups solve to the cap at most once each.
- Tests
  - [x] A fixture with a recursive group that gives up (here: over its work share, since no small group was
    found that reaches its round cap) reports one such solve and remembered ones after it, with a deterministic
    `--print-types` result; existing inference tests pass unchanged.
- Done 2026-10-13: `Ending` per solve (`converged`, `capped`, `over_share`, `remembered`), `iterate` and
  `give_up` in `semantic/types/inference.cpp`, `Inference::failed`. Decided share: an eighth of the pass budget
  (`COMPONENT_WORK_SHARE`), checked after each round. The pass that widens every input to `term()` forgets the
  remembered groups, so each gets one solve with those inputs (in 58O, `erl_scan` converged in 8 rounds there).
  - Tests: no hand-written group capped (walker-style AST rewriters of luerl's shape converge in 3 rounds), so
    `tests/compiler/inference/budgets.py` (CTest `inference_budgets`) generates a two-member group sized from
    the reported pass budget whose first round exceeds its share: `1 over work share`, remembered in the next
    pass, `-> _` with `inferred=widened`, deterministic, and the linked executable runs; a small copy converges
    with its precise types. The cap path uses the same `give_up` code, so its result is unchanged by
    construction. `tests/cli.cmake` accepts the `inference rounds` lines.
  - luerl (Release `build/tool`, same command as 58O): inference 330 s to 113 s, compile 399 s to 182 s;
    passes 3–16 no longer spend the pass budget (2.7M–4.9M of 8M steps), only the final `term()` pass does.
    `erl_scan:scan1/5` 167 s to 5.2 s (1 over work share, 12 remembered); `luerl_comp_*` and `io_lib` left the
    10 slowest (each under 1.5 s, from 8–17 s); `v3_core:clauses/2` 2 over share, `sys_core_fold:body/2` 2
    capped, the rest remembered. The slowest group is now `core_parse:yeccpars1/5` (523 members, converges in
    7 rounds, 22% of walks change a summary: 58R's target). `test-luerl.exe` exits 0.
  - Found on the way (for 58Q/58R or a new step): walks over a huge list literal get much slower per step
    from pass to pass (generated 40,000-element list: 240k steps in 0.1 s in pass 1, 15 s in pass 7).
  - Gate: fast CTest 273/273, `check-quality` clean (after guarding a possible division by zero in
    `round_series`, from 58O).

<a id="step-58q"></a>

### 58Q. Make fixed-point rounds monotone and detect flipping

Backlog: F29. Depends on: [58P](#step-58p).

- Success criteria
  - [x] Each round's member summary only grows: function types whose inputs changed are joined with the
    previous round's (positionally, or merged into one type) instead of replacing them. Changed: `Summary::inputs`
    stays overwritten, because calls read it only after the group is solved (`!solving.contains(callee)`), so it
    cannot make rounds churn and joining it would only widen the final success domain.
  - [x] Flip detection (user direction): each member keeps its last few summaries, more than one, so a member
    that returns to an earlier summary (A → B → A, or a longer cycle) is detected. Changed: such a member gives up
    alone for the rest of the solve (`term()` without function types) instead of joining the cycle, because the
    flip comes from the fact join itself not being monotone at the container depth limit (see 58S), so joining
    the cycle's summaries flips again.
  - [x] `inference rounds` splits changes into grown, flipped and replaced, so the luerl evidence shows which
    cause remains.
  - [x] luerl rerun: inference time recorded against 58P; any `--print-types` change in fixtures is reviewed.
- Tests
  - [x] A fixture whose recursive group used to flip converges, with its `--print-types` output recorded;
    existing inference and pattern-match tests pass or their recorded precision changes are reviewed.
- Done 2026-10-13: `RoundChanges`, `Snapshot`, `RoundHistory` (last 4 summaries per member, `HISTORY`) and
  `next_summary` in `semantic/types/inference.cpp`; `next_round` joins type by type or merges both rounds' types
  (`function_types.cpp`, `join_round`).
  - Cause, from a luerl run with the classification only (no fix): flips appear exactly in the groups that hit
    their cap or work share (`sys_core_fold:body/2` 418 of 485 changes, `v3_core:clauses/2` 263 of 469,
    `erl_scan:scan1/5` 216 of 940, `beam_core_to_ssa:ubody_used_vars/2` 24 of 118); converging groups show
    none. Monotone function types alone changed nothing. Debug output of the first flips (`ttdict:map/2`,
    `ttdict:mapfold/3`) showed the result alternating between two tree-shaped unions that differ one level
    below the depth limit: `join(A, x)` gave `B` and `join(B, x)` gave `A`.
  - luerl (Release `build/tool`, `--no-cache`, idle host): inference 133 s (58P 113 s, 58O 330 s), compile
    206 s (58P 182 s). `erl_scan`, `v3_core` and `sys_core_fold` now converge (0 capped, 0 over work share; 39,
    48 and 43 members gave up alone) instead of giving up as a whole, and the final `term()` pass stays within
    its budget (5.4M of 8M steps; it ran out in 58O and 58P). The extra time is those groups' real rounds:
    `erl_scan` 171 rounds, 11.6 s. `luerl_comp_*` and `io_lib` left the 10 slowest groups (their counts are not
    shown). Still capped twice: `beam_core_to_ssa:ubody_used_vars/2` and `luerl_comp_cg:stmts/2`, whose last
    solves grow steadily (3 and 2 members per round) without flipping. `test-luerl.exe` exits 0. One earlier
    run's 1.7–2x slower phases everywhere (contracts and emit too) was host load.
  - Tests: `tests/fixtures/inference/flips.erl` (CTest `inference_flips`, erlfmt-clean), a tree map whose
    `map/2` flips: `expect:` is the precise result, `today:` the given-up `_`; `inference_budgets` checks its
    `inference rounds` line shows a flip and no cap. The existing inference, pattern-match and type tests pass
    unchanged (no `--print-types` change).
  - Gate: fast CTest 273/273 and `check-quality` clean on the code; the fixture and test additions ran after it
    (`inference_flips`, `inference_budgets` pass).

<a id="step-58r"></a>

### 58R. Re-walk only the members whose inputs changed

Backlog: F29, V05. Depends on: [58Q](#step-58q).

- Success criteria
  - [ ] Within a group, the first round walks every member; later rounds walk only members whose callees'
    summaries, own inputs or read `fun F/A` facts changed in the previous round (a worklist in a fixed,
    deterministic order). Unchanged members keep their expression facts instead of discarding and recording
    them again.
  - [ ] The fixed point equals the one round-robin iteration reaches, or any difference caused by widening order
    is recorded as a precision change; `--print-types` of the fixtures is unchanged.
  - [ ] `inference rounds` counts skipped walks; luerl rerun: member walks and inference time recorded against
    58Q (expected for `luerl_parse:yeccpars1/5`: about a fifth of the walks).
  - [ ] Decide whether the same dirty tracking applies between passes (re-infer only components whose inputs
    or callee summaries changed since the previous pass) and either implement it here or add a step.
- Tests
  - [ ] Inference and pattern-match tests pass unchanged; a recursive fixture's walk count drops in
    `--print-stage-times` while its printed types stay the same.

<a id="step-58s"></a>

### 58S. Make fact joins monotone at the container depth limit

Backlog: F29. Depends on: [58Q](#step-58q). Added 2026-10-13 from 58Q's evidence.

- Success criteria
  - [ ] `Lattice::join` (and `widen`) return a fact that holds both arguments also at the container depth
    limit: a union member cut off at the limit and the same member kept one level deeper no longer alternate
    (`ttdict:map/2`, `flips.erl`), for example by truncating every member at the same depth before joining or by
    keeping one member per shape regardless of the first element's tag at the cut-off level.
  - [ ] A check over the inference fixtures and generated deep tree facts that `within(a, join(a, b))` and
    `within(b, join(a, b))` hold.
  - [ ] luerl rerun: flips drop to none or near none; the given-up members of 58Q get their facts back;
    inference time recorded against 58Q. `flips.erl` reaches its `expect:` line or a recorded nearer one.
- Tests
  - [ ] A focused lattice test for the join property on deep trees (it cannot be reached reliably through source),
    plus `inference_flips`.

## H3. Drop-in replacements for OTP runtime modules

Added 2026-10-12 (user direction). Backlog: F36 (other modules), D01 for code
loading answers. OTP's `kernel` and `stdlib` are plain Erlang, but they call
modules that ERTS preloads and implements in C (NIFs, BIFs, port drivers).
Clause compiles `kernel`/`stdlib` sources found through `source_search_paths`,
so those calls fail with `unknown module`. Whatever needs the runtime and
cannot be written in plain Erlang is decided per module, and where a module
mixes concerns per group of functions (user direction 2026-10-12). A module
is either **provided** — a Clause replacement with OTP 29's exact export
list, first all stubs so it is discoverable and callers compile, then real
groups (runtime builtin, Erlang body over existing runtime services,
OTP-shaped refusal such as `{error, enotsup}`, or a stub raising `undef`) —
or **missing**, possibly forever. Low-level modules such as the `prim_*`
file and socket layers can stay missing: Clause provides the higher-level
standard modules above them (`file`, `gen_tcp`, `gen_udp`, `inet`, and
`socket`, `net`, `filelib` where decided), and because Clause modules win
over OTP's, the OTP callers of a missing module never enter a batch.

Evidence, 2026-10-12 (`clau.bat --project test-luerl --entry luerl:init`,
458 stderr lines, only the 62D5 build changed since 41H): `unknown module`
for 12 modules, plus 3 `erlang:get_stacktrace/0` errors that OTP reports too
(luerl source, not a gap). A scan of every `lib/kernel/src` and
`lib/stdlib/src` module for remote calls into modules neither application
defines adds four more ERTS modules. Calls to other applications (`compile`,
`crypto`, `yecc`, `erl_syntax`, `erl_prettypr`) and to generated `erl_parse`
(yecc, open since 41H) are outside this phase. The current ledger (the
proposed direction is confirmed or changed in 41I and in each module's
step):

| Module | Kept by OTP in | luerl errors | kernel/stdlib callers | Functions called |
| --- | --- | --- | --- | --- |
| `prim_socket` | `erts/preloaded` (NIF) | 114 | `socket`, `inet` | ~60 (`open`, `bind`, `connect`, `send*`, `recv*`, `getopt`, `setopt`, `ioctl`, `info`, ...) |
| `unicode_util` | generated into `stdlib` from `lib/stdlib/uc_spec` | 101 | `string`, `unicode`, `prim_tty` | `cp/1`, `gc/1`, `casefold/1`, `lowercase/1`, `uppercase/1`, `titlecase/1`, `nfc/nfd/nfkc/nfkd/1`, `category/1`, `is_whitespace/1`, `is_wide/1`, `pattern_whitespace/0`, `is_other_id_start/1`, `is_other_id_continue/1`, `is_letter_not_pattern_syntax/1` |
| `persistent_term` | `erts/preloaded` (BIFs) | 48 | 14 (`logger_*`, `erl_features`, `code_server`, `inet_db`, `os`, `io_ansi`, ...) | `get/0,1,2`, `put/2`, `erase/1` (exports also `put_new/2`, `info/0`) |
| `init` | `erts/preloaded` | 35 | 33 | `get_argument/1`, `get_arguments/0,1`, `get_plain_arguments/0`, `script_name/0`, `get_status/0`, `stop/0`, `objfile_extension/0`, `archive_extension/0`, `code_path_choice/0`, `fetch_loaded/0`, `ensure_loaded/1`, `notify_when_started/1`, `get_configfd/1`, `set_configfd/2`, `run_on_load_handlers/0` |
| `erl_prim_loader` | `erts/preloaded` | 29 | 9 (`code`, `code_server`, `filelib`, `inet_db`, ...) | `read_file/1`, `read_file_info/1`, `read_link_info/1`, `list_dir/1`, `get_cwd/0,1`, `get_path/0`, `is_basename/1`, `prim_*` |
| `erts_internal` | `erts/preloaded` (BIFs) | 22 | 9 (`maps`, `string`, `code`, `net_kernel`, ...) | `cmp_term/2`, `map_next/3`, `list_to_integer/2`, `map_to_tuple_keys/1`, `map_hashmap_children/1`, distribution (`new_connection/1`, `abort_pending_connection/2`, `dynamic_node_name/0,1`, `get_creation/0`, `get_dflags/0`), `beamfile_chunk/2`, `beamfile_module_md5/1` |
| `prim_net` | `erts/preloaded` (NIF) | 21 | `net` | 16 (`gethostname/0`, `getaddrinfo/2`, `getifaddrs/1`, `if_*`, ...) |
| `socket_registry` | `erts/preloaded` | 14 | `socket`, `gen_tcp_socket`, `gen_udp_socket` | `monitor/1,2`, `cancel_monitor/1`, `which_sockets/1`, `which_monitors/1`, ... |
| `zlib` | `erts/preloaded` (NIF) | 10 | 8 (`zip`, `erl_tar`, `disk_log_1`, `beam_lib`, `code`, ...) | `open/0`, `close/1`, `deflateInit/6`, `deflate/2,3`, `deflateEnd/1`, `inflateInit/2,3`, `inflate/2`, `safeInflate/2`, `inflateEnd/1`, `inflateReset/1`, `gzip/1`, `gunzip/1`, `uncompress/1` |
| `prim_file` | `erts/preloaded` (NIF) | 8 | `raw_file_io`, `application_controller` (and `file` through them) | `open/2`, `close/1`, `file_desc_to_ref/2`, ... (51 exports) |
| `erts_code_purger` | `erts/preloaded` | 3 | `code_server` | `purge/1`, `soft_purge/1`, `finish_after_on_load/2` |
| `prim_eval` | `erts/preloaded` | 1 | `erl_eval` | `'receive'/2` |
| `prim_inet` | `erts/preloaded` (port driver) | hidden (`inet` ignored) | 10 (`inet`, `inet_tcp`, `inet_udp`, `inet_sctp`, ...) | ~60 |
| `prim_buffer` | `erts/preloaded` (NIF) | — | `raw_file_io_delayed`, `raw_file_io_inflate` | `new/0`, `read/2`, `write/2`, `size/1`, `wipe/1`, ... |
| `atomics`, `counters` | `erts/preloaded` (BIFs) | — | `erl_erts_errors` | `info/1` (whole BIF modules) |

Proposed direction:

- **Provided:** `init`, `persistent_term`, `unicode_util`, `atomics`,
  `counters`, `erts_code_purger`, the `stdlib` group of `erts_internal`
  (`cmp_term/2`, `map_next/3`, `list_to_integer/2`, map helpers); to decide:
  `erl_prim_loader`, `zlib`, `prim_eval`.
- **Missing, possibly forever:** `prim_file`, `prim_buffer`, `prim_socket`,
  `prim_net`, `prim_inet`, `socket_registry`, and the distribution and
  `.beam` groups of `erts_internal`. Their OTP callers (`raw_file_io*`,
  `socket`, `net`, `inet_tcp`, `gen_tcp_socket`, `net_kernel`, ...) are
  shadowed by Clause modules or recorded as unsupported.

Two findings shape the steps:

- **Precedence.** Today a module found in `source_search_paths` wins over the
  library ([library](../docs/library.md#how-programs-get-them)). With
  `references/otp/lib/**/src` on the search path, OTP's `file`, `io`, `lists`,
  `maps`, `os`, `gen_tcp` replace Clause's library modules of the same name,
  and a user's copy of `erts/preloaded/src` would replace the replacements.
  Decided 2026-10-12 (user): every module Clause provides wins over OTP's
  module of the same name. OTP is optional on the user's machine; when it is
  present it must not change what a build compiles. Consequence: Clause's
  library modules shadow OTP's complete ones, so each of them must also export
  OTP 29's full list (stubs where not implemented), or OTP code calling a
  function Clause lacks stops at `undefined function`.
- **Kernel services.** OTP's `file`, `io`, `code`, `logger` and friends call
  registered servers (`file_server_2`, `code_server`, the group leader's io
  server, `logger`) that the `kernel` application starts at boot. A Clause
  executable starts none of them, so making their modules compile is not
  making them work. Which services exist at startup is decided in 41I.

The ledger above lives on in `docs/library.md` (41K) and is refreshed by every
step of this phase: rerun the luerl command, classify each new error
(`unknown module`, `undefined function M:F/A`, unsupported syntax or feature,
other), and add or adjust steps here before coding the next one.

<a id="step-41i"></a>

### 41I. Decide the replacement-module contract

Backlog: F36. Depends on: [41B](00-finished.md#step-41b). **Decision.**

- Success criteria
  - [x] `docs/library.md` gains a "Runtime module replacements" contract:
    - which modules qualify (counterparts of `erts/preloaded` modules and of
      modules OTP generates at build time, such as `unicode_util`) and where
      they live (proposed `library/erts/` next to `library/stdlib/`, with the
      library lookup searching both);
    - precedence (decided, user 2026-10-12): every module Clause provides —
      replacements, Clause's own `lists`, `maps`, `file`, `io`, `os`,
      `gen_tcp`, `gen_udp`, `inet`, `erlang` and any later library module —
      wins over a same-named module found through `source_search_paths`, so
      an installed or referenced OTP never changes what a build compiles;
    - explicit inputs (decided, user 2026-10-12): a source the user lists
      explicitly (`sources`, `source_dirs`, positional inputs) with the name
      of a Clause module is allowed and replaces the Clause module, with a
      warning that names both; order is explicit inputs, then Clause
      modules, then `source_search_paths`;
    - `ignore_modules` (decided, user 2026-10-12) applies to Clause modules
      too: an ignored Clause module is missing from the batch, and an
      explicitly listed source can then provide it (no warning, since
      nothing is replaced);
    - every Clause library module exports OTP 29's complete list for its
      name, so OTP callers compile against it; what is not implemented is a
      `stub` or `refused` like in the replacement modules;
    - a decision record per module (and per function group where a module
      mixes concerns), kept in `docs/library.md`: **provided** (the module
      exists; each group `implemented`, `refused` or `stub`) or **missing**
      (no module at all, possibly forever, with the reason and the
      higher-level Clause modules that shadow its OTP callers); a missing
      module never gets a stub, so a reference to it stays a compile-time
      error, never a run-time surprise. A missing module's name is
      reserved: a same-named file on `source_search_paths` (such as OTP's
      `erts/preloaded/src/prim_file.erl`) is not used, and the diagnostic
      says Clause does not provide the module; an explicit input may still
      provide it, with the replacement warning. Modules decided as
      trivially implementable for the runtime need no separate decision;
    - kernel services at startup: none (OTP modules that need a registered
      server fail as in OTP without one), a minimal set started by the
      startup object (for example `file_server_2` and an io server), or a
      booted `kernel` application; record the choice and its cost;
    - per-function statuses kept in an inventory per module, as
      `erlang.terms` does: `implemented` (runtime builtin or Erlang body),
      `refused` (an OTP-shaped error or return, such as `{error, enotsup}`,
      that callers already handle), `stub` (raises
      `error({not_implemented, F, A})` with `M:F/A` on top of the stack),
      `declined` (with the
      reason, for example distribution or code loading pending step 71);
    - OTP compatibility: the export list is exactly OTP 29's, observed from
      the host OTP (no copied source); results and error terms of
      implemented functions match OTP; differences go to
      `docs/differences.md`.
  - [x] The ledger refresh rule of this phase is written into the contract.
- Tests
  - [x] None beyond the gate.
- Done 2026-10-12: [runtime module replacements](../docs/library.md#runtime-module-replacements). Library
  modules move to `library/<application>/` (`erts`, `kernel`, `stdlib`) with `library/modules.terms` naming
  provided and missing (reserved) modules; inventories `{Name, Arity, Group, Status}`, statuses `implemented`,
  `refused`, `stub`, `declined`; no kernel services at startup for now (OTP modules needing one fail with
  `noproc` as on OTP without it). The ledger table and its refresh rule live in `docs/library.md`.

<a id="step-41j"></a>

### 41J. Generalize the export inventory and stub generator

Backlog: F36. Depends on: [41I](#step-41i).

- Success criteria
  - [x] `observe_erlang.escript` and `generate_erlang.py` generalize to any
    module: an escript observes a module's exports from the host OTP
    (`M:module_info(exports)`, with `module_info` itself left out), and a
    generator writes `<m>.terms` and the stub module once; after that the
    module is hand-edited and a check keeps it consistent. `erlang` keeps
    its current files and auto-import output.
  - [x] The check, without OTP: every inventory entry is exported by the
    module, nothing else is exported, every `stub` entry is a stub,
    and every `implemented` entry has a body or a catalog builtin. With
    host OTP: the inventory still matches OTP's exports (`host_otp` label).
  - [x] A committed report script runs the ledger classification of this
    phase over a `clau` stderr file (module, error kind, callers, called
    functions with arities), so each refresh is one command.
- Tests
  - [x] The consistency and freshness checks as CTests, including one
    deliberately inconsistent fixture inventory that fails.
- Done 2026-10-12: new `tests/compiler/library/` (`observe_module.escript`, `library_modules.py` with `check`,
  `rejects`, `generate`, `freshness`, `report`, `reporting`); the `erlang` observer and generator stay as they
  are (flags and auto-import output are erlang-only). Generated stubs are erlfmt-stable. The report resolves every
  luerl call site, including modules named through macros (the error column points at the call's `(`).
  CTests `library_inventory`, `library_rejects`, `library_report`, `library_freshness` (`host_otp`).

<a id="step-41k"></a>

### 41K. Make the provided runtime modules discoverable

Backlog: F36. Depends on: [41J](#step-41j).

- Success criteria
  - [x] Replacement modules with OTP 29's full export lists, all `stub`,
    for every module 41I decides as provided (proposed: `atomics`,
    `counters`, `erts_code_purger`, `erts_internal`, `init`,
    `persistent_term`, `unicode_util`, and `erl_prim_loader`, `zlib`,
    `prim_eval` if decided so), found by the library lookup. Modules
    decided as missing get no file.
  - [x] Module lookup order is explicit inputs, Clause modules, then
    `source_search_paths`; an explicit input that replaces a Clause module
    warns; `ignore_modules` removes Clause modules too. `docs/library.md`
    and `docs/projects.md` say so.
  - [x] Clause's existing library modules (`lists`, `maps`, `file`, `io`,
    `os`, `gen_tcp`, `gen_udp`, `inet`) get inventories and OTP 29's full
    export lists, with stubs for what they do not implement yet. For every
    missing module, each OTP caller luerl pulls in is either shadowed by a
    Clause module (new stub modules such as `socket`, `net`, `filelib`
    where 41P/41R decide so) or recorded as unsupported.
  - [x] The luerl rerun shows no `unknown module` error for provided
    modules and none reached through a shadowed caller; the next layer of
    errors is classified into the ledger in `docs/library.md`, and the
    steps below are adjusted or extended to it.
  - [x] `docs/library.md` lists provided and missing modules with their
    group statuses.
- Tests
  - [x] An executable calls one stub of each provided module directly,
    through `fun M:F/A` and by dynamic call, and gets
    `{not_implemented, F, A}` (user direction 2026-10-12, instead of `undef`)
    with the stub on top of the stack; inventory checks from 41J
    pass.
  - [x] Precedence: a project whose search path holds a `lists.erl` and a
    `prim_file.erl` of its own (stand-ins for OTP's) compiles Clause's
    `lists` and never the stand-in `prim_file` (a call to it reports that
    Clause does not provide the module), with the same output with and
    without that directory; the
    same `lists.erl` listed in `sources` replaces Clause's with the
    warning; with `lists` in `ignore_modules` a call to `lists` reaches the
    ignored-module placeholder (`{module_ignored, lists}`) without a warning.
    Open question for the user: today `ignore_modules` also ignores a listed
    `lists.erl`, so a listed source cannot provide an ignored module yet.
- Done 2026-10-12: library split into `library/{erts,kernel,stdlib}/` with `library/modules.terms`
  (`driver/library_modules`: application directories, reserved missing names); discovery searches the library
  before `source_search_paths`, warns when an explicit input replaces a Clause module and adds a `note` naming
  a reserved missing module where the batch first names it. Provided all-stub modules: `atomics`, `counters`,
  `erl_prim_loader`, `erts_code_purger`, `erts_internal`, `init`, `persistent_term`, `prim_eval`, `zlib`,
  `unicode_util`, and `application_controller` (it shadows OTP's, the only remaining caller of `prim_file`).
  Existing `lists`, `maps`, `io`, `file`, `os`, `gen_tcp`, `gen_udp`, `inet` completed to OTP 29's export lists
  (`library_modules.py complete`). Every library stub raises `error({not_implemented, F, A})` (user direction),
  `erlang.erl` included. luerl: no unknown module, no undefined function; the next layer is 473
  `undefined remote type` errors (41K1, 41K2). `test-luerl.toml` needs `NEW_STACKTRACE` in `defines` (luerl's
  `rebar.config.script` sets it on OTP 21+). Gate: fast CTest 267/268 (known `codegen_resource_cli`),
  `check-quality` clean. Tests `library_stubs`, `library_precedence`.

<a id="step-41k1"></a>

### 41K1. Declare OTP's exported types in Clause library modules

Backlog: F36. Depends on: [41K](#step-41k).

Clause's modules win over OTP's, so OTP code that names `file:filename()`,
`io:format()`, `erlang:time_unit()` or `maps:iterator_order()` in its specs
and types finds no such type. luerl (2026-10-12): `file` 305 errors
(`filename`, `name`, `name_all`, `filename_all`, `date_time`, `posix`, `fd`,
`io_device`, `mode`), `io` 51 (`format`, `device`), `erlang` 25
(`time_unit`, `timestamp`, `spawn_opt_option`, `stacktrace`, ...), `inet` 12,
`maps` 6.

- Success criteria
  - [x] Each provided module with exported OTP types includes a
    `<module>.types.hrl` observed from the host OTP; the check requires the
    include and the freshness test compares the header's forms.
  - [x] Decided (user, 2026-10-12): write each type as the documentation shows
    it, or as OTP's implementation defines it when unclear. The installed
    docs chunk has no type definitions, so the headers take them from the
    beam's debug info or the shipped source of preloaded modules.
    `erlang`'s generated module includes its header too.
- Tests
  - [x] A program whose specs name a type of each completed module compiles;
    inventory checks cover types.
- Done 2026-10-12: `tests/compiler/library/observe_types.escript` (exported types plus the local types and records
  they use, `--check`); headers for `atomics`, `counters`, `erlang` (66 forms, OTP's built-in type names included),
  `zlib`, `file`, `gen_tcp`, `gen_udp`, `inet`, `os`, `io`, `maps`; `library_modules.py include`; CTest
  `library_types`. Library inputs carry `CompilationInput::library_`: types their headers name from modules
  outside the program, and union widening inside them, are not reported. luerl's 473 `undefined remote type`
  errors are gone; with 41K2 the target compiles, links
  (`build/emqx.exe`) and the executable exits 0.

<a id="step-41k2"></a>

### 41K2. Remote types of ignored modules

Backlog: F36. Depends on: [41K](#step-41k).

- Success criteria
  - [x] A remote type of an ignored module (luerl: `erl_parse` 63 errors,
    `compile` 11, both ignored by the manifest) warns `attempt to import a
    type from a module ignored in project settings: M:T` and is `term()`
    (user, 2026-10-12); documented in `docs/projects.md#ignored-sources`.
  - [x] A source `sources` lists literally is never ignored: `ignore_modules`
    and `ignore_dirs` have no effect on it and the build warns (user,
    2026-10-12); wildcard matches and `source_dirs` are ignored as before.
- Tests
  - [x] A project ignoring a module whose types another module's specs name
    compiles with the decided diagnostic (`library_ignored`); a listed
    `lists.erl` in `ignore_modules` is compiled with the warning
    (`library_precedence`).
- Done 2026-10-12: placeholders' path prefix is `semantic::IGNORED_MODULE_DIRECTORY`; the type resolver warns for
  them. `project::Ignore::keep` records literally listed sources the rules match; the project driver warns.
  `test-luerl.toml` no longer ignores `inet` (a Clause module now).

<a id="step-41l"></a>

### 41L. `init` arguments and status

Backlog: F36. Depends on: [41K](#step-41k).

- Success criteria
  - [ ] Trivial for the runtime, so implemented: `get_argument/1`,
    `get_arguments/0`, `get_plain_arguments/0` (the executable's argv, with
    no emulator flags: `home` from the environment, other flags `error`),
    `script_name/0`, `get_status/0` (`{started, started}`),
    `objfile_extension/0`, `archive_extension/0`, `code_path_choice/0`,
    `fetch_loaded/0` (the registered modules), `notify_when_started/1`,
    `ensure_loaded/1`, `stop/0,1` with OTP's exit status rules.
  - [ ] Boot and configuration entries (`boot/1`, `get_configfd/1`,
    `set_configfd/2`, `restart/0,1`, `reboot/0`) decided per function.
- Tests
  - [ ] OTP-goldened executable with arguments and environment.

<a id="step-41m"></a>

### 41M. `persistent_term`

Backlog: F36, F05. Depends on: [41K](#step-41k).

- Success criteria
  - [ ] `put/2`, `put_new/2`, `get/0,1,2`, `erase/1`, `info/0` as runtime
    builtins over one runtime-wide table safe for several workers; how a
    stored term is shared (copied into an immutable literal area or copied
    out on every `get`) and released after `erase` is decided and
    documented in `docs/runtime-heap.md`; `badarg` cases as OTP.
- Tests
  - [ ] OTP-goldened executable with large values, several processes and
    collections; the multi-worker stress runs under TSan with V02.

<a id="step-41n"></a>

### 41N. `erts_internal`, `atomics`, `counters` and `erts_code_purger`

Backlog: F36, D01. Depends on: [41K](#step-41k).

- Success criteria
  - [ ] `erts_internal` functions that `stdlib` needs: `cmp_term/2`,
    `map_next/3` (the `maps` iterator protocol), `list_to_integer/2`,
    `map_to_tuple_keys/1`, `map_hashmap_children/1`; distribution and
    `.beam` functions `declined` or `refused` with OTP's non-distributed
    answers.
  - [ ] `atomics` and `counters` as runtime builtins (new, get, put, add,
    sub, exchange, compare_exchange, info), if trivial; otherwise decided.
  - [ ] `erts_code_purger:purge/1`, `soft_purge/1` answer as for a module
    without old code; `finish_after_on_load/2` follows step 71.
- Tests
  - [ ] OTP-goldened executable using `maps` iterators and ordering,
    `string:to_integer`-style callers, atomics and counters.

<a id="step-41o"></a>

### 41O. `unicode_util` from the Unicode data

Backlog: F36. Depends on: [41K](#step-41k).

- Success criteria
  - [ ] Decide how the tables are produced: an owned generator reading the
    Unicode Character Database files OTP 29 pins (`lib/stdlib/uc_spec`),
    run once and committed like other goldens (check the Unicode data
    license allows committing the generated tables), so normal builds need
    no OTP.
  - [ ] `cp/1`, `gc/1` (extended grapheme clusters per UAX #29),
    `casefold/1`, `lowercase/1`, `uppercase/1`, `titlecase/1`,
    `nfc/1`, `nfd/1`, `nfkc/1`, `nfkd/1`, `category/1`, `is_whitespace/1`,
    `is_wide/1`, `pattern_whitespace/0` and the identifier helpers, with
    OTP's results on binaries, lists and mixed chardata, and OTP's error
    tuples for invalid input. Split into sub-steps if the tables make one
    commit too large.
- Tests
  - [ ] Goldens from OTP's `unicode_util` over every code point for the
    property functions and over sampled strings for clusters and
    normalization; `string` and `unicode` functions luerl uses run.

<a id="step-41p"></a>

### 41P. Files: Clause `file` and `filelib`; `erl_prim_loader`, `prim_file`, `prim_buffer`

Backlog: F36, F35. Depends on: [41I](#step-41i), [41K](#step-41k).

- Success criteria
  - [ ] Decisions per module and group, following 41I's kernel-services
    decision: Clause's `file` (over the runtime file driver) is the file
    layer programs use and grows toward OTP's export list (`read_file_info`,
    `consult`, `open/2` modes, ...) as the ledger demands; `filelib` is
    either OTP's Erlang code running on Clause's `file` or a Clause module;
    `erl_prim_loader` is provided over the file driver (its callers are
    `code`, `code_server`, `filelib`, `inet_db`) or missing with those
    callers shadowed; `prim_file` and `prim_buffer` are proposed missing
    forever, with `raw_file_io*` and `application_controller` shadowed or
    recorded.
- Tests
  - [ ] OTP-goldened executables for the implemented file operations,
    including errors (`enoent`, `eacces`, `enotdir`).

<a id="step-41q"></a>

### 41Q. `zlib`

Backlog: F36. Depends on: [41K](#step-41k).

- Success criteria
  - [ ] Decide: link a pinned zlib into the runtime (the 41E
    `term_to_binary(_, [compressed])` gap would close with it) and
    implement the stream API, `gzip/1`, `gunzip/1`, `compress/1`,
    `uncompress/1`, `zip/1`, `unzip/1` as runtime builtins; or `refuse`
    the module with OTP's `badarg`/`not_supported`-style errors.
- Tests
  - [ ] If implemented: byte-exact goldens against OTP for compression
    levels and strategies, round trips, damaged input.

<a id="step-41r"></a>

### 41R. Networking: Clause `gen_tcp`, `gen_udp`, `inet`, `socket`, `net`; the `prim_*` layer

Backlog: F36, F35. Depends on: [41I](#step-41i), [41K](#step-41k).

- Success criteria
  - [ ] Clause's `gen_tcp`, `gen_udp` and `inet` (over the runtime socket
    ports, 57F) are the networking layer programs use and grow toward
    OTP's export lists as the ledger demands. Decide whether Clause also
    provides `socket` and `net` (OTP's newer API) over the same ports, or
    leaves them missing; `prim_socket`, `prim_net`, `prim_inet` and
    `socket_registry` are proposed missing forever, with their OTP callers
    (`socket`, `net`, `inet_tcp`, `inet_udp`, `gen_tcp_socket`,
    `gen_udp_socket`, ...) shadowed or recorded. `net` answers that need no
    socket (`gethostname/0`, `getaddrinfo/2`, `getnameinfo/2`,
    `if_names/0`) are implemented if trivial.
- Tests
  - [ ] OTP-goldened executables for whatever is implemented; refusal
    shapes for the rest.

<a id="step-41s"></a>

### 41S. `prim_eval` and the next luerl layer

Backlog: F36. Depends on: [41K](#step-41k)–[41R](#step-41r).

- Success criteria
  - [ ] `prim_eval:'receive'/2` (selective receive driven by a matcher fun,
    used by `erl_eval`) implemented or refused.
  - [ ] Rerun the luerl command and refresh the ledger; every remaining
    error is assigned to a new step of this phase, to another backlog item
    (for example `erl_parse` through yecc on the host OTP), or recorded as
    declined; the phase stays open until `luerl:init` links.
- Tests
  - [ ] As each new step defines.

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

<a id="step-69a"></a>

### 69A. Manual performance benchmarks against OTP

Backlog: V05. Depends on: [57F](00-finished.md#step-57f) (sockets),
[57E](00-finished.md#step-57e) (files), [62](00-finished.md#step-62) (`--lto`).
Added 2026-10-12 (user direction). **Manual only:** no CTest registration, not
part of the commit gate, `check-quality` or any preset; normal builds and tests
never need it or host OTP.

- Success criteria
  - [x] Owned benchmark sources in `benchmarks/` (new top-level directory,
    recorded in `files.md`), each one `.erl` module that both `erlc`/`erl` and
    `clau` build unchanged, timing its own kernel with
    `erlang:monotonic_time/1` after a warm-up and printing one result line:
    - data generation: lists, tuples, maps, binaries, big integers built in
      bulk;
    - data processing: sort, `lists:foldl`/`map`, map updates, binary
      matching, string/term conversion;
    - garbage collection: many short-lived processes and a long-lived process
      with a growing then shrinking heap (time plus collection count where
      both systems expose one);
    - file I/O: write, read back, line reads and `read_file`/`write_file` of
      small and large files in a temporary directory;
    - sockets: loopback `gen_tcp` echo throughput and connect/accept rate,
      `gen_udp` round trips;
    - message passing between processes as the cross-cutting baseline.
  - [x] One driver script (`benchmarks/run.py`, Python as `cmake/` scripts)
    with fixed iteration counts and repetitions; reports the median and
    spread of N runs per benchmark, wall time per run including startup
    listed separately from the kernel time. A whole default run, builds of
    the programs included, stays under 10 minutes (user direction
    2026-10-12); iteration counts are sized for the slowest system.
  - [x] Systems compared on the same host in one run:
    - OTP with the JIT (`erlang:system_info(emu_flavor)` confirms `jit`);
    - OTP without the JIT via `erl -emu_flavor emu` when that flavor is
      installed, otherwise the column reads `n/a (no emu flavor)`;
    - Clause at `HEAD`, Release build, O2, with and without `--lto`;
    - Clause at a previous version: by default the newest commit whose commit
      date is at least one day older than `HEAD` (`--baseline <rev>`
      overrides), built in a `git worktree` under `build/bench-base/<hash>` and
      cached by hash; a benchmark the baseline cannot build reads `n/a`.
  - [x] Text report: one row per benchmark, columns for each system's time and
    the change in percent against OTP JIT, OTP emu and the Clause baseline
    (`+12.3% faster` / `-40.1% slower`, same sign convention everywhere),
    with a header naming date, host, CPU, OS, OTP release, ERTS version,
    emu flavors found and both Clause commit hashes and dates.
  - [x] History: one record per run day and host (run date, HEAD commit
    hash and date, per-benchmark medians of Clause, OTP and the baseline)
    under `benchmarks/history/`, kept locally and out of git (user direction
    2026-10-12); a rerun on the same
    day replaces that day's record and report (user direction 2026-10-12).
    The driver prunes records and reports run more than 6 months before the
    newest, and the report shows the trend of each benchmark over the kept
    history (same host only).
  - [x] Invocation: a `make benchmark` target in the `Makefile` (manual use;
    agents never run it) and `make-benchmark.bat` for Windows, following
    `make-test.bat` (calls `tools\windows-toolchain.cmd`, honours `CMAKE`,
    `JOBS`, `CMAKE_ARGS` overrides). Both configure a Release build directory
    (`build/bench`) with `BUILD_TESTING=OFF`, build `clau` and the runtime,
    then call `benchmarks/run.py`; extra driver options pass through
    (`BENCH_ARGS` in make, remaining arguments in the `.bat`).
  - [x] `docs/validation.md` documents how to run it and that results are
    host-specific evidence, not gate results.
- Tests
  - [x] One recorded manual run through `make-benchmark.bat` on the Windows
    x64 host, its summary recorded in this step (history and report stay
    local); the gate shows no new CTest and no OTP need.
- Done 2026-10-12: `benchmarks/` (6 programs, 29 kernels, `bench_util.erl`, `run.py`), `make benchmark`,
  `make-benchmark.bat`, ignored `benchmarks/history/`. Run on Windows x64 (32 threads, OTP 29 / ERTS 17.1, JIT
  only: no emu flavor installed), HEAD `a7bc0c9` against baseline `89d0c7d`, 5 runs: 3 min 43 s in total.
  Clause O2 is 20.2x slower than OTP JIT (geometric mean); sockets are at par or faster (`tcp_connect`
  +42%, `tcp_echo` +9%, UDP -43%); the worst kernels look quadratic: `<<Bin/binary, X>>` appends ~480x,
  `maps:put` ~620x, binary matching ~730x, map updates ~420x; `lists:sort` ~120x. `--lto` and the
  one-day baseline are within noise of HEAD. The connect kernel closes on the server side first: client-side
  TIME_WAIT used up Windows' 16384 ephemeral ports (`eaddrinuse`). The gate is unchanged: no CTest added.

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
    The build directory and caches are already selected as [62D](00-finished.md#step-62d).
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
