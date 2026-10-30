# Runtime term API — implemented containers and future families

Status: small integers, atoms/booleans, tuples and proper/improper lists are
implemented with checked contextual admission and one-word host Terms.
See [containers](../../docs/terms.md#tuples-lists-strings), [runtime atoms](../../docs/terms.md#atoms),
[process memory](../../docs/runtime.md#process-memory), and [generated roots](../../docs/abi.md#root-scopes).
The table below also reserves future numeric/map/binary/identity APIs; a declaration
alone does not establish implementation. Graphs copy between heaps of one runtime
(`copy_to`); collection runs at safepoints or on host request
([heap contract](../../docs/runtime-heap.md)).

Host [Term](../include/clause/runtime/terms.hpp) is exactly one `Word` and trivially
copyable; its only field is `value_`. It carries no atom pin, heap pointer, lifetime
token or collection count. Private layouts use target Word slots that a walker
parses from headers; contextual admission checks owned range and header shape
in Debug builds (`CHECKED_ADMISSION`); Release builds trust the word.

## Class boundary and immediate ABI

`Term` is a final host value API with private storage and process-bound
`TermFactory` construction. Compound words are trusted pointers, as ERTS Eterms
are: accessors decode cells directly and child handles wrap stored slot words.
`TermAccess::validate` checks representation only, not ownership or liveness.
Unchecked mirrors (`tuple_element_unchecked`, `head_unchecked`, ...; OPTM.3)
skip the kind check for callers that proved it and cannot fail; Debug builds
abort on a term of another kind. The default zero word is invalid. Registered roots are rewritten by moving GC;
unrooted heap Terms are valid only until the next collection or process exit.

The implemented term representation retains [v1 names](../../abi/include/clause/abi/v1.hpp)
with checked C++ integer helpers in [term.hpp](../../abi/include/clause/abi/term.hpp).
A generated function uses the native free-function calling convention and returns
an unsigned pointer-width term, accepting a forward-declared live project context
and a borrowed term-array pointer. Arity belongs to the resolved identity; a zero-arity array may be null.
The context propagates unchanged through direct calls. Call contract revision 2
requires [checked failure propagation](../../docs/abi.md#failure-channel-revision-2) before
using a returned word. C++ `Term`, STL values,
`std::expected` and exceptions never cross that generated-function boundary. Host
services use C++ APIs; no C linkage or C-compatible header surface is maintained.
There is no BEAM/FFI compatibility promise or public heap ABI. Step 9 implements
[runtime lifecycle](../../docs/runtime.md#lifecycle) independently of term services.

Tags are numerical low bits, decoded with masks/shifts rather than C++ bitfields
or inactive union members. Primary bits 0–1 reserve header=0, list=1, boxed=2 and
secondary=3. Bits 2–3 then select pid=0, port=1, tertiary=2 or small integer=3.
Tertiary bits 4–5 reserve atom=0, catch=1, empty tuple=2 and nil=3. Small integer
encoding is `(unsigned(value) << 4) | 0xf`, after checking the exact
signed range `[-2^(word_bits-5), 2^(word_bits-5)-1]`. Negative decoding explicitly
reconstructs the signed payload without implementation-defined unsigned-to-signed
conversion or signed right shift. Only private constructors encode published heap starts.

Both 32-bit and 64-bit codecs are tested on every host. Cross compilation takes
width/alignment from the LLVM target layout, never host `sizeof(Word)`. The native
C++ ABI declarations, runtime layouts and LLVM term/signature types agree on size and alignment.
Other native runtime toolchains still need their own full layout validation.

## Explicit process-heap layout

Superseded by the classic heap contract in
[docs/runtime-heap.md](../../docs/runtime-heap.md) (plan 11 phase C); the
reservations below remain until steps 8B–8D replace them.

`Word` is the runtime target's unsigned pointer-width type, aligned to 4 or 8 bytes.
`TermTag`, `BoxHeader` and host `Term` are each one word. The private header
reserves low two bits 00, five kind bits at bits 2–6, and a content-word count
starting at bit 7.
That count excludes the header and includes all remaining prefix, trailing payload
and allocation padding. Tuple construction initializes its header before publication; other boxed families below remain reserved.
Cons cells have no header; they contain exactly a head term and a tail term.

| Private prefix | Fixed size | Trailing storage / future tracing |
|---|---|---|
| `BignumCell` | Native C++ layout | Owned Boost value; no term slots; may need alignment stronger than a word |
| `FloatCell` | One word + 8 bytes | IEEE binary64 bytes, no traced slots |
| `ExternalIdentityCell` | 2 words + numbers + 1 | Creation and 2 to 5 numbers untraced, the node atom last and traced |
| `ConsCell` | 2 words | Head and tail traced |
| `TupleCell` | 1 word | Arity consecutive term slots |
| `MapCell` | 1 word | Key/value term pairs |
| `HeapBinaryCell` | 2 words | Bit length, then untraced data bytes rounded up to words (at most 64 bytes) |
| `RefcBinaryCell` | 6 words | Bit offset/length, `shared_ptr` to an off-heap buffer, off-heap list link; moved by its relocation hook |
| `ExternalFunctionCell` | 4 words | Module/name terms traced; arity untraced |
| `ClosureCell` | 3 words | Untraced callable registry ID and count, followed by traced capture slots |
| `NativeRecordCell` | 2 words | Runtime `RecordDefinition` address followed by traced field slots |

All variable payloads follow fixed prefixes as separately allocated storage, without
flexible arrays or fake `[1]` members. Future allocation must check count arithmetic,
word rounding and budgets, respect each prefix's alignment, and establish the C++
lifetimes of both prefixes and trailing elements before access. Native C++ members
need explicit construction/destruction and cannot be serialized or moved by blind
byte copying. No constructors perform writes into unallocated trailing storage.

The collector traces only term slots, rewrites relocated pointers, and never
interprets numeric bytes, registry IDs or the off-heap `shared_ptr` as terms.
Raw heap pointers do not survive a collection except as rewritten roots. Runtime
IDs remain stable while referenced, and code references require pinning.

## Coverage and construction

The categories follow the [OTP data-type reference](https://www.erlang.org/doc/system/data_types.html).
Native records are a distinct, experimental OTP 29 category; traditional records
remain tuples. Booleans are atoms and strings can be integer lists; neither needs
a separate storage kind. Binaries are bitstrings with a bit count divisible by eight.

| Category                             | Creation                                                         | Inspection / extraction                                                                          | Functional changes                                               |
| ------------------------------------ | ---------------------------------------------------------------- | ------------------------------------------------------------------------------------------------ | ---------------------------------------------------------------- |
| Integer, including bignum            | `integer`, `integer_decimal`                                     | `is_integer`, checked `integer_value`, lossless `integer_decimal`                                | Construct a replacement                                          |
| Float                                | `floating`                                                       | `is_float`, `float_value`                                                                        | Construct a replacement                                          |
| Atom / boolean                       | `atom`, `boolean`                                                | `is_atom`, `is_boolean`, `atom_utf8`, `atom_id`, `boolean_value`                                 | Construct a replacement                                          |
| Nil / cons / proper or improper list | `nil`, `cons`, `list`                                            | `is_nil`, `is_cons`, `is_list`, `is_proper_list`, `head`, `tail`, `list_length`, `list_elements` | `prepend`, `append`, `with_list_element`                         |
| Tuple                                | `tuple`                                                          | `is_tuple`, `tuple_size`, `tuple_element`, `tuple_elements`                                      | `with_tuple_element`                                             |
| Map                                  | `map`                                                            | `is_map`, `map_size`, `map_contains`, `map_find`, `map_entries`                                  | `with_map_entry`, `with_existing_map_entry`, `without_map_entry` |
| Bitstring / binary                   | `bitstring`, `binary`                                            | `is_bitstring`, `is_binary`, `bit_size`, copied bytes                                            | `bit_slice`, `concat_bits`                                       |
| Pid / port / reference               | Wrap issued identity; `make_reference` creates a fresh reference | Corresponding predicate and opaque identity accessor                                             | Identities are immutable                                         |
| Function                             | `external_function`, `closure`, `function`                       | `is_function`, arity predicate, `function_arity`, opaque identity                                | Construct a replacement closure                                  |
| Native record                        | `native_record`                                                  | Category/descriptor predicates, descriptor and named fields                                      | `with_record_field`                                              |

`ProcessContext` and `ProcessIdentity` are forward-declared here and sketched in
[process.hpp](../include/process.hpp). Other identity classes, closure and record descriptors
remain forward declarations for their owning subsystems. Their complete types are
required before calling corresponding `expected<T, ...>` accessors. None of these
declarations represents an implemented process, port, module or scheduler API.

Runtime services issue process/port/reference identities; wrapping them does not
spawn a process, open a port or forge an identity. The module service issues closure
and native-record descriptors; callers cannot supply executable addresses.
External funs retain module/name/arity for later resolution and need not resolve
at construction. Closure descriptors validate capture count and owner; function
identities retain captures. Invocation and scheduler integration are outside this API.
Distribution and serialization, including imports of remote identities, are deferred.

## Values, ownership and errors

The following contracts apply to admitted atoms and containers; future categories
must satisfy equivalent ownership rules before admission. Contextual admission
checks storage liveness, readable ranges, cell shape, and atom/pid/port ownership.
Ordinary Term access trusts the admitted word; it has no per-handle lifetime check.

- Factories bind to one process context. Returned Terms are unrooted words;
  copying copies only the word, assigning rebinds only that C++ handle.
  The invalid default slot must be initialized before use. A Term is trivially
  copyable and safe to destroy without accessing its backing.
- Input spans and strings are borrowed only for the duration of a call. Extraction
  returns owned text/bytes, copied opaque identities or unrooted child Terms.
  Heap borrows and unrooted heap Terms do not survive movement or collection.
- Composite construction and updates accept terms from the same process context.
  Foreign compound inputs return `wrong_owner`; immediates are owner-independent
  and atoms are shared within one runtime. `copy_to`/`ProcessHeap::add` retain
  values readable by the destination (its heap or literals) and copy other
  graphs under a same-runtime caller contract. Ordinary C++ handle copies
  never transfer process ownership.
  Runtime-issued identities/descriptors must belong to the same runtime instance.
- Heap Terms held past the next collection or process exit read moved or freed
  memory: access is undefined, with no `stale_term` error. Factories, reservations
  and message buffers keep their own lifetime tokens and still report
  `expired_context`. Non-fixed atom spelling access reports `expired_context`
  after runtime teardown; fixed atom records are immortal. Terms stay on their
  process execution thread; another process may only read them while stable.
- Semantic failures use `TermResult<T>`. Wrong accessors return `wrong_type`;
  narrowing/index/slice errors return `out_of_range`. Invalid atoms, decimal text
  and bit encodings are rejected. Allocation/traversal budgets return
  `resource_limit`; later unimplemented operations return `not_implemented`.
  There are no success-shaped stub definitions in this sketch.
- Future host APIs without a nonthrowing contract may throw `std::bad_alloc`;
  no general `noexcept` promise is made. The future ABI adapter must catch and
  translate host exceptions. Term errors are not automatically Erlang exceptions;
  the BIF/compiler boundary chooses the appropriate Erlang failure behavior.
- `exactly_equal` compares admitted values structurally within explicit work limits.
  Later numeric and identity families must extend that shared dispatch. Map keys use exact equality (integer `1` and float `1.0` are distinct).
  There is no pointer-based equality operator or exposed hashing policy.

## Heap ownership, copying and collection

`ProcessContext` owns one `ProcessHeap` as an explicit member, and `TermFactory`
allocates there. The heap's `add(value)` and `value.copy_to(destination)` are the
same operation (step 28): immediates and same-runtime atoms transfer as they
are, a term readable by the destination keeps its identity, and a graph of another
process of the same runtime is copied flat, sharing literal areas and off-heap
binary buffers ([copying between heaps](../../docs/runtime-heap.md#copying-between-heaps)).
Factories accept only inputs `retain` admits, so foreign graphs there stay
`wrong_owner`. Copying a future pid/reference/fun preserves identity, not liveness.

Both heaps must belong to the same runtime and be live; this is a caller contract,
not a compound-copy ownership check. Source cells are read through trusted pointers,
with literals recognized by the destination runtime's registry. The source must
remain stable until copying returns; cross-worker messaging uses buffers
([message buffers](../../docs/runtime-heap.md#message-buffers)). Invalid source
lifetimes are undefined, not detected errors. Root atoms, pids and ports are
re-admitted by the destination. An expired destination reports `expired_context`;
destination budget or runtime-limit exhaustion reports `resource_limit` and host
allocation failure `out_of_memory`, with both heaps and every charge unchanged.
A copied graph survives the source's collection and teardown; remote serialization
stays outside this API.

`ProcessHeap::collect(roots)` is the explicit GC boundary: at a safe point it
copies the live graph and rewrites roots, otherwise it returns
`HeapError::unsafe_point` ([collection](../../docs/runtime-heap.md#collection)).
Raw `allocate()` spans are runtime-internal construction borrows: publish/root
the completed cell before any collection; partially initialized cells must not
be scanned. Saved continuation, mailbox and receive-candidate roots join before
generated code may collect.

## Operation details to review

- Updates are **persistent**: each returns a new term; existing aliases retain
  their values. Private sharing/copy-on-write is permitted only when unobservable.
  Builders and mutable references are not part of this proposal.
- C++ indices are zero-based. An Erlang `element/2` or `setelement/3` adapter must
  validate/convert the language's one-based index at its boundary.
- `cons(head, tail)` accepts any tail and represents improper lists. `is_list`
  tests nil/cons like the Erlang BIF; `is_proper_list` traverses to verify the final
  tail is nil (returns false for nonlists). `prepend` accepts nil/cons, including
  improper lists. `append` adds **one element** at the end of a proper list and
  may copy its spine. Length, bulk extraction and replacement require proper
  lists; nonlist receivers return `wrong_type`, improper tails `improper_list`.
  Traversals must be iterative and bounded, without exposing cyclic term creation.
- Tuple replacement preserves arity; an empty tuple is valid. List/tuple bulk
  extraction can be expensive and is subject to resource budgets.
- Map construction consumes entries in order, with the last duplicate key winning.
  Insert replaces or adds, update-existing reports `missing_key`, removal of an
  absent key succeeds unchanged. Lookup distinguishes missing from any stored
  term. Enumeration order is unspecified and must not expose the backing container.
- Decimal integer input accepts an optional sign and at least one ASCII digit,
  without whitespace, separators or a size-limited host conversion. Output is
  canonical decimal (no leading plus/zeros, zero is `0`). Floats require finite
  values; integer access never silently coerces a float. Atoms validate UTF-8,
  preserve spelling without Unicode normalization, and enforce OTP-compatible
  length limits plus runtime AtomStorage budgets. New names receive sequential
  process-wide, never-reused IDs wrapped as atom Terms; existing names retain
  their IDs.
  The startup cap defaults to 2^20 entries and cannot exceed the hard limit of 2^26.
- Bitstrings use exactly `ceil(bit_count / 8)` bytes with MSB-first significant
  bits; unused low bits in the last byte must be zero on input and output. Empty
  bitstrings/binaries are valid. Slices use bit offsets/counts and checked bounds.
  `binary_bytes` rejects non-byte-sized values; no storage-alignment promise leaks.
- Native-record construction requires all fields in registered descriptor order;
  defaults are supplied by lowering before construction. Named access/update
  takes an atom; an unknown field reports `unknown_field`. Descriptor identity,
  visibility and compatibility must follow the selected OTP 29 contract when
  implemented, not be approximated by a tuple tag.

## Example flow (illustrative; not executable)

With `TermFactory terms(context)`, create `terms.integer(42)` and
`terms.atom("answer")`; check each `TermResult` before extracting its value.
Construct a list from an array/span of terms, then use `list.append(value)` to
grow it. Construct a tuple and call `tuple.with_tuple_element(0, replacement)`;
construct a map and call `map.with_map_entry(key, replacement)`. Each operation
produces a separate result, and the original list/tuple/map remains unchanged.
Call `is_integer()` before a bounded `integer_value()` when convenient; the
accessor remains checked even if the predicate was omitted. Bignums can instead
be extracted losslessly through `integer_decimal()`.

## Review decisions and later validation

Review the explicit heap prefixes/slot format and tracing table, the unified
public `Term` versus typed wrapper classes, persistent update
names, process confinement/root lifetime, opaque identity ownership, explicit
errors with host allocation exceptions, and whether bulk extraction is sufficient
before adding iterators. Immediate tag allocation is fixed by ABI v1; GC/root machinery, heap services
and runtime bridging remain implementation decisions.

Continue implementing host services in small steps under `runtime/src/terms/` and
move each implemented declaration into `runtime/include/clause/runtime/`.
Heap structs now reside privately under `runtime/src/terms/`. Define all opaque
dependencies and review their lifetimes before enabling identity/descriptor APIs.
Keep unsupported capabilities explicit; a declaration does not expand the compiler
subset. Future tests must cover all predicates, bignum/narrowing boundaries,
improper tails, empty containers, alias preservation, map exact-key semantics,
bit edges, descriptor validation, wrong owners, roots across GC and failure cleanup.
Add cross-heap graph-copy coverage, source-exit independence, partial-copy failures,
mailbox/cursor roots and collection-safe-point rejection.
Validate prefix/array alignment, allocation overflow, scanner coverage and layout
assertions on 32-bit and 64-bit target builds before enabling heap code generation.
Step 7 compiles all prefix assertions on native macOS arm64 and tests the complete
64-way tag truth table, immediate integer boundaries/overflow for both widths,
and target-derived LLVM layouts/signatures. These checks do not establish heap
allocation, ownership, collection or native foreign-platform runtime correctness.
