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
  first builtins of another module ([io](io.md)); output devices (plan step
  41U): `io:format/3`, `io:fwrite/1,2,3`, `io:put_chars/2`, `io:nl/0,1`;
- the `lists` data functions (plan step 41T, [lists module](#lists-module));
- every `math` function (plan step 41V, [math module](#math-module));
- the `binary` module (plan step 41W, [binary module](#binary-module));
- `os` environment, process and version queries and the `maps` BIFs (plan step 41X,
  [maps module](#maps-module));
- `persistent_term` (plan step 41M2, [persistent terms](#persistent-terms));
- the `unicode` conversions (plan step 41Y1, [unicode module](#unicode-module));
- the `code` module's loaded-module queries (plan step 41Z, [code module](#code-module));
- the `init` module's command-line answers (plan step 41L, [init module](#init-module));
- `atomics`, `counters` and the `erts_internal` functions stdlib calls (plan step 41N,
  [atomics and counters](#atomics-and-counters), [erts_internal](#erts-internal));
- `unicode_util`'s tables (plan step 41O1, [unicode_util](#unicode-util));
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
  `unicode` and `latin1`; they run to completion;
- the [external term format](#external-term-format) (plan step 41E):
  `term_to_binary/1,2`, `term_to_iovec/1,2`, `external_size/1,2`,
  `binary_to_term/1,2`;
- [time](#time) and unique values (plan step 41F): `monotonic_time/0,1`,
  `system_time/0,1`, `time_offset/0,1`, `convert_time_unit/3`,
  `timestamp/0`, `now/0`, `unique_integer/0,1`, `localtime/0`,
  `universaltime/0`, `date/0`, `time/0`, `localtime_to_universaltime/1,2`,
  `universaltime_to_localtime/1`, `posixtime_to_universaltime/1`,
  `universaltime_to_posixtime/1`; [timers](processes.md#timers):
  `start_timer/3,4`, `send_after/3,4`, `cancel_timer/1,2`, `read_timer/1,2`;
- [process introspection](processes.md#introspection) (plan step 41G1):
  `process_info/1,2`, `processes/0`, `group_leader/0,2`,
  `garbage_collect/0,1,2`, `yield/0`; [system information](#system-information):
  `statistics/1`, `system_info/1`, `memory/0,1`;
- [spawn options and aliases](processes.md#spawn-options) (plan step 41G2):
  `spawn_opt/2..5`, `spawn_request/1..5`, `spawn_request_abandon/1`,
  `alias/0,1`, `unalias/1`, `monitor/3`, `hibernate/3`;
- [fun information](funs.md#fun-information), [hashing](#hashing) and MD5
  (plan step 41G3): `fun_info/1,2`, `fun_to_list/1`, `is_builtin/3`,
  `phash/2`, `phash2/1,2`, `md5/1`, `md5_init/0`, `md5_update/2`,
  `md5_final/1`;
- [nodes](processes.md#nodes) and [code loading](#code-loading) (plan step
  41H): `nodes/0,1,2`, `is_alive/0`, `monitor_node/2,3`,
  `disconnect_node/1`, `module_loaded/1`, `loaded/0`, `pre_loaded/0`,
  `delete_module/1`, `prepare_loading/2`, `finish_loading/1`,
  `get_module_info/1,2`.

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

## Lists module

Plan 11 step 41T (`runtime/src/builtins/lists/`). OTP's `lists` is Erlang code
(apart from a few BIFs), so Clause's builtins raise what OTP's clauses raise:

- `sublist/2,3`, `droplast/1`, `nthtail/2`, `last/1`, `duplicate/2`,
  `join/2`, `flatten/1,2`, `max/1`, `min/1`, `usort/1`, `umerge/1,2` and
  `keydelete/3` raise `function_clause` for an argument no OTP clause
  accepts, and walk lists as lazily as OTP: `sublist([a | b], 1)` is `[a]`,
  `nthtail(1, [a | b])` is `b`, `flatten/2` does not check its tail beyond
  `is_list`.
- `keymember/3` and `keysearch/3` are BIFs in OTP: `badarg` for a position
  that is not a positive small integer or an improper tail reached before a
  match.
- Keys and order compare with `==` and term order; of elements comparing
  equal the first wins (`max([1, 1.0])` is `1`, `usort([1.0, 1])` is
  `[1.0]`, `umerge/1,2` keep the element of the earlier list).
- They run to completion. Functions that take funs (`foreach/2`,
  `mapfoldl/3`, `mapfoldr/3`, `splitwith/2`, `partition/2`, `filtermap/2`,
  `uniq/2`, ...) are Erlang in `library/stdlib/lists.erl`, because a builtin
  cannot call a fun and get its result back; so are `prefix/2`, `suffix/2`
  and `uniq/1` (step 41L), with OTP's clauses and guards.
- A `function_clause` from a builtin has no frame of its own
  ([differences](differences.md)).

## Math module

Plan 11 step 41V (`runtime/src/builtins/math.cpp`). `library/stdlib/math.erl`
holds OTP 29's exports as stubs; every one is a builtin over the C library
(`pi/0` and `tau/0` are constants). Arguments are numbers converted to floats:
anything else is `badarg`, an integer too large for a float `badarith`. A
result that is not a finite float (a domain error such as `sqrt(-1)`,
`log(0)`, `acos(2)`, or an overflow such as `pow(10, 400)`, `exp(1000)`) is
`badarith`. `floor/1` and `ceil/1` return floats. Results are the host C
library's, which can differ from OTP's in the last bit
([differences](differences.md)). On 64-bit targets generated code computes
`sqrt`, `floor`, `ceil` (LLVM intrinsics) and `exp`, `log`, `log2`, `log10`,
`sin`, `cos`, `pow` (the same C library functions, called without LLVM's
substitutions) of number arguments inline into a float cell; other operands and
non-finite results call the builtin, which raises (OPT0.16).

## Binary module

Plan 11 step 41W (`runtime/src/builtins/binary/`). `library/stdlib/binary.erl`
holds OTP 29's exports; `replace/3,4` (including a fun replacement and
`{insert_replaced, ...}`) and `list_to_bin/1` are Erlang over the builtins,
every other function is a builtin:

- Patterns are a non-empty binary, a non-empty list of non-empty binaries or
  a compiled pattern. A match is the leftmost position and there the longest
  pattern; `matches/2,3` and `split/2,3` with `global` take non-overlapping
  matches from the left. `{scope, {Start, Length}}` limits where matches may
  start and end (a negative length counts back); `split/3` also takes
  `global`, `trim` and `trim_all`.
- `compile_pattern/1` returns `{bm, [Pattern]}` or `{ac, Patterns}`, Clause's
  own shape (OTP: a reference inside the tuple, [differences](differences.md)).
- `part/2,3`, `bin_to_list/2,3`, `at/2`, `first/1`, `last/1`, `copy/1,2`,
  `longest_common_prefix/1`, `longest_common_suffix/1`, `join/2`,
  `decode_unsigned/1,2`, `encode_unsigned/1,2`, `encode_hex/1,2` and
  `decode_hex/1` follow OTP; every wrong argument (a bitstring, a range
  outside the subject, an unknown option) is `badarg`.
- `referenced_byte_size/1` is the binary's own size.
- They run to completion.

## Maps module

Plan 11 step 41X (`runtime/src/builtins/maps.cpp`, `runtime/src/builtins/os.cpp`).

- `maps:get/3`, `is_key/2`, `remove/2`, `new/0`, `size/1`, `merge/2`,
  `take/2`, `update/3`, `with/2`, `without/2` and `from_keys/2` are builtins
  with OTP's errors: a non-map is `{badmap, Map}`, a missing key of
  `update/3` `{badkey, Key}`, a key list that is not a proper list `badarg`.
  Keys match exactly (`1` and `1.0` are different keys). `filter/2`,
  `map/2`, `foreach/2` and `update_with/3,4` are Erlang in
  `library/stdlib/maps.erl`.
- `os:getenv/0` (`"Name=Value"` strings; Windows' per-drive `=C:` entries
  are left out), `os:env/0` (`{Name, Value}` pairs), `os:putenv/2`,
  `os:unsetenv/1` (`badarg` unless the arguments are strings and the name is
  non-empty without `=`), `os:getpid/0` and `os:version/0` are builtins;
  `os:timestamp/0`, `os:system_time/0,1`, `os:perf_counter/0,1` and
  `os:getenv/2` are Erlang over the `erlang` time builtins and `getenv/1`.

## Persistent terms

Plan 11 step 41M2 (`runtime/src/builtins/persistent_term.cpp`) over the
runtime's table ([runtime](runtime.md#persistent-terms)). `put/2` and
`put_new/2` return `ok` (`put_new/2` of a stored key is `ok` for an exactly
equal value, else `badarg`); `get/1` of a key not stored is `badarg`,
`get/2` returns the default; `get/0` lists every `{Key, Value}`; `erase/1`
says whether the key was stored; `info/0` is `#{count => N, memory => Bytes}`
(the words of the stored copies). Keys match exactly (`1` and `1.0` differ).
Every read copies the value into the caller's heap.

## Unicode module

Plan 11 step 41Y1 (`runtime/src/builtins/unicode/`). `library/stdlib/unicode.erl`
holds OTP 29's exports and types; `characters_to_binary/1,2,3`,
`characters_to_list/1,2`, `bin_is_7bit/1`, `bom_to_encoding/1` and
`encoding_to_bom/1` are builtins, the normalization, category and identifier
functions stay stubs; they can now be written over [unicode_util](#unicode-util) when a program needs them.

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

Plan 11 step 41Z (`runtime/src/builtins/code.cpp`). `library/kernel/code.erl`
holds OTP 29's exports and types; no `code_server` process exists, and the
queries about loaded modules answer for the linked program: a module is
loaded when the program has it (compiled code or runtime builtins).
`ensure_loaded/1` gives `{module, M}` or `{error, nofile}`,
`ensure_modules_loaded/1` `ok` or `{error, [{M, nofile}]}`, `is_loaded/1`
`{file, preloaded}` or `false`, `which/1` `preloaded` or `non_existing`,
`module_status/1` `loaded` or `not_loaded`, `all_loaded/0` and
`module_status/0` list every module, `is_sticky/1` is `false`. A module
argument that is not an atom is `function_clause`. Code paths and loading
stay stubs until [step 71](../.agents/11-plan.md#step-71). OTP's
`io_lib:format/2` error path (`code:ensure_modules_loaded/1`) now raises the
original `badarg`.

## Init module

Plan 11 step 41L (`runtime/src/builtins/init.cpp`, `library/erts/init.erl`).
An executable is a started system without a boot script. Startup decodes the
command line once (`ProgramArguments`, `runtime/src/startup/arguments.cpp`):
the arguments after the runtime options are both the arity-1 entry's argv
and `get_plain_arguments/0`. There are no emulator flags; the only flag is
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

## Unicode util

Plan 11 steps 41O1 and 41O2 (`library/stdlib/unicode_util.erl`, `runtime/src/builtins/unicode/`). OTP generates
`unicode_util` while it builds (900 KB of Erlang); Clause keeps the data in generated C++ tables
(`unicode/tables.cpp`) queried by the builtins of its private `clause_unicode` module (`table/1`, `class/1`,
`cases/1`, `flags/1`, `compose_pair/2`), and `unicode_util.erl` answers OTP's functions over them with OTP's result
shapes:

- `cp/1` takes the first code point of chardata as OTP does, keeping binary tails and flattening up to three more
  code points of nested lists; `{error, Rest}` at invalid UTF-8;
- `lookup/1`, `category/1`, `get_case/1`, `spec_version/0` (`{17, 0}`), `pattern_whitespace/0`; `uppercase/1`,
  `lowercase/1`, `titlecase/1`, `casefold/1` map the first code point (`{badarg, Rest}` at invalid UTF-8);
  `is_wide/1`, `is_whitespace/1`, `is_other_id_start/1`, `is_other_id_continue/1`,
  `is_letter_not_pattern_syntax/1`;
- `gc/1` (step 41O2): extended grapheme clusters by UAX #29, with OTP's rule order (CR LF, controls, Latin-1 fast
  paths, prepend, Hangul syllables, regional indicator pairs, extended pictographic and ZWJ sequences, Indic
  conjuncts) and its result shapes: a cluster is a code point or a list, and the rest stays the chardata it came
  from where OTP keeps it (`{error, Rest}` at invalid UTF-8);
- `nfd/1`, `nfc/1`, `nfkd/1`, `nfkc/1` (step 41O2) normalize the first cluster: canonical or compatibility
  decomposition with algorithmic Hangul, stable canonical ordering, then composition by the composition pairs.

`tools/unicode/generate.py` writes the tables once per Unicode update: case mappings, categories (including the
neighbor categories OTP's generator gives some unassigned code points), decompositions and the property predicates
come from the host OTP's answers for every code point (`tools/unicode/observe.escript`), the grapheme break, emoji,
Indic and composition data from the Unicode Character Database files OTP 29 pins (`lib/stdlib/uc_spec`, Unicode
17.0). Only the generated tables are committed, with the Unicode copyright notice the Unicode License v3 asks
for; normal builds need neither OTP nor the data files. Test `executables_unicode_tables` compares digests of the
answers of plane 0 and sample blocks of the other planes with OTP's (all 1.1M code points matched when
the tables were generated); `executables_unicode_clusters` compares clusters and normal forms of hand-picked
strings for every rule and a seeded sweep of 2000 random strings.

## Zlib module

Plan 11 step 41Q (`runtime/src/builtins/zlib/streams.cpp`, `library/erts/zlib.erl`). The runtime links zlib
1.3.2, the release OTP 29 bundles ([zlib](compile.md#zlib)), so compressed bytes equal OTP's for every level,
strategy, window size and flush. A stream is a [handle reference](runtime-heap.md#handle-references) whose buffer
holds the `z_stream`; its finalizer ends an active session when the last copy dies. The private `clause_zlib`
builtins (`open/0`, `close/1`, `set_controller/2`, `deflate_init/2`, `inflate_init/3`, `deflate/3`, `inflate/2`,
the end, reset, parameter and dictionary calls) run one operation over all their input; `zlib.erl` keeps OTP's
API, argument errors (`bad_compression_level`, `bad_windowbits`, ...), stream states (`not_initialized`,
`already_initialized`, `not_on_controlling_process`, a closed stream refusing a new init), `eos_behavior` (`cut`,
`reset`, `error`), `data_error` from `inflateEnd/1` before the end of a stream, dictionaries
(`{need_dictionary, Adler}` raised or returned), output kept by `deflateParams/3` for the next `deflate`, and the
whole-buffer `compress/1`, `uncompress/1`, `zip/1`, `unzip/1`, `gzip/1`, `gunzip/1` (concatenated members). A gzip
header names the platform, as OTP's zlib writes it. `term_to_binary/2` with `compressed` or `{compressed, Level}`
compresses as OTP decides (into five bytes fewer than the payload, else plain) and `binary_to_term/1,2` inflates
compressed input.

## Re module

Plan 11 steps 41ZA and 41ZB (`runtime/src/builtins/re/`, `library/stdlib/re.erl`): OTP 29's exports and types
over the PCRE2 release OTP bundles ([PCRE2](compile.md#pcre2)). `version/0` is PCRE2's version and date as a
binary (`<<"10.47 2025-10-21">>`, as on OTP).

A compiled pattern is OTP 29's `{re_pattern, CaptureCount, Unicode, UseCRLF, Ref}`, where `Ref` is a
[handle reference](runtime-heap.md#handle-references) owning the `pcre2_code`; its finalizer frees the code with
the last copy, and processes share it read-only. As in OTP, the C side is small and the rest is Erlang:

- `clause_re:compile/2` compiles a regex binary with compile options (`{ok, Pattern}` or `{error, {Message,
  Offset}}` with PCRE2's message); `re:inspect/2` answers `namelist`.
- `clause_re:run/4` is OTP's `re:run` BIF without its traps: one match of a binary subject from `{offset, N}`,
  compiling a textual regex first (`{error, {compile, Reason}}` with `report_errors`), with every capture spec
  (`all`, `first`, `all_but_first`, `none`, `all_names`, numbers and names, duplicated names) and type
  (`index`, `list`, `binary`), `match_limit` and `match_limit_recursion` (`{error, match_limit}` with
  `report_errors`, else `nomatch`), and OTP's `badarg` for compile options or other newline conventions given
  with a compiled pattern.
- `re.erl` converts Unicode chardata to UTF-8 and captures back to characters, runs `global` matches one at a time
  (after an empty match it retries anchored and non-empty, then steps one character, or a CR LF pair with CRLF
  newlines, as OTP's `loopexec` does), and implements `replace/3,4` (`\N`, `\gN`, `\g{N}`, `&`, a replacement
  fun) and `split/2,3` (`{parts, N}`, `trim`, `group`, `{return, Type}`) with OTP's results.

Exported patterns (`export`, `import/1`) are not provided. `executables_re_module` compares compiling, matching,
captures, global and Unicode matches, replace, split and argument errors with OTP.

## Prim eval

Plan 11 step 41S (`runtime/src/process/receive.cpp`, `library/erts/prim_eval.erl`). `prim_eval:'receive'(Fun,
Timeout)` is the receive of `erl_eval`, whose clauses are a fun: it scans the mailbox from the oldest message
with the steps of a compiled [receive](processes.md#receive), the private `clause_receive` builtins `peek/0`
(`{message, M}` or `none`), `skip/0`, `take/0`, `restart/0` and `wait/1` (the wait builtin of compiled code, with
its timeout and `timeout_value`). The first message for which `Fun` answers anything but `nomatch` is removed and
that answer returned; `timeout` when `Timeout` expires. An exception from `Fun` leaves every message in place.
`executables_prim_eval_receive` compares mailbox order, timeouts, a message arriving during the wait and
exceptions with OTP.

## Atomics and counters

Plan 11 step 41N (`runtime/src/builtins/atomics.cpp`). An `atomics` array is a
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

Plan 11 step 41N (`runtime/src/builtins/erts_internal.cpp`,
`library/erts/erts_internal.erl`). The functions OTP's `stdlib` and `kernel`
call:

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
`finish_after_on_load/2` wait for [step 71](../.agents/11-plan.md#step-71).

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
- `integer_to_list/1` of a small integer, `list_to_integer/1` of a short
  decimal string and `atom_to_list/1` work on words without staging
  allocations; other arguments take the general paths (OPT0.21).
- List walks validate the list once and read its cells as words, with no
  `Term` per cell; `Left ++ Right` copies a `Left` of at most 64 cells in one
  step without portion state (OPT0.12).

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
  `lists`); its stubs raise `error({not_implemented, F, A})` with
  `erlang:F/A` on top of the stack, only when called. A name the module does not export is
  `undefined function erlang:F/A`.
- An unqualified call or `fun F/A` of an OTP auto-import (`get(K)`,
  `fun put/2`) that the module neither defines, imports nor suppresses with
  `-compile({no_auto_import, ...})` reaches `erlang:F/A` the same way.
- Builtins are sticky: when the module `erlang` registers, the runtime skips
  every export a registered builtin has the name of, so `M:F(Args)` with
  `M = erlang` and external funs bind the builtin, never a stub.

## External term format

Plan 11 step 41E (`erlang/term_encoding.cpp`, `erlang/term_decoding.cpp`).
Both directions share the codec of
[external_term.hpp](../abi/include/clause/abi/external_term.hpp) with the
compiler, and walk terms with explicit stacks, so nesting depth costs no
native stack.

- `term_to_binary/1,2` writes OTP's bytes for a non-distributed node
  (`nonode@nohost`, creation 0): UTF-8 atoms, `NEW_FLOAT`, `STRING_EXT` for
  byte lists up to 65535 elements, maps in key order, pids, ports and
  references in their `NEW_PID`, `V4_PORT` and `NEWER_REFERENCE` forms, and
  `EXPORT_EXT` for `fun M:F/A`. Options: `{minor_version, 0..2}` (0 writes
  float text, 0 and 1 Latin-1 atoms), `deterministic`, `compressed` and
  `{compressed, 0..9}`; anything else is `badarg`.
- `term_to_iovec/1,2` returns the encoding as a list of one binary;
  `external_size/1,2` its exact size.
- `binary_to_term/1,2` checks every length against the input before
  allocating, ignores bytes after the term, and returns `badarg` for
  malformed input, invalid atom text, non-finite floats, repeated map keys
  and identities of another node; `safe` admits only existing atoms, `used`
  returns `{Term, BytesRead}`. CTests: `executables_external_terms` (OTP
  bytes) and `builtins_etf_fuzz` (mutated encodings decode or raise).

## Time

Plan 11 step 41F (`erlang/time.cpp`). Monotonic time is the steady clock;
`native` and `perf_counter` are nanoseconds; units are the OTP atoms
(including the deprecated plural ones) or a positive parts-per-second
integer. `convert_time_unit/3` rounds down, as OTP. `unique_integer/0,1`
draws from one program-wide counter, so every value is positive, unique and
increasing whatever the options (`positive`, `monotonic`; others `badarg`).
`now/0` is system time made strictly increasing. Calendar conversions read
`{{Y, M, D}, {H, Mi, S}}` strictly (an invalid date or time is `badarg`) and
use the C library's local time zone.

## System information

Plan 11 step 41G1 (`erlang/system.cpp`).

- `statistics/1`: `wall_clock` and `runtime` (`{Total, SinceLastCall}` in
  milliseconds), `run_queue`, `total_run_queue_lengths`,
  `total_active_tasks`, `context_switches` (time slices run). Clause keeps no
  reduction, garbage collection or I/O counters: `reductions`,
  `exact_reductions`, `garbage_collection` and `io` report zeros.
- `system_info/1`: `otp_release` (`"29"`), `version` (`"17.1"`, the ERTS
  Clause follows), `machine` (`"Clause"`), `system_version`,
  `system_architecture`, `wordsize` and `{wordsize, internal | external}`,
  `process_count`/`process_limit`, `port_count`/`port_limit`,
  `atom_count`/`atom_limit`, `schedulers`, `schedulers_online`,
  `logical_processors*`, `thread_pool_size`, `smp_support`, `threads`,
  `build_type`, `heap_type`, `kernel_poll`, `debug_compiled`,
  `time_correction`, `time_warp_mode`, `endian`, `compat_rel`.
- `memory/0,1`: OTP's types; `processes` is the runtime-wide account of
  process memory, `atom` an estimate from the atom count, `binary`, `code`
  and `ets` are 0.
- Other items are `badarg`.

## Hashing

Plan 11 step 41G3 (`erlang/hashing.cpp`, `erlang/md5.cpp`).

- `phash2/1,2` and `phash/2` hash a term's structure: exactly equal terms
  hash equally in every program run (atoms by spelling, maps in key order,
  `1` and `1.0` apart). `phash2/1` is in 0..2^27-1, `phash2/2` in
  0..Range-1, `phash/2` in 1..Range; Range is 1..2^32, else `badarg`. The
  values are Clause's own, not OTP's.
- `md5/1` and `md5_init/0`, `md5_update/2`, `md5_final/1` give RFC 1321's
  digests (OTP's bytes) of iodata. The context is an opaque 96-byte binary
  of Clause's own layout; another binary is `badarg`.

## Code loading

Plan 11 step 41H (`erlang/code.cpp`). Every module is linked into the program
and stays loaded; [step 71](../.agents/11-plan.md#step-71) decides about
dynamic code. `module_loaded/1` is true for a compiled module or a module with
runtime builtins (`erlang`, `io`, `os`); `loaded/0` and `pre_loaded/0` list
them all. `delete_module/1` returns `undefined` for an unknown module and
raises `error:notsup` for a loaded one. `prepare_loading/2` returns
`{error, badfile}` (no binary is code the program can load) and
`finish_loading/1` accepts only `[]`. `get_module_info(M[, Item])` continues
as `M:module_info(...)`.

## Source layout

- The runtime implements builtins in `runtime/src/builtins/`. A large module
  gets its own subdirectory with one translation unit per builtin family:
  module `erlang` lives in `runtime/src/builtins/erlang/` (`core`: type tests,
  guard BIFs, operators, raising, `display`, `halt`, `function_exported`;
  `term_access`; `lists`: `length`, `++`, `--`; `conversions`; `processes`:
  identities, spawning, messages, links, monitors, registered names;
  `ports`; `dictionary`: the [process dictionary](processes.md#process-dictionary);
  `binary_conversions` with `conversion_text`, the text rules it shares with
  `conversions`; `term_encoding` and `term_decoding`: the external term
  format; `time` and `timers`; `introspection` and `system`; `spawning`;
  `fun_info`, `hashing` and `md5`; `node` and `code`).
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
