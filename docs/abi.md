# Generated-code ABI (revision 21)

Private contract between compiler output and the runtime. It is project-internal
C++23, not BEAM-compatible and not a general FFI. Objects and runtime must come
from the same build; older descriptor revisions are rejected before use.

Headers: [v1.hpp](../abi/include/clause/abi/v1.hpp) (term/context/function
types), [term.hpp](../abi/include/clause/abi/term.hpp) (immediate integer
codec), [status.hpp](../abi/include/clause/abi/status.hpp),
[builtins.hpp](../abi/include/clause/abi/builtins.hpp),
[startup.hpp](../abi/include/clause/abi/startup.hpp) (program startup).

## Terms

A term is one unsigned target-pointer-width word (32 or 64 bits). Layouts derive
from the configured LLVM target, so cross-target code uses target widths.

- Small integers: low four bits `0xf`; signed payload of `word_bits - 4` bits,
  range `[-2^(word_bits-5), 2^(word_bits-5)-1]`. Larger values are heap bignums.
- Atoms: low six bits `0x0b`, non-recycled process-wide payload. IDs are never
  serialized or used for ordering. `false`, `true`, `undefined`, `ok` and
  `error` are fixed: payloads 0–4 in every runtime.
- Empty tuple `0x2b` and nil `0x3b` exactly; other payload bits are invalid.
- Local pids: low four bits `0x3`, payload the process number, admitted only
  when the runtime issued it
  ([pids and references](terms.md#pids-and-references)).
- Boxed and list words point into the owning process heap and are admitted only
  after the runtime proves ownership (see [terms](terms.md)).
- Primary tags: list `0x1`, boxed `0x2`. A header keeps its kind from bit 2 and
  the count of following words from bit 7 (`header_count_shift`); a tuple's kind
  is 0. Generated code reads tuple elements and cons cells inline only after
  inference or a dominating shape test proved the cell
  ([proofs](specialization.md#proofs)); match plans test tuple and cons shapes
  inline at every optimization level (a boxed tag, then the header), and exact
  equality compares words first, calling `CLAUSE_exact_v1` only for two distinct
  list or boxed words. The runtime asserts these constants.

## Functions and symbols

Entry signature (native C calling convention, no C linkage required):

```cpp
TermWord function(ProcessContext *context, const TermWord *arguments);
```

Arguments are a borrowed, word-aligned array in source order (null at arity 0).
The context is live and passed unchanged through calls. A returned word is
usable only after checking the failure channel.

Symbols: `clausev1_<hex module>_<hex function>_<arity>`, lowercase hex of UTF-8
bytes, canonical decimal arity; reversible and host-independent. Exported
entries are external, others internal.

## Module registration

Each module emits `clausev1_<hex module>__0.descriptor` and `.register`. The
descriptor holds ABI version, term width, export table (name, arity, host entry
and the `FrameDescriptor` dynamic calls enter), atom spellings (UTF-8
pointer/size pairs), native record descriptors (module, name and field atom
slots, export flag; [native records](native-records.md#representation)) in an
external `<prefix>.records` table that other modules of a batch reference, fun
descriptors in a private `<prefix>.funs` table ([funs](funs.md#representation)),
the read-only literal area ([areas](runtime-heap.md#areas)), the writable atom
and fun caches generated code reads inline, and the `code_convention` of its
bodies. Registration calls `CLAUSE_register_module_v4(Runtime*)`, which
validates version/width and all exports, interns atoms, builds a frozen registry
and publishes it with the code image in one transaction. Duplicate modules never
replace code; any failure publishes nothing (already interned atoms stay in the
bounded table).

- Call `.register` explicitly before resolving exports. There are no global
  constructors; static-archive users must reference registration entries.
- `llvm.used` keeps descriptors and entries; linking without the runtime fails
  on the missing service symbol.
- The descriptor address is the atom-binding key; its image must stay mapped for
  the module lifetime. Each runtime has its own bindings for the same image.
- Atom expressions other than the fixed atoms read `atom_cache` when the
  context's `FrameState::runtime` owns the cache, else `CLAUSE_atom_v3`; they
  never intern ([atoms](terms.md#atoms)).
- A startup object (`clausev1_start`) lists every descriptor in a
  `StartupDescriptor` and its native `main` calls
  `CLAUSE_main_v1(argc, argv, descriptor)`, which registers all modules and runs
  the entry ([executables](executables.md#startup-object)).

## Failure channel (revision 2)

Errors are not encoded in term bits. After every non-tail generated call the
caller checks the channel before using the result or evaluating the next
argument: it reads the `failed` word of the context's `FrameState`, which the
runtime keeps equal to `CLAUSE_call_failed_v2(context)`. On failure the
callee returns an invalid zero word.

| Outcome | Transport |
| --- | --- |
| Pattern mismatch, guard rejection | Continuation to next candidate; channel untouched |
| Exhausted clauses | `error:function_clause`; `error:{case_clause, Value}` with owned payload for a `case`; `error:if_clause`; `error:{try_clause, Value}` for a try's `of` clauses; `error:{else_clause, Value}` for a maybe's `else` clauses |
| Body match failure | `error:{badmatch, Value}` with owned payload |
| Comprehensions | `error:{bad_generator, Tail}`, `error:{bad_filter, Value}`, `error:{bad_generators, Inputs}` (`ErrorReason` 16-18); a strict generator's rejection is `{badmatch, Element}` |
| Record access, bad arguments, arithmetic, maps | `badrecord`, `badarg`, `badarith`, `badmap`/`badkey` |
| Native record field missing | `ErrorReason::badfield` (20), payload `{{Module, Name}, Field}` |
| Calling a value (`F(Args)`) | `ErrorReason::badfun` (22, payload the value), `badarity` (23, payload `{Fun, Args}`), `undef` (24), recorded by `CLAUSE_apply_v1` ([funs](funs.md)) |
| Dynamic calls (`M:F(Args)`, `apply/2,3`, `fun M:F/A` with variables) | `badarg` for a non-atom module or function, an improper argument list or an invalid arity; `undef` when no module of the program exports the function; `badfun`/`badarity` as above ([funs](funs.md#dynamic-calls)) |
| External native construction without a value | `ErrorReason::novalue` (21), payload `{{Module, Name}, Field}` |
| Integer result past the size limit | Service outcome `ValueOutcome::system_limit` (3): a guard rejects, a body raises `error:system_limit` (`ErrorReason` 19) |
| Invalid lazy left operand | `{badarg, Value}` |
| Infrastructure (OOM, limits, ownership, internal) | `CallError::runtime_failure` with exact `Status` |
| `erlang:error/1,2,3`, `exit/1`, `throw/1`, `erlang:raise/3` | `raised_error`/`raised_exit`/`raised_throw`: class from the ID, owned payload is the whole reason |
| `erlang:halt/0,1` | `CallError::halted` with `halt_status` (and slogan) |

Reasons are typed IDs recorded by `CLAUSE_raise_v2`; the three `raised_*` IDs
select class `exit` or `throw` (otherwise `error`) and carry any term as the
reason. `error/2,3` raise through `CLAUSE_error_v1(context, reason, args)`,
which also keeps a list `args` for the top stack frame. First failure wins;
nested invocations share the channel. `GeneratedInvocation` is the host scope:
it checks pending failures before entry and after return, copies result or
error, and clears only at the outermost exit (also on C++ exceptions). No
exception crosses generated entries. Raw entry callers must open a
`GeneratedInvocation`; normal hosts use `ResolvedFunction::call`.

`catch Expr` redirects every failure check and raise inside `Expr` to a handler
block that calls `CLAUSE_catch_v1(context, slot)`. For a pending Erlang
exception it writes the catch value to the root slot and clears the channel: the
thrown term, `{'EXIT', Reason}` for an exit, or `{'EXIT', {Reason, []}}` for an
error (typed reasons become their OTP terms such as `{badmatch, V}`; the stack
is described [below](#stack-traces)). Halts and infrastructure failures stay
pending, and the handler's own check continues to the enclosing handler or
function exit. Bindings made inside `Expr` are unsafe afterwards, so the join
only merges the value.

`try Body of ... catch ... end` protects only `Body` the same way. Its handler
calls `CLAUSE_exception_v2(context, class_slot, reason_slot, stack_slot)`, which
writes the class atom (`error`, `exit` or `throw`), the reason and the stack
trace term to root slots and clears the channel (halts and infrastructure
failures stay pending as for `catch`). Catch clauses then match `Class:Reason`
with ordinary patterns and guards; an omitted class matches `throw`, and a named
stack variable binds the stack term. When none matches,
`CLAUSE_reraise_v2(context, class, reason, stack)` records the exception again
with a `raised_*` reason and the same stack, which reports and catches exactly
like the original. `of` clauses select on the body value and raise
`{try_clause, Value}` (`ErrorReason::try_clause = 14`); exceptions inside `of`
clauses and handlers go to the enclosing handler.

`maybe` needs no service: each `?=` is an ordinary match whose mismatch edge
leaves the body for the maybe's exit with the unmatched (already rooted) value.
Without `else` that value is the result; otherwise `else` clauses select on it
like `case` clauses and raise `{else_clause, Value}`
(`ErrorReason::else_clause = 15`) when none matches.

Comprehensions use the existing services with a few operations: the accumulated
elements are reversed by the construction `reverse`
(`ContainerConstruction::reverse = 2`, values `{List, Tail}`), which builds the
result in one heap reservation; list generators test and read cons cells inline;
a binary comprehension joins its pieces with `BitOperation::concat` (`{List}`)
and a map comprehension builds its map with `MapOperation::from_list`
(`{Pairs}`, later keys win). Map generators take `MapOperation::entries` (11,
`{Map}`) once: a cell with the keys and values at words `1 + 2i` and `2 + 2i` in
canonical key order (a classic map itself, or a tuple-shaped copy of an off-heap
map's entries that only generated code reads) and read each entry inline;
`key_at`/`value_at` (`{Map, Position}`) remain for other callers. A zip's
remaining input is shown with `MapOperation::iterator`, OTP's `{K, V, Next}`
chain ending in `none`. `MapOperation::update_owned` (10) takes the operands of
`update` for a map update whose source the compiler proved unread afterwards: an
off-heap source map is updated in place and its stub left consumed (empty).
Bitstring generators use ordinary pattern extraction plus a final `binary`/`all`
segment for the rest.

`try ... after A end` adds a second protection around the body and all `of` and
catch clauses. On the normal path `A` runs after the selected value is rooted
and its value is discarded. The after handler takes the exception with
`CLAUSE_exception_v2`, runs a second copy of `A` and re-raises with
`CLAUSE_reraise_v2`; an exception or failure inside `A` leaves through the
enclosing handler instead, replacing the original. Halts and infrastructure
failures skip `A`. Root slots belong to the function frame, so every path
releases them at the function exit.

### Stack traces

Each root frame names its generated function with a private
`abi::v1::FrameDescriptor` (module descriptor, module and function name atom
slots, arity). When an Erlang exception is recorded, the channel copies the
innermost 8 named frames (BEAM's default `backtrace_depth`); the term
`[{Module, Function, Arity, []}, ...]` is built only when a handler, `catch` or
report asks for it. The top frame shows the `error/2,3` argument list instead of
the arity when that argument is a list.

`erlang:raise(Class, Reason, Stack)` (`CLAUSE_reraise_v2`) accepts the stacks
BEAM accepts: a proper list of `{M, F, A}` (completed with a `[]` location) or
`{M, F, A, Location}` with atom `M`, `F` and a list `Location`, cut to 8
entries; the stack is then kept as given and frames are no longer captured. An
invalid class or stack records nothing and the call evaluates to `badarg`, as in
OTP. `erlang:get_stacktrace/0` is rejected with OTP 29's "removed" lint text.

Differences from OTP, all visible only in the stack term:

- Locations are always `[]` (OTP adds `{file, F}`, `{line, L}`), and `error/3`
  options add no `error_info`.
- A `function_clause` top frame shows the arity, not the argument list.
- No frame names a failing BIF or operator (OTP adds
  `{erlang, '+', Args, [{error_info, ...}]}`), and nothing below the entry
  function appears.
- A tail call releases the caller's frame, so, as in OTP, a caller that ended in
  a tail call is missing from the trace. OTP also turns calls to functions that
  never return into tail calls; Clause does not.
- `{Fun, Args}` stack entries of `erlang:raise/3` are rejected.

## Frames and transfers

Generated functions run on explicit frames
([execution model](execution-model.md); services in
[frames.hpp](../abi/include/clause/abi/frames.hpp)). Each function has a
`<symbol>.frame` descriptor (`FrameDescriptor`: module descriptor, module and
function atom slots, arity, body code, slot count, term slot count; external for
exported functions) and an internal `<symbol>.body` of type `abi::v1::Code`,
`void(void *context, TermWord x0, ...)` with `abi::v1::register_arguments` term
parameters; with a Clang-built runtime on x86-64 and AArch64 the bodies and
every transfer use the `preserve_none` convention, as `abi::v1::Code` does
there: bodies never return normally, so they save no callee-saved registers;
registration rejects a module whose `code_convention` (0 none, 1 C, 2
`preserve_none`) is not the runtime's. An exported `<symbol>` keeps the
`TermWord(Context *, const TermWord *)` signature as a host entry calling
`CLAUSE_invoke_v1(context, frame, arguments)`.

- Every context starts with an `abi::v1::FrameState`: stack block, top, push
  limit, current frame offset, reductions left, register pointer, live register
  count, slow bits, the failed word, the heap block's free words
  `[heap_top, heap_limit)`, the runtime identity atom caches are claimed by, the
  context's pid word (`self`), one target word each. A body reads its frame
  header (`stack + frame`) and the registers from it on entry and switches on
  the header's resume word.
- Binary patterns (64-bit targets): the start test, the end test and integer
  segments of a constant whole-byte width up to 56 bits at a byte-aligned
  position read the cell inline (`abi::v1::heap_binary_*_word`, `refc_*_word`:
  an off-heap view's `data_` is its buffer's first byte); other segments and
  unaligned positions call `CLAUSE_bits_v1`. A `sub_binary` subject
  (`sub_*_word`) is read through its original. A trailing `Rest/binary` or
  `Rest/bits` of unit 1 is bump-allocated inline as a 4-word `sub_binary` cell
  of the original.
- Binary loops: when every clause of a function matches one parameter as a
  binary (or ignores it), never keeps the whole binary, and reads the rests it
  binds only as that parameter of self tail calls, the function gets a private
  variant `<symbol>.loop`. The function's own step makes the rest cell as above
  and enters the variant with it (a rest the service made, which may be
  shared, re-enters the function instead). The variant makes no rest cell: each
  self tail call stores the new offset and bit length into the parameter's
  cell, which only this loop references, and passes the same cell on, so a
  loop over a binary allocates once. The variant reads the cell's view once
  per step, before its clauses, and skips the bitstring test of the parameter.
  The variant has the function's name and arity in stack traces.
- A binary construction whose every byte is known at compile time (small integer
  constants and string literals at constant whole-byte widths, at most 64 bytes)
  is an inline binary cell of the module literal area, like other compound
  literals.
- `Left ++ Right` where `Left` is written as a proper list (a string literal, or
  a list without a tail) is built as `[E1, ..., En | Right]`, as OTP's compiler
  does, without calling `erlang:'++'/2`. Any other `Left ++ Right` walks `Left`
  inline (`codegen/lowering_append`): a proper list of at most 32 cells is copied
  onto `Right` in the heap block's free words; a longer or improper `Left`, or a
  block without room, enters the builtin, which keeps every error and portion.
- A pure builtin of literal arguments is evaluated at compile time, as OTP's
  `sys_core_fold` does: `atom_to_list(Atom)` of an atom literal is that name's
  literal string, with no call.
- `erlang:make_tuple(N, V)` with a small `N` in 1..64 bump-allocates the tuple
  and fills it with `llvm.experimental.memset.pattern` where the heap has room;
  otherwise the builtin runs.
- Binary constructions (64-bit targets) whose segments are all integers of
  constant whole-byte widths up to 64 bits, at most 64 bytes in all, are
  bump-allocated inline as `heap_binary` cells (header, bit length, zeroed data
  words, then each segment's bytes) when every value is a small integer and the
  heap has room; otherwise `CLAUSE_bits_v1` builds them. The same segments after
  a whole-binary accumulator (`<<Acc/binary, ...>>`) are written into the
  accumulator's buffer when it is a `refc_binary` (or a `sub_binary` of one)
  ending, byte-aligned, at the cell's `refc_append_end_word` with room below
  `refc_append_limit_word`: the bytes go past the end, the end advances, and the
  result is a 4-word `sub_binary` of that cell.
- `+ - * /` (64-bit targets) with a float operand, and `/` of any two numbers,
  read each operand as a double (a small integer converted, a float cell's value
  read through a select of its cell or a constant stand-in cell, so no load
  depends on a branch), compute in registers and bump-allocate the 2-word float
  cell when the result is finite, the divisor nonzero and the heap has room;
  otherwise `CLAUSE_immediate_v1` raises `badarith` or promotes.
- Map expressions without a base whose keys are atom or small integer literals
  (at most 32 distinct) are written inline as classic map cells, keys sorted at
  compile time in exact term order, the last value of a repeated key kept;
  otherwise `CLAUSE_map_v1` builds them.
- Map patterns (`#{K := V}`, `#{}`), `map_get/2`, `is_map_key/2`, `map_size/1`
  and updates (`M#{K := V}`, `M#{K => V}`, at most 8 fields) read a classic map
  cell inline when every key is an atom or a small integer, whose term equality
  is word equality: a scan of the key words, then the value word, or a copy of
  the cell with the new values when it already holds every key and the heap has
  room. A missing key fails a pattern at once; map trees, other keys, a missing
  key elsewhere and errors take `CLAUSE_map_v1` (small-map lookups).
- Tuples and lists of at most 64 words are bump-allocated inline in
  `[heap_top, heap_limit)` (header or cons cells written in place, `heap_top`
  advanced), else built by `CLAUSE_construct_v1`; the result is rooted like a
  service output. The runtime takes inline words into its accounting before
  every heap operation and resets both fields after one; they are null while no
  heap block exists or a per-process heap budget applies, so every allocation
  then reaches the service ([heap](runtime-heap.md#areas)). `CLAUSE_frame_v1`
  and `CLAUSE_registers_v1` remain for other consumers.
- A body leaves only by `musttail` calls. Calls, tail calls and returns run
  inline when they can, else the code comes from `CLAUSE_enter_v1` (call),
  `CLAUSE_tail_v1` (tail call) or `CLAUSE_return_v1` (return), which keep the
  state current ([execution model](execution-model.md#implementation)).
- Every transfer passes the first `abi::v1::register_arguments` (6) X registers
  as C parameters after the context: a call's leading arguments, a return's
  result (as `x0` of the caller's body), zero for unused ones. Further arguments
  travel in the register array. Around every transfer service the generated code
  stores the parameters into the array (the services and the collector read them
  there) and loads them back after it. The runtime calls code the same way
  (`ProcessStack::run_code`), and its bottom-frame body stores `x0` into the
  first register for the host.
- An entry zeroes the frame and copies only the arguments past the parameters
  into their slots; a body keeps a parameter argument in its slot only where
  `lower_frames` spills it across a call or safepoint. Evaluated values are not
  stored eagerly: lowering marks computed terms (`clause.term` metadata) and
  `lower_frames` gives a term slot only to a value that crosses a call or
  safepoint; service argument arrays still go through root slots. Failed
  candidates clear their slots. Values a body still needs after a call or a
  loop-head safepoint are spilled: terms to term slots (counted in the
  descriptor's `roots`), other words to raw slots after them; raw slots are not
  roots.
- A result passes as `x0`; error payloads are root words of the channel (BEAM
  `fvalue`). A failure returns to the caller like a result and every caller
  checks the channel after the call; the header's handler word stays 0.
- The stack has no cap by default; a push the host cannot allocate records
  `out_of_memory`, and one beyond an optional per-process
  `StackOptions::limit_words` records `resource_limit` (infrastructure
  failures).
- A call of a function value passes its arguments, and the fun's captured values
  after them, in the registers; `CLAUSE_apply_v1` returns the `FrameDescriptor`
  the transfer enters ([funs](funs.md#representation)).
- Safepoints: `CLAUSE_enter_v1`/`CLAUSE_tail_v1` collect before pushing the
  callee frame (its arguments are register roots); the heap sets the
  `slow_collect` bit whenever it wants a collection, so inline entries never
  skip one. And `CLAUSE_safepoint_v1(context)` at each comprehension loop head
  collects in place; the head calls it only while `FrameState::slow` has
  `slow_collect`, then spends one reduction and, with none left, enters
  `erlang:self/0` (the entry suspends the process), so long call-free loops
  share the scheduler. Every other service is a critical section that never
  moves the heap
  ([collection in generated code](runtime-heap.md#collection-in-generated-code)).

## Runtime services

Generated code calls checked C++ services: `CLAUSE_exact_v1` (exact equality),
`CLAUSE_immediate_v1` (immediate predicates/queries), `CLAUSE_construct_v1`,
`CLAUSE_inspect_v1` (for other consumers; generated code tests and reads tuple
and cons cells inline), `CLAUSE_integer_v1`, `CLAUSE_float_v1`, `CLAUSE_map_v1`,
`CLAUSE_bits_v1`, `CLAUSE_record_v1` (native record make/get/update/match/test
under a `RecordCheck`; outcomes `bad_record`, `bad_field`, `no_match`),
`CLAUSE_make_fun_v1` (build a fun of a `FunDescriptor`). Each returns success,
semantic error (`badarg`/`badarith`/...) or infrastructure failure and writes
output only on success. `CLAUSE_display_v1`
([output.hpp](../abi/include/clause/abi/output.hpp)) prints one
`erlang:display/1` line and yields `true`; it has no semantic error.
`CLAUSE_halt_v1` never succeeds: it records a halt request (`CallError::halted`
with the exit status) or `badarg`, so the caller unwinds. Linker spellings
follow the target's Itanium or Microsoft C++ mangling.

Inline forms (`codegen/lowering_inline.cpp`): type tests (`is_atom`,
`is_integer`, `is_number`, `is_boolean`, `is_tuple`, `is_list`, `is_bitstring`,
`is_float`, `is_map`, `is_pid`, `is_port`, `is_reference`, `is_function/1`,
native `is_record/1`, `is_binary/1` by the bit length word) read tags and, after
a boxed tag test, the header kind (`abi::v1::HeaderKind`); they never call a
service. Comparisons of two immediates (`==`, `/=`) or two small integers (`<`,
`=<`, `>`, `>=`), `element/2`, `tuple_size/1`, `bit_size/1` and `byte_size/1`
(from the bit length word, while the size is a small integer), `hd/1`, `tl/1`, the boolean
operators, `band`, `bor`, `bxor`, `bsr`, `bsl` (while the result stays small)
and small `div`/`rem` run inline; `self/0` reads `FrameState::self`. They run
inline where their operands allow and call `CLAUSE_immediate_v1` otherwise,
which keeps every error and bignum case. `setelement/3` of a small index within
a boxed tuple copies the tuple into the heap block's free words and replaces the
slot inline; otherwise, or without room, it enters the bridge builtin.

`CLAUSE_builtin_frame_v1(context, builtin)`
([builtins.hpp](../abi/include/clause/abi/builtins.hpp)) returns the
`FrameDescriptor` of the production builtin with index `builtin` in
`abi::v1::bridge_builtins` (append-only); generated code enters it like a
function with the arguments in the registers, and errors and failures go to the
checked channel ([builtins](builtins.md)). A `FrameDescriptor` with a null body
is a builtin: entering it runs the builtin on the registers and returns into the
caller, or suspends the process at the builtin's continuation when it trapped
([portions](builtins.md#portions)).

A direct builtin (`BuiltinCall::direct`: pure conversions such as
`integer_to_list/1`, `atom_to_list/1`, `list_to_tuple/1`, `setelement/3`,
`phash2/1`) never traps, waits or acts on another process. Generated code calls
it with `CLAUSE_builtin_call_v1(context, builtin, arguments)`, which runs its
body at once and returns the result, with no frame transfer, no resume point and
no entry safepoint; it spends one reduction, and a failure goes to the checked
channel as a service's does. A quick builtin (`BuiltinCall::quick`: `length/1`,
`binary_to_list/1`, `list_to_binary/1`, `iolist_to_binary/1`) is called the
same way first; its bounded fast path returns 0 when it declines (longer
inputs, other shapes, every error, and always while calls are traced), and the
generated code then enters the builtin's frame as for any bridge builtin.

`abi::v1::dispatch_builtin` calls host-registered builtins by module/function
bytes, argument array and arity, returning a `Status`; output is written only on
success.

| Status | Value | Meaning |
| --- | ---: | --- |
| `ok` | 0 | Success |
| `not_implemented` | 1 | Known deferred feature, reported once |
| `invalid_argument` | 2 | Invalid input |
| `diagnostic_failure` | 3 | Report delivery failed |
| `out_of_memory` | 4 | Allocation failed, state rolled back |
| `busy` | 5 | Live contexts or running dispatch |
| `wrong_owner` | 6 | Object belongs to another runtime/context |
| `resource_limit` | 7 | Cap or budget exhausted |
| `stopped` | 8 | Owner shut down |
| `abi_mismatch` | 9 | Version or term width differs |
| `internal_error` | 10 | Unexpected failure contained |
| `unknown_builtin` | 11 | Signature not registered or catalogued |
| `erlang_error` | 12 | Structured Erlang error recorded |
| `output_failure` | 13 | Standard output rejected a write |

## Revisions

| Revision | Change |
| --- | --- |
| 1 | Initial descriptors and entries |
| 2 | Checked failure channel |
| 3 | Atom spellings and slots in descriptors |
| 4 | Mandatory generated root scopes |
| 5 | Explicit process frames and transfers |
| 6 | Native record descriptors in module descriptors, `CLAUSE_record_v1` |
| 7 | Fun descriptors in module descriptors, `CLAUSE_make_fun_v1`, `CLAUSE_apply_v1` |
| 8 | Export descriptors name their `FrameDescriptor`; dynamic call services `CLAUSE_call_v1`, `CLAUSE_apply_list_v1`, `CLAUSE_call_list_v1`, `CLAUSE_make_external_fun_v1` |
| 9 | Every context starts with `abi::v1::FrameState`; inline transfers |
| 10 | Leading X registers passed as code parameters |
| 11 | Fixed words of `false` and `true` |
| 12 | Inline heap fields `heap_top`/`heap_limit` in `FrameState` |
| 13 | Fixed `undefined`, `ok`, `error`; `ModuleDescriptor::atom_cache` and `FrameState::runtime` |
| 14 | Module literal areas: `ModuleDescriptor::literals`, `literal_words` |
| 15 | `FrameState::self`, the context's pid word |
| 16 | `Code` uses `preserve_none` on x86-64 and AArch64 with a Clang-built runtime; `ModuleDescriptor::code_convention` |
| 17 | Six X registers as `Code` parameters |
| 18 | `sub_binary` cells (`HeaderKind::sub_binary`, `sub_*_word`) and inline `Rest` extraction |
| 19 | Append end and limit words in `refc_binary` cells (`refc_append_*_word`, 8 payload words) and inline appends |
| 20 | `MapOperation::entries` and inline map generators |
| 21 | `ModuleDescriptor::fun_cache` and inline closures |
| 22 | Direct builtins: `BuiltinName::direct`, `CLAUSE_builtin_call_v1` |
| 23 | Quick builtins: `BuiltinName::call` (`BuiltinCall`), 0 from `CLAUSE_builtin_call_v1` declines |
| 24 | `RecordDescriptor::definition` (the field list as External Term Format bytes, for `records:get_definition/2`) and `ModuleDescriptor::features` (atom slots of the enabled features, for `erl_features:used/1`) |
