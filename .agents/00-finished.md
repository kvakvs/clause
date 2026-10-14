# Completed implementation work

Compact record of completed work, compatibility decisions and historical
evidence (consolidated 2026-09-30, compacted 2026-10-04, plan 11 phases F–M
added 2026-10-10, plan 11 step table and N2 moved here 2026-10-11, phases H2,
L2 and L3 moved here and every step record rewritten as bullet lists
2026-10-12). Current behavior lives in the linked `docs/`; architecture and
file ownership are in [arch.md](arch.md) and [files.md](files.md). Full earlier
wording, criteria and evidence are in Git history at the listed commits.

- **Foundations** (2026-09-17): CLI/CMake scaffold.
- **Preprocessor** (steps 1–13, 2026-09-18): complete; qualification: focused
  OTP comparisons, no full upstream Common Test run.
- **Parser** (steps 1–18, 2026-09-19): complete; qualification: platform
  matrix not closed.
- **Projects** (steps 1–22, 2026-09-20): complete; qualification: historical
  macOS and later Windows evidence stay distinct.
- **Test migration** (2026-09-28): frontend, project and runtime migrations;
  qualification: ledger audit (plan 11 step 70).
- **Compiler/runtime milestone** (steps 1–46, 2026-09-29): immediate-only
  subset and runtime skeleton.
- **Pattern matching and guards** (steps 1–20 and 15a, 2026-10-01–03):
  function clauses and body matches over the admitted domain.
- **Plan 11** (steps 1–68, 41A–41H, 62C–62E, 65A–65C, 73A–73G: phases A–L,
  H2, L2, L3, M without 66/69/70, N2 without 73H; 2026-10-03–12): linked
  executables, `-behaviour` callback checks, `module_info/0,1` and
  informational attributes, `-import`, parse transforms on host OTP with
  abstract-format export/import, ERTS-style heaps and collection, exceptions,
  recursion, comprehensions, records, funs, processes, multi-worker
  scheduling, ports, `erlang` module builtins, export liveness, build
  directory and SQLite build caches, precise inference, specialization,
  tooling, Linux/x86/ARM validation and sanitizers.

