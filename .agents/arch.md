# Architecture

Compact map of how Clause works. History in `00-finished.md` and `docs/validation.md#history`; open
work in `11-plan.md`, `20-opt.md`, `21-lib.md`; file locations in `files.md`.

C++23, project-internal APIs, CMake, warnings as errors. `clau` owns the compiler pipeline;
`clause_runtime` is separately linkable and LLVM-free; the generated-code ABI (`abi/include`) is
shared by both. Stages exchange owned internal data; public interchange/stage readers are reserved
directories only. Tests prefer real CLI/project sources with owned OTP-generated goldens
(`tests/compiler/executables/run.py`, `tests/fixtures/programs/`); build and test need no OTP
(`docs/validation.md`).

## 1. Compiler (`clau`)

### Frontend

- Source: `SourceManager` owns UTF-8/Latin-1 buffers. Incremental lexer, directives and preprocessor
  keep decoded values, physical/logical spans, macro/include ancestry and feature snapshots;
  per-session macros and include/conditional stacks preserve rescan/stringification. PP conditions
  use a closed arbitrary-integer evaluator (not guard legality).
- Parser: `ParserSession` consumes expanded forms transactionally into flat AST arenas
  (owner/generation-checked IDs, closed variants, token provenance);
  token/node/depth/work/diagnostic limits roll back partial forms. Ordinary OTP 29 grammar; syntax
  acceptance is separate from executable admission.
- Printing (`docs/compile.md#source-printing`): `print_source` (`--print-source`) round-trips to the
  same tree, `SourceNotes` add annotations; `--print-types` adds `%% inferred:` signatures.
- Parse transforms (`transforms/`): term arena, ETF, abstract-form export/import, external `erl`
  loader with cached replies.

### Drivers, projects, caches

- Drivers (positional and project) keep complete target AST batches, run common semantic/backend
  stages, aggregate located diagnostics; targets isolated. Syntax-only actions stop early; compiles
  happen in memory; publication waits for every target (staged exclusive writes, multi-file not
  atomic).
- Input closure (`driver/frontend add_referenced`): modules named by literal atoms
  (`semantic::referenced_modules`, also `-behaviour`, imports, OTP auto-imports) that no input
  declares are parsed from the library directory, then project `source_search_paths`, until closed.
  `--print-inputs` stops here.
- Ignored sources (`project::Ignore`) leave discovery; each referenced ignored module gets a
  placeholder (`driver/placeholders`) returning `{module_ignored, M}`. `project/directory_index`:
  lazy listings, `may_exist` skips probes.
- Entry (`driver/entry`, `docs/executables.md`): `--entry`/manifest `entry` (`F/1`, else exported
  `F/0`) or sole `main/1` exporter; `#!` escripts get a rewritten header and exported `main/1`.
- Predefined forms (`driver/predefined`): `behaviour_info/1` and `module_info/0,1` (md5 of
  `print_source`), hidden from own exports/calls/specs, counted in `Module::predefined_`.
- Build dir `<manifest-dir>/build` (`--build-dir`), temp in `tmp/`. SQLite cache `cache/v2/cache.db`
  (entries + parts, blobs > 256 KB, WAL, `--no-cache`) stores transform replies, whole-batch compile
  results (`driver/compile_cache`) and inference results for library-like dirs
  (`driver/inference_cache`, key = clau identity + transitively atom-reached inputs).
  Preprocess/parse are not cached.

### Semantic analysis

- Bindings: LLVM-free index validates module/function/export identities; binding IDs distinguish
  definitions, reads, exact checks; tentative candidates publish on success. `_` binds nothing, body
  `=` analyzes RHS first, binary sizes see only preceding segments.
- Branches: `semantic::branch_clauses` views case/if/try/maybe/receive as clauses; names bound in
  several clauses share one identity (`Function::exports`); try names are always unsafe.
- Calls: `resolve_calls` orders SCCs callees-first (iterative Tarjan); builtin calls
  (`body_builtin`, `module_builtin`), dynamic, fun and import calls. Port BIFs report capability
  "ports"; `on_load`, `nifs`, non-inert `-compile` are rejected. `check_behaviours` gives erl_lint
  behaviour warnings.
