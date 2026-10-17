# File lookup

Directory map; find files inside by name. **C** = `compiler/src/`, **R** =
`runtime/src/`; **+** = planned, create only with implementation.
[Architecture](arch.md) · [Backlog](01-todo.md) · [Plan](11-plan.md) ·
[History](00-finished.md) · [Optimizations](20-opt.md) · [Library](21-lib.md).

## Placement

| Kind | Home |
| --- | --- |
| Compiler API / AST | `compiler/include/clause/compiler/` (`ast/`, `mangling.hpp`) |
| Runtime API | `runtime/include/clause/runtime/` |
| Generated-code ABI | `abi/include/clause/abi/` (one header per service family, `external_term.hpp` ETF codec) |
| Private headers | Beside owning source; runtime stays LLVM-free |
| Sketches / legacy forwarders | `runtime/include/*.hpp`, `runtime/include/unverified/` |
| New sources / tests | Register in owning `CMakeLists.txt` |

## Compiler — C

| Directory | Owns |
| --- | --- |
| `source/`, `diagnostics/` | Buffers, positions, diagnostic provenance |
| `lexer/`, `parsing/` | Tokens, literals; Boost parser boundary, token mechanics |
| `preprocessor/` | Directives, sessions, includes, macros, closed condition evaluator |
| `ast/`, `parser/` | Owned arenas; Erlang grammar, attributes, types/specs |
| `printing/` | Token/AST dumps, Erlang source printer |
| `driver/` | CLI, frontend batch, predefined forms, entry/escript, placeholders, transforms sources, caches glue, stage times, inspections, publication; `C/main.cpp` |
| `project/` | Manifest TOML/schema, discovery, ignore rules, directory index, plan/execution |
| `semantic/` | Symbols, calls, bindings, patterns/match plans, guards/services, records, funs, imports, behaviours, liveness, map ownership |
| `semantic/types/` | Declared types, lattice, inference, contracts, type printing, inference store |
| `codegen/` | LLVM state/target, lowering (`lowering_*`, inline appends in `lowering_append`, small-map lookups in `lowering_map_access`), binary loops, frames, funs, literals, proofs/specialization, startup, verify/optimize/emit, asm listing, runtime symbols |
| `transforms/` | Parse transforms: term arena, ETF, abstract-form export/import, `erl` loader, pipeline |
| `cache/` | Build cache store (SQLite + blobs) |
| `artifacts/` | Staged writes, safe names, file replacement |
| `linking/` | Executable linking, toolchain discovery, runtime archive, library directory |
| **+** `stage_writers/`, `stage_readers/` | Reserved: `{preprocessed,abstract,ir}` |

## Runtime — R

| Directory | Owns |
| --- | --- |
| `.` | Runtime lifecycle, shared state |
| `terms/` | Words/terms, atoms, integers, floats, maps (incl. trees, cursors), bitstrings, funs, records, identities, dynamic calls, equality/order, text |
| `memory/` | Process heap, budgets, block reuse (`heap_blocks`), off-heap, walker/verifier, collector, graph copy, message buffers, persistent terms |
| `process/` | Context, stack/frames, exceptions, exits, mailbox storage, receive, identities, profiling |
| `scheduler/` | Executor/workers, signals, ports table, timers/timer wheel, aliases, introspection |
| `modules/` | Code server, registration, atom bindings/caches, literal areas |
| `builtins/` | Registry, bridge, typed adapters, portions, text/format; per-module subdirs (`erlang/`, `lists/`, `binary/`, `unicode/`, `re/`, `zlib/`, `collections/`: ordsets, orddict, proplists) and module TUs |
| `ports/` | Port drivers (fd, spawn, file, sockets), reactor, I/O service |
| `startup/` | `CLAUSE_main_v1`, argv, runtime options, halt |
| `diagnostics/` | Runtime feature reporting |

## Library, build, support

| Location | Lookup |
| --- | --- |
| `library/{erts,kernel,stdlib}/` | Erlang library modules; `<m>.terms` inventories, `*.types.hrl`; `modules.terms` registry, `documented.terms` (OTP-documented functions) (`docs/library.md`) |
| Root | `CMakeLists.txt`, `CMakePresets.json`, `Makefile`, `make-*.bat`, `run-macos.sh`, `clau.bat`, `decompile-*.bat`, `.clang-{format,tidy}` |
| `cmake/` | Options, dependencies, toolchains, quality checks (Lizard/tidy scope) |
| `tools/` | Windows toolchain script, `asm.py`, `unicode/` table generator, quality requirements, `luerl-trace/` |
| `docs/` | Reference notes, index `docs/README.md` |
| `runtime/design/` | Runtime design contracts |
| `references/` | `otp-pin.cmake`; ignored `otp/` checkout (`docs/otp-reference.md`) |
| `benchmarks/` | Manual OTP comparison, `run.py`, ignored `history/` |
| `examples/` | `compile/`, `project/` |
| `.agents/` | Plans, map, history, AI notes |
| Local/generated | `build/`, `thirdparty/` (incl. `tools/erlfmt/`), `.venv-quality/` |

## Tests

- Runners `tests/compiler/<area>/`, data `tests/fixtures/<area>/`. Areas:
  `preprocessor`, `parser`, `project`, `semantic`, `patternmatch`, `codegen`,
  `executables`, `programs`, `printing`, `inference`, `lint`, `builtins`,
  `library`, `linking`, `cache`, `transforms`.
- `tests/runtime/`, `tests/abi/`: LLVM-free runtime/ABI tests;
  `tests/prototypes/`: decision prototypes (not CTest).
- Shared: `tests/cli.cmake`, `tests/compiler/{frontend_cases,printing_roundtrip}.cmake`;
  `matrix.py` fast/full selection (`CLAUSE_TEST_MODE`); `CLAUSE_OTP_AUDITS`
  gates live OTP audits. Corpus provenance: `docs/validation.md`.
- **+** `tests/interop/`: external consumers (D06).

## Backlog → owners

Validation → `C/semantic`; facts → `C/semantic/types`; LLVM → `C/codegen`;
options → `C/driver` + `C/project`; publication → `C/artifacts`; linking →
`C/linking`; values → `R/terms` + `R/memory`; processes → `R/process` +
`R/scheduler`; builtins → `R/builtins`; code → `R/modules`. Tests use the
owning area; generated-program behavior → `codegen`/`executables`, runtime →
`tests/runtime/`. IDs in `01-todo.md`; D-items optional (D04/D05 stage dirs,
D06 **+** `R/interop`).
