# Process heap contract

Process heaps follow the classic ERTS design. This note is the contract; plan
11 phase C (steps 8A–8I) delivered it, and later steps named below extend it.
[runtime.md](runtime.md#process-memory) summarizes the API.

## What phase C replaced

| Before phase C | Problem | Replacement |
| --- | --- | --- |
| A list of chunks that never move | Cells cannot be compacted or copied; capacity only grows | One contiguous heap block plus fragments, moved by a copying collector (8G, 8H) |
| Each cell is a node in a per-process `std::map` index | Heap words alone are not parseable; one host allocation and an O(log n) lookup per cell | Self-describing cells; admission by owned range and header (8C, 8D) |
| Every bitstring cell has a fixed 64-byte array and a `shared_ptr`, released through a destructor registry | Large cells for small data; nothing can move a cell or find its dead copies | Variable-size heap binaries and off-heap binary cells on a per-process off-heap list (8B) |
| Host `Term` pins the heap with `shared_ptr<HeapStorage>`; runtime-held values live only in `Term`s | Nothing a collector can find or rewrite | ERTS model: C++ holds raw words only between safe points; runtime-held values are process root words; host callers pass explicit roots to `collect()` (8E) |
| One heap buffer per generated root frame | No process stack to scan | One stack of root frames per process (8F) |
| No overflow area | Allocation either fits the budget or fails | Heap fragments while the heap must not move (8G) |

Generated code, its ABI and every observable program result stay unchanged.

## Word layout

A term is one target word (32 or 64 bits); encodings are in
[abi.md](abi.md#terms). Every heap area is a sequence of objects that a walker
parses from its first word:

- **Header word** (primary tag `00`): bits 2–6 hold the `BoxedKind`, bits 7 and
  up the number of words that follow the header. A boxed term points at its
  header. The count covers every prefix, payload and padding word, so the walker
  skips untraced payload without interpreting it. It has 25 bits on 32-bit
  targets (at most 33,554,431 words) and 57 on 64-bit ones
  (`BoxHeader::MAX_COUNT`): the tuple arity limit (2^24 - 1) and the largest
  bignum (131,072 words on 32-bit) fit by static assertion, maps check it
  when built, and cons cells and fixed-size cells have no large count.
- **Cons cell**: two term words (head, tail) with no header. A list term points
  at the head. A head is never a header because no term has tag `00`.
- **Filler**: the all-zero word (kind `tuple`, count 0) is a one-word filler;
  kind `filler` with count n covers n further words. Reservations start zeroed,
  so reserved but unused words parse as filler. Nonempty tuples always have a
  nonzero count and `{}` is an immediate.

No cell needs alignment stronger than a word. `memory/heap_walk` parses an area cell by cell and `ProcessHeap::verify`
checks a whole heap (8C). Cells hold only words and bytes,
except the off-heap binary's `std::shared_ptr` (below), so a cell moves by
copying its words.

| Kind | Words after the header | Traced words |
| --- | --- | --- |
| cons (no header) | 2 words in total | head, tail |
| `tuple` | n element slots | all |
| `map` | 2n slots: keys in exact term order, each followed by its value | all |
| `native_record` | address of the runtime's `RecordDefinition`, then n field values in definition order | values |
| `fun_closure` | address of the runtime's `FunDefinition`, then n captured values ([funs](funs.md)) | values |
| `bignum` | sign word, then magnitude limbs, least significant first | none |
| `floating` | 8 bytes: 1 word (64-bit) or 2 words (32-bit) | none |
| `reference` | 8 bytes: the reference number ([pids and references](terms.md#pids-and-references)) | none |
| `heap_binary` | bit length, then data bytes rounded up to words (at most 64 bytes) | none |
| `refc_binary` | bit offset, bit length, `std::shared_ptr` (2 words), off-heap link: 5 words | none |
| `filler` | n unused words | none |

The `map` count is in words (entries = count / 2). Pids are immediates, admitted against the runtime's issued
numbers. Kinds not yet admitted (external identities) follow the
same rules when they arrive: identities and descriptors are registry IDs in
untraced words, never owning C++ pointers.

## Off-heap binaries

A binary larger than 64 bytes is an immutable buffer that floats outside every
process heap, shared by reference count (BEAM ProcBin and `Binary`).

- Its boxed `refc_binary` cell holds a `std::shared_ptr` to the buffer, a bit
  offset and a bit length; slices of a large binary are new `refc_binary` cells
  sharing the buffer (ERTS sub-binaries are not used). Copying a cell to another
  process copies the `shared_ptr` ([copying between heaps](#copying-between-heaps)),
  never the bytes.
- Each cell is linked into its process's off-heap list through its link word.
  The list is the only way to find these cells' C++ state.
- Moving a cell copies its other words and move-constructs the `shared_ptr` into
  the new cell, so the old copy owns nothing. After a collection the list sweep
  relinks moved cells and destroys the `shared_ptr` of dead ones; teardown
  destroys all of them. A buffer is freed when its last cell, in any process,
  dies.
- Each process counts its cells per buffer (`HeapStorage::buffers_`); a buffer
  is charged once to every process that references it (its off-heap words, the
  virtual binary heap of ERTS) until that process's last cell for it dies, and
  once to the runtime-wide account from creation until the buffer is freed.
- `std::shared_ptr` is two pointers on every supported STL; a `static_assert`
  keeps the cell size fixed at five words after the header on both widths.

## Areas

- **Heap.** One block `[start, top, end)` per process with bump allocation
  (8G). It is created by the process's first allocation, sized
  `max(min_heap_words, request)` so that request always fits, and owned by
  that process alone.
- **Fragments.** When a request does not fit and the heap may not move, it
  goes into the newest fragment if it fits there, else into a new fragment
  sized to fit (at least the minimum heap size) and chained to the process. The
  next collection merges fragments into the new heap block. A reservation
  lives in one area; rollback resets that area's top and drops a fragment (or
  the heap block) the reservation created. Allocation never moves the heap:
  overflow stays in fragments until the next safepoint or host collection
  ([collection in generated code](#collection-in-generated-code)).
- **Stack.** Generated frames (BEAM Y registers) live on one flat stack per
  process, apart from the heap (`ProcessStack`, step 19). Each frame is a
  four-word header (caller's header offset, descriptor, resume, handler)
  followed by term slots (spilled terms included) and raw spill slots; frames
  link by offsets, so the
  block grows by doubling and moves. It has no cap by default; an optional
  per-process `StackOptions::limit_words` bounds it separately from the heap
  ([execution model](execution-model.md#implementation)).
- **Off-heap list.** As above.
- **Old heap.** None. Generational collection is deferred; immutable terms
  never point from older to newer data, so a high-water mark and an old heap
  can be added later without changing cells.

## Sizing and budget

- The heap starts at `min_heap_words` (233 words, as ERTS) and grows along the
  ERTS size sequence: 12, 38, then each size is the sum of the previous two
  plus one up to 833,026 words, then 20% steps (`heap_size_at_least`).
- A collection's new block is the smallest such size that keeps the words it
  may receive below 75% of it: first all used words, since live data is not
  known before copying. A result less than 25% live is copied once more into
  the size its live data needs (8H); failing to allocate that block keeps the
  larger one. Neither is smaller than `min_heap_words`. Both sizes count the
  process stack's words as live (step 26), as ERTS keeps the stack inside the
  heap block.
- With a budget set (below), a new block holds at most its live words plus half of the
  budget left after them and off-heap buffers (`block_limit`, step 27), never
  less than `min_heap_words`. The other half stays free for fragments and new
  off-heap buffers, so garbage allocated after a collection reaches the next
  safepoint as a trigger instead of exhausting the budget. The first copy is
  sized from all used words, so a block above the limit for the words that
  survived is copied once more into its policy size.
- There is no memory cap by default, per process or for the runtime: the heap
  grows until the host refuses memory (`out_of_memory`), while the sizing
  above keeps it near its live size. An optional per-process budget,
  `HeapOptions::limit_bytes` (default `UNLIMITED_HEAP_BYTES`), covers the heap
  block, fragments and the bytes of off-heap buffers this process references;
  exceeding it is `limit_exceeded`. During a collection the old and new
  blocks coexist; only the new block is checked against the budget, capped at
  the budget left after off-heap buffers. A buffer's charge returns when the
  process drops its last cell for it. The stack keeps its own optional cap,
  `StackOptions::limit_words`. Programs set both caps with `--max-heap` and
  `--max-stack` ([runtime options](executables.md#runtime-options)).

## Runtime memory limit

- An optional runtime-wide limit, `RuntimeOptions::memory_limit_bytes`
  (default `UNLIMITED_HEAP_BYTES`; programs set it with `--max-memory`), bounds
  the memory of all processes together: heap blocks, fragments, off-heap
  buffers and stack capacity (step 27A). OTP has no such limit; it is closest
  to running the VM under an OS memory limit, but fails one process instead of
  the node.
- One account per runtime (`detail::RuntimeMemory`, shared by every heap
  storage and stack) is charged when a block, fragment, off-heap buffer or
  stack capacity is created and released when it is dropped; a buffer shared by
  several processes is charged once and released when its last reference dies
  (step 28). Teardown returns every charge of the process.
  `Runtime::memory_bytes()` reports the total.
- To each process the limit acts as a budget of the storage it owns plus what
  the limit leaves (`HeapStorage::budget`, `room`), so the sizing above keeps
  half of the free memory free after each collection, and a request beyond it
  is `limit_exceeded` (`resource_limit`) for the requesting process only;
  other processes keep running. Another process's garbage counts until that
  process collects.
- A collection's to-space is charged even past the limit, because it replaces
  the blocks it releases at the end of the same collection.
- The stack doubles while the limit allows it, then grows only by the frame
  being pushed.

## Admission

Pointers into a process heap are created only by the compiler and the runtime
inside that process, and always name an object start; there are no interior
pointers to detect. Admission (8D) is an ownership check for words handed back
to a process:

1. The address is word-aligned inside one of the process's areas, below its
   `top`: the heap block is checked first, then fragments sorted by address.
   Foreign and stale words fail here without any load.
2. A boxed word names a header of an admitted kind (not filler); a list word
   names a cons cell (a word that is not a header).

Accessors decode kind, count and payload from the header itself. `verify()`
remains the full check that every slot names an object start, for tests.

## Roots and safe points

`ProcessContext::visit_roots` enumerates every root word for the collector
(step 23). At a safe point nothing else holds heap words of the process:

| Owner | Root words | Notes |
| --- | --- | --- |
| Frame term slots | The first `roots` slots of every frame on the stack (step 19) | Bottom frames have none; resume and handler indices are integers |
| Raw frame slots | None | Spilled native values; generated code keeps no heap word there at a safe point (step 24 reload rule) |
| Registers | `x[0..live)` (`ProcessStack::keep_registers`) | A suspended entry's arguments (step 43); every push and pop clears `live` |
| Failure channel | Error payload (BEAM `fvalue`), `erlang:error/2,3` argument list, stack trace term | Rebound in place; captured trace frames are descriptor pointers into code |
| Trap state | The term words of a trapping builtin's `TrapState` (step 43A) | Released when the builtin finishes or fails |
| Process dictionary | Every key and value (step 41C) | Until erased or the process ends |
| Mailbox | Every message in the signal inbox and the message queue (step 45), including `'EXIT'` and `'DOWN'` messages | Until a receive takes it; rewritten in place, so the receive cursor (a list position) and timeout deadline stay valid |
| Explicit roots | The span a host passes to `collect(roots)` (8E) | Read back after the call |
| Off-heap list | None | Links are swept and relinked, not traced |

No heap cell holds a pin. Atoms are immediates and the atom table is never
collected. Fun cells name code through their untraced `FunDefinition`, which
lives as long as the runtime; loaded modules are never unloaded, so neither
funs nor trace descriptors need a pin. Small immediates are not
roots.

As in ERTS C code, a host `Term` is a raw tagged word valid until the next safe
point of its heap. It does not pin heap storage; it keeps a weak context
lifetime token and the heap's collection count, so use after teardown reports
`expired_context` and use after a later collection reports a stale-term error.
A `Term` is only valid inside its own process; other processes may only read
it.

The heap moves only at a safe point, and never while a reservation is open:

- an explicit host `collect()` while the context runs no generated code;
- a `collect()` while running generated code has declared a `SafePoint`
  scope, promising that it holds heap words only in the roots above. The
  runtime opens one only at the generated-code safepoints of the next section.

Any other request returns `unsafe_point` and changes nothing, not even the
failure channel of a running generated call. Allocation never moves the heap:
a request that does not fit creates a fragment.

## Message buffers

Plan 11 step 41F. A `MessageBuffer`
([message_buffer.hpp](../runtime/src/memory/message_buffer.hpp)) keeps a term
outside every process: `copy_of` copies the graph from its process heap into a
`HeapStorage` of its own (its own lifetime token, the runtime's atoms and
memory account, off-heap binaries shared and held), with the same `GraphCopy`
a send uses; `copy_to` copies it into a receiver's heap. Immediates need no
storage. The buffer is never collected and is freed with its last owner.
Timer messages use it, so they are never re-encoded.

## Collection in generated code

Decision of plan 11 step 24 (2026-10-06), implemented in step 26
([implementation](#implementation)). Generated
code collects only at a few **safepoints** where every live term already sits
in a root. Everything else, including every allocating service, is a
**critical section** that never moves the heap.

### Triggers

A safepoint collects when the heap asks for it; otherwise it costs one check.

| Trigger | Condition at the safepoint | ERTS counterpart |
| --- | --- | --- |
| Heap full | Any fragment exists: an allocation did not fit the heap block since the last collection | Heap top reaches the heap end |
| Off-heap binary pressure | Off-heap words reach the virtual binary heap limit: 46,422 words at first, after each collection twice the surviving off-heap words, never less than that, but at most the survivors plus half of the budget left free after the heap block (step 27) | `bin_vheap_sz` / binary virtual heap |
| `erlang:garbage_collect/0` | Always; arrives with the builtin families (steps 36-37) as a forced safepoint | Explicit full sweep |

The new block is sized for the live words **plus the stack words in use**
(ERTS keeps the stack inside the heap block): a deep stack gets a larger
heap, so a long recursion collects in proportion to its allocation rather than
rescanning the whole stack every few hundred words.

### Safepoints

| Point | Where | Live outside frame term slots |
| --- | --- | --- |
| Function entry | In `CLAUSE_enter_v1` / `CLAUSE_tail_v1` (and so host invocation), before the callee frame is pushed | The callee's arguments `x[0..arity)`, kept as roots (`keep_registers`) |
| Loop head | A call of `CLAUSE_safepoint_v1(context)` at the head of every comprehension generator loop | Nothing |

Every Erlang loop is either recursion, which passes a function entry per
step, or a comprehension, which passes its loop head, so garbage between two
safepoints is bounded by straight-line code and single service results.

Not safepoints (critical sections, which keep allocating into fragments):
every other runtime service, including allocation, construction and matching
services; `CLAUSE_return_v1`; exception propagation; and later message
delivery (step 45). Services may therefore hold raw heap words in C++ for their
whole run, and their input arrays and outputs need no reload.

### Waiting and suspended processes

Plan step 51. A process that is not running is never collected: it is waiting
in a receive, queued after a yield or trap, or not yet started, and everything
it holds is already a root (its frames, the registers of the entry or
continuation it will resume at, trap state and its messages). Messages sent to
it are copied into fragments of its heap. Every delivery wakes a waiting
process, and resuming it repeats the entry of its continuation (the wait
builtin, a trap continuation or the function it yielded at), which is a
function-entry safepoint: the first thing a resumed process does is collect
when its heap asks for it. A process waiting in a selective receive that
skips many messages therefore collects as they arrive, as ERTS collects a
process when it is next scheduled. `executables_mailbox_collection` checks
hoarding, waiting with a timeout and deep recursion under message load, and
that a consumer acknowledging 3,000 messages stays within `--max-heap 65536`.

Rejected: allocation as a safepoint (BEAM `test_heap`). It would need every
service input and every SSA term live across any allocation in a root, a reload
after each allocating service and a retry protocol in every service, while the
two safepoints above already bound the garbage.

### Reload rule

No SSA value (a value in a native register) holds a heap word across a
safepoint, and no native pointer crosses one at all (already an error in
`lower_frames`).

- `lower_frames` treats a loop-head safepoint call like the resume point of a
  call: it splits the block after the call and spills every value read after
  it that was computed before it. A **term value** (a load from a term slot or
  a register, a value stored into a term slot, or a PHI of such values) is
  stored after its definition into its existing term slot or a new term slot
  counted in the descriptor's `roots`, and reloaded before each use. Other
  words (small immediates, atoms, raw integers, flags) keep raw slots: a
  collection never changes them.
- Calls spill and reload the same way, terms into term slots, so the entry
  safepoint sees every live term of every caller.
- The frame base stays valid across a loop-head safepoint, because a
  collection rewrites stack words in place and never moves the stack; every
  transfer re-reads it in the body prologue as before.
- Optimization runs after `lower_frames` and cannot replace a reload by the
  older SSA value: the frame address comes from `CLAUSE_frame_v1`, so the
  safepoint call may write every slot (prototype below).

### Failure behavior

- A collection at a safepoint never records a failure. When its new block
  cannot be allocated (`out_of_memory`) the heap stays as it is and execution
  continues with fragments.
- **Memory exhaustion** (step 27). With no cap, memory runs out only when the
  host refuses a heap block, fragment, off-heap buffer or stack growth:
  `out_of_memory`. With an optional budget or
  [runtime-wide limit](#runtime-memory-limit) set, a request beyond it is
  `limit_exceeded`, reported as `resource_limit`. Both are infrastructure
  failures: no handler runs, the frames unwind to the bottom frame and a
  program prints `clau: runtime failure: entry call failed: <status>`
  and exits with status 70 after flushing stdout and destroying the process
  ([executables](executables.md#exit-status), [differences](differences.md)).
  Because every collection keeps half of the budget left after its survivors
  free ([sizing](#sizing-and-budget), [triggers](#triggers)), a budget fails
  only when the live set no longer fits or when straight-line code between two
  safepoints allocates more than that half; garbage is collected first.

### Implementation

Step 26 (2026-10-06):

- `ProcessStack::safepoint(live)` asks `ProcessHeap::wants_collection()` (a
  fragment exists, or off-heap words reached `binary_limit_words_`), keeps
  `x[0..live)` as roots, opens a `SafePoint` and collects; a failed collection
  is ignored. `enter` (and so `tail` and `invoke`) calls it with the callee's
  arity before pushing; `CLAUSE_safepoint_v1` calls it with 0.
- Comprehension lowering emits `CLAUSE_safepoint_v1` at the head of every
  generator loop (`lowering_comprehensions`).
- `lower_frames` splits each body after a safepoint call and spills crossing
  values as after a call. `home()` keeps the argument slot or a same-block
  term slot when one holds the value; otherwise a term value (`term_value`:
  loaded from a term slot or register, stored into a term slot, or a PHI of
  those) gets a new term slot, which `place_slots` appends to the leading
  term slots before the raw slots, and any other value a raw slot.
- Golden `executables_garbage_collection` (OTP-generated) allocates more than
  64 MiB with a small live set: a tail loop building a 400-word
  string per step, 9,000 off-heap 8 KiB binaries, a comprehension whose filter
  allocates per element, 20,000-deep body recursion keeping a nested term
  (tuple, list, binary) per frame, and an error payload caught after unwinding
  allocating frames and kept through a long allocating loop.

Step 27 (2026-10-06):

- `collected_size` caps the block at `block_limit(live)`; `shrink` also runs
  when the block exceeds the limit of the surviving words; `collect` caps
  `binary_limit_words_` at the survivors plus half of the budget left free
  after the block.
- Before, a block could take the whole remaining budget (sized from used
  words including garbage) and the virtual binary heap could exceed it once
  survivors passed half the budget, so allocations failed with garbage still
  uncollected: under the then-default 64 MiB budget, a 64-bit run retaining
  700 64 KiB binaries while dropping four per step failed at 68% live; with
  the caps, 1,010 (99%) fit.
- The default 64 MiB heap budget and 2^24-word stack budget were removed (user
  direction): both are opt-in per process
  (`Runtime::create_context(HeapOptions, StackOptions)`), uncapped by default.
- Golden `executables_heap_growth` keeps 1,100 binaries of 65,540 bytes
  (72 MB) while dropping four per step and prints the count as OTP does.
  `runtime_collection` `near_budget` checks both caps with a 10,000-word
  budget (heap block and off-heap buffers).

Step 27A (2026-10-06):

- Runtime-wide limit and `--max-heap`, `--max-stack`, `--max-memory`
  ([runtime memory limit](#runtime-memory-limit)). Authored golden runs prove
  bounded memory again: `garbage_collection` `churn` and `binaries` under a
  1 MiB runtime limit, `comprehension` under a 1 MiB heap cap and `payload`
  under a 16 MiB runtime limit (each allocates more than 64 MiB);
  `tail_calls` loops under a 4 KiB stack and 64 KiB heap cap; `deep` under
  1 MiB and `deep_recursion` `build` under a 64 KiB stack fail with
  `resource_limit`. `deep` itself needs about 100 MiB at O0 (live frames keep
  stale terms and are about 130 words each), so it has no capped success run.
  `runtime_collection` `shared_limit`: two processes under a 40,000-word
  limit; the holder's block stops at 28,000 words, the other collects 20
  rounds of garbage near the limit, then fails a 16,000-word list with
  `resource_limit` while the holder keeps allocating, and teardown returns
  every charge.

### Prototype

[tests/prototypes/safepoint](../tests/prototypes/safepoint/) holds one
comprehension-style loop in post-`lower_frames` form (`loop.ll`): a term `Y`
computed before the loop is stored to a term slot and reloaded after the
loop-head safepoint. `python tests/prototypes/safepoint/run.py` compiles it for
`x86_64-pc-windows-msvc`, `aarch64-unknown-linux-gnu` (64-bit words),
`i686-pc-windows-msvc` and `armv7-unknown-linux-gnueabihf` (32-bit words) at
O0 and O2 and checks that a load of `Y`'s frame word follows the safepoint
call. All eight pass with clang 23.1.2. At O2 (i686) the loop keeps the
slot reload and never reuses the register that held `Y`:

```text
LBB0_2:                     # loop head
    pushl  %edi
    calll  _clause_safepoint_v1
    pushl  20(%esi)         # cursor reloaded from its term slot
    ...
    pushl  24(%esi)         # accumulator
    pushl  28(%esi)         # Y reloaded from its term slot
    calll  _make
```

## Collection

A full-sweep Cheney copy (8H, `memory/heap_collect`): allocate the new block
first (failure is `out_of_memory` and leaves the heap untouched), then mark
host `Term`s stale, copy the object behind every root word and scan the new
block left to right, copying the children of each copy. A moved boxed object's
header is replaced by a boxed pointer to its copy; a moved cons cell gets a zero
head and a tail pointing to its copy. Forwarding preserves sharing. Roots are
rewritten in place, the off-heap list is swept (copies relinked in list order,
dead cells destroyed), and the old block and fragments are freed. A heap that
was never allocated is not collected. `CollectionStats` reports words before,
live words, the new heap block, the merged fragments, stack slot capacity and
off-heap words.

## Copying between heaps

`ProcessHeap::add(value)`, equivalently `value.copy_to(heap)`, returns a term
of the destination heap (step 28, BEAM `size_object` and `copy_struct`):

- Immediates and atoms of the same runtime need no storage, and a term of the
  destination heap keeps its identity. A graph of another process of the same
  runtime is copied; another runtime's graph is `wrong_owner`, an expired
  source `expired_context`, a source handle older than its heap's last
  collection `stale_term`. Factories still refuse foreign inputs
  (`ProcessHeap::retain`), so only an explicit copy moves a graph.
- One walk with an explicit stack (no recursion) finds every distinct object
  reachable from the value, keyed by address, so internal sharing survives:
  `{T, T}` copies `T` once, unlike ERTS's default `copy_struct`, which
  flattens sharing. The copy is one reservation of the sum of their words, in
  the heap block or a fragment, filled in walk order with pointers rewritten
  to the copies.
- An off-heap binary's copy is a new cell sharing the buffer; the destination
  holds the buffer (charging its own off-heap words if it held none of it)
  before reserving, and lists the cell only after the reservation commits.
- The source is only read. A failure (`resource_limit` for the destination
  budget or runtime limit, `out_of_memory` for the host) drops the buffer
  holds and rolls the reservation back, so both heaps and every charge are as
  before. A copy owns no source storage: it survives the source's collection
  and teardown.

## Measurements

`runtime_heap_measurements` (full-mode CTest; numbers printed, not gated) builds
a 100,000-element list of `{Index, Float}` tuples through `TermFactory`, walks
it back through checked accessors, and creates 1,000 contexts that each hold
one small tuple. Since 8I it also collects with the list as the only root and
walks the copy. Side bytes are host allocations beyond heap backing (an object
index before 8D, the fragment chain since 8G).

| Revision | Build | Kernel build / walk | Heap used / capacity words | Side bytes | Bytes per context | Heap words per context |
| --- | --- | --- | --- | --- | --- | --- |
| `bb09359` (chunk list, object index) | Windows x64 Debug, clang-cl | 264 / 81 ms | 700,000 / 704,512 | 24,002,256 (about 80 per cell) | 66,217 | 8,192 |
| 8D (chunk list, owned range) | Windows x64 Debug, clang-cl | 185 / 147 ms | 700,000 / 704,512 | 3,440 | 66,057 | 8,192 |
| 8G (233-word heap, about 3,000 fragments) | Windows x64 Debug, clang-cl | 219 / 174 ms | 700,000 / 706,223 | 163,878 | 2,377 | 233 |
| 8I, before collection | Windows x64 Debug, clang-cl | 216 / 173 ms | 700,000 / 706,223 | 163,878 | 2,377 | 233 |
| 8I, after one collection | Windows x64 Debug, clang-cl | collect 56 ms / walk 72 ms | 700,000 / 999,631 | 0 | — | — |

Against the `bb09359` baseline: per-cell side metadata is gone (24 MB to none
once collected), a context needs 2.4 KB and 233 heap words instead of 66 KB and
8,192 words, building is about 20% faster, and walking is slower until a
collection merges the fragments (fragment admission is a binary search over
about 3,000 ranges); after one the walk takes 72 ms. A 700,000-word live set
collects in about 56 ms into a 999,631-word block, the ERTS size that keeps it
below 75%.
