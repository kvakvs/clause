# Runtime

`clause_runtime` is an LLVM-free C++23 library. It owns contexts, process heaps,
atoms, loaded modules and lifecycle bookkeeping, and runs Erlang processes
on scheduler workers ([processes](processes.md)). All APIs are
project-internal; host calls must be serialized per runtime and not overlap
a program run, whose workers synchronize among themselves
([threads](#threads)).

## Linking

Link exactly one runtime built for the target through `Clause::generated_program`:

```cmake
add_executable(harness harness.cpp)
target_link_libraries(harness PRIVATE Clause::generated_program)
```

It brings the archive, ABI/runtime headers and C++23, but not LLVM.
[link_consumer.cpp](../tests/runtime/link_consumer.cpp) shows a full lifecycle.

## Lifecycle

[runtime.hpp](../runtime/include/clause/runtime/runtime.hpp):

- `Runtime::start(options)` → `std::expected<std::unique_ptr<Runtime>, Status>`.
  Defaults: current ABI version and native term width, `max_atoms` 2^20
  (at most 2^26; programs set it with `--max-atoms`,
  [runtime options](executables.md#runtime-options)). The number of contexts
  is not limited. `process_heap` and `process_stack` are the options of
  contexts created by `create_context()`; `memory_limit_bytes` is the optional
  [runtime-wide limit](runtime-heap.md#runtime-memory-limit), reported by
  `memory_bytes()`. All are uncapped by default.
- `create_context()` or `create_context(heap_options, stack_options)` →
  borrowed `ProcessContext*`,
  stable until destroyed. Heap defaults: 233-word minimum heap
  (`min_heap_words`) and no memory cap (`limit_bytes` =
  `UNLIMITED_HEAP_BYTES`; a set budget is a word multiple at least the minimum
  heap). The stack is uncapped too unless `StackOptions::limit_words` is set.
- `destroy_context(ctx)`, `shutdown()`: shutdown returns `busy` while contexts
  remain; after that it succeeds idempotently and later calls return `stopped`.
  The destructor cleans up remaining contexts.
- Context and runtime identities are non-recycled; exhaustion fails. A
  context's identity carries its pid number ([pids](terms.md#pids-and-references)). A context
  pointer is a borrow, not an identity. `lifetime()` gives a weak token that
  reports `alive() == false` before heap teardown.
- Lifecycle calls never throw and are silent. Status values: [abi.md](abi.md#runtime-services).

Teardown order: close scheduler records → destroy contexts → release code
registrations → destroy scheduler service → atom table last. Resolved function
handles keep code and atom spellings alive after runtime teardown but never a
process.

## Process memory

Each context owns one `ProcessHeap`: a single heap block created by its first
allocation and sized `max(min_heap_words, request)`, plus a chain of heap
fragments owned by the same process. Words move only when the host calls
`collect()` at a safe point.

The heap follows the classic ERTS design; [runtime-heap.md](runtime-heap.md)
is its contract (layout, areas, sizing, admission, roots, collection).

- `allocate(words)` returns zeroed word storage. `reserve` gives a move-only
  reservation with explicit commit and automatic rollback. One reservation at a
  time per heap: build children first, reserve the parent last.
- Bump allocation fills the heap block; a request that does not fit goes to the
  newest fragment, else to a new fragment sized `max(min_heap_words, request)`
  (capped by the remaining budget). Words are word-aligned only. Rollback
  resets the area top, drops a fragment (or the heap block) created by the
  reservation and restores accounting exactly.
- Rejects zero, overflow and exhausted budget before
  publishing. Errors: `out_of_memory` (allocation) or `limit_exceeded` (budget);
  generated code receives the exact status.
- Binaries over 64 bytes live in shared buffers outside the heap. Each heap cell
  that refers to one holds a `std::shared_ptr` and joins the process's off-heap
  list when published; teardown walks the list and drops those references
  ([off-heap binaries](runtime-heap.md#off-heap-binaries)).
- `add(value)`/`copy_to` copy a graph of another process of the same runtime,
  keeping its sharing and sharing off-heap buffers; a failed copy changes
  nothing ([copying between heaps](runtime-heap.md#copying-between-heaps)).
- `used_words` counts allocated words; `capacity_words` counts heap block and fragments;
  `off_heap_words` counts the buffers this process references, each once. Backing plus off-heap
  words share the optional `limit_bytes` budget; a collection keeps half of the
  budget left after survivors free, so exhausting it means the live data no
  longer fits ([failure behavior](runtime-heap.md#failure-behavior)).
- Every used word parses as a header-led object, a cons cell or filler
  ([word layout](runtime-heap.md#word-layout)); reserved words start zeroed.
  Raw `allocate()` words must stay zero or hold complete objects.
  `verify()` walks the heap block and every fragment and checks each term slot points at an object
  start of the same process (tests and debugging; `corrupt_heap` otherwise).
- `collect(roots)` copies everything reachable from the process roots and the
  host's root words into a new heap block, frees the old block and fragments,
  releases dead off-heap binaries and rewrites the roots
  ([collection](runtime-heap.md#collection)). Generated code collects at
  function entries and comprehension loop heads when `wants_collection()`
  ([collection in generated code](runtime-heap.md#collection-in-generated-code)).
  It runs only at a safe point (no
  generated code running outside a `SafePoint` scope, no open reservation),
  else `unsafe_point`; the [root inventory](runtime-heap.md#roots-and-safe-points)
  lists what it rewrites; failure
  to allocate the new block is `out_of_memory` with nothing changed.

## Code server and builtins

Each runtime owns one `CodeServer` ([code_server.hpp](../runtime/include/clause/runtime/code_server.hpp),
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
- Registration also indexes generated modules' bindings in hash maps (plan
  step 62A): module, record and fun descriptors to their bindings and
  definitions, and `Module:Function/Arity` atom words to export frames. Dynamic
  calls, `apply/3`, runtime `fun M:F/A` and descriptor lookups therefore take
  constant expected time in the number of modules and exports (about 80 ns per
  export lookup in Debug with 10 or 400 modules). The indexes are built inside
  the registration: a rejected module adds no entry, and they change only under
  the server's exclusive lock.
- `resolve` distinguishes missing module and missing export. `ResolvedFunction`
  pins the module image; `call` checks arity, arguments and results, and turns
  host exceptions into failures.
- Native bodies must be synchronous, non-blocking and must not retain the
  context or argument span.
- A bounded catalog of known deferred BIFs (`self/0`, `length/1`, `spawn/3`,
  `spawn_link/3`, `send/2`, `make_ref/0`, `garbage_collect/0`, `apply/3` (generated
  code calls `apply/2,3` through the dynamic call services instead, [funs](funs.md#dynamic-calls)),
  `tuple_size/1`, `+/2`) reports `not_implemented`; other unregistered names
  return `unknown_builtin` silently. This host path does not reach the
  production builtins.
- Production builtins live in the server's `BuiltinRegistry`, registered at
  runtime startup; generated code, dynamic calls and funs reach them through
  the [builtin bridge](builtins.md).
- `CodeServer::export_frame` finds the `FrameDescriptor` of an export by module,
  function atom and arity; `function_frame` adds the builtins for dynamic
  calls; `external_fun` interns the definitions of external funs built at run
  time.
- `CodeServer::unload` is deferred; dynamic loading is not supported.

## Persistent terms

Plan 11 step 41M1 (`memory/persistent_terms`). One table per runtime, owned by
`Runtime::Impl` and reached through `ProcessContext::persistent_terms()`, keeps
the terms of `persistent_term` (builtins: step 41M2).

- A key and its value are each copied into a `MessageBuffer`
  ([message buffers](runtime-heap.md#message-buffers)) outside every process,
  so they outlive the process that stored them and no collection moves them.
- Entries are found by a hash of the key (the builtins use the term hash of
  `phash2`) and exact equality, compared in place in the buffer
  (`MessageBuffer::value`).
- A read copies the value into the reader's heap (decided 2026-10-14). OTP
  instead hands out a reference into a literal area and copies only on
  replace or erase through a global collection; Clause's admission accepts
  only words of the process's own areas, so in-place literals would need a new
  shared term region. Copying keeps every process heap self-contained: a
  replaced or erased value is freed when the last reader still copying it
  drops its buffer reference, and no global collection is needed. Reads cost
  the size of the value ([differences](differences.md)).
- A shared lock serves readers on any number of workers; put and erase take it
  alone and never copy while holding it. CTest `runtime_persistent_terms`
  covers values outliving their writer, replaced values held by readers,
  concurrent readers and writers, and released charges.

## Scheduler bookkeeping

`SchedulerService` ([scheduler.hpp](../runtime/include/clause/runtime/scheduler.hpp))
records process lifecycle only; it runs no code.

- `register_process(context)` once per context (same runtime); duplicates return
  `already_registered`. `remove_process(id)` retires a non-running record.
- `begin_dispatch` → running; `finish_dispatch` → runnable, waiting or exited
  (with reason). `set_suspended` toggles a flag on runnable/waiting records.
  Other transitions return `invalid_transition`.
- `request_shutdown()` closes registration, dispatch and suspension control;
  inspection and returns stay available for draining.
- `run` and `execute` report `not_implemented`.

Design sketches for workers and processes live in
[runtime/design/](../runtime/design/) and `runtime/include/{scheduler,process}.hpp`.
Messages are implemented ([processes](processes.md#messages)): a send copies
the message into the receiver's heap and appends it to its signal inbox
(`runtime/include/mailbox.hpp`).

## Threads

Scheduler workers (plan step 56, [workers](processes.md#workers)) run
processes on several threads of one runtime. Services they share are
synchronized; everything else stays confined to the thread running its
process or guarded by the executor's mutex.

- Atoms (step 54): `AtomStorage` guards both indexes with a shared mutex.
  Lookups (`lookup`, `boolean`, `size`) share it; `intern` looks up under the
  shared lock and only a new spelling takes the exclusive lock, checks again
  and publishes the entry, so racing interns of one spelling get one word.
  Words stay stable: an entry is never changed or removed before teardown.
- Code (step 55): `CodeServer` guards its modules and external fun
  definitions with a shared mutex. Lookups (`find_module`, `resolve`,
  `atom_word`, `record_definition`, `fun_definition`, `export_frame`,
  `function_frame`, `owns`) share it; `load` and building a new external fun
  definition take it exclusively, so concurrent registrations of one name
  publish one module (the others get `duplicate_module`) and racing
  `external_fun` calls get one definition. The builtin registry is filled at
  runtime startup, before any worker runs, and only read afterwards.
- Pins: modules are never removed while the runtime lives (unloading is
  deferred) and the server is destroyed after every context, so definitions,
  frames and atom slots it returned stay valid for every invocation and every
  fun cell. `ResolvedFunction` and `find_module` handles also keep their
  module after the runtime is gone.
- Pid numbers (step 56): `ProcessNumbers` issues numbers under an exclusive
  lock and admits pid words under a shared one.
- Memory (step 56): the runtime-wide account (`RuntimeMemory`) charges and
  releases with atomic operations; a charge never takes the account past an
  optional limit.
- Port I/O (steps 57C–57F): the I/O service's reader, writer and watcher
  threads and the socket thread ([ports](ports.md#io-thread)) touch process
  state only through the executor's mutex.
- Linking: on Linux the runtime adds `-pthread` for its threads; on Windows
  it links Winsock (`ws2_32`, `mswsock`) for sockets.

## Standard output

`RuntimeOptions::standard_output` ([output.hpp](../runtime/include/clause/runtime/output.hpp))
receives `erlang:display/1` and later `standard_io` bytes. The default writes to
process `stdout` through C stdio (buffered); a host sink returns `false` to
report a failed write.

`erlang:display/1` renders its argument in [display style](terms.md#printing),
writes the text and a newline in one write and returns `true`. Rendering limits
and rejected writes become infrastructure statuses (`resource_limit`,
`output_failure`, ...) in the checked channel, never Erlang exceptions.

## Program startup

`CLAUSE_main_v1` ([startup.hpp](../abi/include/clause/abi/startup.hpp),
`runtime/src/startup/`) runs a whole program for the generated `main`: it
checks every descriptor's ABI, starts a default runtime, registers all modules
before any entry code, builds argv in the entry context, calls the entry and
maps the result to the exit status of [executables](executables.md#exit-status).
Reports go to stderr after stdout is flushed; the context and runtime are torn
down in order on every path. `CLAUSE_halt_v1` implements `erlang:halt/0,1`
(`abort` calls `std::abort`).

## Deferred services

These report one `[feature] notimpl` line ([features](features.md)) and change no
state:

| Boundary | Error |
| --- | --- |
| `TermFactory::port` (no ports yet, plan step 53), `reference(ReferenceIdentity)` and `function(FunctionIdentity)` | `TermError::not_implemented` |
| `SchedulerService::run` / `execute` | `SchedulerError::not_implemented` |
| `CodeServer::unload` | `CodeError::not_implemented` |
| `dispatch_builtin` on a catalogued BIF | `Status::not_implemented` |