- Guards (`docs/guards.md`): pinned audited catalog separate from availability; canonical `true` for
  comma tests, semicolon continues at the next alternative.
- Funs (`docs/funs.md`): `index_funs` gives every `fun F/A`, `fun M:F/A` and lambda a `Module::funs`
  entry; closures capture in definition order.
- Records: declaration-order layouts, per-use tuple expansion, `record_info/2` folds; native records
  (`-export_record`/`-import_record`, `#_`).
- Liveness (`semantic/liveness`): worklist from entry over calls, funs, literal apply/spawn targets,
  escaped module atoms; `-compile({clause_liveness, strict})` modules escape only their own name and
  their atom producers do not count (`docs/compile.md#strict-liveness`); dead exports keep names with
  null entries (`--keep-exports`). Map ownership
  marks fresh single-read maps for in-place updates (`MapOperation::update_owned`).

### Type inference (`docs/semantic.md#inference-domain`)

- Declared types stay separate from implementation facts; specs never authorize representation
  checks. `contracts.cpp` reports spec conflicts.
- Lattice (`semantic/types/lattice`): family joins, term-ordered unions, `FactLimits` budgets,
  integer-threshold widening, positional lists, map associations past the key budget.
- `inference.cpp`: postorder walk over frames; clause joins widen after 8 rounds, give up on a group
  at the cap or 1/8 of pass work, or when a member oscillates. Passes repeat until local inputs and
  fun reads settle, else a final pass with `term()` inputs. Later passes skip components nothing they
  read changed for, warm-start recursive solves, and give costly ones (lattice joins) `term()` inputs.
  Walk scopes save variable facts as flat slots per definition (`BindingValues`); each function's
  binding index is built once per inference; expression facts are hash maps.
- `Summary::types` = one `FunctionType` per clause; non-recursive callees are re-analysed per call
  under a budget; dependent facts index interned `Dependent`s while `Fact::type` is the erased join.

### Lowering (`codegen/`)

- Patterns/guards (`docs/patterns.md`): flat match plans with explicit success/mismatch
  continuations, tentative SSA, fresh bindings per candidate; iterative lowering keeps source order
  without host recursion.
- Control flow: `case`/`if`/`maybe` join through PHIs; raises, `catch`, `try` (`ProtectedScope`,
  after body on normal and handler paths) and `receive` (peek → match/take or skip → wait, timeout =
  after clause) call checked runtime services.
- Comprehensions: generator loops with reversed accumulators in term slots; binary and map
  generators read inline.
- Funs: `<prefix>.funs` descriptors bound to runtime `FunDefinition`s; calls go through a
  preparation service then the `clause.apply` marker. O2 inlines known anonymous funs (written in
  the call, or `F = fun` bound once): clauses in place, the fun built only where no clause matches
  (`lowering_fun_inlining`); `lists` map/filter/foreach/all/any/flatmap/fold*/mapfold* of such funs
  become loops in the caller (`lowering_list_loops`) that collect only at yields; `maps:fold` too
  (`lowering_map_loops`). Fold funs returning `Acc#{...}` own the accumulator from step 2 on (update
  op chosen at run time).
