# Builtins

Functions the runtime implements in C++: `erlang` BIFs and functions of other
modules whose OTP counterparts are BIFs, NIFs or need the runtime. Registered
by module, function and arity; reached directly, by dynamic calls and as funs.
Sources live in `runtime/src/builtins/`; observable differences from OTP are in
[differences](differences.md).

## Which builtins exist

- Catalog `abi::v1::bridge_builtins`
  ([builtins.hpp](../abi/include/clause/abi/builtins.hpp)): every builtin both
  compiler and runtime know; group comments name the implementing file.
- An entry's index is the number generated code passes to the bridge; the
  catalog may be reordered (compiler and runtime build together).
- Module `erlang`: guard BIFs, operators as functions, `display/1`, `halt/0,1`,
  raising, term access, conversions, processes, ports, external term format,
  time, system information, fun information, hashing, code loading.
- Other modules: one section each below; private helper modules
  (`clause_unicode`, `clause_re`, `clause_zlib`, `clause_receive`, ...) serve
  Erlang library code.
- Per-function status (builtin, Erlang body, stub) is in
  `library/<application>/<module>.terms` ([library](library.md#files)).
- A qualified call of a catalog builtin calls the bridge; any other `erlang`
  function reaches the [erlang library module](#erlang-module).

## How calls reach them

| Source | Path |
| --- | --- |
| `abs(X)`, `X + Y`, `display`, `halt`, `error` | Inline code or a dedicated service |
| Catalog builtins without inline service (`A -- B`, ...) | Entered like a function: `CLAUSE_builtin_frame_v1`, arguments in registers |
| Direct builtins (never trap, wait or touch another process) | `CLAUSE_builtin_call_v1` returns the result |
| Quick builtins (`length`, `binary_to_list`, `list_to_binary`, `iolist_to_binary`) | Bounded fast path (256 cells, 4096 result bytes); else enter the frame |
| `A ++ B` | Short proper `A` copied inline; else entered |
| `fun abs/1`, `fun erlang:'+'/2` | External fun `erlang:F/A` bound to the builtin |
| `M:F(Args)`, `apply/3`, runtime `fun M:F/A` | Module export first, then builtin |

- `fun F/A` of an auto-import the module neither defines nor suppresses is
  `fun erlang:F/A`, as in OTP. Non-auto-imports (`display/1`, `raise/3`,
  `make_tuple/2,3`, ...) need the `erlang:` prefix.
- A builtin frame has a null body: entering pushes no frame, so a tail call
  returns to the caller's caller.
- Errors (`badarg`, `badarith`, `system_limit`, `{badmap, M}`, `{badkey, K}`)
  use the checked failure channel.
- Term access and conversions follow OTP's `badarg` rules:
  - `setelement/3`: small index in the tuple; `make_tuple/2,3`: size
    0..16,777,215; `A ++ B`: proper `A`; `A -- B`: proper lists, `=:=`,
    O((n + m) log m).
  - `list_to_atom/1`: 256th character is `system_limit`; full atom table
    (`--max-atoms`) exits 70.
  - `integer_to_list/2`, `list_to_integer/2`: base 2..36; `system_limit` past
    1,262,611 decimal digits.
  - `float_to_list/1,2`: OTP's formats (`scientific`, `decimals`, `compact`,
    `short`); 256+ bytes of text is `badarg`.
  - Binary twins share text rules (`erlang/conversion_text.cpp`); float text
    needs digits both sides of the point.
- `function_exported/3`: true for a program export or a registered builtin.

## Portions

Builtins whose work grows with a list or binary run in bounded portions, like
OTP's trapping BIFs.

- Each entered builtin spends a reduction; a portion does `WORK_PER_REDUCTION`
  (16) units per reduction left in the slice.
- Work left: `ProcessStack::trap` names a continuation; state terms stay in
  registers (roots), native state in `TrapState`. Process yields, resumes later.
- May continue in an Erlang export (`io` to a process device traps into
  `file:write/2`).
- Results, errors and order equal running to completion. Host calls
  (`call_builtin`) finish every trap at once.
- Small inputs take word-level fast paths (`++` with ≤ 64 cells, short
  `integer_to_list`, `list_to_integer`, `atom_to_list`, tuple conversions).
- Trapping: `length/1` in a body (guard stays inline), `++`, `--`,
  `binary_to_list/1`, `list_to_binary/1`, `iolist_to_binary/1`.
- Everything else, including `io:format` and tuple builtins, runs to
  completion.

## Typed builtins

- C++ functions `Result F(ProcessContext &, Parameters...)` registered with
  `typed_entry<F>(module, name)`
  ([typed.hpp](../runtime/src/builtins/typed.hpp)).
- Adapter admits argument words, converts to parameter types; mismatch is
  `badarg` before the body runs. `Named<Role>` prevents swapped term parameters.
- Return a term, `BuiltinResult` or raw `Word`, or throw `BuiltinFailure`;
  other C++ exceptions become `out_of_memory` or `internal_error`.
- Guard BIFs, operators and trapping list builtins are word-level `BuiltinBody`
  adapters; hot conversions read cells in place (`TermAccess::trusted`).

## Registration

- `BuiltinRegistry`
  ([builtin_registry.hpp](../runtime/include/clause/runtime/builtin_registry.hpp)),
  owned by `CodeServer`, maps module/function/arity to a `BuiltinFrame`; `add`
  registers a batch all-or-none.
- Startup registers every table of `production_builtins()`
  (`builtins/registry.cpp`).
- Sticky: module exports named like a builtin are skipped, so calls never reach
  a `clause_builtin_placeholder` body or a same-named user function.
- Host path `abi::v1::dispatch_builtin` is separate
  ([runtime](runtime.md#code-server-and-builtins)).

## Source layout

- Large modules get a subdirectory with one file per family (`erlang/`,
  `lists/`, `binary/`, `unicode/`, `re/`, `zlib/`, ...); small ones are single
  files.
- Each family file defines a `*_builtins()` table listed in
  `production_builtins()`. New family = new file; split files past ~400 lines.
- The compiler never reads runtime sources; the catalog is the shared list.

## Erlang module

- `library/erts/erlang.terms`: OTP 29 exports as `{Name, Arity, Flags, Status}`
  (flags `auto_import`, `guard`; status `implemented` or
  `{unavailable, Reason}`), statuses kept by hand.
- `tests/compiler/builtins/generate_erlang.py` writes `library/erts/erlang.erl`
  (stubs) and `compiler/src/semantic/erlang_auto_imports.inc`. CTests
  `builtins_erlang_inventory` (current) and `builtins_erlang_freshness` (host
  OTP).
- Builtins win for `erlang:F(...)`, `fun erlang:F/A`, `apply` and imports; other
  exports raise `{not_implemented, F, A}` when called; unknown names are
  `undefined function`.
- Extras (`executables_erlang_gaps`, `_packet_decoding`, `_suspension`):
  - `adler32`, `crc32` and `*_combine` over pinned zlib; `iolist_to_iovec/1`.
  - `append_element/2`, `insert_element/3`, `delete_element/2`.
  - Cookies as on a non-alive node; `halt/2` as `halt/1`.
  - `send/3`, `spawn/2,4` and kin ([processes](processes.md#builtins)).
  - `decode_packet/3`: every OTP packet type and option, OTP's http terms.
  - `suspend_process/1,2`, `resume_process/1`, `process_display/2`.

## External term format

`erlang/term_encoding.cpp`, `term_decoding.cpp`; codec shared with the compiler
([external_term.hpp](../abi/include/clause/abi/external_term.hpp)), explicit
stacks.

- `term_to_binary/1,2`: OTP's bytes for `nonode@nohost`; options
  `minor_version`, `deterministic`, `compressed`; local funs and native records
  are `notsup`.
- `term_to_iovec/1,2`, `external_size/1,2`.
- `binary_to_term/1,2`: lengths checked before allocating; `safe`, `used`;
  malformed input or foreign identities are `badarg`.
- The codec's stacks and `term_to_binary`'s encoded bytes are per-thread
  buffers kept between calls (up to 1 MB each, `builtins/scratch.hpp`), so a
  large term does not take fresh pages each call; small integers skip the
  general item path both ways.
- CTests `executables_external_terms`, `builtins_etf_fuzz`.

## Time

`erlang/time.cpp`.

- Monotonic time is the steady clock; `native` is nanoseconds; OTP unit atoms
  or parts-per-second integers.
- `convert_time_unit/3` rounds down; `unique_integer/0,1` one increasing
  counter; `now/0` strictly increasing.
- Calendar conversions are strict and use the C library's local zone.
- Timers: [processes](processes.md#timers).

## System information

`erlang/system.cpp`, `erlang/system_flags.cpp`.

- `statistics/1`: `wall_clock`, `runtime`, run queues, `context_switches`;
  reduction, GC and I/O counters report zeros.
- `system_info/1`: `otp_release` `"29"`, `version` `"17.1"`, `machine`
  `"Clause"`, counts, limits, fixed emulator answers; full list in `system.cpp`.
- `memory/0,1`: OTP's types; `binary`, `code`, `ets` are 0.
- `system_flag/2`: `backtrace_depth` (≤ 64) and `min_heap_size` act; most
  others are read back without effect; `cpu_topology`, `erts_alloc`, ...
  `badarg`.

## Hashing

`erlang/hashing.cpp`, `erlang/md5.cpp`.

- `phash2/1,2`, `phash/2`: OTP's values for atoms, numbers, binaries, lists,
  tuples, maps; pids, ports, refs, funs hash Clause's own way.
- `md5/1` and `md5_init/update/final`: RFC 1321; context is Clause's own 96-byte
  binary.

## Code loading

`erlang/code.cpp`. Every module is linked and stays loaded; dynamic code is
deferred.

- `module_loaded/1`, `loaded/0`, `pre_loaded/0` cover compiled and builtin
  modules.
- `delete_module/1` of a loaded module is `notsup`; `prepare_loading/2` is
  `{error, badfile}`.

## Lists module

`builtins/lists/`. OTP's `lists` is Erlang, so builtins raise what its clauses
raise.

- Walks (`sublist`, `flatten`, merges, `key*`, `zip`, `sort`, `usort`, ...)
  raise `function_clause` like OTP and stay as lazy (`sublist([a | b], 1)` is
  `[a]`).
- `keymember/3`, `keysearch/3` are BIFs: `badarg` rules.
- Equal elements: first wins (`max([1, 1.0])` is `1`).
- Funs-taking functions and the rest are Erlang in `library/stdlib/lists.erl`;
  inside it builtins are called qualified.
- Builtin `function_clause` has no frame of its own.

## Math module

`builtins/math.cpp`.

- Every OTP 29 function over the C library; non-numbers `badarg`, non-finite
  results `badarith`.
- 64-bit codegen inlines `sqrt`, `floor`, `ceil`, `exp`, `log*`, `sin`, `cos`,
  `pow` on numbers, keeping raw doubles.

## Binary module

`builtins/binary/`; `replace/3,4` and `list_to_bin/1` are Erlang.

- Patterns: binary, list of binaries or compiled; leftmost, then longest match;
  `scope`, `global`, `trim`, `trim_all`.
- `compile_pattern/1` returns `{bm, ...}` or `{ac, ...}` without a reference.
- Other functions follow OTP; wrong arguments `badarg`;
  `referenced_byte_size/1` is the binary's own size.

## Maps module

`builtins/maps.cpp`, `builtins/os.cpp`.

- Every documented `maps` function; core ones are builtins with OTP's errors;
  keys match exactly.
- Fun-taking functions are Erlang in `maps.erl`; iterators use
  [`erts_internal:map_next/3`](#erts-internal).
- Map cursors walk flat and tree maps in key order; `clause_maps:entries/1`
  feeds `maps:fold/3`.
- `os`: `type`, `getenv`, `env`, `putenv`, `unsetenv`, `getpid`, `version` are
  builtins; time functions are Erlang; `os:cmd/1` uses a port.

## Collections

`builtins/collections/`: every documented function of `ordsets`, `orddict`,
`proplists`, `dict`, `sets`, `gb_trees`, `gb_sets`, `queue`, `array`.

- List walks are builtins; fun-taking functions are Erlang.
- Each builtin walks as OTP's clauses do: same results, same errors for any
  input.
- `dict`, version 1 `sets`: OTP's linear hash records (same layout and order).
  Version 2 sets are maps.
- `gb_trees`, `gb_sets`: shared balanced tree engine; trees equal OTP's.
- `queue`: OTP's `{Rear, Front}` moves; queues equal OTP's.
- `array`: OTP 29's `#array{}` record, changed exactly as OTP.

## Sets of sets

- `sofs` (`builtins/sofs/`, `executables_sofs_sets`): OTP's records over sorted
  lists; algebra, relations and families are builtins, fun-taking functions
  Erlang.

## Graph

- `graph` (`builtins/graph/`, `executables_graph_module`): OTP's `#graph{}`;
  traversals and paths are builtins with explicit stacks, updates are Erlang.

## Digraph

- `digraph`, `digraph_utils` (`builtins/digraph/`,
  `executables_digraph_tables`): OTP's three ETS tables; updates Erlang over
  `ets`; path searches and traversals are builtins reading tables in place.

## ETS

`builtins/ets/` over the runtime's tables ([runtime](runtime.md#ets-tables)).

- Builtins: table lifecycle, insert/lookup/delete, walks, counters, `info`;
  folds, `i/0..3`, `fun2ms/1` Erlang.
- Options and access rights as OTP; every failure `badarg`.
- Match specifications ([runtime](runtime.md#match-specifications)): `select*`,
  `match*`, `match_spec_*`, `erlang:match_spec_test/3`; results in walk order,
  OTP-shaped continuations.
- Table files: `tab2file`, `file2tab`, `tabfile_info` read and write OTP's
  disk_log format both ways.
- `to_dets/2`, `from_dets/2` over `dets`; `table/1,2` stubs.
- CTests `executables_ets_*` run OTP goldens.

## Dets

`dets.erl` over `clause_dets` (`builtins/dets/`): OTP's version 9 file format
byte for byte, same file orders.

- Engine (C++): header, buddy free lists, linear hashing, write cache flush,
  scans, bulk layouts.
- Processes (Erlang): `dets` server with OTP's registry tables; one process per
  table holding file handle, cache, fixes, timers.
- Opening repairs and errors as OTP; funs run where OTP runs them.
- `table/1,2` wait for `qlc`; undocumented helpers are stubs.
- CTest `executables_dets_tables` compares with OTP, file MD5s included.

## Encodings

`builtins/encoding/`.

- `base64`: every function a builtin; decoding errors match OTP's state
  machines.
- `json`: `decode/1`, `encode/1`, `format/1,2` and binary encoders are
  builtins (fallback to Erlang past 256 nesting levels); callback decoders and
  fun-taking encoders are Erlang over `clause_json` scanners.

## POSIX messages

`builtins/erl_posix_msg.cpp`, `library/stdlib/erl_posix_msg.erl`.

- `erl_posix_msg:message/1` (OTP's internal module, which `file:format_error/1`
  needs): the English text of a POSIX reason from a table observed from OTP 29,
  `"unknown POSIX error: "` and the name for another atom, `badarg` for a
  non-atom.

## Network names

`builtins/net/`, `library/kernel/clause_net.erl`, `library/kernel/net.erl`
(plan 21 step LIB60).

- `clause_net:gethostname/0`, `getaddrinfo/2`, `getnameinfo/4`,
  `getservbyname/2`, `getservbyport/2` call the system's functions and answer
  as OTP's `prim_net`: address infos as maps of `family`, `socktype`,
  `protocol` and a socket address, prim_net's error names (`eservice`,
  `enodata`, ...), `{error, einval}` for an unknown service. `net` checks the
  arguments with OTP's guards and passes text as UTF-8 binaries.
- `clause_net:inet_getifaddrs/0` lists interfaces as OTP's `inet_drv` does
  for `inet:getifaddrs/0` (on Windows from `GetAdaptersAddresses` and
  `GetIfEntry` with its flag, prefix and classful-netmask rules; elsewhere
  from `getifaddrs(3)`, merged per name and flags as `prim_inet`).
  `clause_net:getifaddrs/0` lists them as `net:getifaddrs(all)` (Windows:
  OTP's `net` module's packet entries and sorted flags; elsewhere prim_net's
  maps with packet or link addresses); `net` filters them by family, flags or
  fun. `if_names/0` is the index and name list both `net:if_index2name/1`
  and `if_name2index/1` search.
- `inet:getservbyname/2`, `getservbyport/2` and `gethostbyaddr/1` (a
  `getnameinfo` with `NI_NAMEREQD`) use the same builtins; network namespaces
  answer `{error, enotsup}` ([differences](differences.md)).

## Filename

`builtins/filename/`.

- Path functions are builtins following OTP's clauses for the host's family
  (Unix or Windows drives and `//` names); OTP's result shapes and errors.
- `absname/1,2`, `basedir/2,3` are Erlang.

## Filelib

`builtins/filelib.cpp`, `library/stdlib/filelib.erl`, `library/stdlib/clause_filelib.erl`.

- `clause_filelib:escape/1` spells `@` and `\` so `filename:split/1` keeps
  escapes; `compile_part/1` compiles one part of a split pattern (`?`, `*`,
  `**`, character sets with ranges, `{...}` alternatives folding in the rest of
  their part) with OTP's quirks (`**` only as a whole part, `]` first or as a
  range's low end, `{badpattern, missing_delimiter}`, a `case_clause` for nested
  alternatives); `matches/3` and `match/2` filter directory entries.
- The walk (literal prefixes, `..`, `**`, existence checks) is Erlang over
  `file` and `filename`, so results are OTP's, sorted.

## String

`builtins/string/`: every `string` function.

- Walks follow `string.erl` over `unicode_util:cp/1`, `gc/1`: OTP's tails,
  sub-binaries, mixed lists and errors.
- Trailing walks loop instead of recursing.
- Case functions return the input when nothing changes; `equal/4` normalizes
  via `unicode/normalize.cpp`.
- `to_integer/1`, `to_float/1`, Latin-1 API, `jaro_similarity/2` as OTP.

## URI string

`builtins/uri/`.

- `parse/1`: one RFC 3986 walker in OTP's order, OTP's error fragments.
- `recompose`, `normalize`, `resolve`, `percent_decode`, `quote`, query
  functions follow OTP's walks and error styles.
- `unicode/graphemes.cpp` segments clusters for builtins that need them.

## Random numbers

`builtins/rand/`.

- `rand`: OTP's states and handlers; every algorithm gives OTP's sequences.
  Known handlers run in C++ (`clause_rand:*`); others run OTP's generic Erlang.
- `splitmix64_next/1`, `exsp_*`, `mwc59*` are builtins.
- `tools/rand/` observes OTP constants and generates `tables.cpp`.
- `random`: `uniform_s/1` builtin, rest Erlang.

## Calendar

`builtins/calendar.cpp`.

- Gregorian arithmetic builtins over any integers, OTP's formulas and errors.
- Local/universal time and RFC 3339 are Erlang in `calendar.erl`.

## Timer

`builtins/timer.cpp`.

- `seconds/1`, `minutes/1`, `hours/1`, `hms/3` and `now_diff/2` as Erlang
  arithmetic over any numbers: `badarith` for a non-number, `function_clause`
  for a `now_diff/2` argument that is not a 3-tuple.
- The timers are Erlang in `timer.erl`, with no `timer_server`: sends to a local
  pid use `erlang:send_after/3`; any other one-shot or interval timer is a
  process that makes an alias, which its `TRef` carries and `cancel/1` sends
  to. An interval timer monitors the process it works for and ends with it;
  `apply_repeatedly` waits for each action to end before the next tick.

## Persistent terms

`builtins/persistent_term.cpp` over the runtime's table
([runtime](runtime.md#persistent-terms)).

- `put/2`, `put_new/2`, `get/0,1,2`, `erase/1`, `info/0` with OTP's results;
  keys match exactly; reads return the stored term without a copy.

## Unicode module

`builtins/unicode/`: every function is a builtin.

- Encodings `latin1`, `utf8`, `utf16`, `utf32` (with endianness); else `badarg`.
- Chardata: binaries, code points, nested lists; consecutive binaries are one
  stream.
- Failures give `{error | incomplete, Converted, Rest}` with OTP's `Rest` shape.
- Normal forms, `category/1`, `is_whitespace/1`, `is_id_*` keep OTP's
  shortcuts.

## Code module

`builtins/code.cpp`. No `code_server` process; answers describe the linked
program.

- `ensure_loaded/1`, `is_loaded/1` (`{file, preloaded}`), `which/1`
  (`preloaded`), `all_loaded/0`, `module_status/0,1`; `is_sticky/1` `false`.
- Code path (`library/kernel/code.erl`, plan 21 step LIB61): a persistent term
  built on first use as OTP's interactive code server builds it: `ERL_LIBS`
  application `ebin` directories, the newest version of each application in
  `lib_dir()`, then `"."`. `root_dir/0` is the executable's directory. Path
  changes normalize and check directories as OTP; `lib_dir/1`, `priv_dir/1`
  and `compiler_dir/0` name the first path directory `.../Name[-Vsn][/ebin]`.
- Loading answers for linked code: object code found on the path gives
  `erlang:prepare_loading/2`'s `{error, badfile}`, missing code `nofile`;
  `purge/1` `false`, `soft_purge/1` `true`, `delete/1` `false`;
  `get_doc/1` `{error, missing}`; no coverage support.

## Io module

`builtins/terminal.cpp`, `library/stdlib/clause_io.erl` (plan 21 step LIB63).

- `clause_io:isatty/1` (`stdin`, `stdout`, `stderr`): a console on Windows,
  `isatty(3)` elsewhere; `window_size/1`: `{Columns, Rows}` of a terminal
  (`GetConsoleScreenBufferInfo`, `TIOCGWINSZ`), else `{error, enotsup}`.
- They answer `io:getopts/0,1`'s terminal flags, `io:columns/0,1`,
  `io:rows/0,1` and `io_ansi:enabled/0,1`.

## Os module

`builtins/os.cpp`, `library/kernel/os.erl`.

- `type/0`, `getenv/0,1`, `env/0`, `putenv/2`, `unsetenv/1`, `getpid/0`,
  `version/0` are builtins.
- `cmd/1,2`: Erlang over a spawn port with OTP's protocol: Windows runs
  `COMSPEC /c Command`; elsewhere `/bin/sh -s unix:cmd` reads the command and
  echoes `^D` (and the exit status) at its end. `max_size`,
  `exception_on_failure`, UTF-8 output decoded to characters.
- `find_executable/2` (builtin): OTP's search over `PATH` elements (Windows:
  the current directory first, `.exe`, `.com`, `.cmd`, `.bat`).
- `set_signal/2` (builtin): `badarg` on Windows as OTP; elsewhere `sigaction`;
  a handled signal acts as OTP's default `erl_signal_handler`.

## Init module

`builtins/init.cpp`, `library/erts/init.erl`. A started system without a boot
script.

- Command line decoded once (`startup/arguments.cpp`): plain arguments feed the
  entry's argv and `get_plain_arguments/0`.
- Only flag is `home`; `script_name/0` is the executable's path.
- Other functions are Erlang with OTP's started answers; `script_id/0` is
  `{"Clause", "29"}`.
- `stop/0,1`, `reboot/0` halt at once; `boot/1`, `restart/0,1`,
  `make_permanent/2` refused with `notsup`.

## Atomics and counters

`builtins/atomics.cpp`.

- `atomics` array is a [handle reference](runtime-heap.md#handle-references)
  over `std::atomic_ref` values; arity 1..2^27; OTP's `badarg` rules and
  `info/1`.
- `counters` is Erlang over a signed atomics array; write-concurrency counters
  are exact.

## Erts internal

`builtins/erts_internal.cpp`, `library/erts/erts_internal.erl`: the functions
`stdlib` and `kernel` call.

- Builtins `cmp_term/2` and `map_next/3` (map iterators, key order).
- Erlang: `list_to_integer/2`, `binary_to_integer/2`, `map_to_tuple_keys/1`,
  `term_type/1`, counters, `dynamic_node_name`.
- `get_creation/0` `undefined`; distribution and `.beam` groups declined.
- `erts_code_purger`: nothing to purge.

## Unicode util

`unicode_util.erl` over `clause_unicode` builtins and generated tables
(`unicode/tables.cpp`).

- `cp/1`, `gc/1` (UAX #29), `nfd/nfc/nfkd/nfkc`, case mappings with OTP's
  shapes; `spec_version/0` `{17, 0}`.
- `tools/unicode/generate.py` builds tables from host OTP answers and Unicode
  17.0 data; builds need neither.
- CTests `executables_unicode_tables`, `_clusters`, `_forms`.

## io_lib

`io_lib.erl`, `builtins/io_lib/`. OTP's deep character lists built in a C++
arena; `io:format` uses the same engine.

- Writers (`write.cpp`): `write`, `write_string`, `write_atom`, ... escape and
  limit as OTP.
- Format engine (`format_scan.cpp`, `format_build.cpp`): every control,
  `chars_limit`, OTP's float rounding; errors `badarg`.
- Pretty printer (`pretty*.cpp`): OTP's `io_lib_pretty` layouts.
- Scanner (`fread.cpp`): `fread/2,3` controls and continuations.
- Collectors (`collect.cpp`), printable checks, `indentation/2`.
- `maps_order` funs: builtin records maps and continues in
  `clause_io_lib:sort_maps/6`, rerunning with sorted keys.
- Nesting past 256 is `system_limit`. CTests `executables_io_lib_text`,
  `_pretty_print`, `_fread_scanning`.

## Compiler tables

`builtins/front_end/`.

- `erl_internal`: BIF and guard tables from `erlang.terms`; type and operator
  tables; `add_predefined_functions/1`.
- `erl_anno`: OTP's shapes and errors.
- `erl_features`: answers from Clause's compiler; `used/1` reads descriptors or
  `.beam` `Meta` chunks.
- `records`: see [native records](native-records.md#reflection).
- CTests `executables_compiler_tables`, `_records_reflection`.

## Erl scan

`erl_scan.erl` over `clause_erl_scan:scan/6` (`front_end/scanner*.cpp`), its own
scanner following OTP's `scan1/5`.

- OTP's tokens, annotations, sigils, triple-quoted strings, errors and rests.
- `reserved_word_fun`, `text_fun` callbacks run in Erlang in token order.
- `tokens/3,4` continuations rescan the unfinished token.
- CTest `executables_erl_scan_tokens`.

## Erl parse

`erl_parse.erl` over `clause_erl_parse:parse/1` (`front_end/parser*.cpp`), a
recursive-descent parser of OTP's grammar.

- OTP's abstract format and LALR error behavior (stop token, action-vs-syntax
  error order).
- `first_anno/1`, `last_anno/1` follow `map_anno/2`'s order.
- `normalise/1`, `abstract/1,2`, `tokens/1,2` builtins; annotation walks with
  funs Erlang.
- CTest `executables_erl_parse_forms`.

## Erl eval

`erl_eval.erl` interprets the abstract format in Erlang; `clause_erl_eval`
builtins handle bit syntax segments (`front_end/erl_eval.cpp`) and pattern
matching (`front_end/eval_match.cpp`, plan 21 step LIB34C), and
`erl_eval:partial_eval/1` and `is_constant_expr/1` are builtins too.

- `clause_erl_eval:match/4` is `match1/5`: literals, variables, lists, tuples,
  maps, native records, `"abc" ++ T` prefixes, operator constants and binary
  patterns, giving `{match, Bindings}`, `nomatch` or `invalid` (raised as
  `{illegal_pattern, Pattern}` by `erl_eval.erl`). Map keys and segment sizes are
  guard expressions evaluated over the outer bindings (`front_end/guard_eval.cpp`,
  shared with `epp`'s `-if`); bindings are read and stored as `orddict` or a map,
  as OTP does, an unsorted list included.
- `clause_erl_eval:bits_gen/4` takes a binary generator's pattern from the front
  of the bits; the evaluator loop, guards (they call the non-local handler) and
  the comprehension walks stay Erlang.

- `bit_type/2` settles a `bin_element`'s specifiers with `erl_bits`' defaults
  and errors (`front_end/bit_types.cpp`, shared with `normalise/1`);
  `bit_segment/3` builds one segment (`badarg`, `system_limit`), `bit_take/3`
  takes one from the front of a bitstring (`nomatch`).
- Funs made by the evaluator are closures over OTP's
  `{Anno, Bindings, Lf, Ef, UsedVars, Clauses}` environment, so `fun_data/1`
  and `erlang:fun_info/2` answer as in OTP; `fun M:F/A` with runtime parts uses
  `erlang:make_fun/3`.
- `exprs/2`, `expr/2` and `check_command/2` check commands with
  `erl_lint:exprs_opt/3` as OTP does.
- CTest `executables_erl_eval_exprs`.

## Erl lint

`erl_lint`'s checks are builtins (`front_end/lint*.cpp`, plan 21 step
LIB34C) written after OTP's `erl_lint` clause for clause; `erl_lint.erl`
keeps what calls funs or other modules.

- One `Lint` object holds OTP's `#lint{}` state; variable tables are vectors
  sorted by name (OTP's orddicts), sets and maps OTP keeps in term order are
  ordered containers, so errors keep insertion order and warnings sort as in
  `return_status/1`.
- `module/1,2,3` go through `clause_erl_lint:module/4` with the behaviours'
  `behaviour_info/1` answers looked up in Erlang
  ([differences](differences.md#erl_lint)); `format_error/1` formats the
  `clause_erl_lint:format_spec/1` answer (nested `io_lib:format/2` parts
  included) so results equal OTP's terms.
- Deprecations come from OTP's `otp_internal` tables
  (`front_end/otp_obsolete.inc`, generated by
  `tests/compiler/library/generate_obsolete.escript`).
- `erl_eval:partial_eval/1` and `erl_bits:set_bit_type/2` used by the checks
  are C++ (`lint_eval.cpp`, `bit_types.cpp`).
- CTest `executables_erl_lint_checks`.

## Erl pp

`erl_pp`'s printing is a builtin (`front_end/pp*.cpp`, plan 21 step LIB34C),
`clause_erl_pp:print/2`, ported from the two passes of the earlier Erlang
module; `erl_pp.erl` keeps the hooks and `legalize_vars/1`.

- The first pass turns a form into layout items in an arena (`pp_items`,
  `pp_exprs`, `pp_forms`, `pp_types`); the second lays them out against the
  line width (`pp_layout`), writing atoms, strings, chars and values through
  `io_lib`'s C++ writers.
- A form only the hook can print stops the layout with
  `{hook, Expr, Column, Precedence}`; `erl_pp.erl` calls the hook and prints
  again with the answers so far, so each hook runs once, in OTP's order.
- Precedences and `erl_parse:abstract/2` come from `erl_parse.cpp`.
- CTest `executables_erl_pp_forms`.

## Epp

`epp`'s `-if` and `-elif` conditions are evaluated by a builtin,
`clause_epp:eval_if/1` (`front_end/epp_eval.cpp`, plan 21 step LIB34C), so a
program that preprocesses files does not link `erl_eval`.

- It evaluates the guard expression as `erl_eval:exprs/3` does with no bindings:
  literals, lists, tuples, maps and map updates, binaries (segments through
  `clause_erl_eval`'s bit builtins), operators with `andalso`/`orelse` giving
  their right side's value, and calls of guard BIFs (local auto-imported or
  `erlang:`) through the match specification guard table.
- Any error (unbound variables, undefined records, a raising BIF, a local call)
  makes the condition false, as `epp` catches it.
- `epp`, `file` and `erl_features` are compiled with
  [strict liveness](compile.md#strict-liveness).

## Erl expand records

`erl_expand_records:module/2` is a builtin (`front_end/expand_records.cpp`,
plan 21 step LIB34C): the expansion walks forms in C++ instead of a large
Erlang clause dispatch, so programs that call it compile no extra code.

- OTP's traversal order (binary segments last to first, match right side
  first) and `recN` numbering; `is_record/3` guard tests become patterns.
- Filters of comprehensions are guard tests by `erl_lint`'s rules
  (`front_end/guard_tests.cpp`, with the module's records and local or
  imported functions).
- Options `dialyzer`, `expand_inits`, `[no_]strict_record_tests`, also from
  `-compile` attributes.
- CTest `executables_erl_expand_records_forms`.

## Zlib module

`builtins/zlib/streams.cpp`, `library/erts/zlib.erl`.

- Links zlib 1.3.2 as OTP 29 ([zlib](compile.md#zlib)): same bytes.
- Stream is a handle reference owning `z_stream`; `clause_zlib` builtins run one
  operation over all input.
- `zlib.erl` keeps OTP's API, errors, states and whole-buffer helpers.
- `term_to_binary` compression follows OTP's size rule.

## Zstd module

`builtins/zstd/contexts.cpp`, `library/stdlib/zstd.erl` (plan 21 step LIB54E).

- Links zstd 1.5.7 as OTP 29 ([zstd](compile.md#zstd)): same frames.
- Contexts and dictionaries are handle references owning libzstd's; a context
  referencing a dictionary keeps it alive. `clause_zstd` builtins mirror OTP's
  NIF: output chunks of `ZSTD_CStreamOutSize()` / `ZSTD_DStreamOutSize()` (a
  frame that states a smaller size decompresses into one chunk of it),
  `{continue, Rest, Out}`, `{flush, Out}` and `{done, Out}` results, owner
  checks (`not_on_controlling_process`), `{badarg, {invalid_key | invalid_value,
  Term}}` and `{zstd_error, Name}` errors, booleans read back as `false` as
  OTP's NIF reads them.
- `zstd.erl` keeps OTP's iodata, streaming and frame handling.

## Re module

`builtins/re/`, `re.erl` over bundled PCRE2 ([PCRE2](compile.md#pcre2)).

- Compiled pattern: OTP 29's `{re_pattern, ...}` with a handle reference.
- `clause_re:compile/2`, `clause_re:run/4` (one match, every capture option,
  OTP's errors).
- `re.erl`: chardata conversion, `global` loop, `replace`, `split`.
- `export`/`import/1` not provided. CTest `executables_re_module`.

## Prim eval

`process/receive.cpp`, `library/erts/prim_eval.erl`.

- `prim_eval:'receive'/2` scans the mailbox with `clause_receive` builtins
  (`peek`, `skip`, `take`, `restart`, `wait`); first non-`nomatch` answer wins.
- CTest `executables_prim_eval_receive`.
