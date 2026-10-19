# Architecture

Compact map of how Clause works. History in `00-finished.md` and
`docs/validation.md#history`; open work in `11-plan.md`, `20-opt.md`,
`21-lib.md`; file locations in `files.md`.

C++23, project-internal APIs, CMake, warnings as errors. `clau` owns the
compiler pipeline; `clause_runtime` is separately linkable and LLVM-free; the
generated-code ABI (`abi/include`) is shared by both. Stages exchange owned
internal data; public interchange/stage readers are reserved directories only.
Tests prefer real CLI/project sources with owned OTP-generated goldens
(`tests/compiler/executables/run.py`, `tests/fixtures/programs/`); build and
test need no OTP (`docs/validation.md`).

## 1. Compiler (`clau`)

### Frontend

- Source: `SourceManager` owns UTF-8/Latin-1 buffers. Incremental lexer,
  directives and preprocessor keep decoded values, physical/logical spans,
  macro/include ancestry and feature snapshots; per-session macros and
  include/conditional stacks preserve rescan/stringification. PP conditions use
  a closed arbitrary-integer evaluator (not guard legality).
- Parser: `ParserSession` consumes expanded forms transactionally into flat AST
  arenas (owner/generation-checked IDs, closed variants, token provenance);
  token/node/depth/work/diagnostic limits roll back partial forms. Ordinary
  OTP 29 grammar; syntax acceptance is separate from executable admission.
- Printing (`docs/compile.md#source-printing`): `print_source`
  (`--print-source`) round-trips to the same tree, `SourceNotes` add
  annotations; `--print-types` adds `%% inferred:` signatures.
- Parse transforms (`transforms/`): term arena, ETF, abstract-form
  export/import, external `erl` loader with cached replies.

### Drivers, projects, caches

- Drivers (positional and project) keep complete target AST batches, run common
  semantic/backend stages, aggregate located diagnostics; targets isolated.
  Syntax-only actions stop early; compiles happen in memory; publication waits
  for every target (staged exclusive writes, multi-file not atomic).
- Input closure (`driver/frontend add_referenced`): modules named by literal
  atoms (`semantic::referenced_modules`, also `-behaviour`, imports, OTP
  auto-imports) that no input declares are parsed from the library directory,
  then project `source_search_paths`, until closed. `--print-inputs` stops here.
- Ignored sources (`project::Ignore`) leave discovery; each referenced ignored
  module gets a placeholder (`driver/placeholders`) returning
  `{module_ignored, M}`. `project/directory_index`: lazy listings, `may_exist`
  skips probes.
- Entry (`driver/entry`, `docs/executables.md`): `--entry`/manifest `entry`
  (`F/1`, else exported `F/0`) or sole `main/1` exporter; `#!` escripts get a
  rewritten header and exported `main/1`.
- Predefined forms (`driver/predefined`): `behaviour_info/1` and
  `module_info/0,1` (md5 of `print_source`), hidden from own
  exports/calls/specs, counted in `Module::predefined_`.
- Build dir `<manifest-dir>/build` (`--build-dir`), temp in `tmp/`. SQLite cache
  `cache/v2/cache.db` (entries + parts, blobs > 256 KB, WAL, `--no-cache`)
  stores transform replies, whole-batch compile results (`driver/compile_cache`)
  and inference results for library-like dirs (`driver/inference_cache`, key =
  clau identity + transitively atom-reached inputs). Preprocess/parse are not
  cached.

### Semantic analysis

- Bindings: LLVM-free index validates module/function/export identities;
  binding IDs distinguish definitions, reads, exact checks; tentative candidates
  publish on success. `_` binds nothing, body `=` analyzes RHS first, binary
  sizes see only preceding segments.
- Branches: `semantic::branch_clauses` views case/if/try/maybe/receive as
  clauses; names bound in several clauses share one identity
  (`Function::exports`); try names are always unsafe.
