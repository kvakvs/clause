# Plan 21: Clause's own erts, kernel and stdlib

The general plan for the missing standard library. Goal: every public module
that OTP 29 documents for `erts`, `kernel` and `stdlib` has a Clause
implementation of its documented functions, so programs need no OTP sources on
`source_search_paths`. Native C++ runtime builtins are preferred; Erlang is used
where a function takes funs, runs a process protocol or is glue. Undocumented
and internal functions stay out unless a public function needs them (user
direction 2026-10-17).

IMPORTANT: Prefer compatibility with Erlang/OTP at all times, if something can
be faster at the cost of compatibility, add that as a future task and we will
create a new API that's for the end user to choose

## Discovery (2026-10-17)

- **Source of truth:** the doc chunks of the installed OTP 29.1.1
  (`code:get_doc/1`). A module counts when its module doc is not `hidden`, a
  function when its doc is not `hidden` and it is not deprecated. The OTP
  checkout (pin `d199a49a01`) gave line counts and NIF use.
  `tests/compiler/library/observe_docs.escript` records the documented
  functions in `library/documented.terms`; `library_modules.py coverage .`
  prints the progress per module.
- **Result:** 120 documented modules (erts 9, kernel 37, stdlib 74) with 2,888
  documented functions; 2,308 are still to do (erts 65, kernel 639, stdlib
  1,604). Appendix A lists them per module.
- **Complete already** (every documented function, or the rest declined):
  `atomics`, `counters`, `erl_prim_loader`, `persistent_term`, `prim_eval`,
  `zlib`, `binary`, `lists`, `maps`, `math`, `re` (`import/1` declined), `init`
  (`boot/1`, `restart/0,1` refused).
- **Partial:** `erlang` (62 left), `code` (51), `file` (30), `io` (34), `inet`
  (19), `unicode` (12), `gen_udp` (5), `os` (4), `gen_tcp` (1).
- **Missing:** 97 modules. Today a program gets the plain-Erlang ones (gen
  servers, `proplists`, `string`, ...) only by compiling OTP's sources from a
  search path; ETS has no implementation at all.
- **Callback-only modules** (no functions to implement, only behaviours to
  honour): `erl_tracer`, `logger_handler`.

## Working rules for plan 21 steps

- Commit each step when its gate is green: subject `[plan21] LIBn. Title`.
  Finished steps move to `00-finished.md` (Plan 21) with their hash.
- The plan 11 gate applies: fast CTest, `check-quality` (clang-tidy and Lizard,
  no suppressions), clang-format, erlfmt for `.erl`/`.terms`, docs
  (`docs/library.md` row, `docs/builtins.md` section, `docs/differences.md`,
  `.agents/arch.md`, `.agents/files.md`).
- **One golden per module family**, generated once from OTP with
  `tests/compiler/executables/regenerate.py`. Print maps and other unordered
  results as sorted lists. Wrap bad arguments in `id/1` so OTP's compiler does
  not warn about them or drop the code after them (as with `maps:map(F, none)`
  in 29.1.1).
- **C++ first:** data functions are typed or raw builtins in
  `runtime/src/builtins/<module>/`. They are listed in the
  `abi/include/clause/abi/builtins.hpp` catalog and have
  `clause_builtin_placeholder` bodies in the `.erl` file. Functions taking funs,
  process loops and behaviours are Erlang, written from the documentation in
  Clause's own shape over native helpers (as `maps:merge_with/3` is over
  `merge/2`). Never copy OTP sources.
- **Representations OTP documents or programs observe stay OTP's:** `queue`'s
  `{Rear, Front}`, `gb_trees`' `{Size, Tree}`, `array` and `sets` records,
  `digraph`'s tuple. `=:=`, printing and `term_to_binary` must agree with OTP.
  Where a representation is opaque and changing it pays off, record it in
  `differences.md`.
- **A module enters `library/` only when all its documented functions work** (or
  are declined with a reason in its `.terms`). A library module shadows the OTP
  source of the same name on the search path, so a half-done module would break
  programs that build today. Related modules land together (the
  `proc_lib`/`gen_*`/`sys`/`supervisor` family) so OTP-sourced and Clause-owned
  modules are not mixed inside one protocol.
- **Local-node semantics:** a Clause program is a non-distributed node
  (`nonode@nohost`). Modules that work on such a node in OTP (`global`, `pg`,
  `rpc`, `erpc`, `net_adm`, `net_kernel`'s queries) behave as OTP does there.
  Remote nodes get OTP's non-distributed answers (`{badrpc, nodedown}`,
  `noconnection`, `pang`).
- **Declined functions** raise or return what OTP gives where the feature is
  unavailable (`{error, enotsup}`, `{error, not_distributed}`, ...). Each one is
  recorded in `.terms` as `{declined, Reason}`, and `modules.terms` records the
  declined modules.
- **Not implemented placeholder**: Such functions are declared but return
  erlang:error({not_implemented, <function name>, <arity>}).
- **Implemented but in C++**: Erlang counterpart function declaration is created
  to keep exports table consistent, and it always returns
  `clause_builtin_placeholder` atom.

## Choosing C++ or Erlang

| Kind | Implement in | Examples |
| --- | --- | --- |
| Pure data transforms, no funs | C++ builtin | `ordsets:union/2`, `proplists:get_value/3`, `string:split/3`, `base64:encode/1`, `calendar:*`, `filename:*`, `json:decode/1`, `uri_string:parse/1` |
| Tables, shared state, OS access | C++ runtime object plus builtins | ETS, `file:pread/3`, `os:find_executable/1`, `net:getaddrinfo/1`, `win32reg` |
| Takes funs | Erlang over C++ helpers | `ordsets:filter/2`, `gb_trees:map/2`, `json:decode/3`, `queue:filter/2`, `ets:foldl/3` |
| Processes, protocols, behaviours | Erlang | `proc_lib`, `gen_server`, `supervisor`, `logger`, `timer:apply_interval/4`, `application` |
| Front-end terms (tokens, forms) | C++ over Clause's lexer and parser | `erl_scan:string/1`, `erl_parse:parse_form/1`, `epp:parse_file/2` |
| Interpreters over forms | Erlang | `erl_eval`, `ms_transform`, `qlc` |

## Tier A: data modules without processes

### LIB1. Ordered lists: `ordsets`, `orddict`, `proplists`

- C++:
  - `ordsets`: everything without funs, as merges over sorted lists
    (`union/1,2`, `intersection/1,2`, `subtract/2`, `is_subset/2`,
    `is_disjoint/2`, `from_list/1`, ...).
  - `orddict`: everything without funs (`store/3`, `fetch/2`, `find/2`,
    `append/3`, `update_counter/3`, `take/2`, ...).
  - `proplists`: all 22 functions (`get_value/2,3`, `get_bool/2`,
    `lookup_all/2`, `expand/2`, `normalize/2`, `split/2`, `to_map/1,2`, ...).
- Erlang: `filter/2`, `filtermap/2`, `map/2`, `fold/3`, `orddict:merge/3`,
  `orddict:update/3,4`.

### LIB2. Hashed collections: `sets`, `dict`

- `sets`: version 2 (map-backed, the OTP 29 default) in C++.
  `new/1`/`from_list/2` with `{version, 1}` build OTP's v1 record; v1 is
  implemented as well, so `is_set/1` and `=:=` agree.
- `dict`: OTP's `dict` record of hashed segments, in C++ (`store/3`, `fetch/2`,
  `append/3`, `update_counter/3`, `merge/3` without fun ...). It needs
  `erlang:phash/2` (LIB16), because the bucket layout must match OTP's.
- Erlang: the fun-taking functions.

### LIB3. Trees and queues: `gb_trees`, `gb_sets`, `queue`, `array`

- C++ builds the same shapes as OTP, so results compare equal and print alike.
  The goldens print the internal structures.
  - `gb_trees` and `gb_sets` follow Andersson's general balanced trees (the
    published algorithm): insert, delete, balance, `from_orddict/1`, iterators,
    `smaller/2`, `larger/2`, ...
  - `queue`'s `{Rear, Front}` lists with OTP's rebalancing rules.
  - `array`'s record and tuple tree: `get/2`, `set/3`, `resize/1,2`,
    `from_list/1,2`, the `sparse_*` lists, `shift/2`, `slice/3`, ...
- Erlang: `map/2`, `filter/2`, `filtermap/2`, `fold/3`, the `foldl`/`foldr`/
  `mapfold*` families, `queue:all/2`, `any/2`, `delete_with/2`.

### LIB4. Text: `string`, `unicode` remainder, `io_lib`

