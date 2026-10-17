# Runtime

`clause_runtime` is an LLVM-free C++23 library. It owns contexts, process heaps,
atoms, loaded modules and lifecycle bookkeeping, and runs Erlang processes on
scheduler workers ([processes](processes.md)). All APIs are project-internal;
host calls must be serialized per runtime and not overlap a program run, whose
workers synchronize among themselves ([threads](#threads)).

## Linking

Link exactly one runtime built for the target through
`Clause::generated_program`:

```cmake
add_executable(harness harness.cpp)
target_link_libraries(harness PRIVATE Clause::generated_program)
```

It brings the archive, ABI/runtime headers and C++23, but not LLVM.
[link_consumer.cpp](../tests/runtime/link_consumer.cpp) shows a full lifecycle.

## Lifecycle

[runtime.hpp](../runtime/include/clause/runtime/runtime.hpp):

- `Runtime::start(options)` → `std::expected<std::unique_ptr<Runtime>, Status>`.
  Defaults: current ABI version and native term width, `max_atoms` 2^20 (at most
  2^26; programs set it with `--max-atoms`,
  [runtime options](executables.md#runtime-options)). The number of contexts is
  not limited. `process_heap` and `process_stack` are the options of contexts
  created by `create_context()`; `memory_limit_bytes` is the optional
  [runtime-wide limit](runtime-heap.md#runtime-memory-limit), reported by
  `memory_bytes()`. All are uncapped by default.
- `create_context()` or `create_context(heap_options, stack_options)` → borrowed
  `ProcessContext*`, stable until destroyed. Heap defaults: 233-word minimum
  heap (`min_heap_words`) and no memory cap (`limit_bytes` =
  `UNLIMITED_HEAP_BYTES`; a set budget is a word multiple at least the minimum
  heap). The stack is uncapped too unless `StackOptions::limit_words` is set.
- `destroy_context(ctx)`, `shutdown()`: shutdown returns `busy` while contexts
  remain; after that it succeeds idempotently and later calls return `stopped`.
  The destructor cleans up remaining contexts.
- Context and runtime identities are non-recycled; exhaustion fails. A context's
  identity carries its pid number ([pids](terms.md#pids-and-references)). A
  context pointer is a borrow, not an identity. `lifetime()` gives a weak token
  that reports `alive() == false` before heap teardown.
- Lifecycle calls never throw and are silent. Status values:
  [abi.md](abi.md#runtime-services).

Teardown order: close scheduler records → destroy contexts → release code
registrations → destroy scheduler service → atom table last. Resolved function
handles keep code alive after runtime teardown, but their atom slot words pin no
spellings. Atom storage retires its global slots before freeing records; spelling
access through a surviving non-fixed atom Term reports `expired_context`.

## Process memory

Each context owns one `ProcessHeap`
([process_heap.hpp](../runtime/include/process_heap.hpp)); its layout, areas,
sizing, admission, roots and collection are specified in
[runtime-heap.md](runtime-heap.md). The host API:

- `allocate(words)` returns zeroed word storage. `reserve` gives a move-only
  reservation with explicit commit and automatic rollback. One reservation at a
  time per heap: build children first, reserve the parent last. Raw
  `allocate()` words must stay zero or hold complete objects
  ([word layout](runtime-heap.md#word-layout)).
- Zero, overflow and an exhausted budget are rejected before publishing:
  `out_of_memory` (allocation) or `limit_exceeded` (budget); generated code
  receives the exact status
  ([failure behavior](runtime-heap.md#failure-behavior)).
- `add(value)`/`copy_to` copy a graph of another process of the same runtime
  ([copying between heaps](runtime-heap.md#copying-between-heaps)).
- `used_words` counts allocated words, `capacity_words` the heap block and
  fragments, `off_heap_words` the off-heap buffers this process references, each
  once ([sizing and budget](runtime-heap.md#sizing-and-budget)).
- `verify()` walks the heap block and every fragment and checks each term slot
  points at an object start of the same process (tests and debugging;
  `corrupt_heap` otherwise).
- `collect(roots)` copies everything reachable from the process roots and the
  host's root words into a new heap block
  ([collection](runtime-heap.md#collection)). It runs only at a safe point,
  else `unsafe_point`
  ([roots and safe points](runtime-heap.md#roots-and-safe-points)); generated
  code collects at its own safepoints
  ([collection in generated code](runtime-heap.md#collection-in-generated-code)).
  Host Terms are unrooted one-word values; rebuild them from rewritten roots
  after collection. They have no lifetime or stale checks and cannot be read
  after collection or process exit. Factories and reservations keep their own
  lifetime tokens and still report `expired_context`.

## Code server and builtins

Each runtime owns one `CodeServer`
([code_server.hpp](../runtime/include/clause/runtime/code_server.hpp),
[callable.hpp](../runtime/include/clause/runtime/callable.hpp)):

```cpp
auto functions = std::make_unique<ModuleRegistry>();
auto added = functions->add("identity", 1,
    [](ProcessContext &, std::span<const Term> args) -> CallResult<Term> { return args.front(); });
auto loaded = context.code_server().load({"native_demo", CodeImage::linked(), std::move(functions)});
auto fn = context.code_server().resolve({.module = "native_demo", .function = "identity", .arity = 1});
```

- A `ModuleRegistry` maps exact name/arity (≤ 255) to one all-`Term` callable.
  `load` freezes and publishes it; duplicates or failures publish nothing.
- Registration also indexes generated modules' bindings in hash maps: module,
  record and fun descriptors to their bindings and definitions, and
  `Module:Function/Arity` atom words to export frames. Dynamic calls, `apply/3`,
  runtime `fun M:F/A` and descriptor lookups therefore take constant expected
  time in the number of modules and exports. The indexes are built inside the
  registration: a rejected module adds no entry, and they change only under the
  server's exclusive lock.
- `resolve` distinguishes missing module and missing export. `ResolvedFunction`
  pins the module image; `call` checks arity, arguments and results, and turns
  host exceptions into failures.
- Native bodies must be synchronous, non-blocking and must not retain the
  context or argument span.
- A bounded catalog of known deferred BIFs (`self/0`, `length/1`, `spawn/3`,
  `spawn_link/3`, `send/2`, `make_ref/0`, `garbage_collect/0`, `apply/3`
  (generated code calls `apply/2,3` through the dynamic call services instead,
  [funs](funs.md#dynamic-calls)), `tuple_size/1`, `+/2`) reports
  `not_implemented`; other unregistered names return `unknown_builtin` silently.
  This host path does not reach the production builtins.
- Production builtins live in the server's `BuiltinRegistry`, registered at
  runtime startup; generated code, dynamic calls and funs reach them through the
  [builtin bridge](builtins.md).
- `CodeServer::export_frame` finds the `FrameDescriptor` of an export by module,
  function atom and arity; `function_frame` adds the builtins for dynamic calls;
  `external_fun` interns the definitions of external funs built at run time.
- Modules are never unloaded; `CodeServer::unload` is deferred and dynamic
  loading is not supported.

## Persistent terms

One table per runtime (`memory/persistent_terms`), owned by `Runtime::Impl` and
reached through `ProcessContext::persistent_terms()`, keeps the terms of
`persistent_term` ([builtins](builtins.md#persistent-terms)).

- Each key and value is copied into an area of its own in the runtime's
  persistent term range (`memory/persistent_region`): one address reservation
  (64 GiB of address space on 64-bit hosts, 256 MiB on 32-bit ones) whose pages
  are committed as areas are allocated, so whether a word names a persistent
  term is one range comparison. Copies share only module literals, so every area
  stands alone; its binaries share their buffers.
- Entries are root words found by a hash of the key (the builtins use the term
  hash of `phash2`) and exact equality, compared in place.
- A read returns the stored word in place, as OTP's literal persistent terms:
  any process may hold it and send it (a copy into another process keeps the
  word); collections leave it alone, and admission accepts it like a literal.
  Message buffers and new persistent terms copy persistent words.
- **Reclamation.** A replaced or erased value's area retires under a new epoch.
  From then on every copy copies its words, and each process registered when it
  retired copies the retiring objects it holds into its own heap at an
  evacuating full sweep (`Copier` with the region, each object once, binary
  buffers held before the sweep): a running or queued process at the start of
  its next slice, a parked one at once, by the put or erase that retired it
  (`Executor::evacuate_parked`, under the executor lock). Once every such
  process has done so or ended, the area is freed. `info/0` counts only current
  entries.
- A shared lock serves readers on any number of workers; put and erase take it
  alone (copies are made before it). CTest `runtime_persistent_terms` covers
  values outliving their writer, in-place reads across collections and between
  processes, replaced and erased values held by readers, concurrent readers and
  writers, and reclamation (retiring copies, evacuation, freed areas and
  charges).

## Scheduler bookkeeping

`SchedulerService`
([scheduler.hpp](../runtime/include/clause/runtime/scheduler.hpp)) is a
host-facing record of process lifecycle; it runs no code. Programs run on the
executor ([processes](processes.md#executor)), which does not use it.

- `register_process(context)` once per context (same runtime); duplicates return
  `already_registered`. `remove_process(id)` retires a non-running record.
- `begin_dispatch` → running; `finish_dispatch` → runnable, waiting or exited
  (with reason). `set_suspended` toggles a flag on runnable/waiting records.
  Other transitions return `invalid_transition`.
- `request_shutdown()` closes registration, dispatch and suspension control;
  inspection and returns stay available for draining.
- `run` and `execute` report `not_implemented`.

## Threads

Scheduler workers ([workers](processes.md#workers)) run processes on several
threads of one runtime. Services they share are synchronized; everything else
stays confined to the thread running its process or guarded by the executor's
mutex.

- Atoms: `AtomStorage` guards its spelling map and owned records with a shared
  mutex. `existing` and `size` share it; `intern` first looks up under the shared
  lock, then takes the exclusive lock for a new spelling and checks again, so
  racing interns get one word. Publication uses the process-wide
  `detail::atom_table`; admission and spelling reads use its lock-free number
  index (`lookup` also checks the record's owner). Fixed atoms need no lock.
  Records stay unchanged until teardown retires their slots
  ([atoms](terms.md#atoms)).
- Code: `CodeServer` guards its modules and external fun definitions with a
  shared mutex. Lookups (`find_module`, `resolve`, `atom_word`,
  `record_definition`, `fun_definition`, `export_frame`, `function_frame`,
  `owns`) share it; `load` and building a new external fun definition take it
  exclusively, so concurrent registrations of one name publish one module (the
  others get `duplicate_module`) and racing `external_fun` calls get one
  definition. The builtin registry is filled at runtime startup, before any
  worker runs, and only read afterwards.
- Pins: modules are never removed while the runtime lives and the server is
  destroyed after every context, so definitions, frames and atom slots it
  returned stay valid for every invocation and every fun cell.
  `ResolvedFunction` and `find_module` handles also keep their module after the
  runtime is gone; their atom slots retain words, not spelling pins.
- Pid and port numbers: `IdentityNumbers` issues numbers under an exclusive
  lock and admits pid and port words under a shared one.
- Memory: the runtime-wide account (`RuntimeMemory`) charges and releases with
  atomic operations; a charge never takes the account past an optional limit.
- Other threads: the timer thread
  ([receive timeouts](processes.md#receive-timeouts)), the I/O thread and, on
  Windows, the reader thread of an `fd` input ([I/O thread](ports.md#io-thread))
  touch process state only through the executor's mutex.
- Linking: on Linux the runtime adds `-pthread` for its threads; on Windows it
  links Winsock (`ws2_32`, `mswsock`) for sockets.

## Standard output

`RuntimeOptions::standard_output`
([output.hpp](../runtime/include/clause/runtime/output.hpp)) receives
`erlang:display/1` and `io` output to standard output ([io](io.md)). The default
writes to process `stdout` through C stdio (buffered); a host sink returns
`false` to report a failed write.

`erlang:display/1` renders its argument in [display style](terms.md#printing),
writes the text and a newline in one write and returns `true`. Rendering limits
and rejected writes become infrastructure statuses (`resource_limit`,
`output_failure`, ...) in the checked channel, never Erlang exceptions.

## Program startup

`CLAUSE_main_v1` ([startup.hpp](../abi/include/clause/abi/startup.hpp),
`runtime/src/startup/`) runs a whole program for the generated `main`
([executables](executables.md)): it checks every descriptor's ABI, starts a
runtime, registers all modules before any entry code, builds argv in the entry
context, runs the entry on the executor and maps the result to the
[exit status](executables.md#exit-status). Reports go to stderr after stdout is
flushed; the context and runtime are torn down in order on every path.
`CLAUSE_halt_v1` implements `erlang:halt/0,1` (`abort` calls `std::abort`).

## Deferred services

These report one `[feature] notimpl` line ([features](features.md)) and change
no state:

| Boundary | Error |
| --- | --- |
| `TermFactory::function(FunctionIdentity)` | `TermError::not_implemented` |
| `SchedulerService::run` / `execute` | `SchedulerError::not_implemented` |
| `CodeServer::unload` | `CodeError::not_implemented` |
| `dispatch_builtin` on a catalogued BIF | `Status::not_implemented` |
