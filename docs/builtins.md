# Builtins

Plan 11 step 36 (2026-10-07): the production builtin bridge. Builtins are
`erlang` functions the runtime implements in C++. The runtime registers them
by module, function and arity; generated code reaches them directly, through
dynamic calls and as fun values.

## Which builtins exist

The bridge catalog `abi::v1::bridge_builtins`
([builtins.hpp](../abi/include/clause/abi/builtins.hpp)) lists every
builtin both the compiler and the runtime know:

- the guard BIFs: type tests (`is_atom/1` … `is_tuple/1`, `is_function/1,2`),
  `abs/1`, `bit_size/1`, `byte_size/1`, `ceil/1`, `element/2`, `float/1`,
  `floor/1`, `hd/1`, `length/1`, `map_get/2`, `map_size/1`, `is_map_key/2`,
  `max/2`, `min/2`, `round/1`, `size/1`, `tl/1`, `trunc/1`, `tuple_size/1`,
  `binary_part/2,3`;
- the operators as functions: comparisons, `not/1`, `and/2`, `or/2`,
  `xor/2`, arithmetic and bitwise operators (`erlang:'+'/1,2` …);
- `display/1`, `halt/0,1`, `error/1,2,3`, `exit/1`, `throw/1`, `raise/3` and
  `function_exported/3`;
- term access (plan step 37): `setelement/3`, `make_tuple/2,3`,
  `tuple_to_list/1`, `list_to_tuple/1` and the list operators `'++'/2` and
  `'--'/2` (`A ++ B`, `A -- B` lower to them);
- conversions (plan step 38): `atom_to_list/1`, `list_to_atom/1`,
  `integer_to_list/1,2`, `list_to_integer/1,2`, `float_to_list/1,2`,
  `binary_to_list/1`, `list_to_binary/1`, `iolist_to_binary/1`
  (`term_to_binary/1` is not selected);
- console output (plan step 40): `io:format/1,2` and `io:put_chars/1`, the
  first builtins of another module ([io](io.md));