- `string` (69): C++ over the grapheme and case tables Clause already generates
  for `unicode_util`. That covers lexemes, `split/2,3`, `trim/1,2,3`,
  `pad/2,3,4`, `find/2,3`, `replace/3,4`, `equal/2,3,4`, `casefold/1`,
  `to_integer/1`, `to_float/1` and `jaro_similarity/2`, plus the obsolete
  Latin-1 API (`substr/2,3`, `tokens/2`, `strip/1,2,3`, ...).
- `unicode`: the normalization forms (`characters_to_nf*`), `category/1`,
  `is_whitespace/1` and `is_id_start/1`, `is_id_continue/1`, in C++ over tables
  generated from the pinned UCD.
- `io_lib` (36): C++ on the existing `io_format` engine. That covers
  `format/2,3`, `fwrite`, `bformat`, `bfwrite`, `write/1,2`, `print/1,4`,
  `write_string`, `write_atom`, `write_char`, the printable checks,
  `scan_format/2`, `unscan_format/1`, `build_text/1`, `indentation/2` and
  `fread/2,3` (a format-directed scanner), plus `nl/0`.
- Fix `io:format("~.2f")`, which raises `badarg` today.

### LIB5. Encodings and formats: `base64`, `uri_string`, `json`, `filename`, `calendar`

- C++:
  - `base64` (12), `uri_string` (15), `filename` (17: Unix and Windows path
    rules, `basedir/2,3` from the environment) and `calendar` (34: Gregorian
    arithmetic, RFC 3339, local time through the existing `erlang:localtime`
    services).
  - `json`: `decode/1`, `encode/1`, `format/1,2` and the leaf encoders
    `encode_integer`, `encode_float`, `encode_binary`, `encode_atom`, ...
- Erlang: `json:decode/3`, `decode_start/3` and `decode_continue/2` (callback
  decoders), and `encode/2`, `format/2,3` with an encoder fun, over the C++ leaf
  encoders.

### LIB6. Random numbers: `rand`, `random`

- `rand` (31): the generators in C++ (`exsss`, the default, plus `exro928ss`,
  `exrop`, `exs1024s`, `exsp`, `mwc59`, `splitmix64`), with OTP's state tuples
  and the process dictionary seed key. Seeded sequences equal OTP's: the golden
  prints sequences from fixed seeds. `normal/0,2`, `shuffle/1`, `bytes/1` and
  the jump functions are C++ too.
- `random` (deprecated, 8): C++ (AS183) for programs that still call it.

### LIB7. Set algebra and graphs: `sofs`, `graph`

- `sofs` (81): C++ for typed sets, relations and families, which are sorted
  lists under OTP's `{Set, Type}` records. Erlang for `specification/2`,
  `substitution/2`, `partition/2` and `image/2` with funs.
- `graph` (63, new in OTP 29, functional and map-backed): C++ for the traversals
  (`topsort/1`, `components/1`, `strong_components/1`, `reachable/2`,
  `get_short_path/3`, ...) and the updates; Erlang for `fold_vertices/3`.

### LIB8. Records and compiler tables: `records`, `erl_internal`, `erl_anno`, `erl_features`

- `records` (8, OTP 29 native records): C++ over Clause's native records (F17).
- `erl_internal` (10): C++, answered from the compiler's own operator tables and
  the builtin catalog.
- `erl_anno` (18): C++ (annotations are integers, tuples or lists).
- `erl_features` (5): C++, answered from Clause's feature set.

### LIB9. `erlang` gaps (62)

- C++:
  - Checksums: `adler32/1,2`, `adler32_combine/3`, `crc32/1,2`,
    `crc32_combine/3` (pinned zlib).
  - Tuples and iovecs: `append_element/2`, `insert_element/3`,
    `delete_element/2`, `iolist_to_iovec/1`.
  - Packets: `decode_packet/3`, shared with the socket packet modes.
  - Small builtins: `is_integer/3`, `halt/2`, `hibernate/0`,
    `bump_reductions/1`, `process_display/2`.
  - Processes: `processes_iterator/0`, `processes_next/1`,
    `suspend_process/1,2`, `resume_process/1`, `process_flag/3`.
  - Signals: `send/3`, `send_nosuspend/2,3`, `link/2`, `exit/3`,
    `exit_signal/3`.
  - Spawning: `spawn*/2,4` for the local node.
  - System flags: `system_flag/2`, the documented flags that apply.
  - Deprecated `phash/2`, needed by `dict`.
- `match_spec_test/3` comes with LIB11.
- Non-distributed answers: `get_cookie/0,1` (`nocookie`), `set_cookie/1,2`.
- Declined:
  - D01: `load_module/2`, `load_nif/2`, `purge_module/1`, `check_*code`.
  - Distribution: `dist_ctrl_*`.
  - Tracing: `trace*`, `system_monitor/0,1,2`, `system_profile/0,2`. LIB71
    revisits these.

## Tier B: ETS and what needs it

### LIB10. ETS tables (C++)

- Tables are a runtime object with off-heap term storage (copied in and out, as
  for `persistent_term`), in `set`, `ordered_set`, `bag` and `duplicate_bag`
  types.
- Table rules: owner, `heir`, `give_away/3`, named tables,
  `public`/`protected`/`private`, deletion when the owner dies, and safe access
  from every scheduler worker (TSan, V02).
- Builtins:
  - Tables: `new/2`, `delete/1`, `info/1,2`, `rename/2`, `setopts/2`, `all/0`,
    `whereis/1`, `i/0,1`.
  - Writes: `insert/2`, `insert_new/2`, `delete/2`, `delete_object/2`,
    `delete_all_objects/1`.
  - Reads: `lookup/2`, `lookup_element/3,4`, `member/2`, `take/2`, `tab2list/1`,
    `slot/2`, `safe_fixtable/2`.
  - Walking: `first/1`, `next/2`, `last/1`, `prev/2` and their `*_lookup` forms.
  - Updates: `update_counter/3,4`, `update_element/3,4`.
- Erlang: `foldl/3`, `foldr/3`.

### LIB11. Match specifications (C++)

- One compiler and interpreter of match specs serves:
  - `ets:match/1,2,3`, `match_object/1,2,3` and `select/1,2,3`, with
    continuations.
  - `select_count/2`, `select_delete/2`, `select_replace/2`,
    `select_reverse/1,2,3`, `match_delete/2`, `repair_continuation/2`.
  - `match_spec_compile/1`, `match_spec_run/2`, `is_compiled_ms/1`, `test_ms/2`
    and `erlang:match_spec_test/3`.
- `ets:fun2ms/1` raises as OTP's does without the transform; it works once LIB72
  (`ms_transform`) and D03 parse transforms exist.

### LIB12. Table files and graphs: `ets` file functions, `digraph`, `digraph_utils`

- `ets:tab2file/2,3`, `file2tab/1,2`, `tabfile_info/1` in OTP's file format
  (over `disk_log`, LIB52); `to_dets/2`, `from_dets/2` and `init_table/2` after
  LIB53, and `table/1,2` after `qlc` (LIB73).
- `digraph` (34): OTP's `{digraph, V, E, N, Cyclic}` over three ETS tables, with
  the updates and queries in C++ reading the tables directly.
- `digraph_utils` (18): C++ traversals over the same tables.

## Tier C: processes and behaviours (Erlang)

### LIB20. `proc_lib` and `sys`

- `proc_lib` (35):
  - Spawning: `spawn*`, `start*` with `init_ack/1,2` and `init_fail/2,3`.
  - Process info: `$ancestors`, `$initial_call`, `initial_call/1`,
    `translate_initial_call/1`, `set_label/1`, `get_label/1`.
  - Other: `hibernate/3`, `stop/1,3`, and crash reports through `logger` (a
    minimal `logger` front first, completed in LIB23).
- `sys` (33): the system message protocol and debug options. That covers
  `get_state/1,2`, `replace_state/2,3`, `get_status/1,2`, `suspend/1,2`,
  `resume/1,2`, `statistics/2,3`, `trace/2,3`, `log/2,3`, `log_to_file/2,3`,
  `install/2,3`, `remove/2,3`, `handle_system_msg/6` and `handle_debug/4`.
  `change_code/4,5` answers as for a static module (D01).

### LIB21. Generic behaviours

- `gen_server` (32), `gen_statem` (30), `gen_event` (33), `supervisor` (14),
  `supervisor_bridge` (2): Erlang over `proc_lib` and `sys`, including the
  request id collections (`reqids_*`, `send_request`, `receive_response`,
  `wait_response`, `check_response`).
- `multi_call` and `abcast` work on the local node only.
- `gen_fsm` (deprecated, 19): a thin adapter over `gen_statem`, last in this
  step.

### LIB22. `timer`

