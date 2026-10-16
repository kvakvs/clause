# Term representations

Admitted kinds: atoms/booleans, arbitrary integers, finite binary64 floats,
tuples, proper/improper lists and strings, maps, bitstrings and ordinary tuple
records; native records ([native records](native-records.md)); funs
([function values](funs.md)); local pids, ports and references
([below](#pids-and-references), [ports](ports.md#identity)). Word encodings are
in [abi.md](abi.md#terms).

## Ownership

- Compound values live in their process heap ([heap contract](runtime-heap.md))
  or in a module's read-only literal area ([areas](runtime-heap.md#areas)).
  Process pointers always name object starts. Admission checks the owned range
  and the header shape, rejecting foreign and stale words
  ([admission](runtime-heap.md#admission)); kind and extent are decoded from
  the header.
- A host `Term` for a heap value is a raw tagged word, valid until its heap's
  next collection ([roots](runtime-heap.md#roots-and-safe-points)); it does not
  pin heap storage. After context teardown, access returns `expired_context`;
  after a later collection, `stale_term`. Terms are always safe to destroy. The
  teardown check reads the context's lifetime token without locking it: a
  context and its terms are used by one thread at a time.
- Construction validates children, reserves, initializes, then publishes in one
  step; failure rolls back backing and counters.
- `Term::from_word(word)` admits only owner-independent immediates;
  `Term::from_word(word, context)` also admits atoms and heap terms of that
  context. Same-heap handoff keeps identity. `copy_to`/`ProcessHeap::add` copy a
  graph of another process of the same runtime with its sharing; term factories
  refuse foreign inputs with `wrong_owner`
  ([copying between heaps](runtime-heap.md#copying-between-heaps)).
- Cells live until a collection finds them unreachable, or until heap teardown
  ([collection](runtime-heap.md#collection)).

## Atoms

- Per-runtime `AtomStorage`, lazily interned by exact UTF-8 spelling, no
  normalization or eviction. `RuntimeOptions::max_atoms` 1..2^26, default 2^20;
  module/export names count. Existing spellings succeed at capacity.
- Up to 255 Unicode scalars; empty, NUL and supplementary characters allowed;
  malformed/overlong/surrogate UTF-8 rejected.
- Payloads come from a process-wide counter so a foreign runtime's atom word is
  detectable. 32-bit processes have 2^26 lifetime identities in total.
- Admitting an atom word reads a lock-free index by atom number (the fixed atoms
  directly, then chunks of 4,096 entries published once under the interning
  lock); numbers past 2^24 fall back to the locked table.
- Host atom Terms pin the spelling, surviving runtime teardown. Moving an atom
  between runtimes means interning `atom_utf8()` in the destination.
- Booleans are the atoms `true`/`false`. The fixed atoms `false`, `true`,
  `undefined`, `ok` and `error` (`abi::v1::fixed_atom_spellings`) have the words
  of atom numbers 0–4 in every runtime, entered with the storage, found without
  the lock and counted neither by `size()` nor toward `max_atoms`; generated
  code uses them as constants. Every other atom word is runtime-private.
- Compound literals of constants (strings, lists and tuples of immediates and
  other literals, floats, bignums) are cells of the module literal area, built
  at compile time and shared ([literal areas](runtime-heap.md#areas)); integer
  literals in the small range are immediates.
- Other atom literals load from the module's atom cache
  (`ModuleDescriptor::atom_cache`: the claiming runtime's identity, then one
  word per slot), which the first runtime to register the module fills; a
  context whose `FrameState::runtime` matches the claim reads it without a call,
  any other calls `CLAUSE_atom_v3`. The claim is given back when the runtime's
  code server releases the module.
- Interning and lookup are safe from concurrent scheduler workers
  ([threads](runtime.md#threads)); a spelling keeps one word.

## Integers

- Values in the target's 28/60-bit payload are immediates; larger ones are
  immutable sign/magnitude cells. Zero and small values always normalize to
  immediates. No host-width narrowing of literals.
- At `-O2`, operands inference proves small skip their tag tests, and a proven
  result range skips the overflow check and fallback too
  ([proofs](specialization.md#proofs)).
- Generated `+`, `-`, `*` try an inline fast path on two immediates, otherwise
  call the runtime service: `+` and `-` add or subtract the tagged words
  directly (`a + (b ^ 15)`) and fall back when the word overflows, which is
  exactly when the result leaves the small range; `*` computes in double
  width with explicit bounds. So do `band`, `bor`, `bxor`, `bsr`, `div`, `rem`
  and comparisons of two small integers
  ([inline forms](abi.md#runtime-services)).
- `div` truncates toward zero; `rem` takes the dividend's sign; bitwise ops use
  infinite two's complement; negative shift counts reverse direction; huge right
  shifts saturate to 0 or -1.
- Errors: wrong operands and zero divisor → `badarith`; `abs/1` → `badarg`.
- Limits: as ERTS, a magnitude of at most `BIG_ARITY_MAX` words: 4,194,240 bits
  on 64-bit targets (65,535 words), 4,194,272 on 32-bit (131,071 words); decimal
  text follows (1,262,593 and 1,262,602 digits). A larger arithmetic result
  raises `error:system_limit` in a body (`ValueOutcome::system_limit`,
  [ABI](abi.md#failure-channel-revision-2)) and fails a guard; an integer
  segment extracting a larger value does not match. The compiler rejects a
  literal past 4,194,240 bits (`illegal integer`, as OTP's scanner) and a
  constant pattern past it (`illegal pattern`).

## Floats

- IEEE binary64 bits pass to the runtime as eight network-order bytes; NaN and
  infinity are rejected. No fast-math.
- `+ - *` stay exact on two integers; any float operand uses binary64. `/`
  always converts both. Nonfinite results and zero divisors → `badarith`.
- `float/1` rounds to nearest-even; `round/1` ties away from zero; `trunc`,
  `floor`, `ceil` return arbitrary integers. Bad operands → `badarg`.
- Exact equality distinguishes `1` from `1.0` and `0.0` from `-0.0`; numeric
  comparison compares the float's exact integer part and fraction, never
  rounding the integer. `min/max` return the first operand on ties.

## Tuples, lists, strings

- Tuple: arity header + fields. Cons: head + tail words. `{}` and `[]` are
  immediates. Strings are lists of code points. Lists have no length cap beyond
  memory (an optional heap budget included). Tuples hold up to 16,777,215
  elements (`MAX_TUPLE_ARITY`, OTP's `MAX_ARITYVAL`); constructors report a
  larger one as `resource_limit`; builtins (`make_tuple`, `list_to_tuple/1`)
  raise `badarg`.
- Services: `hd`, `tl`, `length`, `tuple_size`, `size`, one-based `element`.

## Maps

- Immutable tables sorted by exact key order. Duplicate construction keys keep
  the last value. Construction sorts the keys (O(n log n) comparisons; already
  ascending keys are only checked). Up to 32 entries a map is one heap cell and
  an update inserts by binary search into a copy; above that it is a B+ tree
  of heap cells updated by path copying in O(log n)
  ([map trees](runtime-heap.md#map-trees)). No size or work cap beyond
  memory, as in OTP. Integer and float keys differ (also `0.0` vs `-0.0`, also
  nested).
- `K := V` updates require the key; `K => V` inserts or replaces. Updates stage
  a new table and publish once.
- Body errors: `{badmap, M}`, `{badkey, K}`; guards reject instead.
- Services: `is_map`, `map_size`, `map_get`, `is_map_key`, construction, update.

## Bitstrings

- Packed MSB-first with exact bit length, zeroed padding. Up to 64 bytes live
  inline in a heap binary sized to the data; larger values use a shared
  immutable buffer outside the heap, and slices are sub-binary cells of their
  original ([off-heap binaries](runtime-heap.md#off-heap-binaries)). There is
  no size cap beyond an optional process heap budget; integer segments are
  written without building an integer as wide as the segment.
- Construction stages all segments before publishing. Integer segments truncate;
  native endianness comes from the target data layout. An integer segment of a
  small value and a width of at most 64 bits is written from its machine word
  (whole bytes at once on a byte boundary); others take the multiprecision path.
  Generated code builds small constant-width constructions inline
  ([frames and transfers](abi.md#frames-and-transfers)).
- Appends, as OTP's writable binaries: a construction starting with a whole
  binary (`<<Acc/binary, ...>>`) builds its result in a buffer with spare room
  (twice its size, at least 256 bytes) whose off-heap cell in the creating
  process records its bits in use and capacity (zero in every other cell,
  copies included). A later append whose accumulator view ends exactly there
  writes the new bytes into the spare room, which no view covers, and returns a
  longer view of the same buffer: a sub-binary of the buffer's own off-heap
  cell, which may reach past that cell's bits. Any other accumulator (an older
  view, another process's, an unaligned one, a full buffer) is copied into a new
  doubled buffer. Other views of the buffer never change.
- Float segments: widths 16/32/64; construction may encode infinity, but
  matching rejects infinite/NaN fields. Zero-width float matches extract `0.0`.
- UTF-8/16/32 segments validate scalars, surrogates and truncation.
- Matching advances an explicit bit cursor only on success; `:all` as an
  explicit size is invalid.
- Services: `is_binary`, `is_bitstring`, `bit_size`, `byte_size` (rounds up),
  `size` (rounds down), `binary_part/2,3`. Errors → `badarg`.

## Records

- Ordinary records expand to tuples `{Tag, Fields...}`. Declarations must
  precede use; duplicates, unknown fields, forward/self references and invalid
  wildcard fields are errors.
- Construction evaluates fields in declaration order: explicit value, else
  `_ = V` wildcard default, else declared default, else `undefined`. Each
  default is evaluated separately per use.
- Patterns check arity and tag, then only the listed fields. `#r.f` is the
  one-based index (tag at 1).
- Field access checks arity and tag; failure is `{badrecord, V}` in bodies and
  rejection in guards.
- `is_record(V, r)` uses the declared arity. `is_record/3` needs an atom tag and
  integer arity (non-positive → false; wrong types → `badarg`); an atom third
  argument is the native-record query and returns false. Guards require literal
  arguments.
- Update `Expr#r{f = V, ...}` evaluates the new values in source order, then
  `Expr`, then checks arity and tag (`{badrecord, Value}` on mismatch, also for
  `Expr#r{}`) and builds a new tuple; the other fields are copied. `_ = V` is
  rejected in updates; updates are illegal in patterns and guards.
- `record_info(fields | size, r)` expands at compile time to the field-name list
  or the tuple size. Both arguments must be literal atoms and `r` a tuple record
  declared earlier; it is illegal in guards, and a local `record_info/2` is
  rejected as already defined.
- Native records: [native records](native-records.md) (local, qualified,
  imported and anonymous forms).

## Pids and references

`self/0` returns the calling process's pid, `make_ref/0` a new reference;
`pid_to_list/1` and `ref_to_list/1` return their text.

- A pid is an immediate word (low four bits `0x3`) holding the process's number.
  Numbers come from one process-wide sequence and are never reused, so a runtime
  admits a pid word only when it issued that number: a forged word (never
  issued) or another runtime's pid is `wrong_owner`. The pid of an exited
  process stays a valid term, as in OTP. 32-bit targets have 2^28 numbers per
  program run, 64-bit targets 2^60; creating a process past them fails with
  `resource_limit`.
- A port is an immediate word (low four bits `0x7`) holding its number from its
  own never-reused sequence, admitted like a pid; it prints as `#Port<0.N>` and
  orders between funs and pids ([ports](ports.md#identity)).
- A reference is a heap cell (`reference`: header plus an untraced 64-bit
  number) admitted like every heap term: only in its own process, stale after a
  collection of a host `Term`, copied by value between processes. Numbers come
  from one process-wide counter, so every reference of a program run is unique.
- A handle reference (an OTP magic reference) is a reference cell that also
  holds an off-heap buffer; it is the same reference to every service
  and shares its buffer with every copy
  ([handle references](runtime-heap.md#handle-references)).
- Printing follows OTP's local identities: a pid as `<0.N.S>` (N the low 28 bits
  of its number, S the rest), a reference as `#Ref<0.A.B.C>` (C the low 18 bits
  of its number, B the next 32, A the rest), in both `~w` and display styles.
  Pids order by number, references by number, so later references of a program
  order after earlier ones.

## Comparison and order

Iterative with no work cap, as in OTP: only memory for pending pairs bounds a
comparison, map key searches included, and identical words are equal without a
walk. Byte-aligned bitstrings compare whole bytes at once. Order: numbers <
atoms < references < funs < ports < pids < tuples < native records < maps < nil
< lists < bitstrings ([funs](funs.md#comparison-and-printing) order among
themselves).
Atoms compare by UTF-8 spelling (code-point order); tuples by arity then fields;
maps by size, then keys, then values; bitstrings by logical bits. Two small
integers, two floats or two whole-byte binaries are ordered straight from their
words or cells (`detail::word_order`), with no term admission.

## Printing

`format_term` ([output.hpp](../runtime/include/clause/runtime/output.hpp))
renders any admitted term in one of two OTP styles. Integers, tuples (records
are tuples) and nesting look the same in both.

| | `~w` (`TermStyle::write`) | `erlang:display/1` (`TermStyle::display`) |
| --- | --- | --- |
| Atoms | Quoted unless a Latin-1 lowercase letter starts it and name characters (with `@`) follow; reserved words and `maybe`/`else` are quoted; beyond Latin-1 escapes as `\x{H}` | Quoted unless a Latin-1 lowercase letter starts it and alphanumerics or `_` follow; reserved words and `@` get no special rule; UTF-8 kept |
| Floats | Shortest round trip in OTP layout: `0.1`, `100.0`, `1.0e16`, `1.5e-7` | C `%.6e`: `1.500000e+00` |
| Lists | Elements: `[104,105]`, `[1,2\|3]` | A flat list of printable Latin-1 bytes prints as `"hi"` (raw bytes; only `\n` and `"` escaped) |
| Bitstrings | `<<1,2,5:3>>` | A printable ASCII binary prints as `<<"hi">>`, others as `~w` |
| Maps | `#{k => v,k2 => v2}` | `#{k=>v,k2=>v2}` |

- Maps print in map-key order (`maps:iterator(M, ordered)`, as OTP `~kw`). OTP's
  default `~w` and `erlang:display/1` follow its internal layout instead:
  atom-table order for atom keys of small maps (it varies between VM runs) and
  hash order above 32 keys. Clause does not reproduce that order.
- Rendering is iterative, so depth is limited only by the term. Text is capped
  at 64 MiB by default; exceeding it (for example a widely shared subterm) fails
  with `resource_limit` and returns no partial text.
- Goldens: `runtime_printing` compares both styles with OTP (all corpus results
  plus authored edge cases); display rows whose OTP map order is internal are
  skipped
  ([fixtures](../tests/fixtures/printing/README.md)).