- Inline fast paths (`lowering_inline`, `lowering_map_access`, `lowering_append`,
  `lowering_integer_text`): type tests, small arithmetic/comparisons, tuple/list access, float math,
  binary match reads, small map lookups/updates (literal keys: per-site cached key index, 8
  rewrites), short `++`, small-int `integer_to_list` (digits written onto `++`'s right operand) and
  `list_to_integer`; small cells bump-allocate in `FrameState::heap_top/heap_limit`; the service is
  the slow tail.
- Constants: atoms false/true/undefined/ok/error = 0–4, others via `ModuleDescriptor::atom_cache`;
  compound literals encoded per module (`abi/literals.hpp`, BEAM compact term encoding + extended
  tags; `codegen/literals`, `codegen/lowering_literals`), decoded once per runtime at registration
  into its literal heap (`runtime/src/modules/literal_heap`); code reads `literal_cache` after the
  claim check, other runtimes call `CLAUSE_literal_v1`.
- Specialization (O2, `codegen/proofs`): proven operands skip tag tests; ≤ 3 variants/function,
  32/module, ≤ 2x generic IR. Binary loops (`codegen/binary_loops`): self tail calls on a binary
  rest get a `<sym>.loop` variant updating one owned `sub_binary` in place; other body reads of a
  rest or the whole binary get a copy; TBAA tags (`codegen/alias_tags`), invariant view loads and a
  late GVN keep the cursor in registers. Raw floats: float literals and float-only results keep
  their double in a register (`raw_floats`), their cell dropped when no word use remains. Float
  loops (`codegen/float_loops`): a `<sym>.float` variant takes float parameters as double bits in
  its argument words; `lower_frames` boxes them (`clause.float.box`) and enters the function on
  service paths.
- Frames (`codegen/frames lower_frames`, `docs/execution-model.md`): native `Word f(ctx, args)`
  bodies move to `<sym>.body`; prologue builds the frame + resume switch, splits after non-tail
  calls, spills crossing values to term or raw slots, emits only `musttail` transfers. Self tail
  calls and tail-call cycles (`TailCycles`) reuse frames; each cycle merges into one `<first>.cycle`
  function (`CycleMerge`: shared prologue, resume indices numbered across members, start dispatch on
  the header's descriptor, member transfers become branches, member debug scopes inlined into an
  artificial one). Arguments spill only when crossing a call or safepoint. `Code` =
  `void(ctx, x0..x5)`; enter/tail/return run inline with `CLAUSE_enter/tail/return_v1` slow paths.
- Runtime services are declared once in `codegen/runtime_symbols.hpp` and mangled per target
  (`mangling.hpp`).

### Backend and linking

- LLVM: target-derived layouts, O0/O2 PassBuilder with verification, -Os = O2 + optsize +
  dead-stripping; text/bitcode/object/asm output; `--print-asm` maps line info back to
  `ir`/`erlang`; `-g` emits CodeView/DWARF; `--lto`.
- Parallel code generation (`codegen/parallel`, `--jobs`, default all hardware threads): for object
  output, optimization moves each module through bitcode (use-list order kept) into its own
  `LLVMContext`, largest first, each worker with its own `TargetMachine`; emission then runs on those
  contexts in parallel; diagnostics and outputs merge in module order, objects identical to `--jobs 1`.
- Startup (`codegen/startup`): module `clausev1_start` whose `main` hands a `StartupDescriptor` to
  `CLAUSE_main_v1`; the descriptor lists runtime extension entries (`CLAUSE_crypto_extension_v1` when
  the batch has library `crypto` or `--with-crypto`), whose builtins startup registers.
- Linking (`linking/`): objects staged in `.clause-link-*` beside the output,
  `clang --driver-mode=g++ --target=<triple>` with the runtime archive (members checked via LLVM
  Object) plus installed library inputs from `clause_runtime.link`; projects publish executables
  only after all succeed. Crypto (`linking/crypto`, `openssl`): `clause_runtime_crypto` before the
  runtime archive, libcrypto + system libraries from `clause_runtime_crypto.link` after it, or from
  `--with-crypto=PATH`/`CLAUSE_OPENSSL_ROOT` (an install, or a source tree built once into
  `<build>/cache/openssl/<hash>` with the recipe in `clause_runtime_crypto.openssl`).

## 2. Runtime (`clause_runtime`)

### Execution and ABI

- `ProcessStack` (`process/stack`, one per context): flat word block, 4-word frame headers linked by
  offsets, 256 X registers, uncapped by default. Runtime enters code via `ProcessStack::run_code`;
  exported `<sym>` wraps `CLAUSE_invoke_v1`.
- Reductions: `enter` spends one (4,000/slice); at zero records `resume_` and returns `pause`,
  unwinding to `ProcessStack::run`.
- Errors: first-error channel separates Erlang errors from infrastructure statuses; every fallible
  call checks before using its output; halts unwind like errors. Stack traces copy the 8 innermost
  frames, terms built lazily.
- Startup (`startup/`): ABI check, options (`CLAUSE_FLAGS`, `--max-atoms`, `--args-file`, `--`),
  module registration, entry run; exits 0/N/1|127/70. `--profile` attributes time at frame
  transfers.
- Code server (`modules/`): shared mutex, hash indexes, modules never unloaded; literal areas (literal
  heap areas in the persistent range, owned by `LiteralAreas`), atom and literal caches.

### Terms (`terms/`)

- `Term` = one Word; compound words are trusted pointers (ERTS Eterm style). Admission checks
  range/shape/ownership in Debug only; `*_unchecked` accessors skip proven kind checks.
- Kinds: arbitrary integers (up to `BIG_ARITY_MAX`; bignum times small is a limb loop on heap words,
  other operations go through Boost cpp_int), atoms (per-runtime `AtomStorage`, process-wide
  lock-free `atom_table`; runtime C++ spells literal atoms `"name"_atom`, which caches the word),
  tuples, lists, float cells, maps, bitstrings, funs, pids/ports (immediates from never-reused
  sequences), references (64-bit heap cells), native records.
- Handle references (atomics, re patterns, zlib streams, compiled match specs) use the `refc_binary`
  layout with shared buffers, weak registry, finalizer.
- Maps > 32 entries are persistent B+ trees (`map_tree`, fanout 16, path copying, edit tokens until
  the next collection); owned small-key updates reuse current-token nodes and stub in place.
- Bitstrings: MSB-first, small inline cells, large refc buffers, 3-word `sub_binary` over a
  never-nested original, appendable buffers grow in place; freed power-of-two buffers are reused per
  thread (`off_heap`).
- Order numbers < atoms < refs < funs < ports < pids < tuples < maps < lists < bitstrings,
  iterative; numbers, atoms, local refs, whole-byte binaries and tuples/lists of them order from
  their cells (`word_order`, also `=:=` via `word_exact`), the rest structurally. `format_term`
  prints `~w` iteratively under a byte cap.

### Memory and GC (`memory/`, `docs/runtime-heap.md`)

- `HeapStorage`: lazy young block (≥ 233 words) + fragments + generational old area with high-water
  mark + off-heap lists (refc binaries, tree-map stubs); `HeapMark` rollback; inline bump allocation
  syncs via `absorb`/`expose`.
- Roots: frame term slots, live X registers, failure payload, explicit spans, `TrapState`, mailbox;
  raw slots never.
- Collector (`heap_collect` Cheney `Copier`) runs at safepoints (function entry, comprehension
  heads) when wanted: minor by default, full on `garbage_collect`, binary pressure, budget or a full
  old area. `min_heap_size` floors blocks; shrink below 25% live lowers the block's capacity in
  place (no second copy); idle processes never collected.
- Limits opt-in (`--max-heap/--max-stack/--max-memory`, `RuntimeMemory`); host refusal =
  `out_of_memory`, exit 70.
- Copies (`GraphCopy`, `memory/graph_copy.hpp`): iterative flat copy between heaps, literals
  recognised and not copied, refc buffers shared and charged once. `MessageBuffer` holds terms
  outside processes. A send walks its message once before the executor lock (`OutgoingMessage`,
  persistent terms copied): large messages become buffers adopted as receiver fragments, counted
  toward collection only once received (as ERTS); small ones copy straight into a parked receiver or
  behind an arrival node, which the receiver memcpys and relocates (no second walk). Copy walks and
  collector scans follow list tails in place and cache the last area/literal span. Persistent terms
  live in per-term areas of a reserved range, reclaimed after every process evacuated them.
  `detached_term` blocks hold ETS objects.

### Processes and scheduling (`scheduler/`, `docs/processes.md`)

- `detail::Executor` (in `Runtime::Impl`): `schedulers - 1` extra workers, one `RunQueue` each; home
  affinity, wake-affine placement, stealing, balancing every 4 slices. Default worker count = CPUs
  of the process affinity mask; `--scheduler-cpus` binds worker N round-robin
  (`scheduler/cpus.cpp`).
- Per-process `Schedule` kept in `ProcessContext::schedule()` (started, parked, running,
  blocked_on+pid, holds, ready, ending, port_wait). Touching a process running elsewhere throws
  `builtins::Blocked` and retries after its slice.
- Spawn builds and loads the child outside the executor mutex; a plain spawn publishes (contexts
  lock, table writer lock) and queues it under the run-queue lock only. Run queues and idle state
  have their own lock (after the executor mutex); workers free ended contexts after leaving the
  mutex. A process lifecycle makes 6 allocations (first 64 stack words and 4 mailbox nodes inside
  the context, monitor maps made by the first monitor, flat registries).
- Messages: `Executor::send` copies into `Mailbox` (inbox, queue, saved position, deadline; list
  nodes from a per-mailbox `NodePool`). To a receiver running elsewhere it takes no lock: lock-free
  `ProcessTable` lookup (open addressing, tombstones), message node pushed onto the receiver's
  `Arrivals` (lock-free list), taken 64 at a time when receive would wait. Arrivals and old table
  arrays are freed by quiescent reclamation (per-worker epoch at slice boundaries).
- Timers: hierarchical timer wheel on one timer thread; workers never read the clock.
- Signals (`scheduler/signals.cpp`): links, `trap_exit`, monitors, names; `finish()` notifies names,
  'DOWN', links. Exits print OTP's legacy `=ERROR REPORT====`.

### Ports and I/O (`ports/`, `docs/ports.md`)

- Executor port table; drivers fd, spawn, file (`port_control` outside the mutex; binary data passed
  in place, `ByteBuffer` replies taken over as binaries; `file_changes` owners, permissions, times,
  truncate/advise/allocate), tcp/udp sockets (option records applied by setsockopt or kept with
  inet_drv's defaults, inet_drv traffic counters and status flags, connected UDP).
- One Asio `Reactor` thread per runtime serves pipes, fd input, child exits, sockets (Windows
  overlapped pipes/wait pool; POSIX `async_wait`, `SIGCHLD` reaper).
- Ports are run-queue entities alternating with process slices under a reduction budget; busy ports
  suspend senders, input pauses at 64 KiB held, owners throttle at 1,024 queued messages.

### ETS and match specifications

- ETS (`ets/`, `docs/runtime.md#ets-tables`): objects are `detached_term` blocks read in place; hash
  index (power-of-two buckets, tombstones, no resize while fixed) or `std::set` tree per part; 64
  parts with write_concurrency. Table lock shared by single-key ops; long reads and frees run in
  portions or outside the executor lock at owner end. Registry `ets::Tables`; lock order executor →
  registry → table.
- Match specs (`match_spec/`): `compile` → `Program` (node arrays, detached constants, table or
  trace dialect, OTP lint messages); `Run` binds heads in place and copies bodies into the caller
  heap; compiled specs are handle refs.

### Builtin machinery (`builtins/`, `docs/builtins.md`)

- ABI catalog `abi::v1::bridge_builtins` shared with the compiler; `BuiltinRegistry` maps names to
  null-body `FrameDescriptor`s; `CodeServer::function_frame` = exports, then builtins; library
  `clause_builtin_placeholder` bodies are replaced at registration.
- Call kinds: bridge (`CLAUSE_builtin_frame_v1` + apply marker), direct (`CLAUSE_builtin_call_v1`),
  quick (bounded fast path, then frame). `typed<Function>` adapts typed C++ (mismatch → badarg).
- Runtime extensions: separate archives (`clause_runtime_crypto`, `docs/compile.md#openssl`) whose
  `RuntimeExtension` families `RuntimeOptions::extensions` adds after `production_builtins()`; only
  programs that need them link them.
- Portions (`builtins/portions`): long builtins spend `ProcessStack::budget()` then
  `trap(continuation, state)`; builtins may continue in an Erlang export
  (`semantic/builtin_continuations.hpp`), which keeps the target live.

## 3. Standard library reimplementation (`library/`, `docs/library.md`)

### Model

- Goal (plan 21): every OTP 29-documented function of `erts`, `kernel`, `stdlib` has a Clause
  implementation; programs need no OTP sources. Native C++ builtins preferred; Erlang only for
  fun-taking functions, process protocols and glue. OTP compatibility beats speed.
- Tree `library/{erts,kernel,stdlib}/<m>.erl` with `<m>.terms` inventories and `*.types.hrl`;
  `modules.terms` lists provided and missing (reserved, never taken from search paths) modules;
  `documented.terms` records OTP-documented functions; checked by
  `tests/compiler/library/library_modules.py`.
- Loading: library modules join the batch only when referenced (see input closure); inputs replacing
  a Clause module warn; stubs raise `{not_implemented, F, A}`. `erlang.erl` is generated from
  `erlang.terms`; non-builtin `erlang:F/A` and auto-imports pull it in. Private `clause_*` modules
  (`clause_re`, `clause_unicode`, `clause_io_lib`, `clause_json`, `clause_rand`, `clause_sofs`, ...)
  bind C++ halves to public Erlang APIs.

### erts

- `erlang` BIFs: one TU per family in `builtins/erlang/`; process-wide state in the executor
  (timers, aliases, introspection), per-process state in `ProcessContext` (dictionary, group
  leader).
- `atomics`, `counters`, `persistent_term`, `erts_internal`, `init` (answers as a started node; argv
  decoded once into `ProgramArguments`), `prim_eval:'receive'/2` (Erlang scan over
  `clause_receive`), `zlib` (bundled zlib 1.3.2: streams, compressed term codec). Low-level
  `prim_*`, socket NIF modules are declared missing.

### crypto (plan 21 LIB79)

- `library/crypto/` (application with `crypto.app`, listed as a library resource): `crypto.erl`
  over `clause_crypto` builtins in `runtime/src/builtins/crypto/` (OpenSSL ≥ 3.4, pinned 4.1.0):
  algorithm tables probed on the first call (`algorithms.cpp`), hash/MAC states as handle
  references, OTP's NIF errors turned into `{Class, {File, Line}, Msg}` by `?nif_call`; engines
  refused. Ciphers (LIB79B) and public keys (LIB79C) pending.

### kernel

- `file` (every documented function; a raw file is a server answering as raw; servers stop after
  failed writes and reads as OTP's; `compressed` gzip layer in the server over `zlib`), `io` stdin server, `os` (`cmd/1,2` over OTP's shell protocol; `find_executable`, `set_signal` builtins), `code` (loaded-module builtins; code path in a persistent term, OTP's path changes and lookups, loading answers for linked code), `application_controller`; networking via
  `clause_socket.erl` (+ `clause_socket_options.erl`) + `gen_tcp`, `gen_udp`, `inet` over port socket
  drivers; `net` over `clause_net` builtins (names, services, interfaces); `inet_res` in Erlang over the `clause_net` DNS codec; `socket` over the same driver ports (bind op 22, registry process for handles, monitors, listings); `gen_sctp` without SCTP; `clause_disk_log`
  halt logs.

### stdlib

- Text: `io` (stdin server with OTP's noshell options; file servers' encodings; terminal queries in `builtins/terminal.cpp`)/`io_lib` (`builtins/io_lib/`: deep-chardata arena, `write1`, format scan/build,
  `fread`, io_lib_pretty layout, `maps_order` sort-and-rerun with `clause_io_lib`),
  `builtins/float_text` (OTP rounding, Ryu), `string`, `unicode`/`unicode_util` (generated tables,
  cp/gc, normalization), `re` (bundled PCRE2 10.47; C++ compile/run, rest in `re.erl`), `base64`,
  `json`, `uri_string`, `filename`, `filelib` (wildcard parts compiled/matched in `builtins/filelib.cpp`,
  walk in Erlang), `file_sorter` (exact Erlang port of OTP's runs and merges), `zip`, `erl_tar` (Erlang following
  OTP's archive layouts byte for byte over `zlib` and `file`), `zstd` (`builtins/zstd/` over pinned
  libzstd 1.5.7 objects in the runtime, like zlib), `beam_lib` (Erlang chunk walk as OTP's), `escript` (create/extract), `argparse` (`builtins/argparse/`: validation, parser and help in C++, funs called by Erlang through replayed requests), `qlc_pt` (`builtins/qlc_pt/`: the qlc transform at run time in C++, ms_transform answered by Erlang through replayed requests), `qlc` (Erlang: OTP's prepare/optimize/setup over the transform's handles, joins, caches, file_sorter sorts, cursors, info), the interactive shell (`shell` server + `clause_shell` terminal + `clause_shell_eval` evaluator over `erl_eval`, `clause_io_lib:pretty/2` record printing; `clau --shell` links it with the whole library and keeps exports; `c`, `shell_default`), `win32reg`
  (Erlang over the Windows-only `clause_registry` port driver), `calendar`.
- Time: `timer` without a timer server (per-timer processes reached through aliases; conversions in
  `builtins/timer.cpp`).
- Behaviours (all Erlang): `proc_lib` (ancestors, initial calls, sync starts, crash reports), `sys`
  (system messages, OTP's debug list shapes), `gen` (starts, calls with alias tags, request ids),
  `gen_server`, `gen_statem` (transition step record, time-out zero queue), `gen_event`, `gen_fsm`,
  `supervisor`, `supervisor_bridge`; their reports are `?LOG_*` events.
- Logging (Erlang): `logger` API over the `clause_logger` server (gen_server `logger` started at the
  first use; ETS `logger` for configs, persistent terms for levels), `logger_formatter`,
  `logger_std_h` (one synchronous process per handler: devices, files, rotation), `logger_disk_log_h`
  (same shape over a disk log; common part `clause_logger_h`), `logger_filters`,
  `error_logger` (gen_event fed by its logger handler), `error_logger_file_h`, `log_mf_h`,
  `error_handler`; `kernel/include/{logger,file,eep48,inet}.hrl` and `stdlib/include/{assert,
  ms_transform,qlc,zip}.hrl` serve `include_lib` (no OTP on paths needed). Default formatter and
  file handler are named by literal calls so the batch links them.
- Applications (Erlang): `application` over `application_controller` (registered gen_server entered from a
  proc_lib process at first use; public ETS `ac_tab` for loaded specs, env, masters) and
  `clause_application_master` (group leader per application, start/phases/stop). Specs: clau's
  `driver/app_resources` generates `clause_applications` (specs/0, library_modules/1) from `.app`/`.app.src`
  files when the batch has application_controller; kernel/stdlib virtual and running.
- Local-node kernel services (Erlang): `pg` over `data_publisher` (scope gen_server, ETS per scope, no peers),
  `global` (lazy `global_name_server`, OTP's ETS names/locks), `global_group` (no groups), `erpc` (spawn_request
  locally; other nodes get a simulated `noconnection` reply), `rpc` over erpc with lazy `rex`, `net_adm`/
  `net_kernel`/`auth` non-alive answers, `seq_trace` tokens in the process dictionary; tracing (`erlang:trace*`, `trace`, `erl_tracer`) raises `notsup`. Behaviours call `global`
  literally for `{global, Name}`.
- Collections in C++ (`builtins/collections/`): `lists`, `maps`, `ordsets`, `orddict`, `proplists`,
  `dict`, `sets`, `gb_trees`, `gb_sets`, `queue`, `array`, `sofs`; fun-taking functions stay Erlang.
- ETS-based: `ets` builtins (`builtins/ets/`: tables, reads, writes, select and match families,
  match_spec functions, walks), `digraph`/`digraph_utils` over ETS tables (`DigraphView`,
  `graph::View` queries), ets table files via `disk_log` halt-log items.
- Disk logs: `disk_log` keeps OTP's processes (registry `clause_disk_log_server`, one process per log
  with owners, blocking, write cache) in Erlang over `clause_disk_log` (OTP's halt/wrap/rotate file
  formats, byte for byte); `wrap_log_reader` reads wrap logs offline.
- Disk tables: `dets` keeps OTP's processes (registry server, one process per table with the write
  cache and fixes) in Erlang over a C++ engine of the version 9 file format (`builtins/dets/`) that
  writes OTP's bytes.
- Others: `rand`/`random` (generators + observed tables), `math`, `binary`, `records` (native record
  reflection), front-end support `erl_internal`, `erl_anno`, `erl_features`; `erl_scan` over a C++
  scanner of OTP's rules (re-entrant by rescanning the unfinished token); `erl_parse` over a C++
  recursive-descent parser building OTP's abstract format and errors; `ms_transform` in Erlang over
  abstract forms; `erl_eval` an Erlang interpreter of the abstract format (interpreted funs as
  closures over OTP's env tuple, bit segments via `clause_erl_eval` C++ builtins, command checks with
  `erl_lint`). Clause-heavy front-end walks move to C++ builtins
  (LIB34C): `erl_expand_records:module/2`, the guard tests, `erl_lint` and `erl_pp`
  (`front_end/expand_records.cpp`, `guard_tests.cpp`, `lint*.cpp` over one `Lint` state, `pp*.cpp`
  items then layout with hooks answered by rerunning from Erlang, node helpers in `forms.hpp`).