- Calls: `resolve_calls` orders SCCs callees-first (iterative Tarjan); builtin
  calls (`body_builtin`, `module_builtin`), dynamic, fun and import calls. Port
  BIFs report capability "ports"; `on_load`, `nifs`, non-inert `-compile` are
  rejected. `check_behaviours` gives erl_lint behaviour warnings.
- Guards (`docs/guards.md`): pinned audited catalog separate from availability;
  canonical `true` for comma tests, semicolon continues at the next alternative.
- Funs (`docs/funs.md`): `index_funs` gives every `fun F/A`, `fun M:F/A` and
  lambda a `Module::funs` entry; closures capture in definition order.
- Records: declaration-order layouts, per-use tuple expansion, `record_info/2`
  folds; native records (`-export_record`/`-import_record`, `#_`).
- Liveness (`semantic/liveness`): worklist from entry over calls, funs, literal
  apply/spawn targets, escaped module atoms; dead exports keep names with null
  entries (`--keep-exports`). Map ownership marks fresh single-read maps for
  in-place updates (`MapOperation::update_owned`).

### Type inference (`docs/semantic.md#inference-domain`)

- Declared types stay separate from implementation facts; specs never authorize
  representation checks. `contracts.cpp` reports spec conflicts.
- Lattice (`semantic/types/lattice`): family joins, term-ordered unions,
  `FactLimits` budgets, integer-threshold widening, positional lists, map
  associations past the key budget.
- `inference.cpp`: postorder walk over frames; clause joins widen after 8
  rounds, give up on a group at the cap or 1/8 of pass work, or when a member
  oscillates. Passes repeat until local inputs and fun reads settle, else a
  final pass with `term()` inputs.
- `Summary::types` = one `FunctionType` per clause; non-recursive callees are
  re-analysed per call under a budget; dependent facts index interned
  `Dependent`s while `Fact::type` is the erased join.

### Lowering (`codegen/`)

- Patterns/guards (`docs/patterns.md`): flat match plans with explicit
  success/mismatch continuations, tentative SSA, fresh bindings per candidate;
  iterative lowering keeps source order without host recursion.
- Control flow: `case`/`if`/`maybe` join through PHIs; raises, `catch`, `try`
  (`ProtectedScope`, after body on normal and handler paths) and `receive`
  (peek → match/take or skip → wait, timeout = after clause) call checked
  runtime services.
- Comprehensions: generator loops with reversed accumulators in term slots;
  binary and map generators read inline.
- Funs: `<prefix>.funs` descriptors bound to runtime `FunDefinition`s; calls go
  through a preparation service then the `clause.apply` marker.
- Inline fast paths (`lowering_inline`, `lowering_map_access`,
  `lowering_append`): type tests, small arithmetic/comparisons, tuple/list
  access, float math, binary match reads, small map lookups/updates, short `++`;
  small cells bump-allocate in `FrameState::heap_top/heap_limit`; the service
  is the slow tail.
- Constants: atoms false/true/undefined/ok/error = 0–4, others via
  `ModuleDescriptor::atom_cache`; compound literals in a per-module read-only
  literal area (`codegen/literals`).
- Specialization (O2, `codegen/proofs`): proven operands skip tag tests; ≤ 3
  variants/function, 32/module, ≤ 2x generic IR. Binary loops
  (`codegen/binary_loops`): self tail calls on a binary rest get a `<sym>.loop`
  variant updating one owned `sub_binary` in place; other body reads of a
  rest or the whole binary get a copy; TBAA tags (`codegen/alias_tags`),
  invariant view loads and a late GVN keep the cursor in registers. Raw floats: float literals
  and float-only results keep their double in a register (`raw_floats`), their
  cell dropped when no word use remains. Float loops (`codegen/float_loops`):
  a `<sym>.float` variant takes float parameters as double bits in its
  argument words; `lower_frames` boxes them (`clause.float.box`) and enters the
  function on service paths.
