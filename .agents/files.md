# File lookup

Directory map; find files inside by name. **C** = `compiler/src/`, **R** = `runtime/src/`; **+** =
planned, create only with implementation. [Architecture](arch.md) · [Backlog](01-todo.md) ·
[Plan](11-plan.md) · [History](00-finished.md) · [Optimizations](20-opt.md) · [Library](21-lib.md).

## Placement

| Kind | Home |
| --- | --- |
| Compiler API / AST | `compiler/include/clause/compiler/` (`ast/`, `mangling.hpp`) |
| Runtime API | `runtime/include/clause/runtime/` (`atom_names.hpp`: `"name"_atom` cached literal atoms, `AtomName`) |
| Generated-code ABI | `abi/include/clause/abi/` (one header per service family, `external_term.hpp` ETF codec, `literals.hpp` module literal encoding, `keywords.hpp` the reserved words the lexer and atom printers share) |
| Private headers | Beside owning source; runtime stays LLVM-free |
| Sketches / legacy forwarders | `runtime/include/*.hpp` (`byte_buffer.hpp`: `ByteBuffer`, unzeroed bytes of off-heap buffers and driver replies), `runtime/include/unverified/` |
| New sources / tests | Register in owning `CMakeLists.txt` |

## Compiler — C

| Directory | Owns |
| --- | --- |
| `source/`, `diagnostics/` | Buffers, positions, diagnostic provenance |
| `lexer/`, `parsing/` | Tokens, literals; Boost parser boundary, token mechanics |
| `preprocessor/` | Directives, sessions, includes, macros, closed condition evaluator |
| `ast/`, `parser/` | Owned arenas; Erlang grammar, attributes, types/specs |
| `printing/` | Token/AST dumps, Erlang source printer |
| `driver/` | CLI, frontend batch, predefined forms, entry/escript, placeholders, `app_resources` (generated `clause_applications` from `.app`/`.app.src`), transforms sources, caches glue, stage times, inspections, publication; `C/main.cpp` |
| `project/` | Manifest TOML/schema, discovery, ignore rules, directory index, plan/execution |
| `semantic/` | Symbols, calls, bindings, patterns/match plans, guards/services, records, funs, imports, behaviours, liveness, `builtin_continuations.hpp` (builtins continuing in library Erlang), map ownership |
| `semantic/types/` | Declared types, lattice, inference, contracts, type printing, inference store; `profile.hpp` + `inference_profile.cpp` (`--print-stage-times` inference measurements); `lattice_trace` (lattice operation counts and repeats, profile only); `inference_incremental` (passes skip components, solves replay member walks, nothing they read changed for) |
| `codegen/` | LLVM state/target, lowering (`lowering_*`, inline appends in `lowering_append`, small-map lookups in `lowering_map_access`, raw floats in `lowering_float_arithmetic`, known funs inlined in `lowering_fun_inlining`, `lists` loops of them in `lowering_list_loops`, `maps:fold` loops and owned fold accumulators in `lowering_map_loops`, inline `integer_to_list`/`list_to_integer` and digit prefixes of `++` in `lowering_integer_text`, module literal encoding and reads in `lowering_literals`), binary loops, memory kinds for TBAA (`alias_tags`), float loops, frames, funs, literals, proofs/specialization, startup, verify/optimize/emit (`parallel`: per-module contexts on worker threads), asm listing, runtime symbols |
| `transforms/` | Parse transforms: term arena, ETF, abstract-form export/import, `erl` loader, pipeline |
| `cache/` | Build cache store (SQLite + blobs) |
| `artifacts/` | Staged writes, safe names, file replacement |
| `linking/` | Executable linking, toolchain discovery, runtime archive, library directory |
| **+** `stage_writers/`, `stage_readers/` | Reserved: `{preprocessed,abstract,ir}` |

## Runtime — R

