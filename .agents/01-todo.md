# Missing features and completion backlog

Stable feature owners for the remaining work (created 2026-09-30 from
[outstanding work](00-finished.md#outstanding-work-to-finish), compacted
2026-10-04 and 2026-10-10). [Plan 11](11-plan.md) orders it into small steps.
IDs are references, not priorities. Checked items are delivered scoped slices;
parsed syntax, API sketches and placeholders do not count as support. Full
wording of delivered items is in Git history and [completed work](00-finished.md).

## Shared completion checklist

For each implemented slice: define behavior, exclusions and failures; settle
representation, ownership and ABI first; test through real sources, CLI,
executables and OTP comparisons, keeping justified invariant and fault tests;
replace placeholders only for implemented semantics; update docs, `arch.md`,
`files.md`; format and pass the fresh combined Debug gate without weakened
checks. OTP-dependent work starts by refreshing `maint-29` per
[otp-reference.md](../docs/otp-reference.md).

## Delivered feature owners

Complete for the selected scope; the plan steps hold criteria and evidence.

| ID | Feature | Plan steps | Contract |
| --- | --- | --- | --- |
| F02 | Roots, safepoints, frames and generated-code ABI | 8E, 8F, 17, 19, 23, 24, 26, 51 | [abi](../docs/abi.md#frames-and-transfers) |
| F03 | Process heaps and term construction | 8A–8I, 31B, 32, 42 | [runtime-heap](../docs/runtime-heap.md) |
| F06 | Atom table, synchronized for workers | 54 | [terms](../docs/terms.md#atoms) |
| F07 | Pid, port and reference identities | 42, 45, 48, 50, 57B | [terms](../docs/terms.md#pids-and-references) |
| F08 | Lists, tuples, maps, strings; `lists`/`maps` subsets | 27B–27D, 28, 37, 39 | [terms](../docs/terms.md#tuples-lists-strings) |
| F09 | Binaries and bitstrings, off-heap buffers | 8B, 8H, 28 | [terms](../docs/terms.md#bitstrings) |
| F10 | Arbitrary integers with the ERTS size limit | 27E, 28 | [terms](../docs/terms.md) |
| F12 | Equality, comparisons and term order | 31, 32, 42 | [terms](../docs/terms.md) |
| F13 | Pattern matching and bindings in every context | 9–16, 21, 22, 32–34, 46 | [patterns](../docs/patterns.md) |
| F14 | Guards, identity and function type tests | 9, 10, 52 | [guards](../docs/guards.md) |
| F15 | Multiple function clauses | — | [patterns](../docs/patterns.md) |
| F16 | Sequences, blocks, `case`, `if`, `maybe`, comprehensions | 9, 10, 16, 21, 22 | [features](../docs/features.md) |
| F17 | Records: ordinary, updates, `record_info/2`, native | 29–31E | [features](../docs/features.md) |
| F18 | Function values, closures, named funs | 32–34 | [abi](../docs/abi.md) |
| F19 | Dynamic calls and `apply` | 35, 36 | [features](../docs/features.md) |
| F21 | Recursion and proper tail calls | 17–20, 34 | [execution model](../docs/execution-model.md) |
| F22 | Cooperative processes, exits, links, monitors | 43, 43A, 44, 48, 49 | [processes](../docs/processes.md) |
| F23 | Scheduler workers, wakeups, timer wheel | 56, 57, 62B | [processes](../docs/processes.md) |
| F24 | Signal inbox and message sending | 45 | [processes](../docs/processes.md) |
| F25 | Selective receive and timeouts | 46, 47, 62B | [processes](../docs/processes.md) |
| F26 | Production builtins: bridge, families, `io`, registered names | 4, 36–40, 43A, 50 | [runtime](../docs/runtime.md) |
| F27 | Typed native callables | 41 | [runtime](../docs/runtime.md) |
| F28 | Concurrent code server | 55 | [runtime](../docs/runtime.md) |
| F29 | Source-driven specialization (O2 proofs) | 59 | [specialization](../docs/specialization.md) |
| F30 | Debug information (`-g`) | 60, 63 | [debugging](../docs/debugging.md) |
| F31 | Profiling (`--profile FILE`) | 61 | [profiling](../docs/profiling.md) |
| F32 | Link-time optimization (`--lto`) | 62 | [compile](../docs/compile.md) |
| F33 | Indexed code-server lookups | 62A | [runtime](../docs/runtime.md) |
| F35 | Ports: subprocess, file, standard I/O, sockets | 57A–57G3 | [ports](../docs/ports.md) |
| V02 | ASan/UBSan/LSan and TSan, no suppressions | 67, 68 | [validation](../docs/validation.md) |

## Open items

### Executables and runtime data

- **F01 — Executable startup and linking** (3–8, 43, 58; delivered: entry,
  startup, linking, runtime options, program fixtures end to end):
  [ ] `--args-file` options file like `vm.args` (reserved; reports not
  implemented).
- **F04 — Process garbage collection** (8A–8I, 23–27A, 51; delivered: Cheney
  collector, generated-code triggers, defined exhaustion, caps,
  `garbage_collect/0`): [ ] optional later: generational old heap with minor
  collections.
- **F05 — Graph copying and process isolation** (28, 45; delivered:
  sharing-preserving copies between heaps of one runtime): [ ] cross-runtime
  atom/identity handling (cross-runtime copies are `wrong_owner`).
- **F36 — `erlang` module builtins** ([41A](00-finished.md#step-41a)–[41H](00-finished.md#step-41h); delivered: every
  OTP 29 `erlang` export is implemented, compiler-handled or a named stub in `library/stdlib/erlang.erl`):
  [ ] TSan timer stress (41F, with V02); [ ] replacements for the ERTS-preloaded and generated modules
  `kernel`/`stdlib` call (`init`, `persistent_term`, `unicode_util`, `zlib`, `prim_*`, ...; phase H3,
  [41I](11-plan.md#step-41i)–[41S](11-plan.md#step-41s));
  [ ] leex/yecc generation for `.xrl`/`.yrl` sources through the host OTP.
- **F32 follow-up — export liveness** ([62C](00-finished.md#step-62c)): [x] unused exports leave the export
  table so dead-stripping and `--lto` can remove them (executables; `--keep-exports` opts out).
- **F11 — Floating-point values** (38; delivered: finite binary64 values,
  operations, conversions): [ ] broader numeric scope when selected.

### Language semantics

- **F20 — Erlang exceptions** (11–15; delivered: raise classes, `catch`,
  `try`/`of`/`catch`/`after`, stack traces, `raise/3`): [ ] nested handling and
  cleanup across module and builtin calls.

### Optimization

- **F34 — Precise type inference** ([58A](11-plan.md#step-58a)–[58N3](11-plan.md#step-58n3);
  delivered: fact domain, literal/operator/container/fun facts, caller
  inputs, narrowing by patterns, guards and uses, spec contradictions,
  per-clause function types and call selection, per-call re-analysis,
  dependent facts of `case`/`if`/`try ... of`, nonempty list cells, `apply/2,3` calls,
  `--print-types` trailing `% Type` notes; contract
  [semantic](../docs/semantic.md#inference)):
  [ ] a `case` on a variable that itself depends on an argument splitting over
  that argument; [ ] optional report of calls no function type admits.
  Expectations: `tests/fixtures/inference/` (values 143, base_types 46,
  narrowing 53, clauses 53, dependent 25 functions) and `contracts/`.

### Validation obligations

Keep historical results with their original hosts and revisions.

- **V01 — Native platform matrix** ([63](11-plan.md#step-63)–[66](11-plan.md#step-66);
  delivered: Linux x86-64, 32-bit x86 runtime on Windows/Linux, Linux
  AArch64/armhf under qemu-user): [ ] native ARM hardware; [ ] macOS Apple
  Silicon (66, also `dsymutil` for F30).
- **V03 — Broader OTP compatibility** ([69](11-plan.md#step-69); delivered:
  pin refreshed, program fixtures goldened and matched as executables, steps
  1, 2, 58): [ ] upstream Common Test suites and wider differential
  comparisons.
- **V04 — Remaining test migration** ([70](11-plan.md#step-70); delivered:
  executable golden runner, step 8): [ ] audit the
  [ledger](../docs/validation.md#test-design) and retire covered adapters.

## Optional or explicitly deferred scope

Each ends in a recorded selection, deferral or omission; omission is never
checked as implemented.

- **D01 — Dynamic modules and code upgrades** ([71](11-plan.md#step-71)):
  static-only, native dynamic libraries, or an upgrade model.
- **D02 — Atom collection** ([72](11-plan.md#step-72)): bounded permanent
  storage versus collection.
- **D03 — Behavior-changing attributes and transforms**
  ([73](11-plan.md#step-73)): compile options, on-load; parse transforms
  selected for [73A–73H](11-plan.md#step-73a) through host Erlang/OTP;
  `-behaviour` implemented by [65A](11-plan.md#step-65a);
  `module_info/0,1` and informational attributes implemented by
  [65B](11-plan.md#step-65b); imports and compile hints implemented by
  [65C](11-plan.md#step-65c).
- **D04 — Public stage interchange** ([74](11-plan.md#step-74)): only for a
  concrete consumer.
- **D05 — Intermediate-stage readers** ([75](11-plan.md#step-75)): directories
  reserved only.
- **D06 — C/FFI interoperability** ([76](11-plan.md#step-76)): only for a
  concrete external caller.
- **D07 — Project schema extensions** ([77](11-plan.md#step-77)): defaults,
  profiles, dependencies, exclusions, fetching, watch/cache, parallel builds.
  `build_dir` and caches are selected as [62D](00-finished.md#step-62d): [x] build directory, transform and
  whole-batch compile caches, directory index (62E), SQLite cache backend ([62D5](00-finished.md#step-62d5));
  [ ] per-module compile reuse, directory scan cache (not worth it on measurements).

## References

[Completed work](00-finished.md), [architecture](arch.md), [file map](files.md),
[compilation](../docs/compile.md), [validation](../docs/validation.md),
[runtime](../docs/runtime.md), [runtime heap](../docs/runtime-heap.md),
[features](../docs/features.md); design sketches in `runtime/design/`.