- C++: `hms/3`, `hours/1`, `minutes/1`, `seconds/1`, `now_diff/2`.
- Erlang:
  - `tc/1..4` and `sleep/1`.
  - Timed sends (`send_after/2,3`, `exit_after/2,3`, `kill_after/1,2`) over
    `erlang:send_after`, without a timer server.
  - `apply_after/2,3,4`, `apply_interval/2,3,4`, `apply_repeatedly/2,3,4`,
    `send_interval/2,3` and `cancel/1` through per-timer processes.
  - `start/0` returns `ok`.

### LIB23. Logging: `logger` and friends

- `logger` (68) in Erlang:
  - Configuration: primary, handler and proxy config, kept in `persistent_term`
    plus a `logger` server.
  - Levels: module and application levels, process metadata.
  - Events: the `?LOG`-free API functions (`error/1,2,3`, ...) and
    `format_report/1`.
- Handlers and formatting:
  - `logger_formatter` (`format/2` templates, `check_config/1`) in Erlang over
    `io_lib`.
  - `logger_std_h` (`standard_io`, `standard_error`, files; `filesync/1`).
  - `logger_disk_log_h` after LIB52.
  - `logger_filters` (4).
- Compatibility modules:
  - `error_logger` (20) over `logger`, with report handlers as `gen_event`.
  - `log_mf_h` (2).
  - `error_handler` (3): `undefined_function/3`, `undefined_lambda/3`,
    `raise_undef_exception/3`, matching Clause's `undef` path.

### LIB24. `application`

- `application` (35) and its controller in Erlang.
- Application specs come from `.app` resources compiled into the executable,
  either from project sources or from an `[targets.options.applications]` entry.
- Functions:
  - Lifecycle: `load/1,2`, `start/1,2`, `ensure_started`,
    `ensure_all_started/1,2,3`, `stop/1`, `unload/1`.
  - Environment: `get_env`, `set_env`, `unset_env`, `get_key`, `get_all_key`,
    `get_application/0,1`.
  - Queries: `which_applications/0,1`, `loaded_applications/0`, `permit/2`,
    `start_type/0`, `get_supervisor/1`, `takeover/2`.
- `kernel` and `stdlib` load as virtual applications. The existing all-stubs
  `application_controller` is replaced.

### LIB25. Local-node kernel services

- Real services, in Erlang:
  - `pg` (30), with local scopes, monitors and subscriptions.
  - `global` (21), with names, locks and `trans/2,3,4` on the local node, as on
    an OTP node that is not distributed.
  - `global_group` (10): no groups.
  - `rpc` (25) and `erpc` (27): calls to `node()` run locally; other nodes get
    OTP's non-distributed results.
  - `data_publisher` (12, new in OTP 29).
- Non-distributed answers: `net_adm` (10: `localhost/0`, `ping/1` gives `pang`,
  ...), `net_kernel` (13: `get_state/0`, `monitor_nodes/1,2`; `start/2` gives
  OTP's error without epmd), `auth` (deprecated) and `seq_trace` (9: token
  storage per process; tracer output with LIB71).

## Tier D: Erlang front end as library modules

### LIB30. `erl_scan` (C++)

