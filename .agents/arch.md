# Architecture

Compact map of how Clause works. History lives in `00-finished.md`,
`docs/validation.md#history`; open work in `11-plan.md`, `20-opt.md`,
`21-lib.md`.

## Overall

- C++23, project-internal APIs, CMake, warnings as errors. `clau` owns the
  compiler pipeline; `clause_runtime` is separately linkable and LLVM-free.
  Stages exchange owned internal data; public interchange/readers deferred
  (directory locations only).

## Frontend

- SourceManager owns UTF-8/Latin-1 buffers. Incremental lexer/directive/
  preprocessor keep decoded values, physical/logical spans, macro/include
  ancestry, feature snapshots; per-session macros and include/conditional
  stacks preserve rescan/stringification. PP conditions use their own closed
  arbitrary-integer evaluator (not guard legality).
- ParserSession consumes expanded forms transactionally into flat AST arenas
  (owner/generation-checked IDs, closed variants, token provenance). Token/node/
  depth/work/diagnostic limits roll back partial forms. Covers ordinary OTP 29
  grammar. Syntax acceptance is separate from executable admission; no textual
  AST reader.
- Source printing (`docs/compile.md#source-printing`): `print_source`
  (`--print-source`) round-trips to the same tree; `SourceNotes` hooks add
  annotations/comments. `--print-types` adds `%% inferred:` signatures from
  `semantic::types::type_source`.

## Drivers and projects

- Positional/project drivers keep complete target AST batches, run common
  semantic/backend stages, aggregate located diagnostics; target options and
  results isolated. Syntax-only actions stop early; default compiles in memory;
  publication waits for every target. Debug/progress reporting is opt-in.
- Library loading (`driver/frontend` `add_referenced`): modules named by literal
  atoms (`semantic::referenced_modules`, earliest naming site; also
  `-behaviour`, imports; skips catalog builtin calls) that no input declares are
  parsed from `linking::library_directory()` (relative to `clau`,
  `CLAUSE_DEFAULT_LIBRARY`; library app dirs first, then project
  `source_search_paths` via `project::pattern_directories`) until closed.
  `--print-inputs` (`FrontendRequest::list_inputs`) stops here and lists them.
- Library tree `library/{erts,kernel,stdlib}`, `library/modules.terms`
  (provided / missing = reserved; `driver/library_modules`); inputs replacing a
  Clause module warn; stubs raise `{not_implemented, F, A}`; inventories
  `<m>.terms` checked by `tests/compiler/library/library_modules.py`
  (`docs/library.md#runtime-module-replacements`). `library/erts/erlang.erl` is
  generated from `erlang.terms`; non-builtin `erlang:F/A` and OTP auto-imports
  (`semantic::library_auto_import`) pull it in.
- Ignored sources (`project::Ignore`: ignore_dirs by literal root per ancestor,
  ignore_modules hash + globs) leave discovery/search; each ignored referenced
  module gets a placeholder (`driver/placeholders`, keys from
  `semantic::referenced_functions`) returning `{module_ignored, M}`.
- Entry (`driver/entry`, `docs/executables.md`): `--entry`/manifest `entry`
  (`F/1`, else exported `F/0` with `startup_no_arguments`) or sole `main/1`
  exporter. `#!` escripts: `driver/escript` rewrites header,
  `semantic/escript` exports `main/1`, preferred for entry.
- Predefined forms (`driver/predefined`): `behaviour_info/1` (from
  `semantic::behaviour_info_source`, parsed before `ParserSession::finish`,
  hidden from own exports/calls/funs/specs via `semantic::source_function`) and
  `module_info/0,1` (md5 = `codegen::md5` of `print_source`); counted in
  `Module::predefined_`, no debug scope, omitted by `--print-types`.
  `semantic::check_behaviours` reports erl_lint behaviour warnings.
- Directory index (`project/directory_index`, one per invocation): lazy
  listings; `may_exist` false skips probes (module search, transforms, sources,
  `PreprocessorOptions::may_exist`).