| Directory | Owns |
| --- | --- |
| `.` | Runtime lifecycle, shared state |
| `terms/` | Words/terms (`term_unchecked`: unchecked accessors), atoms (`atom_table`: process-wide atom index), integers (`integer_scale`: bignum times small, limb by limb), floats, maps (incl. trees, cursors), bitstrings, funs, records, identities, dynamic calls, equality/order, text |
| `memory/` | Process heap, budgets, block reuse (`heap_blocks`), off-heap and buffer reuse (`off_heap`), walker/verifier, collector, graph copy (`graph_copy.hpp`; `copy.cpp` uses it for sends, buffers, ETS and persistent copies), message buffers and outgoing sends (`message_buffer.hpp`), persistent terms (`persistent_region`: their address range), `detached_term` (ETS object blocks) |
| `ets/` | ETS tables: `index` (key comparisons, edits), `hash_index`, `tree_index`, `table` (parts, locks, fixation, slot visits), `tables` (registry, owners, heirs); owner end in `scheduler/tables.cpp` |
| `match_spec/` | Match specifications: `program.hpp` (API), `compiler.hpp`, `compile` (clauses, lint), `patterns`, `expressions` (special forms, actions), `functions` (guard BIF table), `run` + `running.hpp` (interpreter), `actions` (trace actions), `handles` (compiled specs as handle refs) |
| `process/` | Context, stack/frames, exceptions, exits, mailbox storage, receive, identities, profiling, `process_table` (lock-free pid lookup) |
| `scheduler/` | Executor/workers, signals, ports table, timers/timer wheel, aliases, introspection; `cpus` (affinity, P-core sets, worker binding) |
| `modules/` | Code server, registration, atom bindings/caches, literal areas, literal heap decoding (`literal_heap`) |
| `builtins/` | Registry, bridge, typed adapters, portions, text/format, `scratch.hpp` (per-thread buffers kept between calls); per-module subdirs (`erlang/`, `lists/`, `binary/`, `unicode/`, `re/`, `zlib/`, `zstd/` (contexts and dictionaries for `clause_zstd`), `collections/`: ordsets, orddict, proplists, dict, sets over `linear_hash`, gb_trees, gb_sets over `balanced_tree`, queue, array; `encoding/`: base64, json; `rand/`: generators, generic algorithms, observed tables; `filename/`: name walks, joins and splits; `uri/`: RFC 3986 parse, compose, codec; `string/`: OTP's string walks; `unicode/code_points.cpp`, `normalize.cpp`: cp/gc and nf* over terms; `unicode/forms.cpp`: their builtins and `unicode`'s forms and properties; `io_lib/`: deep character list arena, `write1` writer, format scan/build/floats, collect functions, `pretty*`: io_lib_pretty (print_length/expand arena, cind/pp layout, pp_bin); `fread.cpp`: the `fread/2,3` scanner; `key_orders.cpp`: `maps_order` funs' sort-and-rerun (with `library/stdlib/clause_io_lib.erl`); `io_format.cpp`: chardata reading for `io`; `front_end/`: `erl_internal` tables (`erlang_bifs.inc` generated), `erl_anno`, `erl_features`, `scanner*.cpp` erl_scan's scanner (`scanner_numbers`, `scanner_texts`: chars, escapes, strings, sigils, triple quotes) and `erl_scan.cpp` its terms (`clause_erl_scan:scan/6`), `parser*.cpp` erl_parse's grammar (`parser_attributes`, `parser_expressions`, `parser_primaries`, `parser_control`, `parser_types`), `abstract_annos`/`abstract_normalise` (annotation walk, first/last_anno, annotation replacement, normalise) and `erl_parse.cpp` (`clause_erl_parse:parse/1`, `abstract/3`, tokens, precedences), `bit_types` (erl_bits' specifier settling, shared by normalise and erl_eval), `erl_eval.cpp` (`clause_erl_eval` bit segment builtins), `forms.hpp` (node reading and building for the C++ walks), `guard_tests` (erl_lint's guard checks), `expand_records.cpp` (`erl_expand_records:module/2`), `lint*.cpp` (erl_lint: `lint.hpp` state, `lint_state` options/errors/packing, `lint_vars` variable tables, `lint_forms` attributes and functions, `lint_checks` post-traversal checks, `lint_exprs` expressions and calls, `lint_scopes` clause exports and comprehensions, `lint_patterns` patterns/binaries/guards, `lint_records`, `lint_types`, `lint_format` format strings, `lint_eval` partial_eval/bit types/`otp_obsolete.inc`, `lint_errors` format_error texts, `lint_builtins` entry points), `pp*.cpp` (erl_pp: `pp.hpp` item arena and printer, `pp_items` builders and dispatch, `pp_exprs`, `pp_forms` attributes and clauses, `pp_types`, `pp_layout` second pass, `pp_builtins` `clause_erl_pp:print/2`), `epp_eval.cpp` (`clause_epp:eval_if/1`), `eval.hpp`/`guard_eval.cpp` (erl_eval bindings and guard expressions over the match specification guard table, for -if, map keys and segment sizes), `eval_match.cpp` (`clause_erl_eval:match/4`, `bits_gen/4`, `erl_eval:partial_eval/1`, `is_constant_expr/1`); `records.cpp`: native record reflection; `erl_posix_msg.cpp`: POSIX error texts; `net/`: `clause_net` name and service lookups (`names.cpp`), interface lists shaped for inet and net (`interfaces.cpp`) from `interfaces_windows.cpp` / `interfaces_posix.cpp`; `filelib.cpp`: wildcard escape, part compiler and matcher; `graph/`: graph queries and traversals over `graph::View`; `digraph/`: `DigraphView` over a digraph's tables, digraph path and digraph_utils builtins; `disk_log/halt_log.cpp`: halt log items for ets table files; `dets/`: the dets file engine (`format`, `file` positioned I/O, `free_lists` buddy system, `header`, `objects` terms and cmp, `table` slots and saving, `growth` segment splits, `work_list` cache flush, `scan` chunks and bchunks, `bulk` init/repair/compaction layouts, `open` repair decisions, `builtins` for `clause_dets`); `ets/`: ets builtins (tables, info, reads, writes, walks, updates, options; `select*`: select/match families, counts, deletes, replaces; `match_specs.cpp`: match_spec_compile/run_r, is_compiled_ms, erlang:match_spec_test; `table_walks.cpp`, `disposal.cpp`: whole-table reads and freeing in portions); `sofs/`: typed sets, relations and families) and module TUs |
| `ports/` | Port drivers (fd, spawn, file, sockets with `socket_options` (option records, setsockopt, kept inet_drv options, traffic counters), `registry_windows` for win32reg), reactor, I/O service; `posix_reason` names errno codes for the file driver and dets; `file_changes` (owners, permissions, times, truncate, advise, allocate) |
| `startup/` | `CLAUSE_main_v1`, argv, runtime options, halt |
| `diagnostics/` | Runtime feature reporting |

