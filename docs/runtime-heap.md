# Process heap contract

Process heaps follow the classic ERTS design: one contiguous heap block per
process plus overflow fragments, self-describing cells, large binaries in shared
buffers off the heap, and a copying collector that runs only at safe points.
This note is the contract; [runtime.md](runtime.md#process-memory) summarizes
the host API.

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
  bignum (131,072 words on 32-bit) fit by static assertion, maps check it when
  built, and cons cells and fixed-size cells have no large count.
- **Cons cell**: two term words (head, tail) with no header. A list term points
  at the head. A head is never a header because no term has tag `00`.
- **Filler**: the all-zero word (kind `tuple`, count 0) is a one-word filler;
  kind `filler` with count n covers n further words. Reservations start zeroed,
  so reserved but unused words parse as filler. Nonempty tuples always have a
  nonzero count and `{}` is an immediate.

No cell needs alignment stronger than a word. `memory/heap_walk` parses an area
cell by cell and `ProcessHeap::verify` checks a whole heap. Cells hold only
words and bytes, except the off-heap binary's `std::shared_ptr` and the off-heap
map's tree pointer (below), so a cell moves by copying its words.

| Kind | Words after the header | Traced words |
| --- | --- | --- |
| cons (no header) | 2 words in total | head, tail |
| `tuple` | n element slots | all |
| `map` | 2n slots: keys in exact term order, each followed by its value; at most 32 entries | all |
| `tree_map` | entry count, root of the off-heap tree, off-heap map link: 3 words ([off-heap maps](#off-heap-maps)) | none on the heap; the tree's words |
| `native_record` | address of the runtime's `RecordDefinition`, then n field values in definition order | values |
| `fun_closure` | address of the runtime's `FunDefinition`, then n captured values ([funs](funs.md)) | values |
| `bignum` | sign word, then magnitude limbs, least significant first | none |
| `floating` | 8 bytes: 1 word (64-bit) or 2 words (32-bit) | none |
| `reference` | 8 bytes: the reference number ([pids and references](terms.md#pids-and-references)) | none |
| `heap_binary` | bit length, then data bytes rounded up to words (at most 64 bytes) | none |
| `refc_binary` | bit offset, bit length, the buffer's first byte (read by generated code), append end and limit, `std::shared_ptr` (2 words), off-heap link: 8 words | none |
| `sub_binary` | bit offset into the original, bit length, the original `heap_binary` or `refc_binary` (never a sub-binary): 3 words | the original |
| `filler` | n unused words | none |

The `map` count is in words (entries = count / 2). Pids and ports are
immediates, admitted against the runtime's issued numbers.

## Off-heap binaries

A binary larger than 64 bytes is an immutable buffer that floats outside every
process heap, shared by reference count (BEAM ProcBin and `Binary`).

- Its boxed `refc_binary` cell holds a `std::shared_ptr` to the buffer, a bit
  offset and a bit length. Slices of a large binary (and slices of those) are
  `sub_binary` cells naming the original cell, as ERTS sub-binaries do: a slice
  takes no buffer reference and keeps the original alive. Copying a cell to
  another process copies the `shared_ptr`
  ([copying between heaps](#copying-between-heaps)), never the bytes.
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
  keeps the cell size fixed at eight words after the header on both widths.

### Handle references

A handle reference is a reference that also holds one off-heap buffer, as an OTP
magic reference holds a resource; `atomics` arrays live in them
([atomics and counters](builtins.md#atomics-and-counters)).

- Its cell has kind `reference` and the `refc_binary` layout: eight words after
  the header, the 64-bit reference number in the offset and bit-length words
  (where a plain reference cell keeps it), then the `shared_ptr` and the link
  word. Every reference service reads only the number, so it compares, hashes,
  prints and encodes like a plain reference.
- It is listed, moved, swept, charged and copied exactly like an off-heap binary
  cell (`layout::off_heap_cell`); its buffer is freed when the last copy of the
  reference in any process dies.
- A process-wide registry maps the numbers of live handles to their buffers
  (weak references), so `binary_to_term/1` of an encoded handle rebuilds a
  handle sharing the buffer while it lives, and a plain reference after. Expired
  entries are dropped as the registry grows.

## Off-heap maps

A map with more than 32 entries (OTP's `MAP_SMALL_MAP_LIMIT`) is a 4-word
`tree_map` stub on the heap naming a process-private persistent B+ tree
(`terms/map_tree`). Smaller maps stay `map` cells.

- Leaves hold up to 32 key and value words in exact key order; branches hold up
  to 32 children with their first keys and entry counts, so lookup and access by
  position are O(log n). Key and value words point into the owner's heap: unlike
  binaries, the tree holds terms.
- An update copies the path to each changed leaf and shares every other node
  with the source map, which stays unchanged, so building a map key by key costs
  O(n log n) instead of O(n^2). Nodes are reference counted; nodes created by
  the same update carry its edit token and change in place until the stub is
  published. A failed update releases what it built.
- Each stub is linked into its process's off-heap map list. A collection copies
  a stub like any cell and, when it scans the copy, rewrites the key and value
  words of the stub's tree, visiting each shared node once per collection. The
  sweep then relinks moved stubs and releases the trees of dead ones; teardown
  releases all.
- Node words are charged to the process as off-heap words (with binaries: enough
  of them requests a collection) and to the runtime-wide account. Freed nodes
  are kept for reuse, up to 1,024 of each kind per process (about 1 MB at most),
  since a path-copying update allocates a node per level and a collection frees
  the old paths together.
- Copying a map to another heap (messages, `MessageBuffer`) builds a new tree in
  the destination from the translated entries.
- An owned update (`MapOperation::update_owned`) whose source the compiler
  proved unread afterwards takes over the source stub's tree reference and
  changes in place every node it reaches through unshared nodes, copying only
  shared ones. The source stub is left consumed (no tree, size 0); a failed
  owned update gives the tree back to it. The compiler marks an update owned
  only when the function built the source map itself (`M = <map expression or
  comprehension>` in the same clause body), reads `M` nowhere else and never
  captures it in a fun (`semantic/map_ownership`).

## Areas

- **Heap.** One block `[start, top, end)` per process with bump allocation. It
  is created by the process's first allocation, sized
  `max(min_heap_words, request)` so that request always fits (fragments start
  at the minimum, at most 233 words), and owned by that
  process alone. Generated code bump-allocates small tuples, lists, floats and
  closures in it directly through `FrameState::heap_top`/`heap_limit`:
  `HeapStorage::absorb` takes those words into the top and `used_words_` before
  any heap operation (reservation, admission, collection, verification, usage
  queries) and `expose` shows the free words again after one. Inline allocation
  never makes a fragment and stays off under a per-process budget.
- **Literal areas.** Each module's compound literals — strings and constant
  lists, tuples of constants, floats, bignum literals and constant binaries —
  are laid out by the compiler as heap cells in one read-only global
  (`ModuleDescriptor::literals`), registered runtime-wide
  (`detail::LiteralAreas`, a lock-free sorted snapshot) when the module is
  registered. Terms of every process may point into them: admission and
  `TermAccess::object` read cells through `HeapStorage::readable` (heap,
  fragments, then literals); the collector leaves literal words as they are
  (`owned` stays heap-only); copies between heaps and into message buffers keep
  pointing at them (a literal root needs no copy); the verifier accepts them.
  Literals hold only immediates (fixed atoms only) and other literals' words.
- **Fragments.** When a request does not fit and the heap may not move, it goes
  into the newest fragment if it fits there, else into a new fragment sized to
  fit (at least the minimum heap size) and chained to the process. The next
  collection merges fragments into the new heap block. A reservation lives in
  one area; rollback resets that area's top and drops a fragment (or the heap
  block) the reservation created. Allocation never moves the heap: overflow
  stays in fragments until the next safepoint or host collection
  ([collection in generated code](#collection-in-generated-code)).
- **Stack.** Generated frames live on one flat stack per process, apart from the
  heap (`ProcessStack`, [execution model](execution-model.md#implementation)).
  It has no cap by default; an optional per-process `StackOptions::limit_words`
  bounds it separately from the heap.
- **Off-heap list.** As above.
- **Old area.** Objects that survived two collections
  ([generations](#generations)); minor collections leave it in place, a full
  sweep copies it into the new block and frees it.

## Sizing and budget

- `min_heap_words` is the process's `min_heap_size`: 233 words by default (as
  ERTS), `--min-heap-size` for every process (ERTS `+hms`), `{min_heap_size, N}`
  in `spawn_opt` or `process_flag(min_heap_size, N)` for one; a setting is
  rounded up to a heap size and never below the runtime default, and
  `process_flag` takes effect at the next collection. Since no block is smaller,
  a process does not collect before it has allocated that many words.
- The heap starts at `min_heap_words` and grows along the
  ERTS size sequence: 12, 38, then each size is the sum of the previous two plus
  one up to 833,026 words, then 20% steps (`heap_size_at_least`).
- A collection's new block is the smallest such size that keeps the words it may
  receive below 75% of it: first all used words, since live data is not known
  before copying. A result less than 25% live is copied once more into the size
  its live data needs; failing to allocate that block keeps the larger one.
  Neither is smaller than `min_heap_words`. Both sizes count the process stack's
  words as live, as ERTS keeps the stack inside the heap block.
- With a budget set (below), a new block holds at most its live words plus half
  of the budget left after them and off-heap buffers (`block_limit`), never less
  than `min_heap_words`. The other half stays free for fragments and new
  off-heap buffers, so garbage allocated after a collection reaches the next
  safepoint as a trigger instead of exhausting the budget. The first copy is
  sized from all used words, so a block above the limit for the words that
  survived is copied once more into its policy size.
- There is no memory cap by default, per process or for the runtime: the heap
  grows until the host refuses memory (`out_of_memory`), while the sizing above
  keeps it near its live size. An optional per-process budget,
  `HeapOptions::limit_bytes` (default `UNLIMITED_HEAP_BYTES`), covers the heap
  block, fragments and the bytes of off-heap buffers this process references;
  exceeding it is `limit_exceeded`. During a collection the old and new blocks
  coexist; only the new block is checked against the budget, capped at the
  budget left after off-heap buffers. A buffer's charge returns when the process
  drops its last cell for it. The stack keeps its own optional cap,
  `StackOptions::limit_words`. Programs set both caps with `--max-heap` and
  `--max-stack` ([runtime options](executables.md#runtime-options)).

## Runtime memory limit

- An optional runtime-wide limit, `RuntimeOptions::memory_limit_bytes` (default
  `UNLIMITED_HEAP_BYTES`; programs set it with `--max-memory`), bounds the
  memory of all processes together: heap blocks, fragments, off-heap buffers and
  stack capacity. OTP has no such limit; it is closest to running the VM under
  an OS memory limit, but fails one process instead of the node.
- One account per runtime (`detail::RuntimeMemory`, shared by every heap storage
  and stack) is charged when a block, fragment, off-heap buffer or stack
  capacity is created and released when it is dropped; a buffer shared by
  several processes is charged once and released when its last reference dies.
  Teardown returns every charge of the process. `Runtime::memory_bytes()`
  reports the total.
- To each process the limit acts as a budget of the storage it owns plus what
  the limit leaves (`HeapStorage::budget`, `room`), so the sizing above keeps
  half of the free memory free after each collection, and a request beyond it is
  `limit_exceeded` (`resource_limit`) for the requesting process only; other
  processes keep running. Another process's garbage counts until that process
  collects.
- A collection's to-space is charged even past the limit, because it replaces
  the blocks it releases at the end of the same collection.
- The stack doubles while the limit allows it, then grows only by the frame
  being pushed.

## Admission

Pointers into a process heap are created only by the compiler and the runtime
inside that process, and always name an object start; there are no interior
pointers to detect. Admission is an ownership check for words handed back to a
process:

1. The address is word-aligned inside one of the process's areas, below its
   `top`: the heap block is checked first, then fragments sorted by address,
   then the literal areas ([areas](#areas)). Foreign and stale words fail here
   without any load.
2. A boxed word names a header of an admitted kind (not filler); a list word
   names a cons cell (a word that is not a header).

Accessors decode kind, count and payload from the header itself. `verify()`
remains the full check that every slot names an object start, for tests.

## Roots and safe points

`ProcessContext::visit_roots` enumerates every root word for the collector. At
a safe point nothing else holds heap words of the process:

| Owner | Root words | Notes |
| --- | --- | --- |
| Frame term slots | The first `roots` slots of every frame on the stack | Bottom frames have none; resume and handler indices are integers |
| Raw frame slots | None | Spilled native values; generated code keeps no heap word there at a safe point ([reload rule](#reload-rule)) |
| Registers | `x[0..live)` (`ProcessStack::keep_registers`) | A suspended entry's arguments; every push and pop clears `live` |
| Failure channel | Error payload (BEAM `fvalue`), `erlang:error/2,3` argument list, stack trace term | Rebound in place; captured trace frames are descriptor pointers into code |
| Trap state | The term words of a trapping builtin's `TrapState` ([portions](builtins.md#portions)) | Released when the builtin finishes or fails |
| Process dictionary | Every key and value | Until erased or the process ends |
| Mailbox | Every message in the signal inbox and the message queue, including `'EXIT'` and `'DOWN'` messages | Until a receive takes it; rewritten in place, so the receive cursor (a list position) and timeout deadline stay valid |
| Explicit roots | The span a host passes to `collect(roots)` | Read back after the call |
| Off-heap list | None | Links are swept and relinked, not traced |
| Off-heap maps | None directly: the key and value words of the tree of every stub the collection copies | Rewritten when the copied stub is scanned; the list is swept like the binary list |

No heap cell holds a pin. Atoms are immediates and the atom table is never
collected. Fun cells name code through their untraced `FunDefinition`, which
lives as long as the runtime; loaded modules are never unloaded, so neither funs
nor trace descriptors need a pin. Small immediates are not roots.

As in ERTS C code, a host `Term` is a raw tagged word valid until the next safe
point of its heap; it does not pin heap storage
([ownership](terms.md#ownership)). A `Term` is only valid inside its own
process; other processes may only read it.

The heap moves only at a safe point, and never while a reservation is open:

- an explicit host `collect()` while the context runs no generated code;
- a `collect()` while running generated code has declared a `SafePoint` scope,
  promising that it holds heap words only in the roots above. The runtime opens
  one only at the generated-code safepoints of the next section.

Any other request returns `unsafe_point` and changes nothing, not even the
failure channel of a running generated call. Allocation never moves the heap: a
request that does not fit creates a fragment.

## Message buffers

A `MessageBuffer`
([message_buffer.hpp](../runtime/src/memory/message_buffer.hpp)) keeps a term
outside every process: `copy_of` copies the graph from its process heap into a
`HeapStorage` of its own (its own lifetime token, the runtime's atoms and memory
account, off-heap binaries shared and held), with the same `GraphCopy` a send
uses; `copy_to` copies it into a receiver's heap. Immediates need no storage.
The buffer is never collected and is freed with its last owner. Messages to a
process running on another worker ([workers](processes.md#workers)), timer
messages, monitor tags and [persistent terms](runtime.md#persistent-terms) use
it, so they are never re-encoded.

## Collection in generated code

Generated code collects only at a few **safepoints** where every live term
already sits in a root. Everything else, including every allocating service, is
a **critical section** that never moves the heap.

### Triggers

A safepoint collects when the heap asks for it (`wants_collection()`);
otherwise it costs one check.

| Trigger | Condition at the safepoint | ERTS counterpart |
| --- | --- | --- |
| Heap full | Any fragment exists: an allocation did not fit the heap block since the last collection | Heap top reaches the heap end |
| Off-heap binary pressure | Off-heap words reach the virtual binary heap limit: 46,422 words at first, after each collection twice the surviving off-heap words or the survivors plus the live heap words, whichever is more (never less than 46,422), but at most the survivors plus half of the budget left free after the heap block | `bin_vheap_sz` / binary virtual heap |
| `erlang:garbage_collect/0,1,2` | A request of the builtin ([introspection](processes.md#introspection)), taken at the target's next safepoint | Explicit full sweep |

A safepoint runs a **minor collection** unless a full sweep is due: a
`garbage_collect` request, off-heap binary pressure, a heap budget, or an old
area without room for the mature words ([generations](#generations)). The
public `ProcessHeap::collect` is always a full sweep.

The new block is sized for the live words **plus the stack words in use** (ERTS
keeps the stack inside the heap block): a deep stack gets a larger heap, so a
long recursion collects in proportion to its allocation rather than rescanning
the whole stack every few hundred words.

### Safepoints

| Point | Where | Live outside frame term slots |
| --- | --- | --- |
| Function entry | In `CLAUSE_enter_v1` / `CLAUSE_tail_v1` (and so host invocation), before the callee frame is pushed; an inline entry takes this path whenever the heap wants a collection | The callee's arguments `x[0..arity)`, kept as roots (`keep_registers`) |
| Loop head | `CLAUSE_safepoint_v1(context)` at the head of every comprehension generator loop, called only while the heap wants a collection | Nothing |

Every Erlang loop is either recursion, which passes a function entry per step,
or a comprehension, which passes its loop head, so garbage between two
safepoints is bounded by straight-line code and single service results.

Not safepoints (critical sections, which keep allocating into fragments): every
other runtime service, including allocation, construction and matching services;
`CLAUSE_return_v1`; exception propagation; and message delivery. Services may
therefore hold raw heap words in C++ for their whole run, and their input arrays
and outputs need no reload.

### Waiting and suspended processes

A process that is not running is never collected: it is waiting in a receive,
queued after a yield or trap, or not yet started, and everything it holds is
already a root (its frames, the registers of the entry or continuation it will
resume at, trap state and its messages). Messages sent to it are copied into
fragments of its heap. Every delivery wakes a waiting process, and resuming it
repeats the entry of its continuation (the wait builtin, a trap continuation or
the function it yielded at), which is a function-entry safepoint: the first
thing a resumed process does is collect when its heap asks for it. A process
waiting in a selective receive that skips many messages therefore collects as
they arrive, as ERTS collects a process when it is next scheduled. The golden
`executables_mailbox_collection` covers hoarding, waiting with a timeout, deep
recursion under message load, and a consumer acknowledging 3,000 messages
within `--max-heap 65536`.

Allocation is not a safepoint (unlike BEAM's `test_heap`): that would need every
service input and every SSA term live across any allocation in a root, a reload
after each allocating service and a retry protocol in every service, while the
two safepoints above already bound the garbage.

### Reload rule

No SSA value (a value in a native register) holds a heap word across a
safepoint, and no native pointer crosses one at all (already an error in
`lower_frames`).

- `lower_frames` treats a loop-head safepoint call like the resume point of a
  call: it splits the block after the call and spills every value read after it
  that was computed before it. A **term value** (a load from a term slot or a
  register, a value stored into a term slot, or a PHI of such values) is stored
  after its definition into its existing term slot or a new term slot counted in
  the descriptor's `roots`, and reloaded before each use. Other words (small
  immediates, atoms, raw integers, flags) keep raw slots: a collection never
  changes them.
- Calls spill and reload the same way, terms into term slots, so the entry
  safepoint sees every live term of every caller.
- The frame base stays valid across a loop-head safepoint, because a collection
  rewrites stack words in place and never moves the stack; every transfer
  re-reads it in the body prologue as before.
- Optimization runs after `lower_frames` and cannot replace a reload by the
  older SSA value: the frame address is loaded from the context's `FrameState`,
  and the opaque safepoint call may write every slot through it
  ([prototype](#prototype)).

### Failure behavior

- A collection at a safepoint never records a failure. When its new block cannot
  be allocated (`out_of_memory`) the heap stays as it is and execution continues
  with fragments.
- **Memory exhaustion.** With no cap, memory runs out only when the host refuses
  a heap block, fragment, off-heap buffer or stack growth: `out_of_memory`. With
  an optional budget or [runtime-wide limit](#runtime-memory-limit) set, a
  request beyond it is `limit_exceeded`, reported as `resource_limit`. Both are
  infrastructure failures: no handler runs, the frames unwind to the bottom
  frame and a program prints
  `clau: runtime failure: entry call failed: <status>` and exits with status 70
  after flushing stdout and destroying the process
  ([executables](executables.md#exit-status), [differences](differences.md)).
  Because every collection keeps half of the budget left after its survivors
  free ([sizing](#sizing-and-budget), [triggers](#triggers)), a budget fails
  only when the live set no longer fits or when straight-line code between two
  safepoints allocates more than that half; garbage is collected first.

### Implementation

- `ProcessStack::safepoint(live)` asks `ProcessHeap::wants_collection()` (a
  fragment exists, off-heap words reached `binary_limit_words_`, or a
  collection was requested), keeps `x[0..live)` as roots, opens a `SafePoint`
  and collects; a failed collection is ignored. `enter` (and so `tail` and
  `invoke`) calls it with the callee's arity before pushing;
  `CLAUSE_safepoint_v1` calls it with 0.
- Comprehension lowering emits the loop-head check at the head of every
  generator loop (`lowering_comprehensions`).
- `lower_frames` splits each body after a safepoint call and spills crossing
  values as after a call. Arguments arrive in registers and are not stored on
  entry: an argument that crosses a call or safepoint is stored into its own
  argument slot after it arrives. `home()` keeps the argument slot or a
  same-block term slot when one holds the value; otherwise a term value
  (`term_value`: loaded
  from a term slot or register, stored into a term slot, or a PHI of those) gets
  a new term slot, which `place_slots` appends to the leading term slots before
  the raw slots, and any other value a raw slot.
- `collected_size` caps the block at `block_limit(live)`; `shrink` also runs
  when the block exceeds the limit of the surviving words; `collect` caps
  `binary_limit_words_` at the survivors plus half of the budget left free after
  the block.
- Coverage: the OTP goldens `executables_garbage_collection` (more than 64 MiB
  allocated with a small live set: tail loops, off-heap binaries, allocating
  comprehension filters, deep body recursion, a caught error payload; with
  capped runs under `--max-heap`, `--max-stack` and `--max-memory`) and
  `executables_heap_growth` (72 MB of retained binaries); `runtime_collection`
  `near_budget` and `shared_limit` check the budget and the runtime-wide limit.

### Prototype

[tests/prototypes/safepoint](../tests/prototypes/safepoint/) holds one
comprehension-style loop in post-`lower_frames` form (`loop.ll`): a term `Y`
computed before the loop is stored to a term slot and reloaded after the
loop-head safepoint. `python tests/prototypes/safepoint/run.py` compiles it for
`x86_64-pc-windows-msvc`, `aarch64-unknown-linux-gnu`, `i686-pc-windows-msvc`
and `armv7-unknown-linux-gnueabihf` at O0 and O2 and checks that a load of `Y`'s
frame word follows the safepoint call, so the optimizer never reuses the
register that held `Y`.

## Collection

A Cheney copy (`memory/heap_collect`), a full sweep or a minor collection
([generations](#generations)): allocate the new block first
(failure is `out_of_memory` and leaves the heap untouched), then mark host
`Term`s stale, copy the object behind every root word and scan the new block
left to right, copying the children of each copy. A moved boxed object's header
is replaced by a boxed pointer to its copy; a moved cons cell gets a zero head
and a tail pointing to its copy. Forwarding preserves sharing. Roots are
rewritten in place, the off-heap list is swept (copies relinked in list order,
dead cells destroyed), and the old block and fragments are freed. A heap that
was never allocated is not collected. `CollectionStats` reports words before,
live words, the new heap block, the merged fragments, stack slot capacity and
off-heap words.

## Generations

Generational collection, as ERTS does it, without its write barrier:

- **High-water mark.** After a collection every object in the new block has
  survived one; the block's top becomes the high-water mark. The next minor
  collection copies the young block and fragments, **promoting** objects below
  the mark into the old area and the rest into a new young block.
- **Old area.** One block that never moves between full sweeps; a minor
  collection neither copies nor scans it. Created by the first minor collection
  that promotes, sized twice the mature words. When it cannot take the mature
  words, the safepoint runs a full sweep instead, which leaves everything in
  the new block below its high-water mark and frees the old area.
- **Invariant.** An immutable object only points at objects older than itself,
  so promoting by address never leaves an old object pointing at a young one.
  The runtime's in-place writes keep it: binary loops and appendable binaries
  write integers, and an owned off-heap map update changes only nodes its tree
  holds alone, published under a new (young) stub. Debug builds check after
  every minor collection that no old object, nor an old stub's tree, points into
  the young block or a fragment.
- **Sweeps.** The off-heap binary and map lists keep cells of the old area,
  relink forwarded ones and release dead young ones; dead old cells wait for the
  next full sweep.
- **Young sizing.** A minor collection keeps the young block's size, as ERTS
  does, and at least an eighth of the old area (ERTS). Generated code collects
  only at safepoints, so allocation between two of them (the returns of a body
  recursion) may overflow far into fragments; when they hold a quarter of the
  block or more, the next block grows by them and is not shrunk. Otherwise a
  young block over 8,000 words less than 25% live is copied into three times its
  live words (ERTS's minor rule).
- **Off-heap limit.** After a minor collection the virtual binary heap limit
  counts the young live words only: the old area may hold dead words, which must
  not postpone the full sweep that frees dead old binaries and map trees.

## Copying between heaps

`ProcessHeap::add(value)`, equivalently `value.copy_to(heap)`, returns a term of
the destination heap (BEAM `size_object` and `copy_struct`):

- Immediates and atoms of the same runtime need no storage, and a term of the
  destination heap keeps its identity. A graph of another process of the same
  runtime is copied; another runtime's graph is `wrong_owner`, an expired source
  `expired_context`, a source handle older than its heap's last collection
  `stale_term`. Factories still refuse foreign inputs (`ProcessHeap::retain`),
  so only an explicit copy moves a graph.
- Copies are flat, as ERTS's default `copy_struct` (without its optional
  sharing-preserving variant): a subterm is copied once per reach, so `{T, T}`
  copies `T` twice, and a term whose sharing doubles at every level grows
  exponentially, as on OTP. One walk with an explicit stack (no recursion) sums
  the words of every reached object; the copy is one reservation of that sum,
  in the heap block or a fragment, filled by a second walk in the same order
  that reads each cell again and points each slot at its child's copy. No table
  maps source objects to copies, and only binary cells and map stubs keep their
  offsets.
- An off-heap binary's copy is a new cell sharing the buffer; the destination
  holds the buffer (charging its own off-heap words if it held none of it)
  before reserving, and lists the cell only after the reservation commits.
- The source is only read. A failure (`resource_limit` for the destination
  budget or runtime limit, `out_of_memory` for the host) drops the buffer holds
  and rolls the reservation back, so both heaps and every charge are as before.
  A copy owns no source storage: it survives the source's collection and
  teardown.

## Measurements

`runtime_heap_measurements` (full-mode CTest; numbers printed, not gated) builds
a 100,000-element list of `{Index, Float}` tuples through `TermFactory`, walks
it back through checked accessors, collects with the list as the only root and
walks the copy, and creates 1,000 contexts that each hold one small tuple. It
reports build and walk times, heap used and capacity words, side bytes (host
allocations beyond heap backing) and bytes and heap words per context.