- Frames (`codegen/frames lower_frames`, `docs/execution-model.md`): native
  `Word f(ctx, args)` bodies move to `<sym>.body`; prologue builds the frame +
  resume switch, splits after non-tail calls, spills crossing values to term or
  raw slots, emits only `musttail` transfers. Self tail calls and tail-call
  cycles (`TailCycles`) reuse frames; each cycle merges into one
  `<first>.cycle` function (`CycleMerge`: shared prologue, resume indices
  numbered across members, start dispatch on the header's descriptor, member
  transfers become branches, member debug scopes inlined into an artificial
  one). Arguments spill only when crossing a call or safepoint. `Code` = `void(ctx, x0..x5)`; enter/tail/return run inline with
  `CLAUSE_enter/tail/return_v1` slow paths.
- Runtime services are declared once in `codegen/runtime_symbols.hpp` and
  mangled per target (`mangling.hpp`).

### Backend and linking

- LLVM: target-derived layouts, O0/O2 PassBuilder with verification, -Os =
  O2 + optsize + dead-stripping; text/bitcode/object/asm output; `--print-asm`
  maps line info back to `ir`/`erlang`; `-g` emits CodeView/DWARF; `--lto`.
- Startup (`codegen/startup`): module `clausev1_start` whose `main` hands a
  `StartupDescriptor` to `CLAUSE_main_v1`.
- Linking (`linking/`): objects staged in `.clause-link-*` beside the output,
  `clang --driver-mode=g++ --target=<triple>` with the runtime archive (members
  checked via LLVM Object) plus installed library inputs from
  `clause_runtime.link`; projects publish executables only after all succeed.

## 2. Runtime (`clause_runtime`)

### Execution and ABI

- `ProcessStack` (`process/stack`, one per context): flat word block, 4-word
  frame headers linked by offsets, 256 X registers, uncapped by default. Runtime
  enters code via `ProcessStack::run_code`; exported `<sym>` wraps
  `CLAUSE_invoke_v1`.
- Reductions: `enter` spends one (4,000/slice); at zero records `resume_` and
  returns `pause`, unwinding to `ProcessStack::run`.
- Errors: first-error channel separates Erlang errors from infrastructure
  statuses; every fallible call checks before using its output; halts unwind
  like errors. Stack traces copy the 8 innermost frames, terms built lazily.
- Startup (`startup/`): ABI check, options (`CLAUSE_FLAGS`, `--max-atoms`,
  `--args-file`, `--`), module registration, entry run; exits 0/N/1|127/70.
  `--profile` attributes time at frame transfers.
- Code server (`modules/`): shared mutex, hash indexes, modules never unloaded;
  literal areas, atom caches.

### Terms (`terms/`)

- `Term` = one Word; compound words are trusted pointers (ERTS Eterm style).
  Admission checks range/shape/ownership in Debug only; `*_unchecked` accessors
  skip proven kind checks.
- Kinds: arbitrary integers (up to `BIG_ARITY_MAX`), atoms (per-runtime
  `AtomStorage`, process-wide lock-free `atom_table`; runtime C++ spells literal
  atoms `"name"_atom`, which caches the word), tuples, lists, float
  cells, maps, bitstrings, funs, pids/ports (immediates from never-reused
  sequences), references (64-bit heap cells), native records.
- Handle references (atomics, re patterns, zlib streams, compiled match specs)
  use the `refc_binary` layout with shared buffers, weak registry, finalizer.
- Maps > 32 entries are persistent B+ trees (`map_tree`, fanout 16, path
  copying, edit tokens until the next collection).
- Bitstrings: MSB-first, small inline cells, large refc buffers, 3-word
  `sub_binary` over a never-nested original, appendable buffers grow in place.
- Order numbers < atoms < refs < funs < ports < pids < tuples < maps < lists <
  bitstrings, iterative (`word_order`). `format_term` prints `~w` iteratively
  under a byte cap.

### Memory and GC (`memory/`, `docs/runtime-heap.md`)