## Library, build, support

| Location | Lookup |
| --- | --- |
| `library/{erts,kernel,stdlib}/` | Erlang library modules; `<m>.terms` inventories, `*.types.hrl`; `modules.terms` registry, `documented.terms` (OTP-documented functions) (`docs/library.md`); `kernel/clause_socket.erl` socket port plumbing and `kernel/clause_socket_options.erl` option codes for `gen_tcp`/`gen_udp`/`inet`, `kernel/clause_net.erl` the `clause_net` placeholders behind `net`; `kernel/clause_logger.erl` is the `logger` server, `kernel/clause_logger_h.erl` the handlers' common part; `kernel/clause_disk_log.{erl,hrl}` disk_log's file formats, `kernel/clause_disk_log_server.erl` its registry; `stdlib/clause_erl_lint.erl` erl_lint's internal builtins; `stdlib/clause_erl_pp.erl` erl_pp's printer; `stdlib/clause_epp.erl` source reading for epp and file:consult (coding comments, decoding, form scans); `stdlib/clause_filelib.erl` filelib's wildcard builtins; `kernel/include/` holds `logger.hrl`, `file.hrl`, `eep48.hrl`; `stdlib/include/zip.hrl` zip's records |
| Root | `CMakeLists.txt`, `CMakePresets.json`, `Makefile`, `make-*.bat`, `run-macos.sh`, `clau.bat`, `decompile-*.bat`, `.clang-{format,tidy}` |
| `cmake/` | Options, dependencies, toolchains, quality checks (Lizard/tidy scope) |
| `tools/` | Windows toolchain script, `asm.py`, `unicode/` and `rand/` table generators, quality requirements, `luerl-trace/` |
| `docs/` | Reference notes, index `docs/README.md` |
| `runtime/design/` | Runtime design contracts |
| `references/` | `otp-pin.cmake`; ignored `otp/` checkout (`docs/otp-reference.md`) |
| `benchmarks/` | Manual OTP comparison, `run.py` (job-object/`--cpus` pinning), `compile_times.py` (clau phase/pass timings, A/B), ignored `history/` |
| `examples/` | `compile/`, `project/` |
| `.agents/` | Plans, map, history, AI notes |
| Local/generated | `build/`, `thirdparty/` (incl. `tools/erlfmt/`), `.venv-quality/` |

## Tests

- Runners `tests/compiler/<area>/`, data `tests/fixtures/<area>/`. Areas: `preprocessor`, `parser`,
  `project`, `semantic`, `patternmatch`, `codegen`, `executables`, `programs`, `printing`,
  `inference`, `lint`, `builtins`, `library`, `linking`, `cache`, `transforms`.
- `tests/runtime/`, `tests/abi/`: LLVM-free runtime/ABI tests; `tests/prototypes/`: decision
  prototypes (not CTest).
- Shared: `tests/cli.cmake`, `tests/compiler/{frontend_cases,printing_roundtrip}.cmake`; `matrix.py`
  fast/full selection (`CLAUSE_TEST_MODE`); `CLAUSE_OTP_AUDITS` gates live OTP audits. Corpus
  provenance: `docs/validation.md`.
- **+** `tests/interop/`: external consumers (D06).

## Backlog → owners

Validation → `C/semantic`; facts → `C/semantic/types`; LLVM → `C/codegen`; options → `C/driver` +
`C/project`; publication → `C/artifacts`; linking → `C/linking`; values → `R/terms` + `R/memory`;
processes → `R/process` + `R/scheduler`; builtins → `R/builtins`; code → `R/modules`. Tests use the
owning area; generated-program behavior → `codegen`/`executables`, runtime → `tests/runtime/`. IDs
in `01-todo.md`; D-items optional (D04/D05 stage dirs, D06 **+** `R/interop`).