- Build dir: `build_dir`/`--build-dir`, default `<manifest-dir>/build`; temp
  files in `<build_dir>/tmp`; link staging beside the output.
- Caches (SQLite `<build_dir>/cache/v2/cache.db`: `entries` + ordered `parts`,
  blobs `v2/blobs/<xxh3-128>` > 256 KB, WAL, one transaction per put, damaged db
  moved aside, thread-safe; `cache::Value{format_, parts_}`; `--no-cache`):
  - transform loader replies (`transforms::HostCache`, key = request + clau/OTP
    identity + transform sources/includes + code-path beams);
  - compile cache (`driver/compile_cache`): whole-batch key = clau identity +
    options + per-input `fingerprint_`; parts "failed", "diagnostic"*, outputs;
    `Compiled` feeds link/deliver for fresh and reused batches;
  - inference cache (`driver/inference_cache`): dirs from
    `project::InferenceCacheDirs` (implicit stdlib/kernel/compiler/syntax_tools,
    `cache_inference_in_dirs`, `--cache-inference-in-dirs`); key = clau identity
    + own and transitive atom-reached inputs (`semantic::named_atoms`); stores
    `semantic/types/inference_store` "results" into `Inference::reused*`.
  Preprocess/parse are not cached.

## Semantic analysis

- LLVM-free indexing validates module/function/export identities. Binding IDs
  distinguish definitions, reads, exact checks; tentative candidates publish on
  success only. `_` binds nothing; body `=` analyzes RHS first; pattern siblings
  share incoming reads; only a binary's preceding segments extend size scope.
  Failure clears partial tables.
- Branches: `semantic::branch_clauses` gives one clause view for case/if/try
  (of then catch, `first_handler`)/maybe else/receive (after = last clause).
  Names bound in several clauses share one identity (`branch_names`), exports in
  `Function::exports`; partial definitions unsafe; try names always unsafe.
- Calls: `resolve_calls` orders SCCs callees-first (iterative Tarjan).
  `semantic::body_builtin` (auto-import unless shadowed/suppressed),
  `module_builtin` (qualified catalog builtin = service), `dynamic_call`,
  `fun_call`; imports (`semantic/imports`, `Module::imports_`) resolve as remote
  calls. Port BIFs (`semantic::port_builtin`) report capability "ports".
  Rejected attributes: `on_load`, `nifs`, non-inert `-compile`
  (`semantic::rejected_attribute`).
- Guards (`docs/guards.md`): pinned audited legal catalog, separate from
  availability; explicit erlang calls, shadowing, imports, no_auto_import and
  legacy tests resolve before operands. Comma tests need canonical true;
  semicolon continues at next alternative; guards bind nothing.
- Funs (`docs/funs.md`): `semantic::index_funs` gives each `fun F/A`, literal
  `fun M:F/A` and lambda (`-f/A-fun-N-`, arity + captures) a `Module::funs`
  entry. Closures get the scope at the fun (`FunScope`), `Function::captures`
  in definition order; named funs via `FunScope::inside`/`Function::fun_names`.
  Variable `fun M:F/A` reads in `Function::fun_operands`.
- Records: layouts keep declaration order/defaults/provenance; per-use expansion
  reuses tuple code; `record_info/2` folds. Native records:
  `-export_record`/`-import_record`, `semantic::external_record`, `Module::peers`
  for external construction/defaults; `#_` anonymous access/update/patterns.
- Liveness (`semantic/liveness`, executables and entry inspections): worklist
  from entry over calls, funs, literal apply/spawn targets, escaped module atoms
  (dynamic call + atom producer = all live); `live_calls` prunes before
  inference; dead exports keep name with null entry/frame. `--keep-exports`.
- Map ownership (`semantic/map_ownership`): updates of fresh single-read
  uncaptured maps lower as `MapOperation::update_owned`.

