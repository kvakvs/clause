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
| H3. Drop-in replacements for OTP runtime modules | [41I](#step-41i)–[41S](#step-41s) | F36, D01 | open |
| M. Validation closure | [63](00-finished.md#step-63)–[70](#step-70), [65A](00-finished.md#step-65a), [65B](00-finished.md#step-65b), [65C](00-finished.md#step-65c) | V01–V04, D03 | open: [66](#step-66), [69](#step-69), [70](#step-70) |
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
      that callers already handle), `stub` (raises `error(undef)` with
      `M:F/A` on top of the stack, like the 41B stubs), `declined` (with the
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
  - [ ] `observe_erlang.escript` and `generate_erlang.py` generalize to any
    module: an escript observes a module's exports from the host OTP
    (`M:module_info(exports)`, with `module_info` itself left out), and a
    generator writes `<m>.terms` and the stub module once; after that the
    module is hand-edited and a check keeps it consistent. `erlang` keeps
    its current files and auto-import output.
  - [ ] The check, without OTP: every inventory entry is exported by the
    module, nothing else is exported, every `stub` entry raises `undef`,
    and every `implemented` entry has a body or a catalog builtin. With
    host OTP: the inventory still matches OTP's exports (`host_otp` label).
  - [ ] A committed report script runs the ledger classification of this
    phase over a `clau` stderr file (module, error kind, callers, called
    functions with arities), so each refresh is one command.
- Tests
  - [ ] The consistency and freshness checks as CTests, including one
    deliberately inconsistent fixture inventory that fails.

<a id="step-41k"></a>

### 41K. Make the provided runtime modules discoverable

Backlog: F36. Depends on: [41J](#step-41j).

- Success criteria
  - [ ] Replacement modules with OTP 29's full export lists, all `stub`,
    for every module 41I decides as provided (proposed: `atomics`,
    `counters`, `erts_code_purger`, `erts_internal`, `init`,
    `persistent_term`, `unicode_util`, and `erl_prim_loader`, `zlib`,
    `prim_eval` if decided so), found by the library lookup. Modules
    decided as missing get no file.
  - [ ] Module lookup order is explicit inputs, Clause modules, then
    `source_search_paths`; an explicit input that replaces a Clause module
    warns; `ignore_modules` removes Clause modules too. `docs/library.md`
    and `docs/projects.md` say so.
  - [ ] Clause's existing library modules (`lists`, `maps`, `file`, `io`,
    `os`, `gen_tcp`, `gen_udp`, `inet`) get inventories and OTP 29's full
    export lists, with stubs for what they do not implement yet. For every
    missing module, each OTP caller luerl pulls in is either shadowed by a
    Clause module (new stub modules such as `socket`, `net`, `filelib`
    where 41P/41R decide so) or recorded as unsupported.
  - [ ] The luerl rerun shows no `unknown module` error for provided
    modules and none reached through a shadowed caller; the next layer of
    errors is classified into the ledger in `docs/library.md`, and the
    steps below are adjusted or extended to it.
  - [ ] `docs/library.md` lists provided and missing modules with their
    group statuses.
- Tests
  - [ ] An executable calls one stub of each provided module directly,
    through `fun M:F/A` and by dynamic call, and gets OTP's `undef`
    exception with the stub on top of the stack; inventory checks from 41J
    pass.
  - [ ] Precedence: a project whose search path holds a `lists.erl` and a
    `prim_file.erl` of its own (stand-ins for OTP's) compiles Clause's
    `lists` and never the stand-in `prim_file` (a call to it reports that
    Clause does not provide the module), with the same output with and
    without that directory; the
    same `lists.erl` listed in `sources` replaces Clause's with the
    warning; with `lists` in `ignore_modules` a call to `lists` is an
    unknown module until `sources` lists one, which then compiles without
    a warning.

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