- `HeapStorage`: lazy young block (≥ 233 words) + fragments + generational
  old area with high-water mark + off-heap lists (refc binaries, tree-map
  stubs); `HeapMark` rollback; inline bump allocation syncs via
  `absorb`/`expose`.
- Roots: frame term slots, live X registers, failure payload, explicit spans,
  `TrapState`, mailbox; raw slots never.
- Collector (`heap_collect` Cheney `Copier`) runs at safepoints (function entry,
  comprehension heads) when wanted: minor by default, full on
  `garbage_collect`, binary pressure, budget or a full old area. `min_heap_size`
  floors blocks; shrink below 25% live; idle processes never collected.
- Limits opt-in (`--max-heap/--max-stack/--max-memory`, `RuntimeMemory`); host
  refusal = `out_of_memory`, exit 70.
- Copies (`GraphCopy`): iterative flat copy between heaps, literals recognised
  and not copied, refc buffers shared and charged once. `MessageBuffer` holds
  terms outside processes; large messages are built before the executor lock
  and adopted as receiver fragments. Persistent terms live in per-term areas of
  a reserved range, reclaimed after every process evacuated them.
  `detached_term` blocks hold ETS objects.

### Processes and scheduling (`scheduler/`, `docs/processes.md`)

- `detail::Executor` (in `Runtime::Impl`): `schedulers - 1` extra workers, one
  `RunQueue` each; home affinity, wake-affine placement, stealing, balancing
  every 4 slices.
- Per-process `Schedule` (running, blocked_on, holds, ready, ending,
  port_wait). Touching a process running elsewhere throws `builtins::Blocked`
  and retries after its slice; sends to running processes buffer until the
  slice ends.
- Spawn copies fun/args into a new context; the main process ends the run.
- Messages: `Executor::send` copies into `Mailbox` (inbox, queue, saved
  position, deadline).
- Timers: hierarchical timer wheel on one timer thread; workers never read the
  clock.
- Signals (`scheduler/signals.cpp`): links, `trap_exit`, monitors, names;
  `finish()` notifies names, 'DOWN', links. Exits print OTP's legacy
  `=ERROR REPORT====`.

### Ports and I/O (`ports/`, `docs/ports.md`)

- Executor port table; drivers fd, spawn, file (`port_control` outside the
  mutex), tcp/udp sockets.
- One Asio `Reactor` thread per runtime serves pipes, fd input, child exits,
  sockets (Windows overlapped pipes/wait pool; POSIX `async_wait`, `SIGCHLD`
  reaper).
- Ports are run-queue entities alternating with process slices under a
  reduction budget; busy ports suspend senders, input pauses at 64 KiB held,
  owners throttle at 1,024 queued messages.

### ETS and match specifications

- ETS (`ets/`, `docs/runtime.md#ets-tables`): objects are `detached_term`
  blocks read in place; hash index (power-of-two buckets, tombstones, no resize
  while fixed) or `std::set` tree per part; 64 parts with write_concurrency.
  Table lock shared by single-key ops; long reads and frees run in portions or
  outside the executor lock at owner end. Registry `ets::Tables`; lock order
  executor → registry → table.
- Match specs (`match_spec/`): `compile` → `Program` (node arrays, detached
  constants, table or trace dialect, OTP lint messages); `Run` binds heads in
  place and copies bodies into the caller heap; compiled specs are handle refs.

### Builtin machinery (`builtins/`, `docs/builtins.md`)

- ABI catalog `abi::v1::bridge_builtins` shared with the compiler;
  `BuiltinRegistry` maps names to null-body `FrameDescriptor`s;
  `CodeServer::function_frame` = exports, then builtins; library
  `clause_builtin_placeholder` bodies are replaced at registration.
- Call kinds: bridge (`CLAUSE_builtin_frame_v1` + apply marker), direct
  (`CLAUSE_builtin_call_v1`), quick (bounded fast path, then frame).
  `typed<Function>` adapts typed C++ (mismatch → badarg).