## Type inference (`docs/semantic.md#inference-domain`)

- Symbolic declared types stay separate from implementation facts; specs never
  authorize representation checks.
- `semantic/types/lattice`: family joins (Gather -> Families -> Assemble),
  term-ordered unions, `FactLimits` budgets, integer-threshold widening;
  positional lists, `#{K => V}` associations past key budget, unions drop tuples
  subsumed by same-size tuples.
- `inference.cpp`: postorder `walk` over `Frame`s (`inference_scopes`);
  `evaluate` = `plain_fact` + `lift` via `inference_operators`/`_values`/
  `_funs`. `BindingFacts` publishes pattern parts, scope stack (`saved`),
  `impossible`, `conditionals`, per-clause `keys`. `merged()` joins clauses,
  widens after `JOIN_ROUNDS` (8), caps 8 + 4 per member; gives up on a group at
  the cap or 1/8 of pass work (remembered); a member flipping back to one of its
  last 4 summaries gives up alone.
- `infer` repeats passes until local inputs (non-exported, not fun-named) and
  `fun_reads` settle, else last pass with `term()` inputs/`opaque_funs`.
  `Inference::solving` stops narrowing by unsolved components.
- `Summary::types` = one `FunctionType` per clause (`dependent::clause_types`,
  `function_types::merge_types`/`select`/`entered`); funs carry them;
  `Lattice::joined_fun` for others. `function_call` re-runs non-recursive
  callees (`reanalyse`, budgets `Inference::reanalyses`). Dependent facts
  (`docs/semantic.md#dependent-facts`): `Fact::dependent` indexes interned
  `Dependent`s; `Fact::type` is the erased join everyone else reads.
- `contracts.cpp`: `Declared` converts specs; `Checker` reports disjoint results,
  entry domains and call arguments.

## Lowering (codegen)

- Patterns/guards (`docs/{patterns,guards}.md`): flat match plans with explicit
  success/mismatch continuations, tentative SSA; fresh bindings per candidate;
  exhaustion raises function_clause. Body match saves RHS once; badmatch keeps
  it. Iterative eager/lazy lowering keeps source order without host recursion;
  strict boolean ops validate; andalso/orelse join through target-word SSA.
- Iterative walker for control flow: `case` (`case_select`/`case_clause_end`,
  PHIs for value + exports, `{case_clause, V}`), `if` (`if_clause`), raises
  (`lower_raise` -> `CLAUSE_raise_v2`), `catch` (`ExpressionLowering::handler`,
  `CLAUSE_catch_v1`), `try` (`ProtectedScope`, `try_clause`,
  `CLAUSE_exception_v2`, `CLAUSE_reraise_v2`; after body lowered on normal and
  handler paths, `AfterPath`), `maybe` (`MaybeScope`, else via `CaseJoin`,
  `else_clause`), receive (loop peek -> match/take or skip -> wait via
  `lower_wait`/`CLAUSE_wait_frame_v1`; after clause on timeout).
- Comprehensions (`codegen/lowering_comprehensions`): generator loops with
  cursor/reversed accumulator in term slots, one reverse at the end; binary
  generators use `GeneratorPattern::element`/`skip`; map generators read
  entries inline; errors `bad_generator`/`bad_filter`/`bad_generators`.
- Funs: `<prefix>.funs` (`abi::v1::FunDescriptor`) bound to runtime
  `FunDefinition`; `fun_closure` cells = header, untraced definition, captures.
  `F(Args)`/dynamic calls/`apply` call a preparation service (`CLAUSE_apply_v1`,
  `CLAUSE_call_v1`, `CLAUSE_apply_list_v1`, `CLAUSE_call_list_v1`) then the
  `clause.apply` marker, which `lower_frames` turns into enter/tail.
  `lower_lambda` emits a private native function; `name_self` builds named funs
  once. Builtin funs of auto-imported catalog builtins become `erlang:F/A`.