**Still unfinished:** macOS Apple Silicon validation, upstream OTP Common Test
evidence, the test-migration ledger audit, the optional scope decisions (D01–D07),
parse transform closure and final closure (plan 11 steps 66, 69–78, 73H).
See [outstanding work](#outstanding-work-to-finish) and [the backlog](01-todo.md).

## Foundations

Targets Erlang/OTP 29 on Windows x86-family, Linux x86/ARM and macOS Apple
Silicon. C++23, CMake 3.28+, warnings as errors, Clang reference toolchain.
`clau` builds the compiler; `clause_runtime` builds independently and never
depends on LLVM or compiler internals; `clause_abi` carries shared
contracts. Stages exchange owned internal data; only directory locations are
reserved for `compiler/src/stage_readers/{preprocessed,abstract,ir}/`. APIs stay
project-internal C++23 (earlier C wrappers and C++26 options were superseded).
Diagnostics keep source/macro/include context on stderr; failure is the exit
status.

## Preprocessor

Contract and limits: [preprocessor.md](../docs/preprocessor.md). Independent of
LLVM/runtime, one isolated session per source; Boost.Parser is a pinned
character-parser dependency (a token-iterator adapter was rejected).

- **1–3:** Parser/oracle boundary, source buffers/encoding/locations, Erlang lexer, form dispatch and recovery.
- **4–6:** Object/parameterized macros, overloads, definition order, recursion checks, rescanning, stringification from original tokens.
- **7–9:** Nested conditionals, include/include_lib lookup, contextual module/file/line/function/machine/OTP macros.
- **10–11:** Restricted condition evaluator; feature configuration and feature-query macros.
- **12–13:** Warning/error directives, failure latching, provenance, CLI options, OTP golden fixtures.

Preserved OTP details: only `true` selects a branch (`42` is false), evaluation
failure is false while malformed syntax diagnoses, skipped regions still check
structure, fun types need no `end`, `maybe`/`else` change token classes when
enabled; `maybe_expr` is approved/default-on, `compr_assign` experimental/off.
Reference: `lib/stdlib/src/epp.erl`, `epp_SUITE.erl` (cases `predef_mac`,
`rec_1`, `overload_mac`, `otp_8130`, `stringify`, `test_if`, `include_local`,
`file_macro`, `function_macro`, `encoding`, `test_error`, `scan_file`),
`erlc_SUITE` `features_*`, `compile_SUITE` `cond_and_ifdef`, `erl_scan_SUITE`
sigils/triple-quoted strings. Full upstream execution needs a matching built OTP
with its Common Test hooks.

## Parser

Contract and evidence: [parser.md](../docs/parser.md). Consumes expanded tokens
directly (no relexing). Move-only module arenas with checked expression,
pattern, type and form IDs; source metadata keeps logical anchors and physical
origins across macros/includes; ASTs outlive preprocessing sessions. Parsing is
transactional per form and bounded in nodes, depth, work and diagnostics.
Syntax acceptance is separate from binding, guard legality, linting and types;
record expansion and `v3_core` are not parsing.

- **1–4:** Grammar/oracle inventory, shared token mechanics, AST ownership, transactional forms.
- **5–9:** Literals, operators/calls, clauses, patterns, guards, maps, records, binaries.
- **10–12:** Blocks, branching, receive, funs, try/catch, maybe, comprehensions.
- **13–15:** Attributes, type/opaque/nominal declarations, specs/callbacks.
- **16–18:** Recovery, `--parse-check`/`--print-pp`/`--print-ast`, differential coverage, pinned corpus.

Closure: 344 grammar productions witnessed (423 inventory rows), 43 positive and
183 negative fixtures, three OTP builder exceptions; a ten-source OTP corpus
(hashes in `tests/fixtures/parser/phase6/otp.tsv`) proves syntax coverage only.

## Projects

Usage and schema: [projects.md](../docs/projects.md). Private code owns toml++
3.4.0 loading, schema, paths/globs, selection, planning, CLI and creation.
Schema 1: integer `schema_version = 1`, unique target names
`[A-Za-z0-9_][A-Za-z0-9_.-]*`, nonempty `sources` or `source_dirs`; unknown keys,
wrong types and duplicates fail everywhere. Repeated `--target` selects in
request order; unknown selectors exit 2. Manifest paths resolve against the
manifest's directory, CLI paths against cwd. Globs support `*`, `?`, `**` only;
directory discovery skips symlinks. Sources keep entry order, sort each
expansion by UTF-8 bytes and deduplicate by physical identity. CLI include paths
precede manifest ones. `--new-project` creates a template exclusively (one `app`
target, `source_dirs = ["src"]`, `build/app[.exe]` output). Since plan 11 steps
6A–7, project targets with `output` or `entry` link executables. Defaults,
dependencies, imports, profiles, exclusions, fetching, watch/cache and parallel
execution stay outside schema 1 (D07); `build_dir` and build caches were
selected ahead of that decision (plan 11 steps 62D and 62E).

## Testing strategy and migration

The [case-level ledger](../docs/validation.md#test-design) is authoritative.
Prefer CLI workflows on real Erlang files, exact artifacts/diagnostics, separately
built runtime consumers and OTP comparisons; test count is not a goal. The
2026-09-28 migration moved 16 native project suites to CLI fixtures, frontend
behavior to CLI/OTP projections and 900 bounded mutations, and runtime behavior
to a separately built consumer. Keep API-only budgets, invalid handles, injected
allocation/write failures, invalid IR and 32/64-bit layout checks that public
workflows cannot reach. Stage dump adapters and private backend checks stay
until equivalent public coverage exists.

## Compiler and runtime milestone (steps 1–46, 2026-09-29)

Contracts: [compilation](../docs/compile.md), [semantics](../docs/semantic.md),
[specialization](../docs/specialization.md), [features](../docs/features.md).

- **1–8:** LLVM SDK discovery, owned compilation results, target policy, verification, object emission, ABI v1, feature reporting.
- **9–14:** Runtime/context lifecycle, immediate terms, frozen builtin registries, lazy memory, scheduler bookkeeping, explicit unavailable services.
- **15–23:** Declarations/exports, subset checks, bindings, acyclic batch calls, symbolic declared types, independent inference, contract warnings.
- **24–28:** Target-width integers, parameters, local/remote calls, versioned descriptors, transactional registration with native execution.
- **29–32:** Bounded specialization with guarded variants, LLVM O0/O2, text/bitcode serialization.
- **33–39:** Artifact publication, options, positional/project integration, `--verbose`, IR and type inspection.
- **40–46:** Native harnesses, OTP comparisons, specialization costs/caps, cross-target inspection, failure hardening, examples.

Lasting decisions: unsupported syntax is rejected even in unused functions;
specs warn on contradictions but never narrow representation; `-O0` stays
generic, `-O2` adds bounded variants (3 per function, 32 per module, 128 per
target, 2x growth) with generic fallback; `--no-type-specialization` always
wins. `--emit obj|llvm-ir|llvm-bc`, `--artifact-dir`, `--target-triple`,
`--print-ir`, `--print-optimized-ir`, `--print-types`. Failed compilation keeps
earlier outputs; multi-file replacement is not atomic. ABI v1: unsigned target
words, checked 28/60-bit small integers, collision-free symbols, generic entries
take a context and aligned term array. Every runnable program links one
matching runtime through `Clause::generated_program`.

Validation: macOS arm64 for steps 1–14 (65 to 93 CTests), Windows x64 for steps
15–46 (76 to 103 CTests, up to 182 quality commands), Clang/SDK 23.1.2,
clang-tidy 22.1.8. Runtime-only ASan passed; full compiler ASan was unavailable
(SDK STL annotation ABI mismatch) and is not counted. Per-step tables are in Git
history and [validation](../docs/validation.md#history).

<a id="completed-patternmatch"></a>

## Pattern matching and guards (steps 1–20 and 15a, 2026-10-01–03, Windows x64)

Context: [retained plan summary](11-plan.md#completed-patternmatch),
[semantic matrix](../docs/patterns.md), [guards](../docs/guards.md). Executes
ordered function clauses with guards, body matches/sequences and acyclic
local/exported remote calls over owned atoms, arbitrary integers, finite floats,
tuples, lists, strings, exact-key maps, bitstrings and ordinary tuple records.

- **1–3:** Semantic matrix (81 signatures), revision-2 checked failure channel, runtime atoms and revision-3 module bindings.
- **4–6:** Clause-local bindings and facts, pattern normalization, immediate matching with shared equality.
- **7–10:** Guard resolution and services, grouped/strict/lazy control flow, ordered clause candidates, RHS-first body matches.
- **11–12:** Stable heaps, revision-4 generated roots and handoffs; rooted tuple/list/string construction and matching.
- **13–17:** Arbitrary integers, binary64 floats, exact-key maps, project-owned goldens (15a), bitstrings, ordinary records.
- **18–20:** Audited guard catalog (77 of 81 rows; `self/0`, `node/0,1`, native `is_record/1` gated), binding facts, stress and closure.

Final: 19 owned corpora with 67,634 expected outcomes plus 106 semantic rows,
1,082,144 golden comparisons over eight driver/policy combinations; fresh
Windows x64 Debug 124/124 CTests and 258 quality units. Descriptors use ABI
revision 4; the revision-2 failure channel is unchanged. Normal builds and tests
need no OTP.

## Plan 11 (steps 1–68, 41A–41N, 41O1, 41O2, 41T, 41U, 41V, 41W, 41X, 41M1, 41M2, 41Y1, 41Y2, 41Z, 58O–58S, 58U–58W, 62C–62E, 65A–65C, 69A, 73A–73G, 2026-10-03–14)

Per-step commits are in [the step list](#plan-11-step-commits); full step text,
criteria and evidence are in Git at those commits. Contracts in the linked
`docs/`. Windows x64 unless noted.

- **1–2 (A):** `maint-29` pin `21776803`; fast/full test modes; changed-scope quality; six OTP-goldened program fixtures with a feature map.
- **3–8 (B):** Entry/argv/exit contract ([executables](../docs/executables.md)), escript mode, `erlang:display/1`, startup object and `halt/0,1`, Clang linking of positional and project builds with staged publication, executable golden runner. Phase close: 138/138 CTests.
- **8A–8I (C):** ERTS-style heap ([runtime-heap](../docs/runtime-heap.md)): off-heap binary list, parseable areas with walker, header admission, raw-word host terms, one heap block plus fragments, Cheney collector with ERTS sizing; per-context footprint 66 KB → 2.4 KB.
- **9–16 (D):** `begin`/`case`/`if` with OTP scoping and `case_clause`/`if_clause`; `error`/`exit`/`throw`; `catch`; `try ... of ... catch` and `after`; stack traces (8 frames), `Class:Reason:Stack`, `raise/3`; `maybe` with `else_clause`.
- **17–22 (E):** Execution model ([execution-model](../docs/execution-model.md)): explicit frames on a flat process stack, `musttail` transfers; recursive call graphs, proper tail calls, deep non-tail recursion; list, binary and map comprehensions with OTP generator/filter errors.
- **23–28 (F):** Roots for frames, registers and the failure channel; safepoints and collection from generated code; `out_of_memory` without default cap, optional `--max-heap`/`--max-stack`/`--max-memory`; list/map caps removed, tuple arity and bignum limits as OTP; sharing-preserving copies between heaps.
- **29–35 (G):** Record updates, `record_info/2`, local/qualified/anonymous native records (31A–31E); funs with and without captures, named funs, dynamic calls and `apply`.
- **36–41 (H):** Production builtin bridge; term-access and conversion families; project-owned `lists`/`maps` subsets; `io:format`/`put_chars`; typed native callables.
- **41A–41H (H2):** 2026-10-11, `erlang` module builtins ([builtins](../docs/builtins.md)): `runtime/src/builtins/erlang/` one file per family; owned OTP 29 export inventory `library/stdlib/erlang.terms` with the generated stub module `erlang.erl` and OTP auto-import table (stubs raise `undef` only when called, builtins replace them); process dictionary; binary/atom/number conversions; external term format (byte-exact, fuzzed); time, unique values and timers (`MessageBuffer` keeps timer messages in runtime format); introspection, spawn options, spawn requests, aliases, `monitor/3`, hibernation; fun information, hashing, MD5; non-distributed node and code-loading answers. Every step has an OTP-goldened executable; luerl rerun shows no missing `erlang` function.
- **42–53 (I):** Pid/reference identities; cooperative executor, interruptible builtins (43A); exits and crash reports; signal inbox and send; selective receive and `after`; links, `trap_exit`, monitors, registered names; collection with mailboxes; identity guards; port scope decision (superseded by J2).
- **54–57 (J):** Synchronized atom table and code server; several scheduler workers; cross-worker wakeups, timers, shutdown.
- **57A–57G3 (J2):** Ports ([ports](../docs/ports.md)): identities and table, I/O poller, subprocess, file, standard I/O and socket ports, one event-driven I/O thread, port tasks on workers, busy-port suspension.
- **58 (K):** Six program fixtures run through their manifests and match OTP at O0/O2 on 1 and 4 workers.
- **58A–58N3 (L):** Inference ([semantic](../docs/semantic.md#inference)): fact domain, literal/operator/container/fun facts, caller inputs, narrowing by patterns, guards, uses and integer comparisons, entry/success domains, spec contradictions as errors, per-clause function types, call selection, per-call re-analysis, dependent facts of `case`/`if`/`try ... of`; `--print-types`.
- **59–62B (L):** O2 proofs remove tag/shape checks; debug info (`-g`); profiling (`--profile`); LTO (`--lto`); hashed code-server lookups; timer wheel. Phase close: 235/235 full CTests, `check-quality-all` clean.
- **62C (L2):** 2026-10-11, export liveness ([executables](../docs/executables.md#export-liveness)): executables leave out the functions their entry cannot reach (direct calls, funs, literal apply/spawn targets; escaped module names keep whole modules; a dynamic call plus an atom-producing builtin keeps everything); left-out exports keep only their names for `function_exported/3`; `--keep-exports` and `keep_exports` opt out.
- **62D1–62D5, 62E (L3):** 2026-10-11–12, build directory and caches ([projects](../docs/projects.md#build-directory), [compile](../docs/compile.md#compile-cache)): per-invocation in-memory index of searched directories (lookups that miss cost no probe); `build_dir`/`--build-dir` with Clause temp files below `<build_dir>/tmp`; a cache store holding parse transform replies and whole-batch compile outcomes, `--no-cache`, `(cached)` stage times; first a file backend (62D2), then SQLite 3.54 (`cache/v2/cache.db`, parts above 256 KB in content-addressed blob files, WAL, safe across threads and processes, damaged databases moved aside, prune); no directory scan cache (measured); `[time] plan`. luerl warm build 3.8 s -> 1.65 s. Phase gate: full CTest 263/264 (known `codegen_resource_cli`), `check-quality-all` clean; 62D5 fast CTest 261/262.
- <a id="plan-11-i2-i3"></a>**58O–58S (I2):** 2026-10-13, inference convergence and cost
  ([semantic](../docs/semantic.md#inference)): `--print-stage-times` `inference rounds` per recursive group;
  groups that hit their round cap or an eighth of the pass work are remembered and given `term()` at once; rounds
  never shrink function types and a member flipping back to one of its last 4 summaries gives up alone; a round
  re-walks only members whose read summaries changed; unions drop exact tuples another same-size tuple holds, so
  joins at the container depth limit stop alternating. luerl inference 330 s to 107 s (work 65.6M to 48.6M
  steps). Tests `inference_budgets`, `inference_trees`, `deep_tree_joins`.
- **58U–58W (I3):** 2026-10-13, inference cache for library directories
  ([projects](../docs/projects.md#inference-cache), [semantic](../docs/semantic.md#inference-cache)):
  `cache_inference_in_dirs`/`--cache-inference-in-dirs` plus implicit `*/stdlib`, `*/kernel`, `*/compiler`,
  `*/syntax_tools` (and `-*` forms), relative patterns matching trailing directories; one module's results as
  bytes (`semantic/types/inference_store`, nodes in graph order, facts by AST arena position); covered modules
  keyed by their own and every atom-reached module's fingerprint (calls, funs, `apply` atoms, native records),
  reused when the kept entry holds every live function, fixed during inference, fresh ones stored. luerl: 138
  of 188 modules covered, inference 180 s to 40.5 s on a warm recompile. Tests `cache_inference`,
  `semantic_inference` round trip, `project_cli`/`cli` `--print-env`. Gate: full CTest 278/279 (known
  `codegen_measurements` IR size bound, failing before 58U too), `check-quality-all` clean.
- **41I–41K2 (H3):** 2026-10-12, runtime module replacements ([library](../docs/library.md#runtime-module-replacements)):
  contract (Clause modules win over `source_search_paths`, explicit inputs replace them with a warning,
  `ignore_modules` applies to them, missing modules are reserved names); library split into
  `library/{erts,kernel,stdlib}/` with `library/modules.terms`; inventories `{Name, Arity, Group, Status}`
  (`implemented`, `refused`, `stub`, `declined`) checked against the modules and, with host OTP, OTP 29's exports
  (`tests/compiler/library/`: observer, generator, ledger report); all-stub replacements of the preloaded modules
  (`atomics`, `counters`, `erl_prim_loader`, `erts_code_purger`, `erts_internal`, `init`, `persistent_term`,
  `prim_eval`, `zlib`, `unicode_util`, `application_controller`); existing library modules completed to OTP 29's
  export lists; stubs raise `{not_implemented, F, A}`; OTP's exported types in `<module>.types.hrl` headers; remote
  types of ignored modules warn and are `term()`; literally listed sources are never ignored. luerl compiles, links
  and exits 0. Tests `library_inventory`, `library_rejects`, `library_report`, `library_freshness`,
  `library_stubs`, `library_precedence`, `library_types`, `library_ignored`.
- **41L (H3):** 2026-10-14, `init` of a started system without a boot script ([builtins](../docs/builtins.md#init-module)):
  startup decodes argv, the executable path and the home directory once (`ProgramArguments` in
  `RuntimeOptions::program`); builtins `get_plain_arguments/0`, `get_arguments/0`, `get_argument/1` (`home` only),
  `script_name/0`; Erlang answers of OTP's started `init` (`{started, started}`, `not_allowed`, `fetch_loaded/0`
  `[]`, configfd in `persistent_term`); `stop/0,1`, `reboot/0` halt at once with OTP's status rules; `boot/1`,
  `restart/0,1`, `make_permanent/2` refused (`notsup`). Also `lists:prefix/2`, `suffix/2`, `filtermap/2`, `zf/2`, `uniq/1,2` (`lists_data`). luerl
  driver rerun: Lua syntax errors now equal OTP's, 33 of 35 snippets match (left: `os.date` needs `re`, `goto`
  frame detail). Test `executables_init_module` (OTP-goldened runs, exit statuses 0/3/1, authored exact argv).
  Gate: fast CTest 287/287 (1 disabled), `check-quality` clean.
- **41N (H3):** 2026-10-14, `atomics`, `counters`, `erts_internal`, `erts_code_purger` ([builtins](../docs/builtins.md#atomics-and-counters)):
  handle references ([runtime heap](../docs/runtime-heap.md#handle-references)): a reference cell in the
  `refc_binary` layout holding an off-heap buffer, listed, swept, charged and copied like an off-heap binary
  (`layout::off_heap_cell`), revived by `binary_to_term/1` through a process-wide weak registry while the buffer
  lives; `atomics` builtins over them (`std::atomic_ref`), `counters` in Erlang over atomics; `erts_internal`
  builtins `cmp_term/2`, `map_next/3` and Erlang `list_to_integer/2`, `binary_to_integer/2`, `term_type/1`,
  `map_to_tuple_keys/1`, `counters_*`, `dynamic_node_name/0,1`; distribution/beam/emulator groups declined;
  `erts_code_purger:purge/1`, `soft_purge/1` answer for linked code; `maps:iterator/1,2`, `next/1`, `lists:sort/2`,
  `split/2`. Test `executables_atomics_counters` (4 sharing processes, persistent term, collections, ETF round
  trip). Gate: fast CTest 288/288 (1 disabled), `check-quality` clean.
- **41O1 (H3):** 2026-10-14, `unicode_util` tables ([builtins](../docs/builtins.md#unicode-util)): generated C++
  tables (`runtime/src/builtins/unicode/tables.cpp`, 573 KB) from `tools/unicode/observe.escript` (the host OTP's
  answers per code point) and `tools/unicode/generate.py` (plus the UCD break, emoji, Indic and composition files);
  private `clause_unicode` builtins `table/1`, `class/1`, `cases/1`, `flags/1`, `compose_pair/2`; `unicode_util`
  `cp/1` (OTP's shapes), `lookup/1`, `category/1`, `get_case/1`, case functions, `is_wide/1`, whitespace and
  identifier predicates, `spec_version/0`, `pattern_whitespace/0`. All 1.1M code points matched OTP once; test
  `executables_unicode_tables` digests plane 0 and 6 sample blocks. Gate: fast CTest 289/289 (1 disabled),
  `check-quality` clean.
- **41O2 (H3):** 2026-10-14, `unicode_util:gc/1` and normalization ([builtins](../docs/builtins.md#unicode-util)):
  UAX #29 clusters in OTP's rule order and result shapes over `clause_unicode:flags/1` (controls, prepend, Hangul
  L/V/T/LV/LVT, regional pairs, pictographic ZWJ sequences, Indic conjuncts); `nfd/nfc/nfkd/nfkc` with algorithmic
  Hangul, canonical ordering and composition pairs; `lists:keysort/2`. Test `executables_unicode_clusters`
  (every rule as lists, binaries and mixed chardata; 2000-string seeded sweep). Gate: fast CTest 290/290 (1
  disabled), `check-quality` clean.
- **41T (H4):** 2026-10-14, `lists` data functions as runtime builtins ([builtins](../docs/builtins.md#lists-module)):
  `runtime/src/builtins/lists/` (`sublist/2,3`, `droplast/1`, `nthtail/2`, `last/1`, `duplicate/2`, `join/2`,
  `flatten/1,2`, `max/1`, `min/1`, `usort/1`, `umerge/1,2`, `keymember/3`, `keysearch/3`, `keydelete/3`) with OTP's
  `function_clause`/`badarg` and laziness; `foreach/2`, `mapfoldl/3`, `mapfoldr/3`, `splitwith/2`, `partition/2` as
  Erlang bodies. The call tracer of 6a7d402a no longer allocates without tracing (`runtime_lifecycle_failure`) and
  passes clang-tidy. Test `executables_lists_data`. Gate: fast CTest 277/278 before the last tidy-only edits
  (`linking_compile_cache` "Access is denied" publishing an exe, passes on rerun), `check-quality` clean.
- **41U (H4):** 2026-10-14, `io` output devices ([io](../docs/io.md#calls)): `io:format/3`, `io:fwrite/1,2,3`,
  `io:put_chars/2`, `io:nl/0,1` as builtins; `standard_io`/`user` standard output, `standard_error` standard error,
  a pid or registered name an I/O server written through `file:write/2` (the builtin traps into it; export liveness
  keeps it live with those builtins); OTP's `badarg`, `terminated`, `calling_self`, `function_clause`. Test
  `executables_io_output`. Gate: fast CTest 279/279 (1 disabled), `check-quality` clean.
- **41V (H4):** 2026-10-14, `math` ([builtins](../docs/builtins.md#math-module)): provided module
  `library/stdlib/math.erl` (OTP 29 exports as stubs) whose every function is a builtin over the C library
  (`runtime/src/builtins/math.cpp`); `badarg` for non-numbers, `badarith` for oversized integers, domain errors and
  overflows. Last-bit differences of the host C library recorded (`erfc(1)` on Windows). Test `executables_math`.
  Gate: fast CTest clean, `check-quality` clean.
- **41W (H4):** 2026-10-14, `binary` ([builtins](../docs/builtins.md#binary-module)): provided module
  `library/stdlib/binary.erl` (OTP 29 exports and types); `runtime/src/builtins/binary/` builtins for search
  (`match`, `matches`, `split` with `global`/`trim`/`trim_all`/`scope`, `compile_pattern` as `{bm | ac, Patterns}`),
  parts and lists, unsigned and hex encodings; `replace/3,4` (fun replacements, `insert_replaced`) and
  `list_to_bin/1` in Erlang over them. Test `executables_binary_module`. Gate: fast CTest clean, `check-quality`
  clean.
- **41X (H4):** 2026-10-14, `os` and `maps` gaps ([builtins](../docs/builtins.md#maps-module)): builtins
  `os:getenv/0`, `env/0`, `putenv/2`, `unsetenv/1`, `getpid/0`, `version/0` and `maps:get/3`, `is_key/2`,
  `remove/2`, `new/0`, `size/1`, `merge/2`, `take/2`, `update/3`, `with/2`, `without/2`, `from_keys/2` with
  OTP's `{badmap, M}`/`{badkey, K}`/`badarg`; Erlang `os:timestamp/0`, `system_time/0,1`, `perf_counter/0,1`,
  `getenv/2` and `maps:filter/2`, `map/2`, `foreach/2`, `update_with/3,4`. Test `executables_os_maps`. Gate: fast
  CTest clean, `check-quality` clean.
- **41M1 (H4):** 2026-10-14, runtime-wide persistent term table ([runtime](../docs/runtime.md#persistent-terms)):
  `memory/persistent_terms` on `Runtime::Impl`, keys and values in message buffers found by key hash and exact
  equality under a shared lock; decided: reads copy the value into the reader's heap (in-place literals would need a
  shared term region), replaced or erased values freed with their last reader. `MessageBuffer::value`/`words`.
  Test `runtime_persistent_terms`; TSan left to the Linux sanitizer host (V02). Gate: fast CTest clean,
  `check-quality` clean.
- **41M2 (H4):** 2026-10-14, `persistent_term` builtins ([builtins](../docs/builtins.md#persistent-terms)): `put/2`,
  `put_new/2`, `get/0,1,2`, `erase/1`, `info/0` over 41M1's table, keys hashed with `phash2`'s term hash
  (`builtins/erlang/hashing.hpp`) and matched exactly; OTP's `badarg` for a missing key and a conflicting
  `put_new/2`. Test `executables_persistent_term` (large values, 8 reader processes, readers keeping copies after
  replace and erase). Gate: fast CTest clean, `check-quality` clean.
- **41Y1 (H4):** 2026-10-14, `unicode` conversions ([builtins](../docs/builtins.md#unicode-module)): provided module
  `library/stdlib/unicode.erl` (OTP 29 exports and types); `runtime/src/builtins/unicode/` builtins
  `characters_to_binary/1,2,3`, `characters_to_list/1,2`, `bin_is_7bit/1`, `bom_to_encoding/1`, `encoding_to_bom/1`
  for latin1/utf8/utf16/utf32 in both byte orders, binaries decoded as one stream, OTP's `error`/`incomplete`
  results with OTP-shaped rest terms (derived from probes); normalization and properties stay stubs (41O). Test
  `executables_unicode`. Gate: fast CTest clean, `check-quality` clean.
- **41Y2 (H4):** 2026-10-14, `file` gaps ([ports](../docs/ports.md#standard-io-and-files)): file-driver operation
  read_file_info (`ports/file_info.cpp`, a fixed status record) behind `file:read_file_info/1,2` (OTP's
  `#file_info{}`, `{time, local | universal | posix}`, `raw`); `file:write_file/3` over `open/2`; `write` no longer
  truncates when `append` is also given. Test `executables_file_info`. Gate: fast CTest clean, `check-quality` clean.
- **41Z (H4):** 2026-10-14, `code` without a code server ([builtins](../docs/builtins.md#code-module)): provided
  module `library/kernel/code.erl` (OTP 29 exports and types); builtins `ensure_loaded/1`, `ensure_modules_loaded/1`,
  `is_loaded/1`, `all_loaded/0`, `which/1`, `module_status/0,1`, `is_sticky/1` answer for the linked program
  (`preloaded`). luerl driver rerun: 32 of 35 snippets print OTP's output; left: `os.date` (`re`), the syntax-error
  path (`init:get_arguments/0`, 41L), `goto` stack-frame detail ([21-lib](21-lib.md)). Test
  `executables_code_module`. Phase gate: full CTest 289/290 (known `codegen_measurements` IR size bound),
  `check-quality-all` clean.
- **69A (M):** 2026-10-12, manual benchmarks against OTP ([validation](../docs/validation.md)): `benchmarks/` (6
  programs, 29 kernels, `run.py`), `make benchmark`, `make-benchmark.bat`, local history; never part of the gate.
  First run: Clause O2 20.2x slower than OTP JIT (geometric mean), sockets at par, quadratic binary appends and map
  updates found.
- **63–65, 67–68 (M):** Linux x86-64 full gate (WSL2); 32-bit runtime on Windows x86 and Linux i386 with cross-linked goldens; arm64/armhf under qemu-user; ASan/UBSan/LSan and TSan clean on Linux after fixing a shutdown use-after-free and a lock-order inversion ([validation](../docs/validation.md)).
- **65A–65C (M):** 2026-10-10, from compiling luerl ([semantic](../docs/semantic.md), [compile](../docs/compile.md)): `-behaviour`/`-behavior` with OTP callback warnings and a generated `behaviour_info/1`; generated `module_info/0,1` returning literal data and informational attributes kept in `module_info(attributes)`; `-import` of any module with OTP conflicts; optimization/reporting `-compile` hints ignored; rejected attributes name themselves. Lint goldens from OTP 29.1.1.
- **73A–73G (N2):** 2026-10-11, parse transforms ([transforms](../docs/transforms.md)): contract; owned abstract term model with ETF and `.abstr` text codecs (the ETF `Writer`/`Reader` in `abi/include/clause/abi/external_term.hpp` is shared with the future runtime `term_to_binary` and distribution); `--print-abstr` equal to OTP `epp` forms; `.abstr` inputs; embedded loader run on host OTP 29 (`--erl`, `PATH`, default install, 600 s limit); transforms applied before lint in positional and project drivers with OTP's diagnostics; precompiled `.beam`s from `--transform-path`/`transform_paths`, project transforms compiled in memory each run. Last gate 242/243 fast CTests (known `codegen_resource_cli` failure), check-quality clean.

<a id="plan-11-step-commits"></a>

### Plan 11 step commits

- <a id="step-1"></a>**1.** Refresh the OTP reference and record a fresh baseline — `2d27503`
- <a id="step-1a"></a>**1A.** Tests run time too long — `07d2c38`
- <a id="step-1b"></a>**1B.** Quality check checks too much — `3cdc743`
- <a id="step-1c"></a>**1C.** Run the available documentation check — `e381a86`
- <a id="step-2"></a>**2.** Author target program fixtures and their feature map — `1a006ac`
- <a id="step-3"></a>**3.** Decide the entrypoint, arguments and exit-status contract — `af55175`
- <a id="step-3a"></a>**3A.** Add escript compile mode — `8bb5f89`
- <a id="step-4"></a>**4.** Add runtime term printing and erlang:display/1 — `d63132b`
- <a id="step-5"></a>**5.** Generate the startup object — `6a246ae`
- <a id="step-6"></a>**6.** Link executables from positional CLI inputs — `8f68c18`
- <a id="step-6a"></a>**6A.** Link a single project target with explicit -o — `0c27f72`
- <a id="step-7"></a>**7.** Link executables from project targets — `bcaa23f`
- <a id="step-8"></a>**8.** Add the executable golden test runner — `736a194`
- <a id="step-8a"></a>**8A.** Decide the classic process heap contract — `02ddf8d`
- <a id="step-8b"></a>**8B.** Split binary cells and add the off-heap list — `34005ee`
- <a id="step-8c"></a>**8C.** Make heap areas parseable and add a heap walker — `ad0f810`
- <a id="step-8d"></a>**8D.** Admit heap words by header instead of the object index — `a0dbc00`, `0c76a8a`
- <a id="step-8e"></a>**8E.** Hold host terms as raw words between safe points — `e5aa219`
- <a id="step-8f"></a>**8F.** Move generated root frames onto a process stack — `e99e4a8`, `b4c99d3`, `9573722`, `7ac15fb`
- <a id="step-8g"></a>**8G.** Replace chunks with a contiguous heap and heap fragments — `c162308`
- <a id="step-8h"></a>**8H.** Collect on explicit host request with a copying collector — `d0cf00b`
- <a id="step-8i"></a>**8I.** Close the heap rework — `dc6e349`
- <a id="step-9"></a>**9.** Lower `begin`/`end` blocks and `case` expressions — `60102a1`
- <a id="step-10"></a>**10.** Lower `if` expressions — `1982697`
- <a id="step-11"></a>**11.** Raise exceptions from source — `80cd244`
- <a id="step-12"></a>**12.** Lower `catch Expr` — `328a290`
- <a id="step-13"></a>**13.** Lower `try … of … catch` — `27867ed`
- <a id="step-14"></a>**14.** Lower `try … after` — `9df4e63`
- <a id="step-15"></a>**15.** Provide stack traces and `erlang:raise/3` — `c311ae7`
- <a id="step-16"></a>**16.** Lower `maybe` expressions — `aecbff2`
- <a id="step-17"></a>**17.** Decide the frame and continuation model — `5447a16`
- <a id="step-18"></a>**18.** Accept recursive call graphs in analysis — `0e6eb6d`
- <a id="step-19"></a>**19.** Implement proper tail calls — `eff731f`
- <a id="step-20"></a>**20.** Support deep non-tail recursion — `26f8b27`
- <a id="step-21"></a>**21.** Lower list comprehensions — `07d249a`
- <a id="step-22"></a>**22.** Lower binary and map comprehensions — `1f58ba4`
- <a id="step-23"></a>**23.** Extend the root inventory to the execution model — `a3ef043`
- <a id="step-24"></a>**24.** Decide collection triggers and safepoints in generated code — `cf9d82b`
- <a id="step-25"></a>**25.** Collect on explicit runtime request (folded into 8H) — no commit of its own
- <a id="step-26"></a>**26.** Collect from generated code — `0a94ccf`
- <a id="step-27"></a>**27.** Report heap exhaustion as a defined failure — `bcd1ed5`, `8e939f1`, `3a21bc0`
- <a id="step-27a"></a>**27A.** Runtime-wide memory limit and program-facing caps — `6a87dae`
- <a id="step-27b"></a>**27B.** Remove list length caps — `5701ab5`
- <a id="step-27c"></a>**27C.** Match OTP's tuple arity limit — `12f772d`
- <a id="step-27d"></a>**27D.** Remove map size and key-work caps — `d4baa1f`
- <a id="step-27e"></a>**27E.** Match OTP's big integer limit — `1c085f9`
- <a id="step-28"></a>**28.** Copy term graphs between heaps — `4d707e5`
- <a id="step-29"></a>**29.** Lower record updates — `ef7e2cc`
- <a id="step-30"></a>**30.** Implement record_info/2 — `7186f18`
- <a id="step-31"></a>**31.** Implement native, qualified and inferred record forms (31A–31E) — no commit of its own
- <a id="step-31a"></a>**31A.** Decide the native record contract — `8e3e18b`
- <a id="step-31b"></a>**31B.** Add native record cells and runtime services — `206afc1`
- <a id="step-31c"></a>**31C.** Compile local native records — `68252fc`
- <a id="step-31d"></a>**31D.** Compile qualified and imported native records — `595650c`
- <a id="step-31e"></a>**31E.** Compile anonymous native record forms — `a80d26b`
- <a id="step-32"></a>**32.** Implement function values without captures — `bffcb27`
- <a id="step-33"></a>**33.** Implement closures with captured variables — `76e4713`
- <a id="step-34"></a>**34.** Implement named funs — `4ad9bce`
- <a id="step-35"></a>**35.** Implement dynamic calls — `919f32e`
- <a id="step-36"></a>**36.** Implement the generic production builtin bridge — `78cbb41`
- <a id="step-37"></a>**37.** Add the term-access builtin family — `b29cbfc`
- <a id="step-38"></a>**38.** Add the conversion builtin family — `cd03833`
- <a id="step-39"></a>**39.** Ship a project-owned library subset for lists and maps — `21f1f77`
- <a id="step-40"></a>**40.** Add console output through io — `f6a5c30`
- <a id="step-41"></a>**41.** Add typed native callables for builtin implementations — `09bdac0`
- <a id="step-41a"></a>**41A.** Organize `erlang` builtins in their own source directory — `9e22ff1`
- <a id="step-41b"></a>**41B.** Declare `erlang` exports through a library stub — `8c7cb84`
- <a id="step-41c"></a>**41C.** Process dictionary — `3fec7f8`
- <a id="step-41d"></a>**41D.** Binary, atom and number conversions — `efa7d4c`
- <a id="step-41e"></a>**41E.** External term format — `da8118c`
- <a id="step-41f"></a>**41F.** Time, unique values and timers — `f9579d5`
- <a id="step-41g"></a>**41G.** Process introspection and spawn options (41G1–41G3) — no commit of its own
- <a id="step-41g1"></a>**41G1.** Process and system introspection — `12a2838`
- <a id="step-41g2"></a>**41G2.** Spawn options, spawn requests, aliases and hibernation — `752c5fc`
- <a id="step-41g3"></a>**41G3.** Fun information, hashing and MD5 — `a0a9bc9`
- <a id="step-41h"></a>**41H.** Single-node distribution and code-loading answers — `89d0c7d`
- <a id="step-41i"></a>**41I.** Decide the replacement-module contract — `8c6a94f1`
- <a id="step-41j"></a>**41J.** Generalize the export inventory and stub generator — `b284a034`
- <a id="step-41k"></a>**41K.** Make the provided runtime modules discoverable — `347ae0ad`
- <a id="step-41k1"></a>**41K1.** Declare OTP's exported types in Clause library modules — `4810de56`
- <a id="step-41k2"></a>**41K2.** Remote types of ignored modules — `4810de56`
- <a id="step-41l"></a>**41L.** `init` arguments and status — commit `[plan11] 41L ...`
- <a id="step-41n"></a>**41N.** `erts_internal`, `atomics`, `counters` and `erts_code_purger` — commit `[plan11] 41N ...`
- <a id="step-41o1"></a>**41O1.** `unicode_util` tables, properties and case — commit `[plan11] 41O1 ...`
- <a id="step-41o2"></a>**41O2.** `unicode_util` grapheme clusters and normalization — commit `[plan11] 41O2 ...`
- <a id="step-41o"></a><a id="step-41t"></a>**41T.** `lists` data functions as builtins — commit `[plan11] 41T ...`
- <a id="step-41u"></a>**41U.** `io` output wrappers — commit `[plan11] 41U ...`
- <a id="step-41v"></a>**41V.** `math` as runtime builtins — commit `[plan11] 41V ...`
- <a id="step-41w"></a>**41W.** `binary` as runtime builtins — commit `[plan11] 41W ...`
- <a id="step-41x"></a>**41X.** `os` and `maps` gaps — commit `[plan11] 41X ...`
- <a id="step-41m1"></a>**41M1.** Runtime-wide persistent term table — commit `[plan11] 41M1 ...`
- <a id="step-41m2"></a>**41M2.** `persistent_term` builtins — commit `[plan11] 41M2 ...`
- <a id="step-41y1"></a>**41Y1.** `unicode` conversions — commit `[plan11] 41Y1 ...`
- <a id="step-41y2"></a>**41Y2.** `file` gaps — commit `[plan11] 41Y2 ...`
- <a id="step-41z"></a>**41Z.** `code` without a code server, and the luerl rerun — commit `[plan11] 41Z ...`
- <a id="step-42"></a>**42.** Implement pid and reference identities — `59064bf`
- <a id="step-43"></a>**43.** Run spawned processes on a cooperative executor — `f092504`
- <a id="step-43a"></a>**43A.** Make long-running builtins interruptible — `0682b54`
- <a id="step-44"></a>**44.** Define process exit and crash reports — `98ad97a`
- <a id="step-45"></a>**45.** Implement the signal inbox and message send — `dbad5db`
- <a id="step-46"></a>**46.** Implement selective receive without timeout — `45b424c`
- <a id="step-47"></a>**47.** Implement receive ... after timeouts — `e404460`
- <a id="step-48"></a>**48.** Implement links, exit signals and trap_exit — `82d8187`
- <a id="step-49"></a>**49.** Implement monitors — `e111740`
- <a id="step-50"></a>**50.** Implement registered process names — `d881d4b`
- <a id="step-51"></a>**51.** Collect garbage with mailboxes and suspended processes — `55347de`
- <a id="step-52"></a>**52.** Enable identity-dependent guards — `dd6b8f2`
- <a id="step-53"></a>**53.** Decide port identity scope — `583e779`
- <a id="step-54"></a>**54.** Synchronize the atom table — `25215cf`
- <a id="step-55"></a>**55.** Synchronize code-server publication and lookup — `8b8a699`
- <a id="step-56"></a>**56.** Run processes on multiple scheduler workers — `c1ff627`
- <a id="step-57"></a>**57.** Handle cross-worker wakeups, timers and shutdown — `fa54da5`
- <a id="step-57a"></a>**57A.** Decide the port contract — `d39a9e1`
- <a id="step-57b"></a>**57B.** Add port identities and the port table — `21a5199`
- <a id="step-57c"></a>**57C.** Run the I/O poller with scheduler wakeups — `d1dc2f2`
- <a id="step-57d"></a>**57D.** Open subprocesses as ports — `6b04929`
- <a id="step-57e"></a>**57E.** Implement file I/O and standard I/O through ports — `b1bc5d7`
- <a id="step-57f"></a>**57F.** Implement sockets as ports — `8c99374`
- <a id="step-57g"></a>**57G.** Schedule ports like processes on an event-driven backend (57G1–57G3) — no commit of its own
- <a id="step-57g1"></a>**57G1.** Serve every port kind from one event-driven I/O thread — `fbacc98`
- <a id="step-57g2"></a>**57G2.** Run port tasks on the scheduler workers — `171b7f9`
- <a id="step-57g3"></a>**57G3.** Suspend senders on busy ports and bound unread input — `616e7b2`
- <a id="step-58"></a>**58.** Run the target fixture projects end to end — `3ec5737`
- <a id="step-58a"></a>**58A.** Decide the inference fact domain — `e52277c`
- <a id="step-58b"></a>**58B.** Infer facts of all literals — `a6c01b5`
- <a id="step-58c"></a>**58C.** Infer operator and builtin results — `e92650f`
- <a id="step-58d"></a>**58D.** Infer container facts — `6adc311`
- <a id="step-58e"></a>**58E.** Infer fun facts — `ebac255`
- <a id="step-58f"></a>**58F.** Infer local function inputs from their callers — `8b1e998`
- <a id="step-58f1"></a>**58F1.** Print consecutive integers as ranges — `bfab7a8`
- <a id="step-58g"></a>**58G.** Narrow facts by patterns and guards; infer entry domains — `22dff64`
- <a id="step-58h"></a>**58H.** Narrow facts by their uses — `c56d515`
- <a id="step-58h1"></a>**58H1.** Narrow integer ranges by guard comparisons — `16fb58e`
- <a id="step-58i"></a>**58I.** Reject specifications that contradict inferred types — `be76267`
- <a id="step-58i1"></a>**58I1.** Print term() as `_` — `579e605`
- <a id="step-58i2"></a>**58I2.** Print argument relations as variable names — `6d02bd1`
- <a id="step-58j"></a>**58J.** Keep wide known containers and print records — `bc94017`
- <a id="step-58j1"></a>**58J1.** Infer try and maybe values — `2607172`
- <a id="step-58k"></a>**58K.** Keep per-clause function types and print them — `c7f52b6`
- <a id="step-58l"></a>**58L.** Match calls against function types — `01fbba5`
- <a id="step-58m"></a>**58M.** Re-analyse callees for each call — `06042fa`
- <a id="step-58n1"></a>**58N1.** Keep dependent facts of case and if — `e63e14c`
- <a id="step-58n2"></a>**58N2.** Carry dependent facts through operations and bindings — `60a5dd4`
- <a id="step-58n3"></a>**58N3.** Dependent facts of tuple scrutinees and try ... of — `175b545`
- <a id="step-58o"></a>**58O.** Measure the fixed-point rounds of recursive groups — `c712d605`
- <a id="step-58p"></a>**58P.** Remember groups that failed to converge — `1ebaca6d`
- <a id="step-58q"></a>**58Q.** Make fixed-point rounds monotone and detect flipping — `fa55a8d5`
- <a id="step-58r"></a>**58R.** Re-walk only the members whose inputs changed — `2a3059ba`
- <a id="step-58s"></a>**58S.** Make fact joins monotone at the container depth limit — `b5aec11f`
- <a id="step-58u"></a>**58U.** Option `cache_inference_in_dirs` and `--cache-inference-in-dirs` — `5490832e`
- <a id="step-58v"></a>**58V.** A module's inference results as bytes — `d8b26d1b`
- <a id="step-58w"></a>**58W.** Reuse cached inference results of covered modules — commit `[plan11] 58W ...`
- <a id="step-59"></a>**59.** Make specialization remove real source checks — `8a5ace2`
- <a id="step-60"></a>**60.** Emit source-level debug information — `116cff1`
- <a id="step-61"></a>**61.** Add opt-in profiling — `ecb8de5`
- <a id="step-62"></a>**62.** Integrate link-time optimization — `5ec3645`
- <a id="step-62a"></a>**62A.** Index code server lookups with hash maps — `aa0198b`
- <a id="step-62b"></a>**62B.** Keep receive timers in a timer wheel — `eccd7c4`
- <a id="step-62c"></a>**62C.** Drop unreachable exports before emission — `fbfe99c`
- <a id="step-62d"></a>**62D.** Build directory and cached build results (62D1–62D5) — no commit of its own
- <a id="step-62d1"></a>**62D1.** Build directory option and temporary files — `0cd45db`
- <a id="step-62d2"></a>**62D2.** Cache store and frontend results — `8e71e4d`
- <a id="step-62d3"></a>**62D3.** Directory scan cache (measured, no code) — `d5a655f`
- <a id="step-62d4"></a>**62D4.** Compile result cache — `176c4dd`
- <a id="step-62d5"></a>**62D5.** Replace the cache file backend with SQLite — `f4d29b9`
- <a id="step-62e"></a>**62E.** In-memory index of searched directories — `0e8a6c5`
- <a id="step-63"></a>**63.** Validate on Linux x86-64 — `5948a44`
- <a id="step-64"></a>**64.** Validate 32-bit x86 (Windows x86 and Linux x86) — `9eef9cc`
- <a id="step-65"></a>**65.** Validate Linux AArch64 and 32-bit ARM — `ab5943b`
- <a id="step-65a"></a>**65A.** Implement `-behaviour`/`-behavior` attributes — `e3f7a89`
- <a id="step-65b"></a>**65B.** Generate `module_info/0,1` and keep informational attributes — `86d84aa`
- <a id="step-65c"></a>**65C.** Import functions, accept compile hints, name rejected attributes — `12dc2d5`
- <a id="step-67"></a>**67.** Run compiler and frontend sanitizers — `58b3243`
- <a id="step-68"></a>**68.** Run ThreadSanitizer on the multi-worker runtime — `9679aea`
- <a id="step-69a"></a>**69A.** Manual performance benchmarks against OTP — `0777b693`
- <a id="step-73a"></a>**73A.** Decide the parse transform contract — `57d4193`
- <a id="step-73b"></a>**73B.** Add the abstract term model and its codecs — `a3f66ab`
- <a id="step-73c"></a>**73C.** Export modules as abstract forms — `9a9f9aa`
- <a id="step-73d"></a>**73D.** Import abstract forms into the AST — `0ebf90e`
- <a id="step-73e"></a>**73E.** Run transforms through the host OTP loader — `13a5e20`
- <a id="step-73f"></a>**73F.** Apply parse transforms in the compile pipeline — `1c92c5c`
- <a id="step-73g"></a>**73G.** Use precompiled and project transform modules — `d1ce36a`

### Notes from completed plan 11 steps

- Inference 58A–58N3 (user direction 2026-10-08/10): domain budgets in
  [inference domain](../docs/semantic.md#inference-domain), dependent facts in
  [dependent facts](../docs/semantic.md#dependent-facts) (printed as
  `$case_of_operator`, `$if_operator`, `$try_of_operator`); expectations in
  `tests/fixtures/inference/`. Specialization reads only proven facts and the
  erased union summaries. Not done: the optional 58L report of a call no
  inferred function type can enter; a `case` on a variable that depends on an
  argument does not split over that argument.
- 59: proofs at O2 with specialization only; source variants still rarely fit
  the 2x size limit. 62: `--lto` on Windows MSVC and ELF targets only. 62B:
  "within one tick" is bounded by the host's timed-wait resolution.
- 64: no 32-bit LLVM SDK, so `clau` stays x64 and 32-bit coverage is the
  runtime plus cross-linked goldens (`cross.py`).
- 67/68: injected host-refusal tests and allocator-replacing tests are out of
  scope.
- 65A–65B: a hand-written `behaviour_info/1` is not evaluated (no ill-defined
  or deprecated callback warnings); `erlang:get_module_info/1,2` is not
  provided; generated functions get no debug locations, source comments or
  `--print-types` output; `md5`, `vsn`, `compile` values differ from OTP
  ([differences](../docs/differences.md)).
- 65C: luerl `src/` passes attribute admission and then stops on missing
  library modules and builtins (`array`, `ordsets`, `math`, `io_lib`, ...).
- 41A–41H: family translation units live under
  `runtime/src/builtins/erlang/` and `production_builtins()` in
  `registry.cpp`, so a new family never touches `core.cpp`; later large
  modules use `runtime/src/builtins/<module>/`. 41B inventory: 351 exports
  (133 implemented, 119 planned, 99 unavailable at the time); the auto-import
  set is OTP's table intersected with the catalog. Differences recorded:
  `list_to_pid/1` admits only issued pids, `term_to_binary` `compressed`
  writes uncompressed bytes, code loading answers for linked code
  (`delete_module/1` `notsup`, `prepare_loading/2` `badfile`) until step 71.
  The ETF fuzz test found two decoder bugs (invalid atom UTF-8, NaN floats);
  timer messages live in a `MessageBuffer` in runtime term format; a reply
  alias is consumed on delivery (`Executor::send_alias`). Phase gate:
  full CTest 258/259, `check-quality-all` clean. The luerl rerun left only
  non-`erlang` modules, planned or declined in [F36](01-todo.md).
- 62C: liveness walks only live functions' bodies, so an export named only in
  unreachable code is dropped too; dead functions get no code and keep only
  their name in the export table. Sizes for a small `lists` user: `-Os`
  2,760,704 vs 2,802,688 bytes kept (the runtime archive dominates). Weak
  references rejected ([executables](../docs/executables.md#export-liveness)).
- 62D–62E: preprocess/parse results are not cached (importing the forms took
  1,063 ms against 819 ms to reparse luerl); a scan cache would save about
  15 ms per walk of all of `references/otp/lib`; per-module compile reuse is
  not sound yet (batch-wide inference, specialization budgets, export
  liveness), so the compile cache key is the whole batch. The directory index
  cut luerl's 15,215 filesystem probes to 0. SQLite against the file backend
  for 2,000 × 8 KB puts: 249–258 ms against 11.1–12.5 s
  ([build caches](../docs/projects.md#build-caches)).
- 73A–73G: OTP ships no `erl_id_trans` module, so owned identity transforms
  replace it; `generated` annotations are ignored and include names are
  written relative to the working directory (difference rows); a transform
  found nowhere is only known to OTP and stays `undefined parse transform`
  from the loader; `transforms_no_otp` uses `--erl none` because Windows
  re-sets `ProgramFiles` in every process.

<a id="plan-20"></a>

## Plan 20: optimizations from OTP T2-Full (2026-10-12–13)

Plan and open steps: [20-opt.md](20-opt.md). Full step text and evidence are in
Git at these commits. Fast CTest and `check-quality` were green at each.

| Step | Commit | Done |
| --- | --- | --- |
| Plan | `4438fde` | `.agents/20-opt.md` from garazdawi/otp PR #855 (T2-Full) plans and the PR text |
| OPT0.1 Inline frame transfers | `3c6cecc` | ABI 9. `abi::v1::FrameState` starts every context; enter, tail and return push, pop and spend reductions inline. Services only on the slow bits (collection wanted, profiling), no reductions, stack growth or builtin. The per-call failure check reads `failed`. |
| OPT0.1A Register-passed arguments | `11439308` | ABI 10. `Code` = `void(ctx, x0, x1, x2)`; leading arguments and results travel as C parameters and are spilled only across calls. The runtime calls code via `run_code`. LLVM loops self tail calls at O2. |
| OPT0.5A Off-heap large maps | `ed3ccc2` | Maps > 32 entries are `tree_map` stubs over a process-private persistent B+ tree (`terms/map_tree`). Updates copy their path (O(log n)); GC rewrites tree words through the stub and frees dead trees; copies rebuild the tree. `executables_large_maps`, `runtime_maps`. Maps measured much faster. |
| OPT0.5B Compiler-proven in-place map updates | `04043339` | `semantic/map_ownership` marks updates of a fresh, single-read, uncaptured map: `MapOperation::update_owned` consumes the source stub and mutates exclusively held nodes. `executables_owned_maps`, inspection check. |

<a id="outstanding-work-to-finish"></a>

## Outstanding work to finish

[The backlog](01-todo.md) expands each gap; [plan 11](11-plan.md) orders it.
Done: production executables, matching and guards, admitted terms, process
heap and collection, graph copying, the Erlang semantics above, identities
and atoms, processes, scheduling and ports, runtime services, tooling and
inference, sanitizers, parse transforms through host OTP, `erlang` module
builtins, export liveness, the build directory and build caches.

- [ ] **Native platform validation:** macOS Apple Silicon (step 66) and native
  ARM hardware; Linux x86-64, 32-bit x86 and ARM under emulation done.
- [ ] **OTP compatibility:** upstream Common Test suites and wider
  differential comparisons (step 69).
- [ ] **Test migration closure:** retire adapters only after equivalent public
  coverage (step 70).
- [ ] **Parse transform closure:** real projects and the user guide (step 73H).

Deferred, not required: dynamic modules (D01), atom collection (D02),
behavior-changing attributes beyond the selected slices (D03), public
interchange (D04), stage readers (D05), C/FFI (D06), project-schema extensions
(D07). Every change keeps formatting, behavioral tests and the fresh combined
Debug quality gate without weakened thresholds or suppressions.