- process identities (plan step 42): `self/0`, `make_ref/0`, `pid_to_list/1`
  and `ref_to_list/1` ([pids and references](terms.md#pids-and-references));
- processes (plan step 43): `spawn/1,3` and `is_process_alive/1`
  ([processes](processes.md)); links and exit signals (step 48):
  `spawn_link/1,3`, `link/1`, `unlink/1`, `exit/2`, `exit_signal/2`,
  `process_flag/2` ([links](processes.md#links)); monitors (step 49):
  `spawn_monitor/1,3`, `monitor/2`, `demonitor/1,2`
  ([monitors](processes.md#monitors)); registered names (step 50):
  `register/2`, `unregister/1`, `whereis/1`, `registered/0`
  ([registered names](processes.md#registered-names));
- messages (plan step 45): `'!'/2` (the `!` operator) and `send/2`
  ([messages](processes.md#messages));
- the process dictionary (plan step 41C): `put/2`, `get/0,1`,
  `get_keys/0,1`, `erase/0,1` ([process dictionary](processes.md#process-dictionary));
- binary, atom and number conversions (plan step 41D): `atom_to_binary/1,2`,
  `binary_to_atom/1,2`, `binary_to_existing_atom/1,2`,
  `list_to_existing_atom/1`, `integer_to_binary/1,2`, `binary_to_integer/1,2`,
  `float_to_binary/1,2`, `binary_to_float/1`, `list_to_float/1`,
  `binary_to_list/3`, `bitstring_to_list/1`, `list_to_bitstring/1`,
  `split_binary/2`, `iolist_size/1`, `list_to_pid/1`, `list_to_ref/1`. They
  share the list conversions' text rules (`conversion_text.cpp`: atom names,
  digits, float options) and OTP's errors: float text needs digits on both
  sides of the point (`list_to_float("1e10")` is `badarg`), an exponent past
  the range is `badarg` and an underflow `0.0`; encodings are `utf8`,
  `unicode` and `latin1`; they run to completion.

Entries are only appended: an entry's index is the number generated code
passes to the bridge service. A qualified call of a catalog builtin of another
module (`io:format(F, A)`) also calls the bridge. Every other `erlang`
function reaches the [erlang library module](#erlang-module).

## How calls reach them

| Source | Path |
| --- | --- |
| `abs(X)`, `X + Y`, `erlang:display(X)`, `halt()`, `error(R)` | Inline services, as before the bridge |
| `erlang:function_exported(M, F, A)`, `setelement(I, T, V)`, `A ++ B`, `A -- B`, `length(L)` in a body (catalog builtins without an inline service) | Entered like a function: `CLAUSE_builtin_frame_v1(context, index)` gives the builtin's frame, the arguments go in the registers ([portions](#portions)) |
| `fun abs/1`, `fun erlang:'+'/2` | External fun `erlang:F/A`; registration binds it to the builtin |
| `M:F(Args)`, `apply(M, F, Args)`, runtime `fun M:F/A` | The code server finds a module's export first, then a builtin |

- `fun F/A` of an auto-imported builtin the module neither defines nor
  suppresses (`-compile({no_auto_import, ...})`) is the external fun
  `erlang:F/A`, as in OTP: `fun abs/1 =:= fun erlang:abs/1` and it prints as
  `fun erlang:abs/1`. `halt/0,1`, `setelement/3`, `tuple_to_list/1`,
  `list_to_tuple/1`, the conversions, the process builtins and the port builtins `open_port/2`, `port_close/1`,
  `port_command/2,3`, `port_connect/2`, `port_control/3`, `port_to_list/1`, `list_to_port/1` are auto-imported like
  OTP's; `display/1`, `raise/3`, `function_exported/3`, `make_tuple/2,3`, `port_info/1,2`, `port_call/2,3` and
  `ports/0` need the `erlang:` prefix ([ports](ports.md#builtins-and-port-messages)).
- A builtin has a `FrameDescriptor` with a null body (`BuiltinFrame`). Entering
  it (`CLAUSE_enter_v1`, `CLAUSE_tail_v1`) pushes no frame: the
  builtin runs on the registers and its result returns into the caller's
  body, so a builtin in tail position returns to the caller's caller.
- Errors are the ones the inline lowering raises in a body: `badarg`,
  `badarith` for arithmetic, `system_limit`, `{badmap, M}`, `{badkey, K}`;
  `raise/3` with an invalid class or stack returns `badarg`. They go through
  the checked failure channel like every service error.
- Term access follows OTP's `badarg` rules: `setelement/3` needs a small
  integer index within the tuple; `make_tuple/2,3` a small size in
  0..16,777,215, and `make_tuple/3` a proper list of `{Index, Value}` pairs
  within the size (later pairs win); `list_to_tuple/1` a proper list;
  `A ++ B` a proper list `A` (`[] ++ B` is `B` for any `B`, a non-list `B`
  ends the result); `A -- B` two proper lists, each element of `B` removing
  the first exactly equal (`=:=`) element of `A`, in O((n + m) log m).
- Conversions follow OTP:
  - `list_to_atom/1` takes a proper list of code points (no surrogates);
    a 256th character is `system_limit` before it is checked. A full atom
    table (`--max-atoms`) stops the program as a runtime failure
    (`resource_limit`, exit 70).
  - `integer_to_list/2` and `list_to_integer/2` take a small base in 2..36;
    digits print uppercase and parse in either case. `list_to_integer`
    accepts one optional sign, skips leading zeros, needs a digit, and raises
    `system_limit` for more than 1,262,611 significant decimal digits (or
    4,194,304 in any base) once its first digits are valid, and for a value
    past the integer limit.
  - `float_to_list/1` is `"%.20e"`; `/2` options apply in order, the last
    format winning: `{scientific, D}` (`"%.*e"`, negative D is 6),
    `{decimals, D}` (D >= 0; fixed with OTP's own rounding below 2^53 and 19
    decimals, `compact` trims trailing zeros, also of an integer with
    `{decimals, 0}` above 2^53), `short` (shortest round-trip digits in
    OTP's fixed/scientific choice). Text of 256 bytes or more is `badarg`.
  - `binary_to_list/1` needs a binary; `list_to_binary/1` a list and
    `iolist_to_binary/1` a list or binary of bytes, binaries and nested
    lists, each list ending in `[]` or a binary.
- `function_exported(M, F, A)` raises `badarg` unless `M` and `F` are atoms
  and `A` a small integer; it is true when a module of the program exports
  `M:F/A` or `M:F/A` is a registered builtin.

## Portions

Plan 11 step 43A (2026-10-08): builtins whose work grows with a list or binary
argument run in bounded portions, like OTP's trapping BIFs, so a long builtin
cannot keep other processes from running ([processes](processes.md)).

- Every bridge builtin a body calls is entered like a function and spends a
  reduction. A portion may do `WORK_PER_REDUCTION` (16) units of work per
  reduction left in the time slice (at least one reduction's worth): list
  cells walked or built, comparisons, bytes. It pays for them from the slice.
- A builtin with work left traps: `ProcessStack::trap` names a continuation
  frame and puts the builtin's state terms in the registers, which stay roots;
  native state (bytes, sort positions, term words kept as roots) lives in the
  process stack's `TrapState`. The process yields and resumes at the
  continuation in a later slice. Collections between portions rewrite the
  registers and state words like other roots.
- Results, errors and evaluation order are the same as running to completion.
  An error found late (an improper tail) is raised when the walk reaches it.
- Host calls (`call_builtin`) continue every trap at once.

| Builtin | Portions |
| --- | --- |
| `length/1` in a body | Counts cells; in a guard it stays the inline service, as OTP's guard BIF does not trap |
| `A ++ B` | Collects `A`'s elements, then builds the copy onto `B` from its end |
| `A -- B` | Collects `B`, sorts it by exact order (bottom-up merge sort), scans `A` with a binary search per element, then builds the kept elements; when nothing is removed the result is `A` itself |
| `binary_to_list/1` | Builds the list from the binary's end |
| `list_to_binary/1`, `iolist_to_binary/1` | Walks the iolist depth first, then makes the binary at once |

Other builtins run to completion. The tuple builtins (`setelement/3`,
`make_tuple/2,3`, `tuple_to_list/1`, `list_to_tuple/1`) do as in OTP: their
work is bounded by the tuple arity limit. The remaining conversions read
inputs bounded by the atom, integer and float limits. Formatting with
`io:format/1,2` runs to completion ([differences](differences.md)). Inline
services of loops (a comprehension's final reverse) run to completion as
part of the loop.

## Typed builtins

Plan 11 step 41 (2026-10-07): builtins that check their own arguments are C++
functions of typed parameters ([typed.hpp](../runtime/src/builtins/typed.hpp)),
`Result Function(ProcessContext &, Parameters...)`, registered with
`typed_entry<Function>(module, name)` (the arity is the parameter count).

- The adapter admits every argument word in order (a word this process does
  not own is the failure it is, never `badarg`), then converts each to its
  parameter type; a mismatch raises `badarg` and the function does not run.
- Parameter types: `Term` (any term, the generic fallback), `std::int64_t`
  (a small integer), `detail::Integer` (any integer), `double` (a float),
  `ListArgument` (a proper list and its elements), `TupleArgument`,
  `BinaryArgument` (bytes of a binary), `AtomArgument` (its spelling).
- Results: `Term`, `TermResult<Term>` (a failed construction is a runtime
  failure), `BuiltinResult<Term>` (`std::expected` with a `BuiltinFailure`),
  or a raw `Word` the function published itself. A function may also throw
  `BuiltinFailure` (an Erlang error such as `badarg` or `system_limit`, or a
  term access failure); `call_builtin` turns every other C++ exception into
  `out_of_memory` or `internal_error`, so none crosses the generated-code ABI.
- The term-access, conversion and io families, `binary_part/2` and
  `function_exported/3` are typed. The other `erlang` builtins pass their
  argument words unconverted to the inline services generated code also calls,
  which admit them; they stay word-level `BuiltinBody` adapters.

## Registration

- `BuiltinRegistry` ([builtin_registry.hpp](../runtime/include/clause/runtime/builtin_registry.hpp)),
  owned by the `CodeServer`, maps exact module/function/arity to a
  `BuiltinFrame`. `add` takes a batch of `BuiltinEntry`s and registers all or
  none: an empty name, more than 255 arguments, a missing implementation, or a
  name already registered (or repeated in the batch) rejects the batch with
  nothing kept.
- Runtime startup registers every table of `production_builtins()`
  (`erlang_builtins()`, `term_access_builtins()`, `conversion_builtins()`,
  `io_builtins()`), most entries made by `typed_entry`,
  which together cover the
  catalog; later families add their own tables.
- A body reads exactly its arity of argument words and records errors in the
  checked channel; host exceptions become `out_of_memory` or `internal_error`
  failures.
- The older host path `abi::v1::dispatch_builtin` (host-registered native
  modules by name, [runtime](runtime.md#code-server-and-builtins)) is separate
  and unchanged.

## Erlang module

Plan 11 step 41B. The compiler knows which `erlang` functions exist from two
owned sources, never from runtime code:

- `library/stdlib/erlang.terms`: OTP 29's `erlang` exports as
  `{Name, Arity, Flags, Status}` (flags `auto_import`, `guard`; status
  `implemented`, `{planned, Step}` or `{unavailable, Reason}`: `distribution`,
  `tracing`, `code_loading`, `not_selected`).
  `tests/compiler/builtins/observe_erlang.escript` observes the host OTP and
  keeps the statuses, which are maintained by hand.
- `tests/compiler/builtins/generate_erlang.py` turns it into
  `library/stdlib/erlang.erl` (Clause's own stub module) and
  `compiler/src/semantic/erlang_auto_imports.inc` (OTP's auto-import list).
  CTest `builtins_erlang_inventory` checks both are current and that
  `implemented` means a catalog builtin, a function the compiler handles
  (`apply/2,3`, `is_record/1,2,3`) or a real Erlang body (`nif_error/1,2`
  raise `error(Reason)` like OTP's stubs); `builtins_erlang_freshness`
  (label `host_otp`) compares the inventory with the host OTP.

Calls:

- `erlang:F(...)`, `fun erlang:F/A`, `apply(erlang, F, [...])` and
  `-import(erlang, ...)` of a builtin go to the builtin, as before. Any other
  export joins the program as the library module `erlang` (compiled like
  `lists`); its stubs raise `error(undef)` with `erlang:F/A` on top of the
  stack, only when called. A name the module does not export is
  `undefined function erlang:F/A`.
- An unqualified call or `fun F/A` of an OTP auto-import (`get(K)`,
  `fun put/2`) that the module neither defines, imports nor suppresses with
  `-compile({no_auto_import, ...})` reaches `erlang:F/A` the same way.
- Builtins are sticky: when the module `erlang` registers, the runtime skips
  every export a registered builtin has the name of, so `M:F(Args)` with
  `M = erlang` and external funs bind the builtin, never a stub.

## Source layout

- The runtime implements builtins in `runtime/src/builtins/`. A large module
  gets its own subdirectory with one translation unit per builtin family:
  module `erlang` lives in `runtime/src/builtins/erlang/` (`core`: type tests,
  guard BIFs, operators, raising, `display`, `halt`, `function_exported`;
  `term_access`; `lists`: `length`, `++`, `--`; `conversions`; `processes`:
  identities, spawning, messages, links, monitors, registered names;
  `ports`; `dictionary`: the [process dictionary](processes.md#process-dictionary);
  `binary_conversions` with `conversion_text`, the text rules it shares with
  `conversions`).
- Each family file defines its own `*_builtins()` table listed in
  `production_builtins()`. A new family adds a new file and table instead of
  growing an existing one; a file past about 400 lines is split by family
  before it grows further. Later large modules follow the same rule
  (`runtime/src/builtins/<module>/`).
- Small modules (`io`, `os`) and the bridge infrastructure (`registry`,
  `invocation`, `bridge`, `portions`, `typed`, `text`, `float_text`,
  `output`) stay in `runtime/src/builtins/`.
- The compiler never reads runtime sources: the catalog
  `abi::v1::bridge_builtins` is the single list both sides share. It stays
  one append-only array; its group comments name the implementing file.