- Records: native records via `CLAUSE_record_v1` + `<prefix>.records`;
  patterns plan `record_test`/`record_field`.
- Inline fast paths (`lowering_inline`): type tests, small comparisons,
  element/tuple_size/hd/tl, boolean ops, small arithmetic/bitwise, float
  `+ - * /`, common `math`, `make_tuple/2`, put/get/erase, binary match
  start/finish/aligned reads, constant binaries, map entries, container
  inspections; immediate service as slow tail. Small tuples/lists/floats/
  closures bump-allocate in `FrameState::heap_top/heap_limit`.
- Constants: fixed atoms false/true/undefined/ok/error = 0-4; other atoms read
  `ModuleDescriptor::atom_cache` when claimed by `FrameState::runtime`, else
  `CLAUSE_atom_v3`. Compound literals and constant binaries live in a per-module
  read-only literal area (`codegen/literals`, runtime `detail::LiteralAreas`).
- Proofs (O2 specialization, `codegen/proofs`, `lowering_proofs`): proven
  operands skip tag tests, tuples/cons read inline, list generators walk cons
  inline. Variants: 3/function, 32/module, 128/target, <= 2x generic IR.

## Execution model (`docs/execution-model.md#implementation`)

- Lowering emits native form (`Word f(ctx, args)`, `clause.frame` marker,
  `erlang-arity`, tail calls as `ret call`); `codegen/frames` `lower_frames`
  (backend, before inspection) moves bodies to `<sym>.body`, builds prologue
  (frame header/registers from `abi::v1::FrameState` + resume switch), splits
  after non-tail calls, spills crossing values (terms to term slots, raw to raw
  slots), emits only `musttail` transfers. Self tail calls keep their frame;
  tail calls between members of one tail-call cycle (`TailCycles`, Tarjan
  over each module's tail-call graph) keep the caller link and the shared term
  slots and zero only the extra ones (OPT1.1B). Arguments are stored into
  their slots only when they cross a call or safepoint (OPT1.2).
- `Code` = `void(ctx, x0..x5)` (`register_arguments` = 6): leading
  arguments/results pass as parameters. Enter/tail/return run inline, falling
  back to
  `CLAUSE_enter/tail/return_v1` on slow bits (`slow_collect`, `slow_profile`),
  reductions, limit, null body. `propagate_failure` reads `FrameState::failed`.
- Descriptors `<sym>.frame` (names, arity, body, slots, roots); exported `<sym>`
  = host wrapper over `CLAUSE_invoke_v1`; runtime enters via
  `ProcessStack::run_code`.
- `ProcessStack` (`process/stack`, one per context): flat word block, 4-word
  headers linked by offsets, 256 X registers, uncapped (opt-in
  `StackOptions::limit_words`). Exceptions return through callers (channel
  check). `enter` spends a reduction (4,000/slice); at zero records `resume_`
  and returns `pause` so the chain unwinds to `ProcessStack::run`.
- Stack traces: `FrameDescriptor` names; `GeneratedCallState::fail` copies 8
  innermost frames into `CallFailure::trace`; terms built lazily
  (`stack_term`); `Exception` = {class, reason, stack}.
- ABI: first-error channel separates Erlang errors from infrastructure statuses;
  every fallible call checks before using output. Halts (`CallError::halted`)
  unwind like errors. Runtime services from generated code are declared in
  `codegen/runtime_symbols.hpp`.

## Runtime terms

- Terms: arbitrary integers (immediates, else heap sign/magnitude up to ERTS
  `BIG_ARITY_MAX`, beyond = `system_limit`), atoms (validated UTF-8, deduped,
  limits, shared-mutex `AtomStorage`, lock-free number index), tuples, lists,
  binary64 float cells, maps, bitstrings, funs, pids, refs, ports.
- Order: numbers < atoms < refs < funs < ports < pids < tuples < maps < lists <
  bitstrings; iterative, no work cap (`terms/word_order`). Exact equality
  compares words first.
- Pids/ports are immediates (tag 0x3 / 0x7) from never-reused sequences
  (`IdentityNumbers`); admission rejects forged words; exited pids stay valid.
  References are heap cells with a 64-bit number; handle references (atomics,
  re patterns, zlib streams) use the `refc_binary` layout with shared buffers,
  weak registry, optional finalizer (`HandleAccess::make`).
- Maps: canonical exact keys, staged updates; > 32 entries are persistent B+
  trees of `map_node` heap cells (`terms/map_tree`, fanout 16, path copying,
  edit tokens for in-place writes until the next collection).
- Bitstrings: MSB-first with zero padding; small inline cells, large refc
  buffers; `sub_binary` 3-word cells over a never-nested original;
  appendable refc buffers grow in place (`BitAccess::append`). Binary loops
  (`codegen/binary_loops`, OPT1.4C): a function passing a binary parameter's
  rest back to itself in tail calls gets a `<sym>.loop` variant that updates
  one owned `sub_binary` cell in place per step (`BinaryLoop` in
  `ExpressionLowering`, rest in `lowering_bits`, calls in `lowering_calls`).
- Native records: `native_record` cells (header, untraced
  `RecordDefinition *`, values); registration binds
  `abi::v1::RecordDescriptor`s.
- Printing: `format_term` renders `~w`/display iteratively under a byte cap;
  goldens in `tests/fixtures/printing/`.

## Heap and GC (`docs/runtime-heap.md`)

- `HeapStorage`: one young heap block (lazy, min 233 words) + `fragments_` +
  `old_` area with `high_water_` (generational) + off-heap
  lists (refc binaries `memory/off_heap`, tree-map stubs); `HeapMark` rollback;
  `absorb`/`expose` sync with inline bump allocation; `token_` lifetime checks.
- Admission = owned range + header shape (`memory/heap_walk`,
  `memory/heap_verify`); literal areas are readable and never copied.
- Roots: frame term slots, live X registers, failure payload/args/stack,
  explicit spans (`ProcessContext::visit_roots`), `TrapState::words()`,
  mailbox words; raw slots never roots.
- `memory/heap_collect` `Copier`: Cheney copy at safepoints (function entry via
  `ProcessStack::enter`, comprehension heads `CLAUSE_safepoint_v1`) when
  `wants_collection()`; services are critical sections. Safepoints run minor
  collections (promote below the high-water mark, old area untouched; Debug
  checks no old→young pointer) unless a full sweep is due (`garbage_collect`,
  binary pressure, budget, old area full); `ProcessHeap::collect` is always a
  full sweep. `min_heap_size` (`spawn_opt`, `process_flag`, `--min-heap-size`)
  floors the first block and every collection. Shrink below 25% live; budgets leave half after survivors.
  Non-running processes are never collected; resumed ones collect at their
  resume safepoint.
- Limits: no default cap; opt-in `--max-heap/--max-stack/--max-memory`,
  runtime-wide `detail::RuntimeMemory` (atomic); host refusal = `out_of_memory`,
  exit 70.
- `memory/copy` `GraphCopy`: iterative flat copy (as OTP copy_struct) between
  same-runtime heaps; refc buffers shared, charged once per process
  (`HeapStorage::buffers_`). `memory/MessageBuffer` holds terms outside
  processes (messages, persistent terms `memory/persistent_terms`); a message of
  512+ words is built before the executor lock and its block adopted as a
  receiver fragment (`HeapStorage::adopt`, `MessageBuffer::take`).

## Builtins (`docs/builtins.md`)

- ABI catalog `abi::v1::bridge_builtins` shared by compiler and
  runtime. `CodeServer::builtins()` (`BuiltinRegistry`, `production_builtins()`)
  maps names to `BuiltinFrame`s (null-body `FrameDescriptor` + `BuiltinBody`).
  `CodeServer::function_frame` = exports, then builtins; registration replaces
  library stubs with builtins. Code server: shared mutex, hash indexes, modules
  never unloaded.
- Body bridge builtins lower to `CLAUSE_builtin_frame_v1` + apply marker;
  direct ones (`BuiltinName::direct`) to a plain `CLAUSE_builtin_call_v1`;
  `++` copies a short proper left list inline (`codegen/lowering_append`);
  `typed<Function>` (`builtins/typed.hpp`)
  adapts typed C++ to `BuiltinBody` (mismatch -> badarg).
- Portions (`docs/builtins.md#portions`): builtins spend
  `ProcessStack::budget()`, then `trap(continuation, state)`; `TrapState` roots;
  `builtins/portions` (`walk`, `build_list`, `ListState`). Builtins may continue
  in an Erlang export (`BUILTIN_CONTINUATIONS`, e.g. io -> `file:write/2`).
- Layout: `runtime/src/builtins/erlang/` (one TU per family; process-wide
  state in executor `scheduler/timers.cpp`, `aliases.cpp`, `introspection.cpp`;
  per-process in `ProcessContext`: `ProcessDictionary`, group leader), module
  builtins `builtins/<module>` (lists, io, math, binary, os, maps,
  persistent_term, unicode, code); fun-taking functions stay Erlang.
- Text: `builtins/io_format`, `builtins/io_pretty` (OTP layout, nesting cap
  256), `builtins/text`, `builtins/float_text` (OTP rounding, Ryu notation).
  `docs/io.md`.
- unicode_util: generated `builtins/unicode/tables.cpp`
  (`tools/unicode/generate.py`) behind private `clause_unicode` builtins.
- re: PCRE2 10.47 merged into clause_runtime; C++ `clause_re:compile/2`/`run/4`,
  rest in re.erl. zlib 1.3.2 likewise (streams, compressed term codec).
  Installed libraries become extra inputs in `clause_runtime.link`.
- `prim_eval:'receive'/2` = Erlang scan over private `clause_receive` builtins.
- Startup decodes argv/executable/home once into `ProgramArguments`; `init`
  answers as a started node.

## Processes and scheduling (`docs/processes.md`)

- `detail::Executor` (`scheduler/executor`, in `Runtime::Impl`,
  `Executor::of(context)`). `run` starts `RuntimeOptions::schedulers - 1`
  extra threads running `work(index)`: one `RunQueue` per worker (own condvar,
  idle flag) under `mutex_`; `Schedule::home` affinity, `place()` wake-affine/
  idle handoff, `steal()`, `balance()` every 4 slices; lazy wakeups.
- Per-process `Schedule` (running, blocked_on, blockers, holds, ready, ending,
  port_wait, throttling) in `schedules_`; thread_local `running_`. Touching a
  process running elsewhere throws `builtins::Blocked`; adapter retries after
  the target's slice (`hold`, `place`, `resume`). Sends to running processes
  buffer until its slice ends.
- `spawn/1,3` copy fun/args into a new context and prepare the call there
  (badarity/undef crash the child). Main process ends the run.
- Messages: `!`/`send/2` -> `Executor::send` (copy, `Mailbox::deliver`);
  `Mailbox` (`runtime/include/mailbox.hpp`): inbox + queue + saved position
  (`peek`, `skip`, `take`, `restart`, `remove`), `deadline_`.
- Timers: hierarchical timer wheel (`scheduler/timer_wheel`) on one timer
  thread per run; workers never read the clock.
- Signals (`runtime/include/signals.hpp`, `scheduler/signals.cpp`): links,
  `trap_exit`, monitors/watchers (`std::map<ReferenceIdentity, Word>`), name.
  `signal()` -> `end()`/`deliver_exit()`; `finish()` -> `notify()` (name,
  monitors 'DOWN', links); `drain()` of `ending_`. Names: executor `names_`;
  name monitors report {Name, nonode@nohost}.
- Exits (`process/exits`): `exit_reason`, `report_exit` (OTP legacy
  `=ERROR REPORT====` on stderr).

## Ports and I/O (`docs/ports.md`)

- Executor `ports_` table of `Port` (driver, links, watchers, name, counters);
  `scheduler/ports.cpp` (open/close/command/connect/info, exit rules,
  `PortEvent`), `builtins/erlang/ports.cpp` (BIFs). Drivers: fd, spawn, file
  (port_control, run outside the mutex via `control_port`), tcp/udp
  (`ports/sockets.*`, values `ports/value.*`, `PortDriver::attach`).
- One `Reactor` (`ports/reactor.*`, Asio io_context thread) per runtime serves
  pipes, fd input, child exits, sockets; `IoService` (`io.hpp`, private
  `io_streams.hpp`) posts to it. Windows: overlapped named pipes, wait-pool
  exits, blocking thread for fd input; POSIX: `async_wait`, `SIGCHLD` `Reaper`.
  `clear()` stops the reactor first, outside the lock.
- Ports are run-queue entities: raw `PortWork` -> `Port::pending`,
  `port_queue_`; `work()` alternates ports and process slices; `run_port`
  frames (`InputDecoder`) under a reduction budget.
- Busy ports suspend senders (`Schedule::port_wait`, `resume_senders()`); input
  pauses at 64 KiB held; `owner_full()` throttles at 1,024 queued messages.
- Library side: `file.erl`, `io.erl` (stdin server), `os:cmd/1`,
  `clause_socket.erl` + `gen_tcp`/`gen_udp`/`inet`.

## Backend, linking, startup

- LLVM lowering: target-derived layouts, collision-free symbols, checked
  services. O0/O2 PassBuilder pipelines verify both sides; -Os = O2 + optsize +
  sections + dead-stripping. Text/bitcode/object/asm serialization;
  `--print-asm` maps `.loc`/`.cv_loc` back to `ir`/`erlang` comments
  (`codegen/assembly_{listing,annotations}`). `-g` emits CodeView/DWARF (PDB
  beside MSVC executables); `clause::runtime::debug_erlang_stack()`.
  `--profile FILE` (`process/profile`) attributes time at frame transfers.
  `--lto` links bitcode with `-flto -fuse-ld=lld` (MSVC/ELF).
- Startup (`codegen/startup`): appended module `clausev1_start` whose `main`
  hands a `StartupDescriptor` to `CLAUSE_main_v1`; runtime ABI-checks, parses
  options (`CLAUSE_FLAGS`, `--max-atoms`/`--args-file`/`--`,
  `startup/options`), registers modules, runs the entry; exits 0/N/1|127/70.
- Linking (`linking/`): objects staged in `.clause-link-*` beside the output,
  `clang --driver-mode=g++ --target=<triple>` (`--linker`, else PATH/LLVM dir)
  with the runtime archive (`--runtime-library`, else build path relative to
  `clau`, members checked via LLVM Object); output replaced. Projects stage
  every executable target (`project/plan`, `linking::stage_executable`) and
  `publish_executable` only after all succeed. Artifact publication stages
  exclusive writes; multi-file publication is not atomic.

## Tests

- Prefer real CLI/project sources and native runtime consumers. OTP-observed
  corpora and goldens are owned; `fixture_sources` blocks OTP-licensed output;
  build/test need no OTP. Counts and platform evidence in
  `docs/validation.md`.
- Executables: `tests/compiler/executables/run.py`, case dir = sources +
  OTP-generated `golden.json` (stdout, exit, stderr regex, source hashes),
  CMake target `executables_<case>`, fast/full policy/driver matrix.
- End-goal programs `tests/fixtures/programs/` (avltree, frames, kvstore, ring,
  supervise, textstats): OTP stdout/exit goldens plus `compile.txt`.
- Prototypes: `tests/prototypes/{execution_model,poller}`.