- `string/1,2,3` and `tokens/3,4` (with OTP's continuation protocol) over
  Clause's C++ lexer, producing OTP's token tuples and annotations.
- `category/1`, `symbol/1`, `text/1`, `location/1`, `line/1`, `column/1`,
  `end_location/1`, `reserved_word/1`, `format_error/1`.

### LIB31. `erl_parse` (C++)

- `parse_form/1`, `parse_exprs/1` and `parse_term/1` over Clause's parser,
  emitting OTP's abstract format.
- `tokens/1,2` and `abstract/1,2` in C++; `normalise/1`, the `*_anno` functions
  and `format_error/1` follow.

### LIB32. `erl_eval` (Erlang)

- An evaluator of abstract forms written for Clause: `exprs/2,3,4`, `expr/2..5`,
  `expr_list/2,3,4` and the bindings API.
- Local and non-local function handlers.
- Funs created by `erl_eval` are interpreted closures.

### LIB33. Readers built on the front end

- `file:consult/1`, `path_consult/2`, `eval/1,2`, `path_eval/2`, `script/1,2`
  and `path_script/2,3`.
- `io:read/1..4`, `scan_erl_exprs/1..4`, `scan_erl_form/1..4`,
  `parse_erl_exprs/1..4`, `parse_erl_form/1..4` (`get_until` with the `erl_scan`
  continuation).

### LIB34. Source tools

- `epp` (18): C++ over Clause's preprocessor, giving forms and encodings.
- `erl_pp` (16): Erlang, printing the abstract format.
- `erl_expand_records` (1): Erlang.
- `erl_error` (2): `format_exception/3,4` in Erlang over `io_lib`.
- `erl_lint` (5): `is_guard_test/1` and `format_error/1` first; `module/1,2,3`
  later, from Clause's semantic checks mapped to OTP's warning terms.

## Tier E: files, archives and logs

### LIB50. `file` remainder (C++ driver operations)

- `pread/2,3`, `pwrite/2,3`, `sync/1`, `datasync/1`, `truncate/1`, `advise/4`,
  `allocate/3`, `write_file_info/2,3`.
- `change_mode/2`, `change_owner/2,3`, `change_group/2`, `change_time/2,3`,
  `sendfile/2,5`, `path_open/3`, `format_error/1`.

### LIB51. `filelib`

- C++: `wildcard/1,2`, glob matching over directory listings, and
  `safe_relative_path/2`.
- Erlang: `ensure_dir/1`, `ensure_path/1`, `fold_files/5`, `is_dir/1`,
  `is_file/1`, `is_regular/1`, `file_size/1`, `last_modified/1`,
  `find_file/2,3`, `find_source/1,2,3`.

### LIB52. `disk_log` and `wrap_log_reader`

- Erlang processes over `file`, with C++ for OTP's halt and wrap log chunk
  formats, so log files are exchangeable with OTP.
- Unblocks `logger_disk_log_h` and the `ets` file functions.

### LIB53. `dets`

- `dets` (44): Erlang server with C++ for the file format.
- Reads and writes OTP's byte-by-byte compatible version-9 files.

### LIB54. Archives: `zip`, `erl_tar`, `zstd`, `file_sorter`, `beam_lib`

- `zip` (22) and `erl_tar` (14): C++ readers and writers over the pinned zlib,
  with an Erlang API (`foldl/3`, options).
- `zstd` (17): C++ over a pinned zstd under `thirdparty/`, like zlib.
- `file_sorter` (14): a C++ external merge sort; Erlang for user order funs.
- `beam_lib` (19): a C++ chunk reader and writer for `.beam` files (Clause makes
  none, but programs inspect them).

### LIB55. Platform modules

- `win32reg` (13): C++ over the Windows registry; `{error, enotsup}` elsewhere,
  as OTP does.
- `escript` (3): `script_name/0` is the executable path; `create/2` and
  `extract/2` over `zip`.

## Tier F: kernel I/O remainder

### LIB60. Sockets and names

- `inet` (19): `getopts/2`, `getstat/1,2`, `getifaddrs/0,1`, `gethostbyaddr/1`,
  `monitor/1`, `cancel_monitor/1`, `info/1`, `i/0,1,2`, `peernames`,
  `socknames`, `get_rc/0`, `format_error/1`.
- `gen_udp` `connect/2,3` and `send/2,3,5`; `gen_tcp` `connect/2`.
- `net` (15): C++ (`getaddrinfo`, `getnameinfo`, `gethostname`, `getifaddrs`,
  interface names, `getservbyname`, `getservbyport`).
- `inet_res` (20): an Erlang DNS client over `gen_udp`, with a C++ DNS message
  codec.

### LIB61. `os` and `code` remainder

- `os` (C++): `cmd/2`, `find_executable/1,2`, `set_signal/2`.
- `code` path functions (Erlang over `persistent_term` and the executable
  layout): `get_path/0`, `set_path/1,2`, `add_path*`, `del_path*`,
  `replace_path/2,3`, `lib_dir/0,1`, `priv_dir/1`, `root_dir/0`,
  `compiler_dir/0`, `where_is_file/1`, `objfile_extension/0`, `clash/0`,
  `all_available/0`, `get_mode/0`, `clear_cache/0`.
- Loading and coverage answer as for a static program (D01): `load_*`,
  `purge/1`, `soft_purge/1`, `delete/1`, `atomic_load/1`, the
  `prepare_loading`/`finish_loading` pair, `get_object_code/1`,
  `modified_modules/0`, the coverage functions. `get_doc/1` gives
  `{error, missing}` unless docs are embedded later.

### LIB62. `socket` and `gen_sctp`

- `socket` (79): a C++ socket object over the existing socket driver (`open`,
  `bind`, `listen`, `accept`, `connect`, `send*`, `recv*`, options,
  `select`/`completion` handles, monitors). This reverses the "missing" decision
  in `modules.terms`.
- `gen_sctp` (23): `{error, eprotonosupport}` where SCTP is absent, as in OTP;
  SCTP support is optional and later.

### LIB63. `io` remainder and terminal output

- `io`: `write/1,2`, `fread/2,3` (over `io_lib:fread`), `columns/0,1`,
  `rows/0,1`, `getopts/0,1`, `setopts/1,2`, `printable_range/0`,
  `get_password/0`. The read and parse family is in LIB33.
- `io_ansi` (153, new in OTP 29): C++ escape-sequence builders and `enabled/0,1`
  terminal detection; `format/1,2,3` and `fwrite/1,2,3,4` over `io_lib`.

## Tier G: tools, transforms and the interactive shell

### LIB70. `argparse`

- `argparse` (6): Erlang (command maps and handler funs), with C++ for splitting
  and converting arguments.

### LIB71. Tracing family

- `trace` (11), `seq_trace` tracer output, `erl_tracer` callbacks and the
  `erlang:trace*` builtins.
- Needs runtime trace points (sends, receives, calls, process events).
- Selected or declined as one decision. The call tracer of plan 11 is the
  starting point.

### LIB72. `ms_transform`

- `parse_transform/2`, `transform_from_shell/3` and `format_error/1` in Erlang
  over the abstract format (LIB31).
- Enables `ets:fun2ms/1` once D03 parse transforms run.

### LIB73. `qlc`

- `qlc` (26): Erlang query handles and evaluation over `ets`, `dets` and lists.
- The `qlc` parse transform needs D03; `string_to_handle/1,2,3` works through
  `erl_eval`.

### LIB74. Interactive modules (last; optional)

- `shell` (18), `shell_default` (1), `shell_docs` (15), `edlin` (1),
  `edlin_expand` (2), `c` (44).
- The process-inspection subset of `c` is useful without a shell: `pid/3`,
  `flush/0`, `i/0,3`, `regs/0`, `nregs/0`, `memory/0,1`, `uptime/0`, `bt/1`,
  `pwd/0`, `cd/1`, `ls/0,1`, `q/0`.
- The compiler and loading parts (`c/1,2,3`, `l/1`, `lm/0`, `nc/1,2`) are
  declined (D01).

### Declined modules (recorded in `modules.terms`)

| Module | Reason | OTP's answer on a non-distributed or unsupported node |
| --- | --- | --- |
| `erl_boot_server` (7), `erl_epmd` (8) | Distribution boot and port mapping | Start fails as without epmd |
| `peer` (11), `slave` (deprecated), `pool` (8) | Start other nodes | `{error, not_alive}` or the matching error |
| `heart` (8) | Needs OTP's heart port program | `heart` not started |
| `erl_ddll` (15) | No dynamic drivers (D01) | `loaded_drivers/0` gives `{ok, []}`, loads give `{error, ...}` |
| `erl_debugger` (13) | No debugger interface | `supported/0` gives `false` |

These still get small modules answering as OTP does where the feature is
missing, so programs that probe for them keep working.

## Suggested order

Ranked by what real programs need first and by dependencies:

1. LIB1 and LIB2 (`proplists`, `ordsets`, `orddict`, `sets`, `dict`).
2. LIB4 text (`string`, `io_lib`, `unicode`).
3. LIB10 and LIB11 (ETS and match specs).
4. LIB20–LIB22 (`proc_lib`, `sys`, behaviours, `timer`).
5. LIB23 and LIB24 (`logger`, `application`).
6. LIB3, LIB5 and LIB6 (trees, queues, encodings, `rand`).
7. LIB9, LIB50, LIB60 and LIB61 (`erlang`, `file`, `inet`, `os`, `code` gaps).
8. LIB30–LIB34 (the front end, `consult`, `io:read`).
9. LIB25 (local-node services) and LIB12 (`digraph`).
10. LIB51–LIB55 (files, logs, archives).
11. LIB7, LIB8, LIB70, LIB62 and LIB63.
12. LIB71–LIB74, then the declined modules' answer stubs.

## Appendix A: documented functions still to do (OTP 29.1.1 doc chunks)

Format: `module` (application, functions to do, ...): name/arities. Deprecated
functions are listed separately and are optional.

- `erlang` (erts, 62, 218 done): adler32/1,2, adler32_combine/3,
  append_element/2, bump_reductions/1, check_old_code/1, check_process_code/2,3,
  crc32/1,2, crc32_combine/3, decode_packet/3, delete_element/2,
  dist_ctrl_get_data/1, dist_ctrl_get_data_notification/1, dist_ctrl_get_opt/2,
  dist_ctrl_input_handler/2, dist_ctrl_put_data/2, dist_ctrl_set_opt/3, exit/3,
  exit_signal/3, get_cookie/0,1, halt/2, hibernate/0, insert_element/3,
  iolist_to_iovec/1, is_integer/3, link/2, load_module/2, load_nif/2,
  match_spec_test/3, process_display/2, process_flag/3, processes_iterator/0,
  processes_next/1, purge_module/1, resume_process/1, send/3,
  send_nosuspend/2,3, set_cookie/1,2, spawn/2,4, spawn_link/2,4,
  spawn_monitor/2,4, suspend_process/1,2, system_flag/2, system_monitor/0,1,2,
  system_profile/0,2, trace/3, trace_delivered/1, trace_info/2,
  trace_pattern/2,3; deprecated: now/0, phash/2
- `init` (erts, 3, 8 done): boot/1, restart/0,1
- `application` (kernel, 35; 5 callbacks): ensure_all_started/1,2,3,
  ensure_started/1,2, get_all_env/0,1, get_all_key/0,1, get_application/0,1,
  get_env/1,2,3, get_key/1,2, get_supervisor/1, load/1,2, loaded_applications/0,
  permit/2, set_env/1,2,3,4, start/1,2, start_type/0, stop/1, takeover/2,
  unload/1, unset_env/2,3, which_applications/0,1
- `auth` (kernel, 0): none; deprecated: cookie/0,1, is_auth/1, node_cookie/1,2
- `code` (kernel, 51, 8 done): add_path/1,2, add_patha/1,2, add_paths/1,2,
  add_pathsa/1,2, add_pathsz/1,2, add_pathz/1,2, all_available/0, atomic_load/1,
  clash/0, clear_cache/0, compiler_dir/0, coverage_support/0, del_path/1,
  del_paths/1, delete/1, finish_loading/1, get_coverage/2,
  get_coverage_mode/0,1, get_debug_info/1, get_doc/1, get_mode/0,
  get_object_code/1, get_path/0, lib_dir/0,1, load_abs/1, load_binary/3,
  load_file/1, modified_modules/0, objfile_extension/0, prepare_loading/1,
  priv_dir/1, purge/1, replace_path/2,3, reset_coverage/1, root_dir/0,
  set_coverage_mode/1, set_path/1,2, soft_purge/1, stick_dir/1, unstick_dir/1,
  where_is_file/1; deprecated: lib_dir/2
- `data_publisher` (kernel, 12; 11 callbacks): handle_call/3, handle_cast/2,
  handle_info/2, init/1, start/2,3, start_link/2,3, subscribe/2, terminate/2,
  unsubscribe/2, update/2
- `disk_log` (kernel, 34): all/0, alog/2, alog_terms/2, balog/2, balog_terms/2,
  bchunk/2,3, block/1,2, blog/2, blog_terms/2, breopen/3, btruncate/2,
  change_header/2, change_notify/3, change_size/2, chunk/2,3, chunk_info/1,
  chunk_step/3, close/1, format_error/1, info/1, log/2, log_terms/2,
  next_file/1, open/1, pid2name/1, reopen/2,3, sync/1, truncate/1,2, unblock/1;
  deprecated: inc_wrap_file/1
- `erl_boot_server` (kernel, 7): add_slave/1, delete_slave/1, start/1,2,
  start_link/1,2, which_slaves/0
- `erl_ddll` (kernel, 15): demonitor/1, format_error/1, info/0,1,2, load/2,
  load_driver/2, loaded_drivers/0, monitor/2, reload/2, reload_driver/2,
  try_load/3, try_unload/2, unload/1, unload_driver/1
- `erl_debugger` (kernel, 13): breakpoint/3, breakpoints/1,3,
  instrumentations/0, peek_stack_frame_slot/4, peek_xreg/3, register/1,
  stack_frames/2, supported/0, toggle_instrumentations/1, unregister/2,
  whereis/0, xregs_count/1
- `erl_epmd` (kernel, 8): address_please/3, listen_port_please/2, names/1,
  port_please/2,3, register_node/2,3, start_link/0
- `erpc` (kernel, 27): call/2,3,4,5, cast/2,4, check_response/2,3,
  multicall/2,3,4,5, multicast/2,4, receive_response/1,2,3, reqids_add/3,
  reqids_new/0, reqids_size/1, reqids_to_list/1, send_request/2,4,6,
  wait_response/1,2,3
- `error_handler` (kernel, 3): raise_undef_exception/3, undefined_function/3,
  undefined_lambda/3
- `error_logger` (kernel, 20): add_report_handler/1,2, delete_report_handler/1,
  error_msg/1,2, error_report/1,2, format/2, get_format_depth/0, info_msg/1,2,
  info_report/1,2, logfile/1, tty/1, warning_map/0, warning_msg/1,2,
  warning_report/1,2
- `file` (kernel, 30, 32 done): advise/4, allocate/3, change_group/2,
  change_mode/2, change_owner/2,3, change_time/2,3, consult/1, datasync/1,
  eval/1,2, format_error/1, path_consult/2, path_eval/2, path_open/3,
  path_script/2,3, pread/2,3, pwrite/2,3, script/1,2, sendfile/2,5, sync/1,
  truncate/1, write_file_info/2,3
- `gen_sctp` (kernel, 23): abort/2, close/1, connect/3,4,5, connect_init/3,4,5,
  connectx_init/3,4,5, controlling_process/2, eof/2, error_string/1, listen/2,
  open/0,1,2, peeloff/2, recv/1,2, send/3,4
- `gen_tcp` (kernel, 1, 11 done): connect/2
- `gen_udp` (kernel, 5, 7 done): connect/2,3, send/2,3,5
- `global` (kernel, 21): del_lock/1,2, disconnect/0, notify_all_name/3,
  random_exit_name/3, random_notify_name/3, re_register_name/2,3,
  register_name/2,3, registered_names/0, send/2, set_lock/1,2,3, sync/0,
  trans/2,3,4, unregister_name/1, whereis_name/1
- `global_group` (kernel, 10): global_groups/0, info/0, monitor_nodes/1,
  own_nodes/0, registered_names/1, send/2,3, sync/0, whereis_name/1,2
- `heart` (kernel, 8): clear_callback/0, clear_cmd/0, get_callback/0, get_cmd/0,
  get_options/0, set_callback/2, set_cmd/1, set_options/1
- `inet` (kernel, 19, 20 done): cancel_monitor/1, format_error/1, get_rc/0,
  gethostbyaddr/1, getifaddrs/0,1, getopts/2, getstat/1,2, i/0,1,2, info/1,
  ipv4_mapped_ipv6_address/1, monitor/1, peernames/1,2, socknames/1,2
- `inet_res` (kernel, 20): getbyname/2,3,4, gethostbyaddr/1,2,3,
  gethostbyname/1,2,3,4, lookup/3,4,5, nnslookup/4,5, nslookup/3,4,
  resolve/3,4,5
- `logger` (kernel, 68): add_handler/3, add_handler_filter/3, add_handlers/1,
  add_primary_filter/2, alert/1,2,3, compare_levels/2, critical/1,2,3,
  debug/1,2,3, emergency/1,2,3, error/1,2,3, format_report/1, get_config/0,
  get_handler_config/0,1, get_handler_ids/0, get_module_level/0,1,
  get_primary_config/0, get_process_metadata/0, get_proxy_config/0, i/0,1,
  info/1,2,3, log/2,3,4, notice/1,2,3, reconfigure/0, remove_handler/1,
  remove_handler_filter/2, remove_primary_filter/1, set_application_level/2,
  set_handler_config/2,3, set_module_level/2, set_primary_config/1,2,
  set_process_metadata/1, set_proxy_config/1, timestamp/0,
  unset_application_level/1, unset_module_level/0,1, unset_process_metadata/0,
  update_formatter_config/2,3, update_handler_config/2,3,
  update_primary_config/1, update_process_metadata/1, update_proxy_config/1,
  warning/1,2,3
- `logger_disk_log_h` (kernel, 1): filesync/1
- `logger_filters` (kernel, 4): domain/2, level/2, progress/2, remote_gl/2
- `logger_formatter` (kernel, 2; 2 callbacks): check_config/1, format/2
- `logger_std_h` (kernel, 1): filesync/1
- `net` (kernel, 15): getaddrinfo/1,2, gethostname/0, getifaddrs/0,1,2,
  getnameinfo/1,2, getservbyname/1,2, getservbyport/1,2, if_index2name/1,
  if_name2index/1, if_names/0
- `net_adm` (kernel, 10): dns_hostname/1, host_file/0, localhost/0, names/0,1,
  ping/1, world/0,1, world_list/1,2
- `net_kernel` (kernel, 13): allow/1, allowed/0, connect_node/1,
  get_net_ticktime/0, get_state/0, getopts/2, monitor_nodes/1,2,
  set_net_ticktime/1,2, setopts/2, start/2, stop/0; deprecated: start/1
- `os` (kernel, 4, 15 done): cmd/2, find_executable/1,2, set_signal/2
- `pg` (kernel, 30): data_diff/2, demonitor/1,2, get_local_members/1,2,
  get_members/1,2, init_global_view/2, init_local_data/1, join/2,3, leave/2,3,
  monitor/1,2, monitor_scope/0,1, new_subscription/2, start/1, start_link/0,1,
  stop_global_view/1, translate_local_data/3, translate_message/2,
  translate_update/3, update_global_view_and_notify/4, update_local_data/2,
  version/0, which_groups/0,1
- `rpc` (kernel, 25): abcast/2,3, async_call/4, block_call/4,5, call/4,5,
  cast/4, eval_everywhere/3,4, multi_server_call/2,3, multicall/3,4,5,
  nb_yield/1,2, parallel_eval/1, pinfo/1,2, pmap/3, sbcast/2,3, server_call/4,
  yield/1
- `seq_trace` (kernel, 9): get_system_tracer/0, get_token/0,1, print/1,2,
  reset_trace/0, set_system_tracer/1, set_token/1,2
- `socket` (kernel, 79): accept/1,2, bind/2,3, cancel/2, cancel_monitor/1,
  close/1, connect/1,2,3, getopt/2,3, getopt_native/3, i/0,1,2, info/0,1,
  ioctl/2,3,4, is_supported/1,2, listen/1,2, monitor/1, number_of/0,
  open/1,2,3,4, peeloff/2,3, peername/1, peernames/2, recv/1,2,3,4,
  recvfrom/1,2,3,4, recvmmsg/6, recvmsg/1,2,3,4,5, rest_iov/2, send/2,3,4,
  sendfile/2,3,4,5, sendmmsg/4, sendmsg/2,3,4, sendto/3,4,5, sendv/2,3,4,
  setopt/3,4, setopt_native/3, shutdown/2, sockname/1, socknames/2,
  supports/0,1,2, use_registry/1, which_sockets/0,1
- `trace` (kernel, 11): delivered/2, function/4, info/3, port/4, process/4,
  recv/3, send/3, session_create/3, session_destroy/1, session_info/1, system/3
- `wrap_log_reader` (kernel, 5): chunk/1,2, close/1, open/1,2
- `argparse` (stdlib, 6): format_error/1, help/1,2, parse/2,3, run/3
- `array` (stdlib, 49): append/2, concat/1,2, default/1, fix/1, foldl/3,5,
  foldr/3,5, from/2,3, from_list/1,2, from_orddict/1,2, get/2, is_array/1,
  is_fix/1, map/2, mapfoldl/3,5, mapfoldr/3,5, new/0,1,2, prepend/2, relax/1,
  reset/2, resize/1,2, set/3, shift/2, size/1, slice/3, sparse_foldl/3,5,
  sparse_foldr/3,5, sparse_map/2, sparse_mapfoldl/3,5, sparse_mapfoldr/3,5,
  sparse_size/1, sparse_to_list/1, sparse_to_orddict/1, to_list/1, to_orddict/1
- `base64` (stdlib, 12): decode/1,2, decode_to_string/1,2, encode/1,2,
  encode_to_string/1,2, mime_decode/1,2, mime_decode_to_string/1,2
- `beam_lib` (stdlib, 19): all_chunks/1, build_module/1, chunks/2,3,
  clear_crypto_key_fun/0, cmp/2, cmp_dirs/2, crypto_key_fun/1, diff_dirs/2,
  format_error/1, info/1, md5/1, strip/1,2, strip_files/1,2, strip_release/1,2,
  version/1
- `c` (stdlib, 44): bt/1, c/1,2,3, cd/1, erlangrc/1, flush/0, h/1,2,3,
  hcb/1,2,3, help/0, ht/1,2,3, i/0,3, l/1, lc/1, lm/0, ls/0,1, m/0,1,
  memory/0,1, mm/0, nc/1,2, ni/0, nl/1, nregs/0, pi/1,3, pid/3, pwd/0, q/0,
  regs/0, uptime/0, xm/1, y/1,2
- `calendar` (stdlib, 34): date_to_gregorian_days/1,3,
  datetime_to_gregorian_seconds/1, day_of_the_week/1,3,
  gregorian_days_to_date/1, gregorian_seconds_to_datetime/1, is_leap_year/1,
  iso_week_number/0,1, last_day_of_the_month/2, local_time/0,
  local_time_to_system_time/1,2, local_time_to_universal_time_dst/1,
  now_to_datetime/1, now_to_local_time/1, now_to_universal_time/1,
  rfc3339_to_system_time/1,2, seconds_to_daystime/1, seconds_to_time/1,
  system_time_to_local_time/2, system_time_to_rfc3339/1,2,
  system_time_to_universal_time/2, time_difference/2, time_to_seconds/1,
  universal_time/0, universal_time_to_local_time/1,
  universal_time_to_system_time/1,2, valid_date/1,3; deprecated:
  local_time_to_universal_time/1
- `dets` (stdlib, 44): all/0, bchunk/2, close/1, delete/2, delete_all_objects/1,
  delete_object/2, first/1, foldl/3, foldr/3, from_ets/2, info/1,2,
  init_table/2,3, insert/2, insert_new/2, is_compatible_bchunk_format/2,
  is_dets_file/1, lookup/2, match/1,2,3, match_delete/2, match_object/1,2,3,
  member/2, next/2, open_file/1,2, pid2name/1, repair_continuation/2,
  safe_fixtable/2, select/1,2,3, select_delete/2, slot/2, sync/1, table/1,2,
  to_ets/2, traverse/2, update_counter/3
- `dict` (stdlib, 21): append/3, append_list/3, erase/2, fetch/2, fetch_keys/1,
  filter/2, find/2, fold/3, from_list/1, is_empty/1, is_key/2, map/2, merge/3,
  new/0, size/1, store/3, take/2, to_list/1, update/3,4, update_counter/3
- `digraph` (stdlib, 34): add_edge/3,4,5, add_vertex/1,2,3, del_edge/2,
  del_edges/2, del_path/3, del_vertex/2, del_vertices/2, delete/1, edge/2,
  edges/1,2, get_cycle/2, get_path/3, get_short_cycle/2, get_short_path/3,
  in_degree/2, in_edges/2, in_neighbours/2, info/1, new/0,1, no_edges/1,
  no_vertices/1, out_degree/2, out_edges/2, out_neighbours/2, sink_vertices/1,
  source_vertices/1, vertex/2, vertices/1
- `digraph_utils` (stdlib, 18): arborescence_root/1, components/1,
  condensation/1, cyclic_strong_components/1, is_acyclic/1, is_arborescence/1,
  is_tree/1, loop_vertices/1, postorder/1, preorder/1, reachable/2,
  reachable_neighbours/2, reaching/2, reaching_neighbours/2,
  strong_components/1, subgraph/2,3, topsort/1
- `edlin` (stdlib, 1): keymap/0
- `edlin_expand` (stdlib, 2): expand/1,2
- `epp` (stdlib, 18): close/1, default_encoding/0, encoding_to_string/1,
  format_error/1, open/1,2,3, parse_erl_form/1, parse_file/2,3,
  read_encoding/1,2, read_encoding_from_binary/1,2, scan_erl_form/1,
  scan_file/2, set_encoding/1,2
- `erl_anno` (stdlib, 18): column/1, end_location/1, file/1, from_term/1,
  generated/1, is_anno/1, line/1, location/1, new/1, set_end_location/2,
  set_file/2, set_generated/2, set_line/2, set_location/2, set_record/2,
  set_text/2, text/1, to_term/1
- `erl_error` (stdlib, 2; 1 callbacks): format_exception/3,4
- `erl_eval` (stdlib, 15): add_binding/3, binding/2, bindings/1, del_binding/2,
  expr/2,3,4,5, expr_list/2,3,4, exprs/2,3,4, new_bindings/0
- `erl_expand_records` (stdlib, 1): module/2
- `erl_features` (stdlib, 5): all/0, configurable/0, enabled/0, info/1, used/1
- `erl_internal` (stdlib, 10): add_predefined_functions/1, arith_op/2, bif/2,
  bool_op/2, comp_op/2, guard_bif/2, list_op/2, op_type/2, send_op/2,
  type_test/2
- `erl_lint` (stdlib, 5): format_error/1, is_guard_test/1, module/1,2,3
- `erl_parse` (stdlib, 15): abstract/1,2, anno_from_term/1, anno_to_term/1,
  fold_anno/3, format_error/1, map_anno/2, mapfold_anno/3, new_anno/1,
  normalise/1, parse_exprs/1, parse_form/1, parse_term/1, tokens/1,2
- `erl_pp` (stdlib, 16): attribute/1,2, expr/1,2,3,4, exprs/1,2,3, form/1,2,
  function/1,2, guard/1,2, legalize_vars/1
- `erl_scan` (stdlib, 14): category/1, column/1, end_location/1, format_error/1,
  line/1, location/1, reserved_word/1, string/1,2,3, symbol/1, text/1,
  tokens/3,4
- `erl_tar` (stdlib, 14): add/3,4, close/1, create/2,3, extract/1,2,
  format_error/1, init/3, open/2, t/1, table/1,2, tt/1
- `escript` (stdlib, 3): create/2, extract/2, script_name/0
- `ets` (stdlib, 70): all/0, delete/1,2, delete_all_objects/1, delete_object/2,
  file2tab/1,2, first/1, first_lookup/1, foldl/3, foldr/3, from_dets/2,
  fun2ms/1, give_away/3, i/0,1, info/1,2, init_table/2, insert/2, insert_new/2,
  is_compiled_ms/1, last/1, last_lookup/1, lookup/2, lookup_element/3,4,
  match/1,2,3, match_delete/2, match_object/1,2,3, match_spec_compile/1,
  match_spec_run/2, member/2, new/2, next/2, next_lookup/2, prev/2,
  prev_lookup/2, rename/2, repair_continuation/2, safe_fixtable/2, select/1,2,3,
  select_count/2, select_delete/2, select_replace/2, select_reverse/1,2,3,
  setopts/2, slot/2, tab2file/2,3, tab2list/1, tabfile_info/1, table/1,2,
  take/2, test_ms/2, to_dets/2, update_counter/3,4, update_element/3,4,
  whereis/1
- `file_sorter` (stdlib, 14): check/1,2, keycheck/2,3, keymerge/3,4,
  keysort/2,3,4, merge/2,3, sort/1,2,3
- `filelib` (stdlib, 16): ensure_dir/1, ensure_path/1, file_size/1,
  find_file/2,3, find_source/1,2,3, fold_files/5, is_dir/1, is_file/1,
  is_regular/1, last_modified/1, safe_relative_path/2, wildcard/1,2
- `filename` (stdlib, 17): absname/1,2, absname_join/2, basedir/2,3,
  basename/1,2, dirname/1, extension/1, flatten/1, join/1,2, nativename/1,
  pathtype/1, rootname/1,2, split/1
- `gb_sets` (stdlib, 42): add/2, add_element/2, balance/1, del_element/2,
  delete/2, delete_any/2, difference/2, empty/0, filter/2, filtermap/2, fold/3,
  from_list/1, from_ordset/1, insert/2, intersection/1,2, is_disjoint/2,
  is_element/2, is_empty/1, is_equal/2, is_member/2, is_set/1, is_subset/2,
  iterator/1,2, iterator_from/2,3, larger/2, largest/1, map/2, new/0, next/1,
  singleton/1, size/1, smaller/2, smallest/1, subtract/2, take_largest/1,
  take_smallest/1, to_list/1, union/1,2
- `gb_trees` (stdlib, 31): balance/1, delete/2, delete_any/2, empty/0, enter/3,
  from_list/1, from_orddict/1, get/2, insert/3, is_defined/2, is_empty/1,
  iterator/1,2, iterator_from/2,3, keys/1, larger/2, largest/1, lookup/2, map/2,
  next/1, size/1, smaller/2, smallest/1, take/2, take_any/2, take_largest/1,
  take_smallest/1, to_list/1, update/3, values/1
- `gen_event` (stdlib, 33; 8 callbacks): add_handler/3, add_sup_handler/3,
  call/3,4, check_response/2,3, delete_handler/3, notify/2,
  receive_response/2,3, reqids_add/3, reqids_new/0, reqids_size/1,
  reqids_to_list/1, send_request/3,5, start/0,1,2, start_link/0,1,2,
  start_monitor/0,1,2, stop/1,3, swap_handler/3, swap_sup_handler/3,
  sync_notify/2, wait_response/2,3, which_handlers/1
- `gen_fsm` (stdlib, 0; 9 callbacks): none; deprecated: cancel_timer/1,
  enter_loop/4,5,6, reply/2, send_all_state_event/2, send_event/2,
  send_event_after/2, start/3,4, start_link/3,4, start_timer/2, stop/1,3,
  sync_send_all_state_event/2,3, sync_send_event/2,3
- `gen_server` (stdlib, 32; 9 callbacks): abcast/2,3, call/2,3, cast/2,
  check_response/2,3, enter_loop/3,4,5, multi_call/2,3,4, receive_response/2,3,
  reply/2, reqids_add/3, reqids_new/0, reqids_size/1, reqids_to_list/1,
  send_request/2,4, start/3,4, start_link/3,4, start_monitor/3,4, stop/1,3,
  wait_response/2,3
- `gen_statem` (stdlib, 30; 8 callbacks): call/2,3, cast/2, check_response/2,3,
  enter_loop/4,5,6, receive_response/1,2,3, reply/1,2, reqids_add/3,
  reqids_new/0, reqids_size/1, reqids_to_list/1, send_request/2,4, start/3,4,
  start_link/3,4, start_monitor/3,4, stop/1,3, wait_response/1,2,3
- `graph` (stdlib, 63): add_edge/3,4, add_vertex/1,2,3, arborescence_root/1,
  components/1, condensation/1, cyclic_strong_components/1, del_edge/2,
  del_edges/2,3, del_path/3, del_vertex/2, del_vertices/2, edges/1,2,3,
  fold_vertices/3, get_cycle/2, get_path/3, get_short_cycle/2, get_short_path/3,
  has_edge/2,3, has_path/3, has_vertex/2, in_degree/2, in_edges/2,
  in_neighbours/2, info/1, is_acyclic/1, is_arborescence/1, is_tree/1,
  loop_vertices/1, new/0,1, no_edges/1, no_vertices/1, out_degree/2,
  out_edges/2, out_neighbours/2, postorder/1,2, preorder/1,2, reachable/2,
  reachable_via_neighbours/2, reaching/2, reaching_via_neighbours/2,
  reverse_postorder/1,2, roots/1, sink_vertices/1, source_vertices/1,
  strong_components/1, subgraph/2,3, topsort/1, vertex/2,3, vertices/1,
  vertices_with_labels/1
- `io` (stdlib, 34, 14 done): columns/0,1, fread/2,3, get_password/0,
  getopts/0,1, parse_erl_exprs/1,2,3,4, parse_erl_form/1,2,3,4,
  printable_range/0, read/1,2,3,4, rows/0,1, scan_erl_exprs/1,2,3,4,
  scan_erl_form/1,2,3,4, setopts/1,2, write/1,2
- `io_ansi` (stdlib, 153): alternate_character_set_mode/0,
  alternate_character_set_mode_off/0, alternate_screen/0,
  alternate_screen_off/0, background/1,3, black/0, black_background/0,
  black_underline/0, blink/0, blink_off/0, blue/0, blue_background/0,
  blue_underline/0, bold/0, bold_off/0, clear/0, color/1,3,
  color_name_to_index/1, curly_underline/0, cursor/2, cursor_backward/0,1,
  cursor_down/0,1, cursor_forward/0,1, cursor_get_position/0, cursor_hide/0,
  cursor_home/0, cursor_horizontal_absolute/1, cursor_horizontal_vertical/2,
  cursor_move/2, cursor_next_line/0, cursor_previous_line/0,
  cursor_report_position/0, cursor_restore/0, cursor_save/0, cursor_show/0,
  cursor_up/0,1, cursor_vertical_absolute/1, cyan/0, cyan_background/0,
  cyan_underline/0, dashed_underline/0, default_background/0, default_color/0,
  default_underline_color/0, delete_character/0,1, delete_line/0,1,
  device_report_attributes/0, dim/0, dim_off/0, dotted_underline/0,
  double_underline/0, enabled/0,1, erase_character/1, erase_display/0,
  erase_line/0, format/1,2,3, fwrite/1,2,3,4, green/0, green_background/0,
  green_underline/0, hyperlink_reset/0, hyperlink_start/1,2, insert_character/1,
  insert_line/0,1, inverse/0, inverse_off/0, invisible/0, invisible_off/0,
  italic/0, italic_off/0, keypad_transmit_mode/0, keypad_transmit_mode_off/0,
  light_black/0, light_black_background/0, light_black_underline/0,
  light_blue/0, light_blue_background/0, light_blue_underline/0, light_cyan/0,
  light_cyan_background/0, light_cyan_underline/0, light_green/0,
  light_green_background/0, light_green_underline/0, light_magenta/0,
  light_magenta_background/0, light_magenta_underline/0, light_red/0,
  light_red_background/0, light_red_underline/0, light_white/0,
  light_white_background/0, light_white_underline/0, light_yellow/0,
  light_yellow_background/0, light_yellow_underline/0, magenta/0,
  magenta_background/0, magenta_underline/0, modify_color/4, overline/0,
  overline_off/0, red/0, red_background/0, red_underline/0, render/1,2, reset/0,
  reverse_index/0, scan/1, scroll_backward/0,1, scroll_change_region/2,
  scroll_forward/0,1, strikethrough/0, strikethrough_off/0, tab/0,
  tab_backward/0, tab_clear/0, tab_clear_all/0, tab_set/0, tigetflag/1,
  tigetnum/1, tinfo/0, tput/1,2, underline/0, underline_color/1,3,
  underline_off/0, white/0, white_background/0, white_underline/0, yellow/0,
  yellow_background/0, yellow_underline/0
- `io_lib` (stdlib, 36): bformat/2,3, bfwrite/2,3, build_text/1, bwrite/2,
  bwrite_string/3, char_list/1, deep_char_list/1, deep_latin1_char_list/1,
  format/2,3, fread/2,3, fwrite/2,3, indentation/2, latin1_char_list/1, nl/0,
  print/1,4, printable_latin1_list/1, printable_list/1,
  printable_unicode_list/1, scan_format/2, unscan_format/1, write/1,2,
  write_atom/1, write_atom_as_latin1/1, write_char/1, write_char_as_latin1/1,
  write_latin1_char/1, write_latin1_string/1, write_string/1,
  write_string_as_latin1/1
- `json` (stdlib, 23): decode/1,3, decode_continue/2, decode_start/3,
  encode/1,2, encode_atom/2, encode_binary/1, encode_binary_escape_all/1,
  encode_float/1, encode_integer/1, encode_key_value_list/2,
  encode_key_value_list_checked/2, encode_list/2, encode_map/2,
  encode_map_checked/2, encode_value/2, format/1,2,3, format_key_value_list/3,
  format_key_value_list_checked/3, format_value/3
- `log_mf_h` (stdlib, 2): init/3,4
- `ms_transform` (stdlib, 3): format_error/1, parse_transform/2,
  transform_from_shell/3
- `orddict` (stdlib, 21): append/3, append_list/3, erase/2, fetch/2,
  fetch_keys/1, filter/2, find/2, fold/3, from_list/1, is_empty/1, is_key/2,
  map/2, merge/3, new/0, size/1, store/3, take/2, to_list/1, update/3,4,
  update_counter/3
- `ordsets` (stdlib, 21): add_element/2, del_element/2, filter/2, filtermap/2,
  fold/3, from_list/1, intersection/1,2, is_disjoint/2, is_element/2,
  is_empty/1, is_equal/2, is_set/1, is_subset/2, map/2, new/0, size/1,
  subtract/2, to_list/1, union/1,2
- `peer` (stdlib, 11): call/4,5, cast/4, get_state/1, random_name/0,1, send/3,
  start/1, start_link/0,1, stop/1
- `pool` (stdlib, 8): attach/1, get_node/0, get_nodes/0, pspawn/3,
  pspawn_link/3, start/1,2, stop/0
- `proc_lib` (stdlib, 35): format/1,2,3, get_label/1, hibernate/3, init_ack/1,2,
  init_fail/2,3, initial_call/1, set_label/1, spawn/1,2,3,4, spawn_link/1,2,3,4,
  spawn_opt/2,3,4,5, start/3,4,5, start_link/3,4,5, start_monitor/3,4,5,
  stop/1,3, translate_initial_call/1
- `proplists` (stdlib, 22): append_values/2, compact/1, delete/2, expand/2,
  from_map/1, get_all_values/2, get_bool/2, get_keys/1, get_value/2,3,
  is_defined/2, lookup/2, lookup_all/2, normalize/2, property/1,2, split/2,
  substitute_aliases/2, substitute_negations/2, to_map/1,2, unfold/1
- `qlc` (stdlib, 26): append/1,2, cursor/1,2, delete_cursor/1, e/1,2, eval/1,2,
  fold/3,4, format_error/1, info/1,2, keysort/2,3, next_answers/1,2, q/1,2,
  sort/1,2, string_to_handle/1,2,3, table/2
- `queue` (stdlib, 37): all/2, any/2, cons/2, daeh/1, delete/2, delete_r/2,
  delete_with/2, delete_with_r/2, drop/1, drop_r/1, filter/2, filtermap/2,
  fold/3, from_list/1, get/1, get_r/1, head/1, in/2, in_r/2, init/1, is_empty/1,
  is_queue/1, join/2, last/1, len/1, liat/1, member/2, new/0, out/1, out_r/1,
  peek/1, peek_r/1, reverse/1, snoc/2, split/2, tail/1, to_list/1; deprecated:
  lait/1
- `rand` (stdlib, 31): bytes/1, bytes_s/2, export_seed/0, export_seed_s/1,
  exsp_jump/1, exsp_next/1, jump/0,1, mwc59/1, mwc59_float/1, mwc59_seed/0,1,
  mwc59_value/1, mwc59_value32/1, normal/0,2, normal_s/1,3, seed/1,2,
  seed_s/1,2, shuffle/1, shuffle_s/2, splitmix64_next/1, uniform/0,1,
  uniform_real/0, uniform_real_s/1, uniform_s/1,2
- `random` (stdlib, 0): none; deprecated: seed/0,1,3, seed0/0, uniform/0,1,
  uniform_s/1,2
- `re` (stdlib, 1, 10 done): import/1
- `records` (stdlib, 8): create/4, get/2, get_definition/2, get_field_names/1,
  get_module/1, get_name/1, is_exported/1, update/4
- `sets` (stdlib, 23): add_element/2, del_element/2, filter/2, filtermap/2,
  fold/3, from_list/1,2, intersection/1,2, is_disjoint/2, is_element/2,
  is_empty/1, is_equal/2, is_set/1, is_subset/2, map/2, new/0,1, size/1,
  subtract/2, to_list/1, union/1,2
- `shell` (stdlib, 18): catch_exception/1, default_multiline_prompt/1,
  erl_pp_format_func/1, format_shell_func/1, hints/1, history/1,
  inverted_space_prompt/1, multiline_prompt_func/1, prompt_func/1,
  prompt_width/1,2, results/1, start_interactive/0,1, start_restricted/1,
  stop_restricted/0, strings/1, whereis/0
- `shell_default` (stdlib, 1): help/0
- `shell_docs` (stdlib, 15): normalize/1, render/2,3,4,5,
  render_callback/2,3,4,5, render_type/2,3,4,5, supported_tags/0, validate/1
- `slave` (stdlib, 0): none; deprecated: pseudo/1,2, relay/1, start/1,2,3,
  start_link/1,2,3, stop/1
- `sofs` (stdlib, 81): a_function/1,2, canonical_relation/1, composite/2,
  constant_function/2, converse/1, difference/2, digraph_to_family/1,2,
  domain/1, drestriction/2,3, empty_set/0, extension/3, family/1,2,
  family_difference/2, family_domain/1, family_field/1, family_intersection/1,2,
  family_projection/2, family_range/1, family_specification/2,
  family_to_digraph/1,2, family_to_relation/1, family_union/1,2, field/1,
  from_external/2, from_sets/1, from_term/1,2, image/2, intersection/1,2,
  intersection_of_family/1, inverse/1, inverse_image/2, is_a_function/1,
  is_disjoint/2, is_empty_set/1, is_equal/2, is_set/1, is_sofs_set/1,
  is_subset/2, is_type/1, join/4, multiple_relative_product/2, no_elements/1,
  partition/1,2,3, partition_family/2, product/1,2, projection/2, range/1,
  relation/1,2, relation_to_family/1, relative_product/1,2, relative_product1/2,
  restriction/2,3, set/1,2, specification/2, strict_relation/1, substitution/2,
  symdiff/2, symmetric_partition/2, to_external/1, to_sets/1, type/1, union/1,2,
  union_of_family/1, weak_relation/1
- `string` (stdlib, 69): casefold/1, centre/2,3, chars/2,3, chomp/1, chr/2,
  concat/2, copies/2, cspan/2, equal/2,3,4, find/2,3, is_empty/1,
  jaro_similarity/2, join/2, left/2,3, len/1, length/1, lexemes/2, lowercase/1,
  next_codepoint/1, next_grapheme/1, nth_lexeme/3, pad/2,3,4, prefix/2, rchr/2,
  replace/3,4, reverse/1, right/2,3, rstr/2, slice/2,3, span/2, split/2,3,
  str/2, strip/1,2,3, sub_string/2,3, sub_word/2,3, substr/2,3, take/2,3,4,
  titlecase/1, to_float/1, to_graphemes/1, to_integer/1, to_lower/1, to_upper/1,
  tokens/2, trim/1,2,3, uppercase/1, words/1,2
- `supervisor` (stdlib, 14; 1 callbacks): check_childspecs/1,2,
  count_children/1, delete_child/2, get_childspec/2, restart_child/2,
  start_child/2, start_link/2,3, stop/1,3, terminate_child/2, which_child/2,
  which_children/1
- `supervisor_bridge` (stdlib, 2; 2 callbacks): start_link/2,3
- `sys` (stdlib, 33; 5 callbacks): change_code/4,5, debug_options/1, get_log/1,
  get_state/1,2, get_status/1,2, handle_debug/4, handle_system_msg/6,
  install/2,3, log/2,3, log_to_file/2,3, no_debug/1,2, print_log/1, remove/2,3,
  replace_state/2,3, resume/1,2, statistics/2,3, suspend/1,2, terminate/2,3,
  trace/2,3; deprecated: get_debug/3
- `timer` (stdlib, 29): apply_after/2,3,4, apply_interval/2,3,4,
  apply_repeatedly/2,3,4, cancel/1, exit_after/2,3, hms/3, hours/1,
  kill_after/1,2, minutes/1, now_diff/2, seconds/1, send_after/2,3,
  send_interval/2,3, sleep/1, start/0, tc/1,2,3,4
- `unicode` (stdlib, 12, 7 done): category/1, characters_to_nfc_binary/1,
  characters_to_nfc_list/1, characters_to_nfd_binary/1,
  characters_to_nfd_list/1, characters_to_nfkc_binary/1,
  characters_to_nfkc_list/1, characters_to_nfkd_binary/1,
  characters_to_nfkd_list/1, is_id_continue/1, is_id_start/1, is_whitespace/1
- `uri_string` (stdlib, 15): allowed_characters/0, compose_query/1,2,
  dissect_query/1, normalize/1,2, parse/1, percent_decode/1, quote/1,2,
  recompose/1, resolve/2,3, transcode/2, unquote/1
- `win32reg` (stdlib, 13): change_key/2, change_key_create/2, close/1,
  current_key/1, delete_key/1, delete_value/2, expand/1, format_error/1, open/1,
  set_value/3, sub_keys/1, value/2, values/1
- `zip` (stdlib, 22): create/2,3, extract/1,2, foldl/3, list_dir/1,2, t/1,
  table/1,2, tt/1, unzip/1,2, zip/2,3, zip_close/1, zip_get/1,2,
  zip_get_crc32/2, zip_list_dir/1, zip_open/1,2
- `zstd` (stdlib, 17): close/1, compress/1,2, context/1,2, decompress/1,2,
  dict/2,3, finish/2, flush/1, get_dict_id/1, get_frame_header/1,
  get_parameter/2, reset/1, set_parameter/3, stream/2

## Appendix B: luerl library tracing (from the luerl work, still usable)

luerl was the reference real-world program for library coverage. All its gaps
are fixed (plan 11 steps 41T–41ZC in `00-finished.md`); its remaining speed work
is in plan 20. The method is still useful for finding which undocumented
functions OTP-sourced modules reach:

- `CLAUSE_CALL_TRACE=FILE` (`runtime/src/process/call_trace.{hpp,cpp}`) logs
  boundary calls with arguments and `RET`/`EXC`. `CLAUSE_CALL_TRACE_FIRST=1`
  adds the first entry of each function, and `CLAUSE_CALL_TRACE_ALL=1` logs
  every entry.
- `tools/luerl-trace/ctrace.erl` is the OTP twin, which prints by the same
  rules. `norm.py`, `mismatch.py` and `firsts.py` diff the traces and list stubs
  reached.
- Stand-ins live only in a scratch copy of `library/`: OTP code must not enter
  the repository.
