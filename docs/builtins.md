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
  `library/<application>/<module>.terms` ([library](libraries/library.md#files)).
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
  (`builtins/registry.cpp`), then those of the runtime extensions the startup
  descriptor lists (`RuntimeOptions::extensions`): the crypto archive's when the
  program links it ([crypto module](#crypto-module)).
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
  exports raise `{not_implemented, F, A}` when called, the tracing ones
  (`{unavailable, tracing}`: `trace*`, `system_monitor`, `system_profile`,
  `seq_trace*`, `dt_*`) `notsup`; unknown names are `undefined function`.
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
  modules; `module_loaded/1` and `loaded/0` also
  [interpreted modules](#module-interpreter).
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
- `to_dets/2`, `from_dets/2` over `dets`; `table/1,2` Erlang over
  `qlc:table/2` (select/3 by default, `first_next`, `last_prev`, `{select, MS}`),
  looking up keys with `==` in an `ordered_set`, else `=:=`.
- CTests `executables_ets_*` run OTP goldens.

## Dets

`dets.erl` over `clause_dets` (`builtins/dets/`): OTP's version 9 file format
byte for byte, same file orders.

- Engine (C++): header, buddy free lists, linear hashing, write cache flush,
  scans, bulk layouts.
- Processes (Erlang): `dets` server with OTP's registry tables; one process per
  table holding file handle, cache, fixes, timers.
- Opening repairs and errors as OTP; funs run where OTP runs them.
- `table/1,2` Erlang over `qlc:table/2` (select/3 by default, `first_next`,
  `{select, MS}`); undocumented helpers are stubs.
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
- `inet:getservbyname/2`, `getservbyport/2` and `gethostbyaddr/1` use the
  same builtins: as OTP's `inet_gethost`, an IPv4 `gethostbyaddr/1` takes its
  name and aliases from `clause_net:gethostbyaddr/1` (`gethostbyaddr(3)`;
  Windows and IPv6 use a `getnameinfo` with `NI_NAMEREQD`, without aliases).
  `net:if_name2index/1` answers `einval` for an unknown name on macOS and
  `enodev` elsewhere, as `if_nametoindex(3)` reports. Network namespaces
  answer `{error, enotsup}` ([differences](differences.md)).
- `clause_net:dns_decode/1` and `dns_encode/1` (`builtins/net/dns.cpp`, plan
  21 step LIB60B) are inet_res's DNS message codec: OTP's `#dns_rec{}`,
  `#dns_header{}`, `#dns_query{}`, `#dns_rr{}`, `#dns_rr_opt{}` and
  `#dns_rr_tsig{}` tuples with inet_dns's values (symbolic types, classes and
  opcodes, escaped name labels, LOC as degrees and meters) and its `formerr`
  rules (compression pointers backwards past the header, names below 255
  bytes, truncated sections only with the TC bit); encoding compresses names.

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

## Argparse

`builtins/argparse/`, `library/stdlib/argparse.erl`,
`library/stdlib/clause_argparse.erl`.

- `clause_argparse:validate/3` checks a command as OTP's `validate/2` does, in
  OTP's order, and returns the first invalid field for `argparse.erl` to raise.
- `parse/6` is OTP's parser as a loop: long forms (`--name=value`), short
  forms, combined flags, attached values, negative numbers, `nargs`, actions,
  types, sub-commands, defaults; OTP's thrown errors and argument maps.
- `help/4` builds OTP's help lists (usage wrapping, sections, templates) through
  the `io_lib`, `string` and `unicode` builtins, so shapes equal OTP's.
- Funs are called by `argparse.erl`: a builtin returns a request (`{custom,
  Fun, Arg}`, `{re, Arg, Re[, Options]}`, `{describe, Fun}`) and is called
  again with the answers so far, replaying its walk; each fun runs once, in
  OTP's order.
- `run/3`, handlers and `format_error/1,2` are Erlang.

## Qlc pt

`builtins/qlc_pt/`, `library/stdlib/qlc_pt.erl`,
`library/stdlib/clause_qlc_pt.erl` (plan 21 step LIB73): the qlc parse
transform at run time, for `qlc:string_to_handle/1,2,3` and `qlc:q/1,2` in
evaluated code, and at compile time for modules including `qlc.hrl`, in Clause's
[transform host](transforms.md#transform-host).

- `clause_qlc_pt:transform_expression/4` and `parse_transform/4` follow
  `qlc_pt.erl` function for function: NodeInfo (unique annotations and what the
  checks note on each node, kept by slot number from 1 as OTP's), `no_shadows`,
  the lint runs that find introduced and generator variables, frames of
  `#bind{}` records for constant and join columns, filter skipping, match
  specifications, join generators, the generated state fun, `Qdata`, `CodeF`
  (`term_to_binary(_, [compressed])` of the original code) and the options fun.
  Intermediate data are Erlang terms shaped as OTP's, so sorts follow term
  order; generated variables are references as OTP's `make_ref/0`.
- Messages come from the C++ `erl_lint` (`Lint`), the path OTP's `qlc_pt`
  takes when the compiler is not available; a lint crash on the forms it builds
  is the compiler's internal error, as OTP's compiler reports it. Records are
  expanded by the C++ `erl_expand_records`, guard tests by `is_guard_test/3`.
- Match specifications run `ms_transform` in Erlang: an attempt without an
  answer is taken as failed and recorded, the walk goes on, and the builtin
  returns `{try_ms, [{Query, Forms}]}` for all of them; `qlc_pt.erl` answers
  each with `try_ms/4`'s result and calls the builtin again with the
  `{Query, Answer}` pairs, replaying its walk. Answers are found by query, so a
  module reruns about once per depth of attempts that depend on earlier
  answers (3 runs for `qlc_queries`' 157 attempts), not once per attempt. The
  result is `{done, Result, Transformed}` (a transformed module raises the
  backtrace depth to 500, as OTP's transform does).

## Qlc

`library/stdlib/qlc.erl` (plan 21 step LIB73): every function in Erlang, since
evaluation calls the query's funs. Written from OTP's documentation; the
algorithms are OTP's where answers, their order or `info/1,2` show them.

- Handles keep OTP's tuples (`{qlc_handle, H}` with `qlc_lc`, `qlc_table`,
  `qlc_append`, `qlc_sort`; `{qlc_cursor, {Pid, Owner}}`); compiled queries are
  the transform's `{qlc_lc, Fun, Opt}` with `qlc_v1` or `simple_v1` data.
- Preparation: lookups at the key or the index with the fewest keys
  (`max_lookup`), match specifications for unary traverse funs and lists, lookup
  joins preferred to merge joins, merge join inputs sorted unless known sorted,
  simple queries for `cache` and `unique`, coalesced, and never for the first
  generator.
- Evaluation: answers are lists ending in `[]` or a continuation fun. `cache`
  uses `ets` tables (`qlc`, private) or a list kept under
  `{Ref, '$_qlc_cache_tmpfiles_'}`, moved to a temporary file past
  `max_list_size`; merge joins put large classes of equal keys on a file
  (`'$_qlc_merge_join_tmpfiles_'`); sorts run `file_sorter` once answers exceed
  `size`. Temporary files are `qlc_<node>_<os pid>_<unique>` in `tmpdir` or the
  working directory; `tmpdir_usage` reports through `error_logger`.
- Errors the evaluation finds (a traverse or lookup fun's non-list answer,
  file errors, `{tmpdir_usage, _}`) are thrown with an internal tag and returned
  by `eval/2`, `fold/4`, `info/2` and cursors, as OTP returns the errors its
  `throw_error/1` throws.
- `info/1,2` prints the prepared query with `erl_pp:expr/3`, table descriptions
  through their format funs (strings parsed by `erl_eval:extended_parse_exprs/1`).
- `string_to_handle/1,2,3` and `erl_eval`'s `qlc:q/1,2` run Clause's
  [`qlc_pt`](#qlc-pt); `ets:table/1,2`, `dets:table/1,2` and `erl_eval` reach
  `qlc` by a runtime name, so only programs that name `qlc` link it.
- CTest `executables_qlc_queries` compares with OTP: its query module compiles
  from `qlc_queries.abstr`, the forms after OTP's transform, so the test needs
  no host OTP ([executable cases](../tests/fixtures/executables/README.md)).

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
  An [interpreted module](#module-interpreter) is loaded from its file:
  `{file, File}`, `File`, and `module_status/1` as OTP derives it from the
  object code at that file (`removed` for a source file, `loaded` for a
  `.beam`).
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
- The standard input server (`io.erl`) reads lines as OTP's group does: a
  `get_until` request (`io:read/1,2`, `io:scan_erl_exprs/1..4`, the shell's
  commands) takes one line at a time and writes its prompt again before each
  further line, and a CRLF line end reads as LF (plan 21 step LIB74).

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
  `clause_io_lib:pretty/2` is `io_lib_pretty:print/2` for the shell (plan 21
  step LIB74): `column`, `line_length`, `depth`, `line_max_chars`, `strings`,
  `encoding` and `records`, the `[{Name, [Field]}]` definitions a
  `record_print_fun` would answer, so tuples of a known name and size print as
  `#Name{Field = Value, ...}` with OTP's layout. CTest
  `executables_shell_session`.
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

## Module interpreter

`builtins/interpreted.cpp` (plan 21 step LIB77B) is the runtime half of
[`clause_interp`](libraries/library.md#module-interpreter): the code server's
registry of interpreted modules (`modules/interpreted_modules.cpp`), with each
module's file and exports.

- `clause_interp:register(Module, File, Exports, Route)` registers or replaces a
  module (`linked` for a module the program links), `unregister/1` removes it,
  `interpreted/1` asks. `Route` is `fun error_handler:undefined_function/3`,
  which keeps that function live in every program that loads modules.
- When no export or builtin answers a call of a registered module
  (`CLAUSE_call_v1`, `CLAUSE_call_list_v1`, `apply/3`, a `spawn` MFA) or of
  its external funs, the call service writes `Module`, `Function` and the
  argument list to the registers and returns `Route`'s frame
  ([dynamic calls](funs.md#dynamic-calls)).
- `clause_interp:route_undefined(Route)` (plan 21 LIB77C) sends every call that
  finds no function to `Route`, so `clause_interp` can interpret a module
  from a `.beam` when first called; `Route` raises `undef` for the rest.
- `erlang:function_exported/3` and the [code module](#code-module) answer for
  registered modules. Programs that never register one pay one atomic flag
  test in these paths.
- CTest `executables_interpreted_modules` (compared with `compile:forms/2` and
  `code:load_binary/3` under OTP).

## Compile

`builtins/compile_messages.cpp` (plan 21 step LIB77E1) quotes sources for the
reports of [`compile`](libraries/library.md):
`clause_compile:quote_source(Source, Encoding, Location)` returns the line of
`Location` from the source binary (latin1, or UTF-8 up to its first invalid
character) as `"% Line| Text
"`, the marker line `"%     |   ^
"` when the
location has a column, and an empty line, as OTP's `sys_messages` prints
them, with OTP's quirks: tabs before the column stay tabs and a column past
the end of the line widens the marker. `<<>>` when the source has no such
line. `compile.erl` reads the file and its `coding` comment and formats the
message.

- CTest `executables_compile_forms` (reports, returned messages, transforms,
  listings and `.beam` files compared with OTP's `compile`; a Clause-only run
  for the options that need OTP's code generator).

## Erl syntax

`builtins/syntax/` (plan 21 step LIB77E2) implements
[`erl_syntax`](libraries/syntax_tools.md#erl_syntax) over OTP's node representation. The
`syntax.hpp` tables drive most of it:

- `FIELDS`: the accessors that read one element of one erl_parse form, or one field of the
  tree's data record, plus the data itself. They are registered from the table, and C++ reads
  them by name with `get(context, node, "case_expr_argument")`. The name is checked at compile time.
- `SIMPLE`: the constructors that build `tree(Type, #Record{Arguments...})`, used by C++ through
  `make(context, "case_expr", ...)`.
- In `build.cpp`, `make_tree/2`'s clauses as group shapes.
- In `shapes.cpp`, `subtrees/1`'s groups as accessors with the parts that may be absent.
- In `revert.cpp`, `revert/1`'s function per node type.

Accessors with several erl_parse forms (`record_expr_type/1`, `attribute_arguments/1`, ...),
constructors that normalize their arguments (`clause/3`'s guards, `list/2`, `cons/2`, ...), list
skeletons, comments and attributes (`nodes.cpp`, `attrs.cpp`), `abstract/1`, `concrete/1`,
`is_literal/1` and `meta/1` (`literals.cpp`) are written out.

`concrete/1` of a binary reverts each field and builds it as `erl_parse:normalise/1` does.
`abstract/1` takes a list for a string when Clause's `io_lib:printable_list/1` accepts it.

- CTest `executables_erl_syntax_trees`.

## Prettypr

`builtins/prettypr/` (plan 21 step LIB77E3a) is the layout chooser of
[`prettypr`](libraries/syntax_tools.md#prettypr): `prettypr:best/3` is OTP's
`rewrite/2`, John Hughes' strict context-passing pretty printer with OTP's
paragraphs, empty documents, floats and null texts, rule for rule, so it
returns OTP's very layout documents, joined strings included
(`[W1 + W2 | [Chars1 | Chars2]]`).

- `prettypr.hpp`: the `Engine` with arenas of documents, strings, sep cells
  and contexts. Documents are read from their terms when the rewriting first
  visits them (one per distinct term, so shared subdocuments cost nothing).
- `rewrite.cpp`: one handler per document kind, switching on the context
  (`floats.cpp` for floating documents). The rewriting is a loop; the one call
  OTP makes under a context (a completed line moved out) pushes a line that
  the result wraps, so deep documents need no deep C++ stack.
- `documents.cpp`: reading terms, OTP's builders (`nest(0, D)` is `D`,
  `expand_sep`, `expand_par`, `horizontal`, `vertical`) and the result terms.
- `builtins.cpp`: `best/3` and `clause_prettypr`'s `width/1` (tabs to the next
  multiple of 8), `words/1` (`text_par/2`'s words) and `layout/1` (the text of
  a layout document). Malformed documents raise `function_clause`.
- CTest `executables_prettypr_layout` (compared with OTP).

## Erl syntax lib

`builtins/syntax_lib/` (plan 21 step LIB77E4a) has the functions of
[`erl_syntax_lib`](libraries/syntax_tools.md#erl_syntax_lib) that take no funs.
They are written over `builtins/syntax/`'s C++ accessors and constructors:

- `analyze.cpp`: the `analyze_*` functions, `is_fail_expr/1` and
  `function_name_expansions/1`. A node they cannot analyse raises
  `throw(syntax_error)`. Errors of `erl_syntax` itself pass through as they are.
- `bindings.cpp`: `annotate_bindings/1,2`. One `Binder` walks expressions and
  patterns by OTP's scoping rules (tables of node types), with ordsets of
  names (`Names` in `syntax_lib.hpp`) merged in Erlang's term order.
- `trees.cpp`: `variables/1` (a `sets:new/0` map), `limit/2,3` and
  `strip_comments/1`, and the subtree rebuilding the others share.
- CTest `executables_syntax_tools_lib` (compared with OTP).

## Comments

`builtins/comments/` (plan 21 step LIB77E4b) has
[`erl_comment_scan`](libraries/syntax_tools.md#erl_comment_scan)'s scanning and
[`erl_recomment`](libraries/syntax_tools.md#erl_recomment):

- `scan.cpp`: `scan_lines/1` is OTP's scanner state machine (line, column,
  margin) over the characters of the text, skipping strings, quoted atoms and
  character literals; a comment's trailing white space is trimmed by
  `string:trim/2`'s C++ walk. `join_lines/1` and `string/1` join consecutive
  lines alone in the same column.
- `recomment.cpp`: OTP's extended tree of leaf, tree and list nodes with the
  line range each covers, built from `erl_syntax`'s subtrees; comments go
  before, after or into nodes by those ranges, forms after a `-file` attribute
  that leaves the file lose their range, and every tree node is rebuilt with
  `make_tree/2` and its original attributes.
- CTest `executables_syntax_tools_comments` (compared with OTP).

## Epp dodger

`builtins/epp_dodger/` (plan 21 step LIB77E4c) has the rewriting of
[`epp_dodger`](libraries/syntax_tools.md#epp_dodger), as `clause_epp_dodger`'s
builtins:

- `tokens.cpp`: `scan/2` is OTP's `scan_form/2` but `-error` and `-warning`:
  directives become `'?preprocessor declaration?'() -> Name...` functions, and
  `scan_macros/3` (a `Scanner` over a deque of tokens, since macro arguments
  are put back in front) makes macros the atoms `'? NAME'` and `'?,NAME'` or
  `{'? <macro> (', Name, Args...}` tuples, with `++` next to strings when
  clever. `quickscan/1` is `quickscan_form/1`: directives killed, macros
  `'?NAME'` atoms or parts of the strings next to them. `skip_macro_args`
  throws `{error, macro_args}` for unclosed arguments.
- `rewrite.cpp`: `rewrite_form/1` turns the directive functions back into
  attributes and the macro atoms and tuples into `erl_syntax` macros.
- CTest `executables_epp_dodger_forms` (compared with OTP).

## Merl

`builtins/merl/` (plan 21 step LIB77E5) has the template functions of
[`merl`](libraries/syntax_tools.md#merl):

- `shapes.cpp`: merl's view of nodes (`[]` is a list with two empty groups,
  `{}` keeps its element group, lists, binary fields, clauses, receives and
  record nodes have fixed groups) and the nodes made from such groups;
  `clause_merl:subtrees/1` gives the groups to `show/1`.
- `templates.cpp`: `template/1` finds metavariables (`'@Name'`, `_@Name`,
  `909N`, `"'@Name"`; `@@`/`9099` globs) and lifts one level per extra
  underscore or 0; `tree/1`, `subst/2`, `tsubst/2`, `alpha/2` and
  `template_vars/1`.
- `match.cpp`: `match/2` binds metavariables group by group (the last group
  first, as OTP's recursion does) with a glob taking the run of trees its
  group leaves, and compares the rest node by node; `meta_template/1`.
- CTest `executables_merl_templates` (compared with OTP).

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

## ASN.1 codec

`builtins/asn1/` in `clause_runtime`, `library/asn1/clause_asn1.erl` (plan 21 step LIB84, [ASN.1](asn1.md)).

- `clause_asn1:encode/3` and `decode/3` take the generated module's schema term, the type name and the value or
  bytes (a binary or iolist); `decode/3` returns `{Value, Rest}`. They interpret the schema as asn1ct's generated
  BER code and `asn1rtt_ber` do, and fail with the same `exit`/`error` reasons, which the generated wrapper turns into
  `{error, {asn1, Reason}}`.
- TLVs are parsed as `asn1rt_nif` parses them (`tlv.cpp`: its error positions, two-byte tag and four-byte length
  limits); `ber_decode.cpp`, `ber_constructed.cpp`, `ber_encode.cpp` and `ber_encode_constructed.cpp` hold the
  primitive and constructed types, `values.cpp` INTEGER, REAL and DER `DEFAULT` comparisons, `legacy.cpp` the legacy
  `BIT STRING` forms.
- PER (`per.hpp`): `per_bits.cpp` bit fields, length determinants, whole numbers and strings with their alignment and
  fragments; `per_encode.cpp`/`per_decode.cpp` the primitive types; `per_encode_constructed.cpp`/
  `per_decode_constructed.cpp` sequences, extension additions, choices and open types; `per_strings.cpp`
  `BIT STRING` values. A PER decode takes a binary and returns the bits left unread.

## Public key

`builtins/public_key/` in `clause_runtime`, `library/public_key/clause_public_key.erl` (plan 21 step LIB80B,
[public_key](libraries/public_key.md)): the record walks and tables of OTP's `pubkey_translation` and
`pubkey_cert_records`, `pubkey_pem`'s text and `pubkey_pbe`'s tables, which `clause_public_key_codec` calls.

- `translate(decode | encode, Term)` (`translate.cpp`) walks a term as `pubkey_translation:decode/1` and `encode/1`
  do: the generated modules' `SubjectPublicKeyInfo_algorithm`, `DSA-Params`, `DSA-Sig-Value`, `OTPExtension`,
  `SingleAttribute`, `OneAsymmetricKey` (version `v1`) and `EncryptionAlgorithmIdentifier` records become the ones
  `public_key.hrl` documents, and back; an improper list fails with `bad_generator` as the comprehension does.
- `oid_name(Table, Oid)` and `name_oid(Table, Name)` (`tables.cpp`, rows in `tables.inc`) look up the tables
  `public_key_algorithm` (the key type of an algorithm), `curve_type`, `named_curve`, `ml_dsa`, `ml_kem`, `slh_dsa`
  and `extension` (an extension's type); `undefined` when the table has no row, `badarg` for another table.
- `asn1_module(Type)` names the generated module `der_decode/2` and `der_encode/2` use for a type
  (`public_key:get_asn1_module/1`), `undefined` for a type of none.
- `pem_decode(Text)` and `pem_entry(Entry)` (`pem.cpp`, plan 21 step LIB80E1) read and write PEM text as
  `pubkey_pem` does: CRLF/CR/LF lines, BEGIN lines without trailing white space, OTP's labels, the `Proc-Type` and
  `DEK-Info` header and base64 bodies in 64-character lines, with `pubkey_pem`'s errors (`function_clause` without
  an END line, `badmatch` of `DEK-Info` tokens, `unhex`'s `list_to_integer` signs). `pem_decode` stops after an
  `ENCRYPTED PRIVATE KEY` entry, `{Entries, Der, Rest}`, so that Erlang decodes it before the rest is read;
  otherwise `{Entries, done}`.
- `pbe_cipher(Name)`, `pbes2_cipher(Oid)`, `pbes1(Oid)`, `pbes1_oid(Name, Digest)` and `prf_digest(Oid)` (`pbe.cpp`)
  are `pubkey_pbe`'s tables: a PEM cipher's crypto cipher, block size and key length, PBES2 schemes, PBES1
  algorithms and PBKDF2 digests; `function_clause` for anything else.
- `policy_add/4`, `policy_map/5`, `policy_intersection/3`, `policy_prune/1`, `policy_constrained/1` and
  `policy_result/1` (`policy.cpp`, plan 21 step LIB80E4) are `pubkey_policy_tree`'s walks over the valid policy tree
  (RFC 5280 6.1.3 d, 6.1.4 b, 6.1.5 g): children and siblings for a certificate's policies and anyPolicy, policy
  mappings, the intersection with the user's policy set, pruning and the constrained node set with its qualifiers,
  in OTP's list orders. `clause_public_key_path` keeps the verify fun calls, the counters and the node limit.
- `pbkdf1_rounds(First, Count, Digest)` (`pbe.cpp`, plan 21 step LIB80E5) runs PBKDF1's rounds after the first
  through `clause_crypto:hash/2` (each digest in front of those before it, as `pubkey_pbe`), without the Erlang
  loop's quadratic binary building; the first round stays `crypto:hash/2` in Erlang so a bad digest fails as there.
- `os_cacerts()` (`os_cacerts.cpp`, plan 21 step LIB80D6) reads the current user's Windows `ROOT` store through
  crypt32 as OTP's `pubkey_os_cacerts` NIF does: `[{Encoding, Der}]` in the NIF's order, `internal_error` when the
  store does not open, `[]` on other systems, where `clause_public_key_cacerts` reads files.

## Ssl

`builtins/ssl/` in `clause_runtime` (plan 21 step LIB81A, [ssl](libraries/ssl.md)): the `ssl` module's cipher suite
table and `ssl_cipher_format`'s name walks, as catalog builtins of `ssl` itself.

- `cipher_suites(Description, Version)` (`suites.cpp`, rows in `suites.inc` observed from OTP) is a list of OTP's,
  as suite maps; `cipher_suites(Description, Version, rfc | openssl)` gives their names. `function_clause` for
  another version, description or string type; the version is checked first.
- `suite_to_str(Suite)` and `suite_to_openssl_str(Suite)` (`suite_names.cpp`) build a suite's RFC and OpenSSL names
  from any map with the parts a clause of OTP's needs, Latin-1 upper-casing as `string:to_upper/1`;
  `function_clause` for other terms and `badarg` for a part that is not an atom.
- `str_to_suite(Name)` (`suite_parse.cpp`) parses RFC and OpenSSL names as `ssl_cipher_format` does and makes the
  map of any parts whose atoms exist (the table's parts always do); `{error, {not_recognized, Name}}` otherwise.
- `clause_ssl_algorithms:suite_code(Suite)` and `code_suite(Code)` (`suites.cpp`, LIB81C1) map a listed suite's map to
  its two-byte code and back, else `undefined`: connections hand libssl suites by code and report the negotiated one
  as a map.
- `clause_ssl_algorithms:hello_extensions(Message, {Major, Minor})` (`hello.cpp`, LIB81C2) decodes the extensions of
  a ClientHello or ServerHello handshake message as OTP's `ssl_handshake` does for the `{handshake, hello}` pause:
  the version they are read for (the first supported version, else the hello's, at most the local one), the keys
  OTP's map starts with for that version and message, each known extension under OTP's guards (others skipped, a
  malformed tail ending the list), and the values without their top-level records. Signature schemes, groups and
  TLS 1.2 curve OIDs come from OTP's tables; certificate authorities stay DER for Erlang to normalize. `error` for
  a hello OTP fails to decode (a duplicate extension, a misplaced `pre_shared_key`, a bad compression method).

## Ssl engine

`builtins/ssl_engine/` in the separate `clause_runtime_ssl` archive, `library/ssl/clause_ssl_engine.erl` (plan 21
step LIB81B, [ssl](libraries/ssl.md#engine)): one TLS connection over OpenSSL's libssl and memory buffers. Each step
takes the bytes received from the transport and returns the bytes to send to it, so the connection process does
all the I/O.

- Links libssl of the same OpenSSL as the [crypto archive](#crypto-module), which it needs. Calls of catalog
  builtins name no module, so `clau` links the archive when a live function of the batch calls a
  `clause_ssl_engine` builtin. The startup descriptor then lists `CLAUSE_ssl_extension_v1` after the crypto entry.
- `context(client | server, Options)` (`context.cpp`) gives `{ok, Context}`, or `{error, {Key, Text}}` for an
  option libssl refuses. It reads the options `ssl.erl` has validated: `versions`, `ciphers` (suite codes, which
  libssl maps to its TLS 1.3 and 1.2 names), `honor_cipher_order`, `groups`, `signature_algs` (OTP's scheme atoms
  and `{Hash, Sign}` tuples), `certs` (`[{[CertDER], {KeyType, KeyDER}}]`), `verify`, `fail_if_no_peer_cert`,
  `client_cas` and `alpn`; LIB81D1 adds `keylog` (secrets and KeyUpdates become events) and a server's
  `client_renegotiation`; LIB81D2a adds a server's `session_cache => Lifetime` (sessions live in Erlang's cache). It
  drops names the linked libssl does not know and repeats, one at a time. It sets OTP's defaults where libssl's
  differ: no session tickets, no libssl session cache (servers still name each session), security level 0, and
  chains as given.
- `connection(Context, #{server_name => Name, session => SessionDER})` (`connection.cpp`) makes an engine; a client
  offers the given session (LIB81D2a). `handshake(Engine, Received)`
  gives `{done | want_read | verify | Failure, Output}`. `read/2` gives `{ok | closed | Failure, Data, Output}`
  with all the plaintext that is available, and answers a requested key update at once. `write(Engine, IoData)`,
  `shutdown/1` (close_notify) and `key_update(Engine, write | read_write)` give `{ok | Failure, Output}`. A
  failure is `{alert, sent | received, Level, Description}`, else `{error, Text}`.
- Verification (`engine.cpp`): with `verify => verify_peer`, libssl's chain callback keeps the peer's chain
  (`peer_chain/1`) and pauses the handshake (`verify`). Erlang validates the chain and answers with
  `verify(Engine, ok | AlertDescription)`. On rejection, libssl sends that alert, through an X509 error that
  `ssl_x509err2alert` maps back to it. Descriptions outside libssl's table become `certificate_unknown`. Servers
  linked with a libssl older than 4.1 cannot pause: they accept in the callback, and the handshake answers
  `verify` once libssl is done. A rejection then ends the connection without an alert.
- `info(Engine)` (`info.cpp`) gives `protocol`, `cipher_suite` (its code), `alpn`, `server_name`, `group`,
  `session_id`, `resumed`, `client_random`, `server_random` and (LIB81D1) the TLS 1.2 `master_secret`; LIB81C2 adds
  `alert` (the last alert sent or received, also one libssl ignored, as TLS 1.3's `user_canceled`) and `unread`
  (received bytes libssl has not read, as the plain data after a close_notify).
- Connection features (`features.cpp`, LIB81D1): `renegotiate(Engine)` gives `{ok | Failure, Output}` with the
  first message of a TLS 1.2 renegotiation, which later reads finish. `export(Engine, Label, Context | no_context,
  Length)` gives `{ok, Material}` from `SSL_export_keying_material`, else `error`. `events(Engine)` gives the
  events libssl's callbacks queued, oldest first: `{secret, Label, Secret}` (NSS key log lines), `key_update_sent`,
  `key_update_received`, `renegotiation_started`, `renegotiated` and `renegotiation_refused` (a received
  `no_renegotiation` warning). `allow_renegotiation(Engine, Boolean)` sets whether a server accepts the client's
  renegotiations. A finished handshake resets the verdict, so a renegotiation verifies the peer again.
- Session reuse (`sessions.cpp`, LIB81D2a): a server with `session_cache` pauses its handshake in libssl's
  ClientHello callback when the hello offers a TLS 1.2 session, with the status `{lookup, SessionId, ServerName |
  undefined}`. Erlang answers with `resume(Engine, SessionDER | none)`, which libssl's session lookup returns on the
  next step. `session(Engine)` gives `{SessionId, SessionDER, OwnCertificate | undefined}` of the current session,
  else `undefined`. `peer_chain/1` of a resumed session is the peer certificate the session kept.
- `configure(Engine, Context)` (`connection.cpp`, LIB81C2) moves an engine to another context of its role
  (`SSL_set_SSL_CTX` and the context's verify mode): a client's `handshake_continue/2,3` options take effect after
  its ClientHello.
- An engine is a handle reference owning the `SSL` behind a lock. It keeps its context alive for libssl's
  callbacks: ALPN selection picks the server's first preference the client offers, else
  `no_application_protocol`.

## Crypto module

`builtins/crypto/` in the separate `clause_runtime_crypto` archive,
`library/crypto/crypto.erl` and `clause_crypto.erl` (plan 21 step LIB79).

- Links OpenSSL's libcrypto ≥ 3.1.2 (pinned 4.1.0, [OpenSSL](libraries/crypto.md));
  only programs that call `crypto` (or pass `--with-crypto`) link the archive,
  whose `CLAUSE_crypto_extension_v1` entry the startup descriptor lists.
- `algorithms.cpp` keeps OTP's name tables (digests, ciphers, MACs, key types,
  curves, KEMs) with their OpenSSL fetch names; the first `crypto` call loads the
  default, base and legacy providers and fetches every digest, cipher and MAC,
  as OTP's NIF does when it loads; curves are probed (a key generated on each) on
  the first `supports(curves)`. `supports/0,1`, `hash_info/1`, `info/0` and
  `info_lib/0` report what the linked libcrypto has.
- Hash states are handle references owning an `EVP_MD_CTX`; `hash_update/2`
  returns a new state over a copy, as OTP's. MAC states own an `EVP_MAC_CTX`
  that `mac_update/2` advances in place. `crypto.erl` feeds texts longer than
  20000 bytes in pieces, as OTP does, so no builtin call runs long.
- Errors are OTP's NIF errors: `{badarg | notsup | error, #{c_file_name,
  c_file_line_num, c_function_arg_num}, Description}`, which `crypto.erl` turns
  into `{Class, {File, Line}, Description}` with `error_info` (`?nif_call`);
  descriptions and checks follow OTP's order, the file and line are Clause's.
- Engines answer as an OTP whose OpenSSL has no engine support (`notsup`;
  OpenSSL 4 has none).
- Ciphers (`cipher.cpp`, step LIB79B) follow OTP's `api_ng.c`: `crypto_init/4`
  states own an `EVP_CIPHER_CTX` behind a lock that `crypto_update/2` advances in
  place; `crypto_final/1` pads as the options say (OTP's `undefined`, `none`,
  `zero`, `random`, `pkcs_padding`); `crypto_one_time/5` runs a temporary state.
  `crypto.erl` feeds `crypto_update/2` texts longer than 20000 bytes in pieces.
  AEAD (`aead.cpp`) follows `aead.c` for GCM, CCM and ChaCha20-Poly1305; a wrong
  tag decrypts to `error`. `crypto.erl` runs the `crypto_aes` and `crypto_prng1`
  `rand` generators as OTP's, over `rand`'s `exro928_*` and `seed58/2` builtins.
- FIPS (`fips.cpp`, step LIB79F): each table entry keeps a second `fips=yes` fetch, used in FIPS mode; an entry
  without one is forbidden there (OTP's `*_FORBIDDEN_IN_FIPS`), leaves `supports/1` and raises OTP's `notsup`
  descriptions. `enable_fips_mode/1` switches OpenSSL's default properties; programs built with `--fips` link the
  archive's `CLAUSE_crypto_fips_extension_v1` entry, whose start hook loads the provider beside the executable
  ([FIPS](libraries/crypto.md#fips)).
- Public keys (step LIB79C) follow OTP's `pkey.c`, `rsa.c`, `dss.c`, `eddsa.c`, `ec.c`, `ecdh.c`, `dh.c`,
  `evp.c` and `srp.c`: `crypto.erl` turns integers into binaries (`format_pkey`, `nif_curve_params`) and the
  builtins build OpenSSL keys from the same `OSSL_PARAM` lists OTP's NIFs build (`params.cpp`, native-order big
  numbers), with OTP's checks, descriptions and argument numbers. `pkey.cpp`: `sign/4,5`, `verify/5,6` (RSA with
  PKCS #1, PSS, X9.31 and no padding, DSA, ECDSA, EdDSA, ML-DSA, SLH-DSA; `{digest, Digest}`), RSA
  `public_encrypt/4` and its three siblings (PKCS #1, OAEP with its digests and label, no padding),
  `privkey_to_pubkey/2`. `keys.cpp`: RSA, DH, X25519/X448, Ed25519/Ed448, ML-DSA, SLH-DSA and ML-KEM key
  generation, DH and X25519/X448 agreement, `encapsulate_key/2`, `decapsulate_key/3`. `curves.cpp`: ECDH keys and
  agreement on named or explicit curves, retried with explicit parameters when OpenSSL does not know OTP's name (as
  OTP's `retry_without_name`), and `ec_curve/1`, whose explicit parameters come from the linked libcrypto's built-in
  curves instead of OTP's `crypto_ec_curves` table (byte for byte the same terms). `srp.cpp`: SRP's big number
  formulas; `crypto.erl` keeps OTP's multiplier and scrambler. OTP's quirks are kept: `sign(dss, none, ...)` fails
  with `"Bad digest type"`, `generate_key(dh, ...)` ignores a given private key, and a multi-byte RSA public
  exponent reaches OpenSSL in native byte order.

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