- Portions (`builtins/portions`): long builtins spend `ProcessStack::budget()`
  then `trap(continuation, state)`; builtins may continue in an Erlang export
  (`semantic/builtin_continuations.hpp`), which keeps the target live.

## 3. Standard library reimplementation (`library/`, `docs/library.md`)

### Model

- Goal (plan 21): every OTP 29-documented function of `erts`, `kernel`,
  `stdlib` has a Clause implementation; programs need no OTP sources. Native
  C++ builtins preferred; Erlang only for fun-taking functions, process
  protocols and glue. OTP compatibility beats speed.
- Tree `library/{erts,kernel,stdlib}/<m>.erl` with `<m>.terms` inventories and
  `*.types.hrl`; `modules.terms` lists provided and missing (reserved, never
  taken from search paths) modules; `documented.terms` records OTP-documented
  functions; checked by `tests/compiler/library/library_modules.py`.
- Loading: library modules join the batch only when referenced (see input
  closure); inputs replacing a Clause module warn; stubs raise
  `{not_implemented, F, A}`. `erlang.erl` is generated from `erlang.terms`;
  non-builtin `erlang:F/A` and auto-imports pull it in. Private `clause_*`
  modules (`clause_re`, `clause_unicode`, `clause_io_lib`, `clause_json`,
  `clause_rand`, `clause_sofs`, ...) bind C++ halves to public Erlang APIs.

### erts

- `erlang` BIFs: one TU per family in `builtins/erlang/`; process-wide state in
  the executor (timers, aliases, introspection), per-process state in
  `ProcessContext` (dictionary, group leader).
- `atomics`, `counters`, `persistent_term`, `erts_internal`, `init` (answers as
  a started node; argv decoded once into `ProgramArguments`),
  `prim_eval:'receive'/2` (Erlang scan over `clause_receive`), `zlib` (bundled
  zlib 1.3.2: streams, compressed term codec). Low-level `prim_*`, socket NIF
  modules are declared missing.

### kernel

- `file`, `io` stdin server, `os` (`os:cmd/1`), `code`,
  `application_controller`; networking via `clause_socket.erl` + `gen_tcp`,
  `gen_udp`, `inet` over port socket drivers; `clause_disk_log` halt logs.

### stdlib

- Text: `io`/`io_lib` (`builtins/io_lib/`: deep-chardata arena, `write1`,
  format scan/build, `fread`, io_lib_pretty layout, `maps_order` sort-and-rerun
  with `clause_io_lib`), `builtins/float_text` (OTP rounding, Ryu), `string`,
  `unicode`/`unicode_util` (generated tables, cp/gc, normalization), `re`
  (bundled PCRE2 10.47; C++ compile/run, rest in `re.erl`), `base64`, `json`,
  `uri_string`, `filename`, `calendar`.
- Collections in C++ (`builtins/collections/`): `lists`, `maps`, `ordsets`,
  `orddict`, `proplists`, `dict`, `sets`, `gb_trees`, `gb_sets`, `queue`,
  `array`, `sofs`; fun-taking functions stay Erlang.
- ETS-based: `ets` builtins (`builtins/ets/`: tables, reads, writes, select and
  match families, match_spec functions, walks), `digraph`/`digraph_utils` over
  ETS tables (`DigraphView`, `graph::View` queries), ets table files via
  `disk_log` halt-log items.
- Disk tables: `dets` keeps OTP's processes (registry server, one process per
  table with the write cache and fixes) in Erlang over a C++ engine of the
  version 9 file format (`builtins/dets/`) that writes OTP's bytes.
- Others: `rand`/`random` (generators + observed tables), `math`, `binary`,
  `records` (native record reflection), front-end support `erl_internal`,
  `erl_anno`, `erl_features`; `erl_scan` over a C++ scanner of OTP's rules
  (re-entrant by rescanning the unfinished token); `erl_parse` over a C++
  recursive-descent parser building OTP's abstract format and errors; `ms_transform` in Erlang
  over abstract forms.
