# Builtins

Builtins are functions the runtime implements in C++: the `erlang` BIFs and
the functions of other modules whose OTP counterparts are BIFs, NIFs or need
the runtime (`io` output, `lists` data functions, `math`, `binary`, ...). The
runtime registers them by module, function and arity; generated code reaches
them directly, through dynamic calls and as fun values.

## Which builtins exist

The bridge catalog `abi::v1::bridge_builtins`
([builtins.hpp](../abi/include/clause/abi/builtins.hpp)) lists every builtin
both the compiler and the runtime know, in groups whose comments name the
implementing source file:

- module `erlang`: the guard BIFs, the operators as functions
  (`erlang:'+'/2`, `'++'/2`, `'--'/2`, ...), `display/1`, `halt/0,1`, the
  raising functions, term access, conversions, process identities,
  [processes](processes.md) (spawning, messages, links, monitors, registered
  names, the process dictionary, timers, introspection, spawn options,
  aliases, nodes), [ports](ports.md#builtins-and-port-messages), the
  [external term format](#external-term-format), [time](#time),
  [system information](#system-information),
  [fun information](funs.md#fun-information), [hashing](#hashing) and
  [code loading](#code-loading);
- other modules: `io` output ([io](io.md)), the data functions of
  [`lists`](#lists-module), [`math`](#math-module),
  [`binary`](#binary-module), [`maps` and `os`](#maps-module),
  [`ordsets`, `orddict`, `proplists`, `dict`, `sets`, `gb_trees`, `gb_sets`,
  `queue` and `array`](#collections), [`base64` and `json`](#encodings),
  [`calendar`](#calendar), [`filename`](#filename), [`rand` and `random`](#random-numbers),
  [`persistent_term`](#persistent-terms), [`unicode`](#unicode-module),
  [`code`](#code-module), [`init`](#init-module),
  [`atomics`](#atomics-and-counters), [`erts_internal`](#erts-internal),
  [`unicode_util`](#unicode-util) and [`re`](#re-module);
- private modules under Erlang library code: `clause_unicode`, `clause_re`,
  `clause_zlib` ([zlib](#zlib-module)) and `clause_receive`
  ([prim eval](#prim-eval)).

Which function of a library module is a builtin, an Erlang body or a stub is
recorded in its inventory `library/<application>/<module>.terms`
([library](library.md#files)); `erlang`'s is
[erlang.terms](#erlang-module).

An entry's index is the number generated code passes to the bridge service;
compiler and runtime are built together, so the catalog may be reordered. A
qualified call of a catalog builtin of any module (`io:format(F, A)`) calls
the bridge. Every other `erlang` function reaches the
[erlang library module](#erlang-module).

## How calls reach them

| Source | Path |
| --- | --- |
| `abs(X)`, `X + Y`, `erlang:display(X)`, `halt()`, `error(R)` | Inline code or a dedicated runtime service |
| `erlang:function_exported(M, F, A)`, `A -- B` in a body (catalog builtins without an inline service) | Entered like a function: `CLAUSE_builtin_frame_v1(context, index)` gives the builtin's frame, the arguments go in the registers ([portions](#portions)) |
| `integer_to_list(I)`, `atom_to_list(A)`, `setelement(I, T, V)`, ... (direct builtins: never trap, wait or act on another process) | Called at once: `CLAUSE_builtin_call_v1(context, index, arguments)` returns the result ([ABI](abi.md#runtime-services)) |
| `length(L)`, `binary_to_list(B)`, `list_to_binary(L)`, `iolist_to_binary(L)` (quick builtins) | `CLAUSE_builtin_call_v1` runs a bounded fast path on the argument words (at most 256 cells, elements or bytes, 4096 result bytes); it returns 0 for longer inputs, other shapes and errors, and the call then enters the builtin's frame |
| `A ++ B` | A short proper `A` is copied inline; otherwise entered like a function ([ABI](abi.md#frames-and-transfers)) |
| `fun abs/1`, `fun erlang:'+'/2` | External fun `erlang:F/A`; registration binds it to the builtin |
| `M:F(Args)`, `apply(M, F, Args)`, runtime `fun M:F/A` | The code server finds a module's export first, then a builtin |

- `fun F/A` of an auto-imported builtin the module neither defines nor
  suppresses (`-compile({no_auto_import, ...})`) is the external fun
  `erlang:F/A`, as in OTP: `fun abs/1 =:= fun erlang:abs/1` and it prints as
  `fun erlang:abs/1`. The auto-imports are OTP's (flag `auto_import` in
  [erlang.terms](#erlang-module)); others, such as `display/1`, `raise/3`,
  `function_exported/3`, `make_tuple/2,3`, `port_info/1,2`, `port_call/2,3`
  and `ports/0`, need the `erlang:` prefix.
- A builtin has a `FrameDescriptor` with a null body (`BuiltinFrame`).
  Entering it (`CLAUSE_enter_v1`, `CLAUSE_tail_v1`) pushes no frame: the
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
  - The binary twins (`atom_to_binary/1,2`, `binary_to_integer/1,2`,
    `float_to_binary/1,2`, `list_to_float/1`, ...) share these text rules
    (`erlang/conversion_text.cpp`): float text needs digits on both sides of
    the point (`list_to_float("1e10")` is `badarg`), an exponent past the
    range is `badarg` and an underflow `0.0`; encodings are `utf8`,
    `unicode` and `latin1`.
- `function_exported(M, F, A)` raises `badarg` unless `M` and `F` are atoms
  and `A` a small integer; it is true when a module of the program exports
  `M:F/A` or `M:F/A` is a registered builtin.

## Portions

Builtins whose work grows with a list or binary argument run in bounded
portions, like OTP's trapping BIFs, so a long builtin cannot keep other
processes from running ([processes](processes.md)).

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
- A builtin may also continue in an Erlang export: `io` output to a process
  device traps into `file:write/2` ([io](io.md#calls)).
- Results, errors and evaluation order are the same as running to completion.
  An error found late (an improper tail) is raised when the walk reaches it.
- Host calls (`call_builtin`) continue every trap at once.
- List walks validate the list once and read its cells as words. Small
  inputs take word-level fast paths without portion state: `Left ++ Right`
  with at most 64 cells in `Left`, `integer_to_list/1` of a small integer,
  `list_to_integer/1` of a short decimal string, `atom_to_list/1`,
  `list_to_tuple/1` and `tuple_to_list/1`.

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

Builtins that check their own arguments are C++ functions of typed parameters
([typed.hpp](../runtime/src/builtins/typed.hpp)),
`Result Function(ProcessContext &, Parameters...)`, registered with
`typed_entry<Function>(module, name)` (the arity is the parameter count).

- The adapter admits every argument word in order (a word this process does
  not own is the failure it is, never `badarg`), then converts each to its
  parameter type (`Term`, a small or any integer, `double`, list, tuple,
  binary, atom, map or local pid arguments; see `typed.hpp`); a mismatch
  raises `badarg` and the function does not run. `Named<Role>` is any term
  under a role type of its own (`collections::Key`, `Value`, `List`, ...), so
  a builtin of several term parameters cannot have two swapped; the builtin
  checks the term.
- A function returns a term, a `BuiltinResult` (`std::expected` with a
  `BuiltinFailure`) or a raw `Word` it published itself, or throws
  `BuiltinFailure` (an Erlang error or a term access failure);
  `call_builtin` turns every other C++ exception into `out_of_memory` or
  `internal_error`, so none crosses the generated-code ABI.
- Most families are typed. The guard BIFs and operators (`erlang/core.cpp`),
  the trapping list builtins and a few others are word-level `BuiltinBody`
  adapters, which pass their argument words to the inline services generated
  code also calls. Hot conversions read their arguments' cells in place
  (`TermAccess::trusted`) instead of admitting `Term`s: `tuple_to_list/1`,
  `phash2/1` (lists, tuples and small integers), and `list_to_atom/1`,
  `list_to_integer/1` of short ASCII strings, and `binary:part/2,3`, a direct
  builtin whose short result is written straight into heap words (word-level
  builtin arguments).

## Registration

- `BuiltinRegistry` ([builtin_registry.hpp](../runtime/include/clause/runtime/builtin_registry.hpp)),
  owned by the `CodeServer`, maps exact module/function/arity to a
  `BuiltinFrame`. `add` takes a batch of `BuiltinEntry`s and registers all or
  none: an empty name, more than 255 arguments, a missing implementation, or a
  name already registered (or repeated in the batch) rejects the batch with
  nothing kept.
- Runtime startup registers every family table of `production_builtins()`
  (`builtins/registry.cpp`), which together cover the catalog.
- Builtins are sticky: when a module registers, the code server skips every
  export that has the name of a registered builtin, so `M:F(Args)`,
  `apply/3` and external funs bind the builtin, never a library module's
  `clause_builtin_placeholder` body ([placeholders](library.md#provided-or-missing)) or a
  same-named user function.
- A body reads exactly its arity of argument words and records errors in the
  checked channel; host exceptions become `out_of_memory` or `internal_error`
  failures.
- The host path `abi::v1::dispatch_builtin` (host-registered native modules by
  name, [runtime](runtime.md#code-server-and-builtins)) is separate.

## Source layout

- The runtime implements builtins in `runtime/src/builtins/`. A large module
  gets its own subdirectory with one translation unit per builtin family
  (`erlang/`, `lists/`, `binary/`, `unicode/`, `re/`, `zlib/`); small modules
  and the bridge infrastructure (`registry`, `bridge`, `portions`, `typed`,
  text and float formatting) are single files.
- Each family file defines its own `*_builtins()` table listed in
  `production_builtins()`. A new family adds a new file and table instead of
  growing an existing one; a file past about 400 lines is split by family
  before it grows further.
- The compiler never reads runtime sources: the catalog
  `abi::v1::bridge_builtins` is the single list both sides share; its group
  comments name the implementing file.

## Erlang module

The compiler knows which `erlang` functions exist from two owned sources,
never from runtime code:

- `library/erts/erlang.terms`: OTP 29's `erlang` exports as
  `{Name, Arity, Flags, Status}` (flags `auto_import`, `guard`; status
  `implemented` or `{unavailable, Reason}`: `distribution`, `tracing`,
  `code_loading`, `not_selected`).
  `tests/compiler/builtins/observe_erlang.escript` observes the host OTP and
  keeps the statuses, which are maintained by hand.
- `tests/compiler/builtins/generate_erlang.py` turns it into
  `library/erts/erlang.erl` (Clause's own stub module, a placeholder body for
  each catalog builtin) and
  `compiler/src/semantic/erlang_auto_imports.inc` (OTP's auto-import list).
  CTest `builtins_erlang_inventory` checks both are current and that
  `implemented` means a catalog builtin, a function the compiler handles
  (`apply/2,3`, `is_record/1,2,3`) or a real Erlang body (`nif_error/1,2`
  raise `error(Reason)` like OTP's stubs); `builtins_erlang_freshness`
  (label `host_otp`) compares the inventory with the host OTP.

Calls:

- `erlang:F(...)`, `fun erlang:F/A`, `apply(erlang, F, [...])` and
  `-import(erlang, ...)` of a builtin go to the builtin. Any other export
  joins the program as the library module `erlang`; its stubs raise
  `error({not_implemented, F, A})` with `erlang:F/A` on top of the stack, only
  when called. A name the module does not export is
  `undefined function erlang:F/A`.
- An unqualified call or `fun F/A` of an OTP auto-import (`get(K)`,
  `fun put/2`) that the module neither defines, imports nor suppresses with
  `-compile({no_auto_import, ...})` reaches `erlang:F/A` the same way.

## External term format

`erlang/term_encoding.cpp`, `erlang/term_decoding.cpp`. Both directions share
the codec of [external_term.hpp](../abi/include/clause/abi/external_term.hpp)
with the compiler, and walk terms with explicit stacks, so nesting depth costs
no native stack. The encoder reads the cells of its validated argument in place
(`TermAccess::trusted`, no per-cell address lookup); the decoder collects
children on one shared stack and writes tuples, lists and strings straight into
heap words it claims once per container (`TermAccess::claim`).

- `term_to_binary/1,2` writes OTP's bytes for a non-distributed node
  (`nonode@nohost`, creation 0): UTF-8 atoms, `NEW_FLOAT`, `STRING_EXT` for
  byte lists up to 65535 elements, maps in key order, pids, ports and
  references in their `NEW_PID`, `V4_PORT` and `NEWER_REFERENCE` forms, and
  `EXPORT_EXT` for `fun M:F/A`; a local fun or a native record is
  `error:notsup`. Options: `{minor_version, 0..2}` (0 writes float text, 0
  and 1 Latin-1 atoms), `deterministic`, `compressed` and
  `{compressed, 0..9}` ([zlib](#zlib-module)); anything else is `badarg`.
- `term_to_iovec/1,2` returns the encoding as a list of one binary;
  `external_size/1,2` its exact size.
- `binary_to_term/1,2` checks every length against the input before
  allocating, ignores bytes after the term, inflates compressed input, and
  returns `badarg` for malformed input, invalid atom text, non-finite floats,
  repeated map keys and identities of another node; `safe` admits only
  existing atoms, `used` returns `{Term, BytesRead}`. CTests:
  `executables_external_terms` (OTP bytes) and `builtins_etf_fuzz` (mutated
  encodings decode or raise).

## Time

`erlang/time.cpp`. Monotonic time is the steady clock; `native` and
`perf_counter` are nanoseconds; units are the OTP atoms (including the
deprecated plural ones) or a positive parts-per-second integer.
`convert_time_unit/3` rounds down, as OTP. `unique_integer/0,1` draws from one
program-wide counter, so every value is positive, unique and increasing
whatever the options (`positive`, `monotonic`; others `badarg`). `now/0` is
system time made strictly increasing. Calendar conversions read
`{{Y, M, D}, {H, Mi, S}}` strictly (an invalid date or time is `badarg`) and
use the C library's local time zone. Timers:
[processes](processes.md#timers).

## System information

`erlang/system.cpp`.

- `statistics/1`: `wall_clock` and `runtime` (`{Total, SinceLastCall}` in
  milliseconds), `run_queue`, `total_run_queue_lengths`,
  `total_active_tasks`, `context_switches` (time slices run). Clause keeps no
  reduction, garbage collection or I/O counters: `reductions`,
  `exact_reductions`, `garbage_collection` and `io` report zeros.
- `system_info/1`: the release and build items (`otp_release` `"29"`,
  `version` `"17.1"`, the ERTS Clause follows, `machine` `"Clause"`,
  `system_version`, `system_architecture`, `wordsize`, `endian`, ...),
  process, port and atom counts and limits, scheduler and processor counts
  and the fixed emulator answers (`smp_support`, `threads`,
  `time_warp_mode`, ...); the full list is in `system.cpp`.
- `memory/0,1`: OTP's types; `processes` is the runtime-wide account of
  process memory, `atom` an estimate from the atom count, `binary`, `code`
  and `ets` are 0.
- Other items are `badarg`.

## Hashing

`erlang/hashing.cpp`, `erlang/md5.cpp`.

- `phash2/1,2` is OTP's `make_hash2` and `phash/2` its `make_hash` (plan 21
  step LIB2), walking list and tuple cells in place: atoms (by OTP's hash of
  their spelling), integers, floats, binaries and bitstrings, lists, tuples
  and maps (order-independent) hash to OTP's values, so `dict` and version 1
  `sets` lay out their buckets as OTP does. Pids, ports and references hash
  by Clause's numbers, funs by module, arity or index and captures, native
  records by field values ([differences](differences.md)). `phash2/1` is in
  0..2^27-1, `phash2/2` in 0..Range-1, `phash/2` in 1..Range; Range is
  1..2^32, else `badarg`.
- `md5/1` and `md5_init/0`, `md5_update/2`, `md5_final/1` give RFC 1321's
  digests (OTP's bytes) of iodata. The context is an opaque 96-byte binary
  of Clause's own layout; another binary is `badarg`.

## Code loading

`erlang/code.cpp`. Every module is linked into the program and stays loaded;
dynamic code is deferred ([step 71](../.agents/11-plan.md#step-71)).
`module_loaded/1` is true for a compiled module or a module with runtime
builtins (`erlang`, `io`, `os`, ...); `loaded/0` and `pre_loaded/0` list them
all. `delete_module/1` returns `undefined` for an unknown module and raises
`error:notsup` for a loaded one. `prepare_loading/2` returns
`{error, badfile}` (no binary is code the program can load) and
`finish_loading/1` accepts only `[]`. `get_module_info(M[, Item])` continues
as `M:module_info(...)`.

## Lists module

`runtime/src/builtins/lists/`. OTP's `lists` is Erlang code (apart from a few
BIFs), so Clause's builtins raise what OTP's clauses raise:

- `sublist/2,3`, `droplast/1`, `nthtail/2`, `last/1`, `duplicate/2`,
  `join/2`, `flatten/1,2`, `flatlength/1`, `delete/2`, `concat/1`, `max/1`,
  `min/1`, `usort/1`, `umerge/1,2`, `merge/1,2`, `merge3/3`, `umerge3/3`,
  `keymerge/3`, `ukeymerge/3`, `ukeysort/2`, `keydelete/3`, `keyreplace/4`,
  `keystore/4`, `keytake/3`, `zip/2,3`, `zip3/3,4`, `unzip/1`, `unzip3/1` and
  `enumerate/1,2,3` raise `function_clause` for an argument no OTP clause
  accepts, and walk lists as lazily as OTP: `sublist([a | b], 1)` is `[a]`,
  `nthtail(1, [a | b])` is `b`, `flatten/2` does not check its tail beyond
  `is_list`, a merge returns the other list as it is when one is `[]` and
  raises `badarg` for an improper rest it appends, and a key merge raises
  `badarg` for an element that is no tuple of the key's size, as `element/2`.
- `zipwith/3,4` and `zipwith3/4,5` are Erlang over the `zip` builtins, which
  pair the elements first; `flatmap/2`, `keymap/3`, `search/2`, `umerge/3`
  and `usort/2` are Erlang too. Inside `lists.erl` a builtin is called
  qualified (`lists:zip(...)`): a local call would reach its placeholder. The
  undocumented `rmerge/2,3`, `rmerge3/3`, `rumerge/2,3`, `rumerge3/3`,
  `rkeymerge/3` and `rukeymerge/3` (OTP's sort helpers) stay stubs.
- `keymember/3` and `keysearch/3` are BIFs in OTP: `badarg` for a position
  that is not a positive small integer or an improper tail reached before a
  match.
- Keys and order compare with `==` and term order; of elements comparing
  equal the first wins (`max([1, 1.0])` is `1`, `usort([1.0, 1])` is
  `[1.0]`, `umerge/1,2` keep the element of the earlier list).
- They run to completion. Functions that take funs (`foreach/2`,
  `mapfoldl/3`, `splitwith/2`, `filtermap/2`, `uniq/2`, ...) are Erlang in
  `library/stdlib/lists.erl`, because a builtin cannot call a fun and get its
  result back; so are the other implemented functions, with OTP's clauses
  and guards.
- A `function_clause` from a builtin has no frame of its own
  ([differences](differences.md)).

## Math module

`runtime/src/builtins/math.cpp`. Every OTP 29 `math` function is a builtin
over the C library (`pi/0` and `tau/0` are constants). Arguments are numbers
converted to floats: anything else is `badarg`, an integer too large for a
float `badarith`. A result that is not a finite float (a domain error such as
`sqrt(-1)`, `log(0)`, `acos(2)`, or an overflow such as `pow(10, 400)`,
`exp(1000)`) is `badarith`. `floor/1` and `ceil/1` return floats. Results are
the host C library's, which can differ from OTP's in the last bit
([differences](differences.md)). On 64-bit targets generated code computes
`sqrt`, `floor`, `ceil` (LLVM intrinsics) and `exp`, `log`, `log2`, `log10`,
`sin`, `cos`, `pow` (the same C library functions, called without LLVM's
substitutions) of number arguments inline into a float cell; other operands
and non-finite results call the builtin, which raises.

## Binary module

`runtime/src/builtins/binary/`. `replace/3,4` (including a fun replacement
and `{insert_replaced, ...}`) and `list_to_bin/1` are Erlang in
`library/stdlib/binary.erl` over the builtins; every other function is a
builtin:

- Patterns are a non-empty binary, a non-empty list of non-empty binaries or
  a compiled pattern. A match is the leftmost position and there the longest
  pattern; `matches/2,3` and `split/2,3` with `global` take non-overlapping
  matches from the left. `{scope, {Start, Length}}` limits where matches may
  start and end (a negative length counts back); `split/3` also takes
  `global`, `trim` and `trim_all`.
- `compile_pattern/1` returns `{bm, [Pattern]}` or `{ac, Patterns}`, Clause's
  own shape (OTP: a reference inside the tuple, [differences](differences.md)).
- `part/2,3`, `bin_to_list/1,2,3`, `at/2`, `first/1`, `last/1`, `copy/1,2`,
  `longest_common_prefix/1`, `longest_common_suffix/1`, `join/2`,
  `decode_unsigned/1,2`, `encode_unsigned/1,2`, `encode_hex/1,2` and
  `decode_hex/1` follow OTP; every wrong argument (a bitstring, a range
  outside the subject, an unknown option) is `badarg`.
- `referenced_byte_size/1` is the binary's own size.
- They run to completion.

## Maps module

`runtime/src/builtins/maps.cpp`, `runtime/src/builtins/os.cpp`.

- Every documented `maps` function works. `maps:get/3`, `is_key/2`,
  `remove/2`, `new/0`, `size/1`, `merge/2`, `take/2`, `update/3`, `with/2`,
  `without/2`, `from_keys/2`, `intersect/2` and `is_iterator_valid/1` are
  builtins with OTP's errors: a non-map is `{badmap, Map}`, a missing key of
  `update/3` `{badkey, Key}`, a key list that is not a proper list `badarg`.
  Keys match exactly (`1` and `1.0` are different keys).
- The functions taking funs are Erlang in `library/stdlib/maps.erl`:
  `filter/2`, `filtermap/2`, `map/2`, `fold/3` and `foreach/2` take a map or
  an iterator (anything else is `{badmap, MapOrIter}`), `intersect_with/3`
  combines over `intersect/2` and `merge_with/3` over `merge/2`.
  `groups_from_list/2,3` call the funs last element first, as OTP does, and
  the private builtin `clause_maps:groups/1` groups the `{Key, Value}`
  pairs. Iterators (`iterator/1,2`, `next/1`) use
  [`erts_internal:map_next/3`](#erts-internal).
- Map cursors (`runtime/src/terms/map_cursor.hpp`) walk a flat map cell and an
  map tree alike, as words in key order, with no `Term` per entry. The
  private builtin `clause_maps:entries/1` returns `{K1, V1, K2, V2, ...}` from
  one, so `maps:fold/3` reads the associations in place with `element/2`
  instead of building a list of pairs; `map_next/3` steps with one too.
- `os:type/0`, `os:getenv/0,1` (`getenv/0`: `"Name=Value"` strings; Windows'
  per-drive `=C:` entries are left out), `os:env/0` (`{Name, Value}` pairs),
  `os:putenv/2`, `os:unsetenv/1` (`badarg` unless the arguments are strings
  and the name is non-empty without `=`), `os:getpid/0` and `os:version/0`
  are builtins; `os:timestamp/0`, `os:system_time/0,1`,
  `os:perf_counter/0,1` and `os:getenv/2` are Erlang over the `erlang` time
  builtins and `getenv/1`; `os:cmd/1` runs over a port
  ([subprocesses](ports.md#subprocesses)).

## Collections

`runtime/src/builtins/collections/` (plan 21 steps LIB1–LIB3B): `ordsets`,
`orddict`, `proplists`, `dict`, `sets`, `gb_trees`, `gb_sets`, `queue` and
`array`, every documented function.

- The walks over lists are builtins: `ordsets` membership, insertion,
  deletion and the set algebra (`union/2`, `intersection/1,2`,
  `subtract/2`, `is_subset/2`, `is_disjoint/2`, `is_set/1`); `orddict`'s
  lookups and updates without funs (`store/3`, `append/3`, `append_list/3`,
  `update_counter/3`, `take/2`, `from_list/1`, ...); all `proplists`
  functions but `property/1,2`, `get_value/2`, `normalize/2`, `to_map/2` and
  `from_map/1`, which are Erlang glue in `library/stdlib/`.
- The functions taking funs (`ordsets:fold/3`, `filter/2`, `map/2`,
  `filtermap/2`, `orddict:update/3,4`, `fold/3`, `map/2`, `filter/2`,
  `merge/3`) are Erlang.
- Each builtin walks its lists as OTP's clauses do, so results and errors
  agree for any input: elements and keys compare with `<` and `==` (of `1`
  and `1.0` the representative OTP keeps is kept, including after `union/2`
  swaps its sets), a rest OTP returns unchecked (`ordsets:union([], X)` is
  `X`) is returned as it is, a shape no clause accepts is `function_clause`,
  a comprehension's non-list input `{bad_generator, Rest}`, `Old ++ New` of
  an improper `Old` `badarg` and `update_counter/3` of a non-number
  `badarith`. `proplists` keys match with `=:=`.
- `proplists:get_keys/1` lists the keys in the order of a map's keys, as
  OTP's set of keys does ([differences](differences.md)).
- `dict` and `sets` (plan 21 step LIB2): a dict and a version 1 set are OTP's
  records over a linear hash table (`collections/linear_hash.cpp`), segments
  of 16 bucket lists with slots from `erlang:phash/2`, expanded and
  contracted exactly as OTP does, so the records, `to_list/1` and fold orders
  equal OTP's. A version 2 set (the default) is a map with `[]` values; two
  maps intersect and subtract with OTP's values (a map's own values once
  enough keys are kept, else `[]`). A dict's lookups and updates, `sets`
  operations without funs, `new/0` and `from_list/1` are builtins; `update`,
  `fold`, `map`, `filter`, `merge` and `sets:new/1`, `from_list/2`, `fold`,
  `filter`, `map`, `filtermap` are Erlang, the hashed ones over the private
  builtins `clause_hashed:entries/1` (storage order), `with_values/2` and
  `keep/2`. A term that is not a dict raises `{badrecord, Term}` (`size/1`,
  `is_empty/1` `function_clause`), one that is not a set `function_clause`.
- `gb_trees` and `gb_sets` (plan 21 step LIB3) share a balanced tree engine
  (`collections/balanced_tree.cpp`): insertion checks Andersson's weight rule
  on the way back up and rebuilds a subtree from its sorted entries as OTP
  does, deletion lifts the bigger side's smallest node, and `gb_sets`' union,
  intersection and difference merge the sorted lists or insert and look up by
  OTP's size rule (`N2 < 10` or `N2 < N1 * round(1.46 ln N2)`), building
  merged trees right-heavy as OTP's `balance_revlist`, so every tree equals
  OTP's. `map`, `fold`, `filter`, `filtermap` and the constants are Erlang.
- `queue` (plan 21 step LIB3): OTP's `{Rear, Front}`; every operation moves
  items between the lists as OTP's does (a one-item side moves, a list left
  alone splits in half), so queues equal OTP's. A term that is not a pair of
  lists is `badarg`, an empty queue's `get`, `drop`, `head` the error
  `empty`. The functions taking funs and the Okasaki names are Erlang.
- `array` (plan 21 step LIB3B): OTP 29's `#array{}` record, a tuple tree of
  16-entry leaves (`[]` for an empty subtree) with one cached leaf and a zero
  offset; `set`, `append`, `shift`, `prepend`, `slice`, `reset`, `resize` and
  the builders (`from_list`, `from_orddict` with its gap markers, `from`)
  change the record exactly as OTP does. The folds and mapfolds are Erlang
  over the private builtins `clause_array:values/3`, `sparse/3` and
  `replace/4`, the last rebuilding a mapped range as OTP's mapfolds do (empty
  subtrees unfolded unless the map is sparse) and reading the cache again.

## Encodings

`runtime/src/builtins/encoding/` (plan 21 step LIB5).

- `base64`: every documented function is a builtin. Encoding takes a binary
  or a list of integers (OTP's arithmetic on them: `badarith` for a
  non-integer, `badarg` past the alphabet table); decoding walks its input as
  OTP's state machines do, so the errors agree for any input: whitespace is
  skipped, input ending after one character of a group is
  `function_clause`, after two or three `missing_padding` unless
  `padding => false`, a character outside the alphabet `badarg` when its
  group is built (`badarith` inside OTP's shifts), anything but whitespace
  after the padding `{case_clause, Symbol}`. `mime_decode` skips every
  character outside the alphabet and data after a misplaced `=`.
- `json` (plan 21 step LIB5E, `encoding/json.cpp`): `decode/1` is a builtin
  walking OTP's decoder states over an explicit stack of open containers, so
  values (an object keeps a key's first value), the rest after the value and
  the errors agree: `unexpected_end` where more input could complete the
  text, `{invalid_byte, Byte}` at the byte OTP rejects (for a cut literal the
  byte after its first), `{unexpected_sequence, Bytes}` for a bad `\u`
  escape or a float `binary_to_float/1` rejects. `encode_binary/1` and
  `encode_binary_escape_all/1` are builtins building OTP's nested iodata, and
  so are the default paths `encode/1` and `format/1,2` (step LIB5F,
  `encoding/json_encode.cpp`, through `clause_json:encode/1` and
  `format/2`); past 256 levels of nesting, or for format options other than
  small non-negative integers, they answer `fallback` and `json.erl` builds
  the same iodata itself. The encoders and formatters taking funs and the
  callback decoders `decode/3`, `decode_start/3`, `decode_continue/2` are
  Erlang; the decoders walk OTP's states over the private scanners
  `clause_json:string/2` and `number/2`, so callbacks run in OTP's order and
  continuation states are OTP's tuples.

## Filename

`runtime/src/builtins/filename/` (plan 21 step LIB5G).

- `flatten/1`, `join/1,2`, `absname_join/2`, `append/2`, `basename/1,2`,
  `dirname/1`, `extension/1`, `rootname/1,2`, `split/1`, `pathtype/1`,
  `nativename/1` and `validate/1` are builtins following OTP's clauses for
  the host's family: Unix `/`, or Windows `/` and `\`, drive letters (an
  upper-case one lowered by `join` and `split`) and `//` network names. Lists
  are walked as OTP's clauses open them (nested lists and atoms where a
  clause opens them, `List ++ Rest` raising `badarg` for an improper one),
  so results keep OTP's shapes: the elements of the name, binaries kept as
  elements by `flatten/1`, sub-lists and sub-binaries, and OTP's
  `function_clause` where a clause meets a non-character.
- `absname/1,2` (the current directory and a drive's own) and
  `basedir/2,3` (the environment) are Erlang over them.

## Random numbers

`runtime/src/builtins/rand/` (plan 21 step LIB6).

- `rand`: states are OTP's `{AlgHandler, AlgState}` with OTP's handler maps
  (their funs are `rand`'s own) and OTP's state layouts, so exported seeds,
  sequences and jumped states equal OTP's for every algorithm (`exsss`,
  `exro928ss`, `exrop`, `exs1024s`, `exsp` and the deprecated `exs64`,
  `exsplus`, `exs1024`, and `dummy`). For a handler equal to OTP's own the
  private builtins `clause_rand:seed/2`, `next/2`, `uniform/2,3`,
  `uniform_real/2`, `bytes/3`, `normal/2`, `shuffle/3` and `jump/2` do the
  work in C++: seeding (SplitMix64, lists, OTP's 3-tuple hashes and seed
  errors), ranges past the generator's bits, dense floats, bytes, the normal
  ziggurat and the shuffle's bit stream. Any other handler is a plug-in:
  `rand.erl` runs OTP's generic algorithms over its funs.
- `splitmix64_next/1`, `exsp_next/1`, `exsp_jump/1` and the `mwc59` functions
  are builtins; `mwc59_seed/0` hashes `rand`'s default seed.
- `tools/rand/observe.escript` observes OTP's constants through `rand`'s
  plug-in API: the ziggurat's strip widths (fast-path products) and bounds
  (where a strip's words reach the slow path), and jumped states, from which
  `tools/rand/generate.py` solves each jump polynomial over GF(2) (checked on
  a second state) and writes `tables.cpp`; strip heights are computed at high
  precision ([differences](differences.md#random-numbers)).
- `random` (deprecated): `uniform_s/1` is a builtin of the Wichmann-Hill
  generator; the seeding and process dictionary functions are Erlang.

## Calendar

`runtime/src/builtins/calendar.cpp` (plan 21 step LIB5).

- The Gregorian arithmetic is builtins over integers of any size with
  Erlang's truncating `div` and `rem`, by OTP's era formulas:
  `date_to_gregorian_days/1,3`, `gregorian_days_to_date/1`,
  `datetime_to_gregorian_seconds/1`, `gregorian_seconds_to_datetime/1`,
  `day_of_the_week/1,3`, `is_leap_year/1`, `last_day_of_the_month/2`,
  `valid_date/1,3`, `seconds_to_time/1`, `seconds_to_daystime/1`,
  `time_to_seconds/1`, `time_difference/2`, `iso_week_number/1`. A shape OTP's
  clauses reject is `function_clause`, a non-integer it computes with
  `badarith`; out-of-range dates are computed as OTP computes them.
- Local and universal time, system time and RFC 3339 parsing and formatting
  are Erlang in `library/stdlib/calendar.erl` over the `erlang` time builtins
  and `proplists`, with OTP's errors (`{badarg, [Arg, Options]}` out of range,
  `{badmatch, _}` and `{case_clause, _}` for a malformed offset).

## Persistent terms

`runtime/src/builtins/persistent_term.cpp`, over the runtime's table
([runtime](runtime.md#persistent-terms)). `put/2` and `put_new/2` return `ok`
(`put_new/2` of a stored key is `ok` for an exactly equal value, else
`badarg`); `get/1` of a key not stored is `badarg`, `get/2` returns the
default; `get/0` lists every `{Key, Value}`; `erase/1` says whether the key
was stored; `info/0` is `#{count => N, memory => Bytes}` (the words of the
stored copies). Keys match exactly (`1` and `1.0` differ). Every read copies
the value into the caller's heap.

## Unicode module

`runtime/src/builtins/unicode/`. `characters_to_binary/1,2,3`,
`characters_to_list/1,2`, `bin_is_7bit/1`, `bom_to_encoding/1` and
`encoding_to_bom/1` are builtins; the normalization, category and identifier
functions of `library/stdlib/unicode.erl` are stubs.

- Encodings: `latin1`, `unicode`/`utf8`, `utf16` and `utf32`, big endian by
  default or `{utf16 | utf32, big | little}`; anything else is `badarg`
  (`encoding_to_bom/1`: `function_clause`, as OTP).
- Chardata is a binary or a list, possibly improper with a binary tail, of
  code points, binaries in the input encoding and nested chardata;
  consecutive binaries decode as one stream, so a character may span two.
  Anything else is `badarg`.
- A character the output cannot take (an invalid code point or byte
  sequence, a code above 255 for `latin1` output) ends the conversion with
  `{error, Converted, Rest}`; input ending inside a character with
  `{incomplete, Converted, Rest}`. `Rest` has OTP's shape: the unconverted
  part of the innermost list headed by the failing element (a binary as its
  unconverted suffix, alone when it ends the top level), inside the remaining
  tail of every enclosing list; unfinished bytes carried from a binary come
  first, and with `latin1` output a failing integer is wrapped in a list.
- `bin_is_7bit/1` is false for anything but a binary of bytes below 128.

## Code module

`runtime/src/builtins/code.cpp`. No `code_server` process exists; the queries
about loaded modules answer for the linked program: a module is loaded when
the program has it (compiled code or runtime builtins). `ensure_loaded/1`
gives `{module, M}` or `{error, nofile}`, `ensure_modules_loaded/1` `ok` or
`{error, [{M, nofile}]}`, `is_loaded/1` `{file, preloaded}` or `false`,
`which/1` `preloaded` or `non_existing`, `module_status/1` `loaded` or
`not_loaded`, `all_loaded/0` and `module_status/0` list every module,
`is_sticky/1` is `false`. A module argument that is not an atom is
`function_clause`. OTP's `io_lib:format/2` error path, which calls
`code:ensure_modules_loaded/1`, therefore raises the original `badarg`. Code
paths and loading are stubs ([step 71](../.agents/11-plan.md#step-71)).

## Init module

`runtime/src/builtins/init.cpp`, `library/erts/init.erl`. An executable is a
started system without a boot script. Startup decodes the command line once
(`ProgramArguments`, `runtime/src/startup/arguments.cpp`): the arguments after
the runtime options are both the arity-1 entry's argv and
`get_plain_arguments/0`. There are no emulator flags; the only flag is
`home`, set at startup as OTP's `erlexec` sets it (`HOME`; on Windows
`HOMEDRIVE` and `HOMEPATH`, else `USERPROFILE`), so `get_argument(home)` is
`{ok, [[Home]]}`, `get_arguments/0` is `[{home, [Home]}]` and every other
flag is `error`. `script_name/0` is the executable's absolute path with
forward slashes. These four are builtins; the rest are Erlang:

- answers of OTP's started `init`: `get_status/0` `{started, started}`,
  `notify_when_started/1` `started`, `ensure_loaded/1` `not_allowed`,
  `fetch_loaded/0` `[]`, `objfile_extension/0` `".beam"`,
  `archive_extension/0` `".ez"`, `code_path_choice/0` `strict`,
  `run_on_load_handlers/0` `ok`, `wait_until_started/0` (waits for
  `{init, started}`); `get_configfd/1` and `set_configfd/2` keep their values
  in `persistent_term` under `{init, configfd, Id}`;
- `script_id/0` is `{"Clause", "29"}`;
- `stop/0,1` and `reboot/0` halt at once with OTP's status rules: a
  non-negative integer is the exit status, a byte list the halt slogan, any
  other argument `badarg`;
- refused: `boot/1` and `restart/0,1` raise `notsup` after OTP's argument
  checks (`restart/1` of a list that is not `[]` or `[{mode, M}]` is
  `badarg`), `make_permanent/2` returns `{error, notsup}` for valid names and
  `{error, badarg}` otherwise.

## Atomics and counters

`runtime/src/builtins/atomics.cpp`. An `atomics` array is a
[handle reference](runtime-heap.md#handle-references) whose buffer holds a
signedness word and the 64-bit values, updated through `std::atomic_ref`
with sequentially consistent operations; it is shared by every copy of the
reference (messages, persistent terms, `binary_to_term/1` while it lives) and
freed with the last one.

- `new/2` takes an arity from 1 to 2^27 and `[{signed, Bool}]` options (the
  last wins); `get/2`, `put/3`, `add/3`, `add_get/3`, `sub/3`, `sub_get/3`,
  `exchange/3`, `compare_exchange/4` and `info/1` are builtins with OTP's
  `badarg` for a bad reference, index or value. Stored values must fit the
  array (int64 or uint64); increments may be any integer from -2^63 to
  2^64 - 1 and wrap. `info/1` reports OTP's `memory` of 40 + 8 bytes per
  value.
- `counters` is Erlang (`library/erts/counters.erl`): `{atomics, Ref}` and
  `{write_concurrency, Ref}` both wrap a signed atomics array, so
  write-concurrency counters are exact at every read. `info/1` of the
  latter reports the array's size and memory.

## Erts internal

`runtime/src/builtins/erts_internal.cpp`, `library/erts/erts_internal.erl`.
The functions OTP's `stdlib` and `kernel` call:

- builtins `cmp_term/2` (exact term order: an integer before an equal
  float) and `map_next/3`, the protocol of `maps:iterator/1,2` and
  `maps:next/1`: an integer path walks the map in key order and continues
  over the remaining keys, a key-list path takes its keys in order; a list
  accumulator receives every association from the path on;
- Erlang `list_to_integer/2` and `binary_to_integer/2` (the helpers of
  `string:to_integer/1` and the conversion BIFs, `big` beyond a small
  integer of the word size), `map_to_tuple_keys/1` (the sorted keys of any
  map), `term_type/1`, the write-concurrency `counters_*` functions,
  `dynamic_node_name/0,1` over `persistent_term`;
- refused: `get_creation/0` is `undefined` (no distribution) and
  `map_hashmap_children/1` raises `badarg` (no map has hashmap nodes);
- declined: the distribution, `.beam` and emulator-internal groups.

`erts_code_purger:purge/1` answers `{false, false}` and `soft_purge/1`
`true`, since linked code has no old code; the purger process and
`finish_after_on_load/2` are declined
([step 71](../.agents/11-plan.md#step-71)).

## Unicode util

`library/stdlib/unicode_util.erl`, `runtime/src/builtins/unicode/`. OTP
generates `unicode_util` while it builds (900 KB of Erlang); Clause keeps the
data in generated C++ tables (`unicode/tables.cpp`) queried by the builtins of
its private `clause_unicode` module, and `unicode_util.erl` answers every OTP
function over them with OTP's result shapes (`{error, Rest}` or
`{badarg, Rest}` at invalid UTF-8). `gc/1` splits extended grapheme clusters by
UAX #29 in OTP's rule order; `nfd/1`, `nfc/1`, `nfkd/1`, `nfkc/1` normalize
the first cluster; `spec_version/0` is `{17, 0}`.

`tools/unicode/generate.py` writes the tables once per Unicode update: case
mappings, categories, decompositions and the property predicates from the
host OTP's answers for every code point (`tools/unicode/observe.escript`), the
grapheme break, emoji, Indic and composition data from the Unicode Character
Database files OTP 29 pins (Unicode 17.0). Only the generated tables are
committed, with the notice the Unicode License v3 asks for; builds need
neither OTP nor the data files. `executables_unicode_tables` and
`executables_unicode_clusters` compare answers, clusters and normal forms
with OTP's.

## Zlib module

`runtime/src/builtins/zlib/streams.cpp`, `library/erts/zlib.erl`. The runtime
links zlib 1.3.2, the release OTP 29 bundles ([zlib](compile.md#zlib)), so
compressed bytes equal OTP's for every level, strategy, window size and flush.
A stream is a [handle reference](runtime-heap.md#handle-references) whose
buffer holds the `z_stream`; its finalizer ends an active session when the
last copy dies. The private `clause_zlib` builtins (open, close, controller,
init, deflate, inflate, end, reset, parameter and dictionary calls) run one
operation over all their input; `zlib.erl` keeps OTP's API, argument errors
(`bad_compression_level`, `bad_windowbits`, ...), stream states
(`not_initialized`, `already_initialized`, `not_on_controlling_process`),
`eos_behavior`, dictionaries (`{need_dictionary, Adler}`) and the
whole-buffer `compress/1`, `zip/1`, `gzip/1` and their inverses
(concatenated gzip members). A gzip header names the platform, as OTP's zlib
writes it. `term_to_binary/2` with `compressed` or `{compressed, Level}`
compresses as OTP decides (into five bytes fewer than the payload, else
plain).

## Re module

`runtime/src/builtins/re/`, `library/stdlib/re.erl`: OTP 29's exports and
types over the PCRE2 release OTP bundles ([PCRE2](compile.md#pcre2)).
`version/0` is PCRE2's version and date as a binary
(`<<"10.47 2025-10-21">>`, as on OTP).

A compiled pattern is OTP 29's `{re_pattern, CaptureCount, Unicode, UseCRLF,
Ref}`, where `Ref` is a [handle reference](runtime-heap.md#handle-references)
owning the `pcre2_code`; its finalizer frees the code with the last copy, and
processes share it read-only. As in OTP, the C side is small and the rest is
Erlang:

- `clause_re:compile/2` compiles a regex binary with compile options
  (`{ok, Pattern}` or `{error, {Message, Offset}}` with PCRE2's message);
  `re:inspect/2` answers `namelist`.
- `clause_re:run/4` is OTP's `re:run` BIF without its traps: one match of a
  binary subject from `{offset, N}`, compiling a textual regex first, with
  every capture spec and type, `match_limit` and `match_limit_recursion`, and
  OTP's errors (`report_errors` results, `badarg` for compile options or
  other newline conventions given with a compiled pattern).
- `re.erl` converts Unicode chardata to UTF-8 and captures back to
  characters, runs `global` matches one at a time (after an empty match it
  retries anchored and non-empty, then steps one character, or a CR LF pair
  with CRLF newlines, as OTP's `loopexec` does), and implements `replace/3,4`
  (`\N`, `\gN`, `\g{N}`, `&`, a replacement fun) and `split/2,3`
  (`{parts, N}`, `trim`, `group`, `{return, Type}`) with OTP's results.

Exported patterns (`export`, `import/1`) are not provided.
`executables_re_module` compares compiling, matching, captures, global and
Unicode matches, replace, split and argument errors with OTP.

## Prim eval

`runtime/src/process/receive.cpp`, `library/erts/prim_eval.erl`.
`prim_eval:'receive'(Fun, Timeout)` is the receive of `erl_eval`, whose
clauses are a fun: it scans the mailbox from the oldest message with the steps
of a compiled [receive](processes.md#receive), the private `clause_receive`
builtins `peek/0` (`{message, M}` or `none`), `skip/0`, `take/0`, `restart/0`
and `wait/1` (the wait builtin of compiled code, with its timeout and
`timeout_value`). The first message for which `Fun` answers anything but
`nomatch` is removed and that answer returned; `timeout` when `Timeout`
expires. An exception from `Fun` leaves every message in place.
`executables_prim_eval_receive` compares mailbox order, timeouts, a message
arriving during the wait and exceptions with OTP.
