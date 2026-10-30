# Completed implementation work

Compact record of completed work, one bullet per step, in plan order. Full step text, criteria and
evidence are in Git at the listed commits; current behavior lives in `docs/`, architecture and file
ownership in [arch.md](arch.md) and [files.md](files.md). Gate counts, per-step dates and
measurements are dropped.

Per-plan records:

- [Plan 11](11-finished.md): steps 1-73 and the luerl library gaps (2026-10-03 to 2026-10-15)
- [Plan 20](20-finished.md): runtime and code generation optimizations (from 2026-10-12)
- [Plan 21](21-finished.md): Clause's own erts, kernel and stdlib (from 2026-10-17)
- [Plan 22](22-finished.md): compiler optimizations, type inference first (from 2026-10-23)
- [Plan 30](30-finished.md): network distribution (from 2026-10-30)

## Before plan 11 (2026-09-17 to 2026-10-03)

- **Foundations:** CLI/CMake scaffold; C++23, warnings as errors, Clang toolchain; `clau`,
  independent `clause_runtime`, shared `clause_abi`; stages exchange owned data, stage readers only
  reserved directories.
- **Preprocessor 1–3:** parser/oracle boundary, source buffers and locations, Erlang lexer, form
  dispatch and recovery ([preprocessor](../docs/preprocessor.md)).
- **Preprocessor 4–6:** object and parameterized macros, overloads, recursion checks, rescanning,
  stringification from original tokens.
- **Preprocessor 7–9:** nested conditionals, include/include_lib lookup, contextual
  module/file/line/function/machine/OTP macros.
- **Preprocessor 10–13:** restricted condition evaluator, feature macros, warning/error directives,
  CLI options, OTP golden fixtures; focused OTP comparisons only, no full upstream Common Test run.
- **Parser 1–4:** grammar/oracle inventory, shared token mechanics, move-only AST arenas with
  checked IDs, transactional forms ([parser](../docs/parser.md)).
- **Parser 5–12:** literals, operators, calls, clauses, patterns, guards, maps, records, binaries,
  blocks, branching, receive, funs, try/catch, maybe, comprehensions.
- **Parser 13–18:** attributes, type/spec/callback declarations, recovery,
  `--parse-check`/`--print-pp`/`--print-ast`, differential coverage; 344 grammar productions
  witnessed, ten-source OTP corpus (syntax only).
- **Projects 1–22:** toml++ manifests (schema 1), targets, globs, selection, planning,
  `--new-project`; platform matrix not closed ([projects](../docs/projects.md)).
- **Test migration:** 16 native project suites moved to CLI fixtures, frontend to CLI/OTP
  projections and 900 mutations, runtime to a separately built consumer; ledger audit still open
  (step VAL3).
- **Compiler/runtime 1–8:** LLVM SDK discovery, owned results, target policy, verification, object
  emission, ABI v1, feature reporting ([compile](../docs/compile.md)).
- **Compiler/runtime 9–14:** runtime/context lifecycle, immediate terms, frozen builtin registries,
  lazy memory, scheduler bookkeeping.
- **Compiler/runtime 15–28:** declarations, subset checks, acyclic batch calls, symbolic types,
  independent inference, target-width integers, calls, descriptors, native registration.
- **Compiler/runtime 29–32:** bounded specialization with guarded variants (3 per function, 32 per
  module, 2x growth), LLVM O0/O2, text/bitcode output.
- **Compiler/runtime 33–46:** artifact publication, options, project integration, `--verbose`,
  IR/type inspection, native harnesses, OTP comparisons, cross-target inspection, examples.
- **Pattern matching 1–3:** semantic matrix (81 signatures), revision-2 failure channel, runtime
  atoms and module bindings ([patterns](../docs/patterns.md)).
- **Pattern matching 4–12:** clause-local bindings, normalization, immediate matching, guard
  resolution and control flow, RHS-first body matches, stable heaps, rooted tuples/lists/strings.
- **Pattern matching 13–20 and 15a:** bignums, floats, exact-key maps, project-owned goldens,
  bitstrings, records, audited guard catalog (77 of 81 rows), stress; 1.08M golden comparisons.

## Outstanding work to finish

[Plan 11](11-plan.md) orders the gaps. Done: everything above.

- [ ] **Native platform validation:** macOS Apple Silicon (step VAL1) and native ARM hardware; Linux
  x86-64, 32-bit x86 and ARM under emulation done.
- [ ] **OTP compatibility:** upstream Common Test suites, wider differential comparisons
  (step VAL2).
- [ ] **Test migration closure:** retire adapters after equivalent public coverage (step VAL3).
- [ ] **Parse transform closure:** real projects and the user guide (step CLAU7).

Deferred, not required: dynamic modules (RT1), atom collection (RT2), behavior-changing attributes
beyond the selected slices (CLAU3), public interchange (CLAU4), stage readers (CLAU5), C/FFI
(RT3), project-schema extensions (CLAU6). Every change keeps formatting, behavioral tests and the fresh
combined Debug quality gate without weakened thresholds or suppressions.
