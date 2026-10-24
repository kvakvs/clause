# Plan 21: Clause's own erts, kernel and stdlib

The general plan for the missing standard library. Goal: every public module that OTP 29 documents
for `erts`, `kernel` and `stdlib` has a Clause implementation of its documented functions, so
programs need no OTP sources on `source_search_paths`. Native C++ runtime builtins are preferred;
Erlang is used where a function takes funs, runs a process protocol or is glue. Undocumented and
internal functions stay out unless a public function needs them (user direction 2026-10-17).

IMPORTANT: Prefer compatibility with Erlang/OTP at all times, if something can be faster at the cost
of compatibility, add that as a future task and we will create a new API that's for the end user to
choose

- When a step is done, remove it from this file and add a 1-2 line bullet with its commit hash and
  headline speedup to `00-finished.md` (Plan 21).R

## Discovery (2026-10-17, counts refreshed 2026-10-18)

- **Source of truth:** the doc chunks of the installed OTP 29.1.1 (`code:get_doc/1`). A module
  counts when its module doc is not `hidden`, a function when its doc is not `hidden` and it is not
  deprecated. The OTP checkout (pin `d199a49a01`) gave line counts and NIF use.
  `tests/compiler/library/observe_docs.escript` records the documented functions in
  `library/documented.terms`; `library_modules.py coverage .` prints the progress per module.
- **Result:** 120 documented modules (erts 9, kernel 37, stdlib 74) with 2,888 documented functions;
  890 are still to do (erts 23, kernel 243, stdlib 624). Appendix A lists them per module.
- **Complete already** (every documented function, or the rest declined): `atomics`, `counters`,
  `erl_prim_loader`, `persistent_term`, `prim_eval`, `zlib`, `binary`, `lists`, `maps`, `math`, `re`
  (`import/1` declined), `init` (`boot/1`, `restart/0,1` refused), `array`, `base64`, `calendar`,
  `dict`, `digraph`, `digraph_utils`, `erl_anno`, `erl_features`, `erl_internal`, `filename`,
  `gb_sets`, `gb_trees`, `graph`, `io_lib`, `json`, `ms_transform`, `erl_scan`, `erl_parse`,
  `orddict`, `ordsets`, `proplists`, `queue`, `rand`, `records`, `sets`, `sofs`, `string`,
  `timer`, `unicode`, `uri_string`, `erl_eval`, `erl_error`, `erl_pp`, `erl_expand_records`, `proc_lib`, `sys`, `gen_server`, `gen_statem`, `gen_event`,
  `gen_fsm` (deprecated), `supervisor`, `supervisor_bridge`, `logger`, `logger_formatter`,
  `logger_std_h`, `logger_filters`, `error_logger`, `log_mf_h`, `error_handler`, `application`, `pg`, `data_publisher`, `global`, `global_group`, `rpc`, `erpc`,
  `net_adm`, `net_kernel` (distribution start declined), `auth`, `seq_trace` (tokens without tracing).
- **Partial:** `erlang` (22 left, all unavailable: code loading, distribution, tracing), `code`
  (51), `io` (11), `file` (18), `inet` (19), `gen_udp` (5), `os` (4), `gen_tcp` (1), `ets` (2,
  LIB73), `dets` (2, LIB73).
- **Missing:** 35 modules (`library_modules.py coverage`). Today a program gets the plain-Erlang
  ones (`filelib`, `disk_log`, ...) only by compiling OTP's sources from a search path.
- **Callback-only modules** (no functions to implement, only behaviours to honour): `erl_tracer`
  (`logger_handler` is provided).

## Working rules for plan 21 steps

- Commit each step when its gate is green: subject `[plan21] LIBn. Title`. Finished steps move to
  `00-finished.md` (Plan 21) with their hash.
- The plan 11 gate applies: fast CTest, `check-quality` (clang-tidy and Lizard, no suppressions),
  clang-format, erlfmt for `.erl`/`.terms`, docs (`docs/library.md` row, `docs/builtins.md` section,
  `docs/differences.md`, `.agents/arch.md`, `.agents/files.md`).
- **One golden per module family**, generated once from OTP with
  `tests/compiler/executables/regenerate.py`. Print maps and other unordered results as sorted
  lists. Wrap bad arguments in `id/1` so OTP's compiler does not warn about them or drop the code
  after them (as with `maps:map(F, none)` in 29.1.1).
- **C++ first:** data functions are typed or raw builtins in `runtime/src/builtins/<module>/`. They
  are listed in the `abi/include/clause/abi/builtins.hpp` catalog and have
  `clause_builtin_placeholder` bodies in the `.erl` file. Functions taking funs, process loops and
  behaviours are Erlang, written from the documentation in Clause's own shape over native helpers
  (as `maps:merge_with/3` is over `merge/2`). Never copy OTP sources.
- **Representations OTP documents or programs observe stay OTP's:** `queue`'s `{Rear, Front}`,
  `gb_trees`' `{Size, Tree}`, `array` and `sets` records, `digraph`'s tuple. `=:=`, printing and
  `term_to_binary` must agree with OTP. Where a representation is opaque and changing it pays off,
  record it in `differences.md`.
- **A module enters `library/` only when all its documented functions work** (or are declined with a
  reason in its `.terms`). A library module shadows the OTP source of the same name on the search
  path, so a half-done module would break programs that build today. Related modules land together
  (the `proc_lib`/`gen_*`/`sys`/`supervisor` family) so OTP-sourced and Clause-owned modules are not
  mixed inside one protocol.
- **Local-node semantics:** a Clause program is a non-distributed node (`nonode@nohost`). Modules
  that work on such a node in OTP (`global`, `pg`, `rpc`, `erpc`, `net_adm`, `net_kernel`'s queries)
  behave as OTP does there. Remote nodes get OTP's non-distributed answers (`{badrpc, nodedown}`,
  `noconnection`, `pang`).
- **Declined functions** raise or return what OTP gives where the feature is unavailable
  (`{error, enotsup}`, `{error, not_distributed}`, ...). Each one is recorded in `.terms` as
  `{declined, Reason}`, and `modules.terms` records the declined modules.
- **Not implemented placeholder**: Such functions are declared but return
  erlang:error({not_implemented, <function name>, <arity>}).
- **Implemented but in C++**: Erlang counterpart function declaration is created to keep exports
  table consistent, and it always returns `clause_builtin_placeholder` atom.

## Choosing C++ or Erlang

Whenever a C++ implementation is viable, prefer it (user direction 2026-10-17): calls between Erlang
and builtins cost nothing notable, and C++ is much faster. Erlang only where a function calls funs,
runs a process protocol or is glue. C++ reproduces OTP's exact result shapes for every input form
(deep chardata, embedded binaries, io_lib's nested char lists), not just equal content.

| Kind | Implement in | Examples |
| --- | --- | --- |
| Pure data transforms, no funs | C++ builtin | `ordsets:union/2`, `proplists:get_value/3`, `string:split/3`, `base64:encode/1`, `calendar:*`, `json:decode/1` |
| Tables, shared state, OS access | C++ runtime object plus builtins | ETS, `file:pread/3`, `os:find_executable/1`, `net:getaddrinfo/1`, `win32reg` |
| Takes funs | Erlang over C++ helpers | `ordsets:filter/2`, `gb_trees:map/2`, `json:decode/3`, `queue:filter/2`, `ets:foldl/3` |
| Walks whose result shapes and quirks are the algorithm (OS rules, chardata continuations) | C++, own structure, same algorithm and result shapes | `filename:*`, `uri_string:*`, `string:*` |
| Processes, protocols, behaviours | Erlang | `proc_lib`, `gen_server`, `supervisor`, `logger`, `timer:apply_interval/4`, `application` |
| Front-end terms (tokens, forms) | C++ (the runtime's own scanner and parser) | `erl_scan:string/1`, `erl_parse:parse_form/1`, `epp:parse_file/2` |
| Interpreters over forms | Erlang | `erl_eval`, `ms_transform`, `qlc` |

## Tier A: data modules without processes

LIB0–LIB11, LIB4F, LIB10F, LIB12A, LIB12B, LIB53 and LIB72 are done (`00-finished.md`); their open
remainders follow.

### LIB9 remainder: declined `erlang` functions

- D01: `load_module/2`, `load_nif/2`, `purge_module/1`, `check_*code`.
- Distribution: `dist_ctrl_*`.
- Tracing: `trace*`, `system_monitor/0,1,2`, `system_profile/0,2`. LIB71 revisits these.

## Tier B: ETS and what needs it

- `ets` and `dets` ship `table/1,2` as stubs until `qlc` (`PARTIAL` in `library_modules.py`).
- Later: `select_delete/2` and `select_replace/2` over a whole table in portions (they run in one
  piece, each part under its lock).

### LIB12C remainder: `ets:table/1,2`

- `table/1,2` after `qlc` (LIB73), with `dets:table/1,2`.

## Tier C: processes and behaviours (Erlang)

Done: LIB20–LIB25 (`00-finished.md`); `logger_disk_log_h` waits for LIB52.

## Tier D: Erlang front end as library modules

LIB32 `erl_eval` is done (`00-finished.md`).

LIB33 (readers built on the front end) is done (`00-finished.md`).

### LIB34. Source tools

- `epp` (18): C++ over Clause's preprocessor, giving forms and encodings.
- `erl_pp` is done (`00-finished.md`).
- `erl_expand_records` is done (`00-finished.md`); `clause_erl_lint:is_guard_test/3` serves its filters.
- `erl_error` is done (`00-finished.md`).
- `erl_lint` (5): `is_guard_test/1` and `format_error/1` first; `module/1,2,3` later, from Clause's
  semantic checks mapped to OTP's warning terms. `clause_erl_lint` (LIB32) already has
  `is_guard_test/1`, `is_guard_expr/1`, `used_vars/2` and the first-error expression checks of
  `exprs_opt/3`; `erl_lint` delegates to it and widens the checks (then drop the `erl_eval` row of
  `differences.md`).

LIB34A (qlc chain) takes from this step what `qlc` and `qlc_pt` need: `erl_pp` whole (16),
`erl_expand_records:module/2`, `erl_lint:is_guard_test/1`, `format_error/1` and the `module/2`
errors a query expression can raise (unbound and unsafe variables, illegal guards, undefined
records), with OTP's error terms.

## Tier E: files, archives and logs

### LIB50. `file` remainder (C++ driver operations)

- `pread/2,3`, `pwrite/2,3`, `truncate/1`, `advise/4`, `allocate/3`,
  `write_file_info/2,3`.
- `change_mode/2`, `change_owner/2,3`, `change_group/2`, `change_time/2,3`, `sendfile/2,5`,
  `format_error/1` (with the `{Line, file, ...}` terms of `eval/1,2` and `script/1,2`).

### LIB51. `filelib`

- C++: `wildcard/1,2`, glob matching over directory listings, and `safe_relative_path/2`.
- Erlang: `ensure_dir/1`, `ensure_path/1`, `fold_files/5`, `is_dir/1`, `is_file/1`, `is_regular/1`,
  `file_size/1`, `last_modified/1`, `find_file/2,3`, `find_source/1,2,3`.

### LIB52. `disk_log` and `wrap_log_reader`

- Erlang processes over `file`, with C++ for OTP's halt and wrap log chunk formats, so log files are
  exchangeable with OTP.
- Unblocks the `ets` file functions and `logger_disk_log_h`: a `logger` handler like `logger_std_h`
  (same handler process, `filesync/1`, OTP's config checks) writing through `disk_log`.

### LIB54. Archives: `zip`, `erl_tar`, `zstd`, `file_sorter`, `beam_lib`

- `zip` (22) and `erl_tar` (14): C++ readers and writers over the pinned zlib, with an Erlang API
  (`foldl/3`, options).
- `zstd` (17): C++ over a pinned zstd under `thirdparty/`, like zlib.
- `file_sorter` (14): a C++ external merge sort; Erlang for user order funs. Split out as LIB54A,
  first step of the qlc chain: all 14 functions, OTP's `binary_term` (4-byte size prefix plus
  `term_to_binary`) and `term` file formats, input and output funs, `tmpdir`, `compressed`, `size`,
  `no_files`, `order` (with funs), `unique`, `header`, and OTP's error tuples.
- `beam_lib` (19): a C++ chunk reader and writer for `.beam` files (Clause makes none, but programs
  inspect them).

### LIB55. Platform modules

- `win32reg` (13): C++ over the Windows registry; `{error, enotsup}` elsewhere, as OTP does.
- `escript` (3): `script_name/0` is the executable path; `create/2` and `extract/2` over `zip`.

## Tier F: kernel I/O remainder

### LIB60. Sockets and names

- `inet` (19): `getopts/2`, `getstat/1,2`, `getifaddrs/0,1`, `gethostbyaddr/1`, `monitor/1`,
  `cancel_monitor/1`, `info/1`, `i/0,1,2`, `peernames`, `socknames`, `get_rc/0`, `format_error/1`.
- `gen_udp` `connect/2,3` and `send/2,3,5`; `gen_tcp` `connect/2`.
- `net` (15): C++ (`getaddrinfo`, `getnameinfo`, `gethostname`, `getifaddrs`, interface names,
  `getservbyname`, `getservbyport`).
- `inet_res` (20): an Erlang DNS client over `gen_udp`, with a C++ DNS message codec.

### LIB61. `os` and `code` remainder

- `os` (C++): `cmd/2`, `find_executable/1,2`, `set_signal/2`.
- `code` path functions (Erlang over `persistent_term` and the executable layout): `get_path/0`,
  `set_path/1,2`, `add_path*`, `del_path*`, `replace_path/2,3`, `lib_dir/0,1`, `priv_dir/1`,
  `root_dir/0`, `compiler_dir/0`, `where_is_file/1`, `objfile_extension/0`, `clash/0`,
  `all_available/0`, `get_mode/0`, `clear_cache/0`.
- Loading and coverage answer as for a static program (D01): `load_*`, `purge/1`, `soft_purge/1`,
  `delete/1`, `atomic_load/1`, the `prepare_loading`/`finish_loading` pair, `get_object_code/1`,
  `modified_modules/0`, the coverage functions. `get_doc/1` gives `{error, missing}` unless docs are
  embedded later.

### LIB62. `socket` and `gen_sctp`

- `socket` (79): a C++ socket object over the existing socket driver (`open`, `bind`, `listen`,
  `accept`, `connect`, `send*`, `recv*`, options, `select`/`completion` handles, monitors). This
  reverses the "missing" decision in `modules.terms`.
- `gen_sctp` (23): `{error, eprotonosupport}` where SCTP is absent, as in OTP; SCTP support is
  optional and later.

### LIB63. `io` remainder and terminal output

- `io`: `write/1,2`, `columns/0,1`, `rows/0,1`, `getopts/0,1`, `setopts/1,2`, `printable_range/0`,
  `get_password/0`. The read and parse family is done (LIB33).
- `io_ansi` (153, new in OTP 29): C++ escape-sequence builders and `enabled/0,1` terminal detection;
  `format/1,2,3` and `fwrite/1,2,3,4` over `io_lib`.

## Tier G: tools, transforms and the interactive shell

### LIB70. `argparse`

- `argparse` (6): Erlang (command maps and handler funs), with C++ for splitting and converting
  arguments.

### LIB71. Tracing family

- `trace` (11), `seq_trace` tracer output, `erl_tracer` callbacks and the `erlang:trace*` builtins.
- Needs runtime trace points (sends, receives, calls, process events).
- Selected or declined as one decision. The call tracer of plan 11 is the starting point.

> > > > 

### LIB73. `qlc`

Dependencies first (user direction 2026-10-19), in this order: LIB30 `erl_scan` (done), LIB31
`erl_parse` (done), LIB54A `file_sorter`, LIB32 `erl_eval` (done), LIB34A (`erl_pp`, `erl_expand_records`,
`erl_lint`'s checks for `qlc_pt`). See [the qlc chain](#qlc-chain-user-direction-2026-10-19).

- `qlc` (26): Erlang query handles and evaluation over `ets`, `dets` and lists. Compiled queries
  come from the host's `qlc_pt` (`qlc.hrl` names `{parse_transform, qlc}`, D03), so Clause's
  `qlc:q/1,2` interprets OTP's generated `{qlc_lc, Fun, Opt}` / `{qlc_v1, QFun, CodeF, Qdata, QOpt}`
  (or `{simple_v1, ...}`) handles exactly: generator and filter numbering, the `QOpt` fun's `size`,
  `template`, `constants`, `equal_constants`, `n_leading_constant_columns`, `constant_columns`,
  `match_specs` and join answers, and `CodeF`'s `term_to_binary` abstract code for `info/1,2`.
- Lookup and match-spec use of `table/2` callbacks, merge and lookup joins, `cache` (`ets`, `list`),
  `unique`, `sort`/`keysort` through `file_sorter` above `max_list_size` with `tmpdir` and
  `tmpdir_usage` (its message levels go through `error_logger`),
  cursors as processes, OTP's error terms and `format_error/1`.
- `info/1,2`: OTP's abstract code printed by `erl_pp:expr/3`, table descriptions parsed with
  `erl_scan` and `erl_eval:extended_parse_exprs/1`, `erl_eval:fun_data/1` for interpreted funs.
- `string_to_handle/1,2,3`: `erl_scan:string/3`, `erl_eval` and a Clause `qlc_pt` (internal module,
  not copied) that does at run time what the host transform does at compile time, with `erl_lint`'s
  errors for bad queries.
- `ets:table/1,2` and `dets:table/1,2` (LIB12C) land with it.
- `erl_eval`'s clause for `qlc:q/1,2` calls in evaluated code (`qlc:transform_from_evaluator/2`, the
  local calls hidden from the transform), with the old `''.qlc:q` spelling.

### LIB74. Interactive modules (last; optional)

- `shell` (18), `shell_default` (1), `shell_docs` (15), `edlin` (1), `edlin_expand` (2), `c` (44).
- The process-inspection subset of `c` is useful without a shell: `pid/3`, `flush/0`, `i/0,3`,
  `regs/0`, `nregs/0`, `memory/0,1`, `uptime/0`, `bt/1`, `pwd/0`, `cd/1`, `ls/0,1`, `q/0`.
- The compiler and loading parts (`c/1,2,3`, `l/1`, `lm/0`, `nc/1,2`) are declined (D01).

### Declined modules (recorded in `modules.terms`)

| Module | Reason | OTP's answer on a non-distributed or unsupported node |
| --- | --- | --- |
| `erl_boot_server` (7), `erl_epmd` (8) | Distribution boot and port mapping | Start fails as without epmd |
| `peer` (11), `slave` (deprecated), `pool` (8) | Start other nodes | `{error, not_alive}` or the matching error |
| `heart` (8) | Needs OTP's heart port program | `heart` not started |
| `erl_ddll` (15) | No dynamic drivers (D01) | `loaded_drivers/0` gives `{ok, []}`, loads give `{error, ...}` |
| `erl_debugger` (13) | No debugger interface | `supported/0` gives `false` |

These still get small modules answering as OTP does where the feature is missing, so programs that
probe for them keep working.

## Suggested order

### qlc chain (user direction 2026-10-19)

Runs now, before the list below: LIB30 `erl_scan` (done), LIB31 `erl_parse` (done), LIB54A
`file_sorter` (its `term` format reads with `io:read/2`, done in LIB33),
LIB32 `erl_eval` (done, with every undocumented export), LIB34A, then LIB73 `qlc` with LIB12C. Each is its own commit. The
runtime cannot link the compiler's `clause_frontend` (a separate, LLVM-free build; the compiler
lexer throws Clause diagnostics and has no `erl_scan` continuations), so `erl_scan` and `erl_parse`
get their own C++ scanner and parser in `runtime/src/builtins/front_end/`, written to OTP's
documented behavior and grammar. For `erl_parse` the user chose this over reusing the compiler's
parser with the parse-transform export (2026-10-19): OTP's error tokens and messages, the tokens'
own annotations, and no Boost in the runtime.

### General order

Ranked by what real programs need first and by dependencies:

1. LIB50, LIB60 and LIB61 (`file`, `inet`, `os`, `code` gaps).
2. LIB30–LIB34 (the front end, `consult`, `io:read`).
3. LIB51–LIB55 (files, logs, archives), then LIB12C.
4. LIB70, LIB62 and LIB63.
5. LIB71–LIB74, then the declined modules' answer stubs.

## Appendix A: documented functions still to do (OTP 29.1.1 doc chunks)

Format: `module` (application, functions to do, ...): name/arities. Deprecated functions are listed
separately and are optional.

- `erlang` (erts, 20 left, all declined or LIB71): check_old_code/1, check_process_code/2,3,
  dist_ctrl_get_data/1, dist_ctrl_get_data_notification/1, dist_ctrl_get_opt/2,
  dist_ctrl_input_handler/2, dist_ctrl_put_data/2, dist_ctrl_set_opt/3, load_module/2, load_nif/2,
  purge_module/1, system_monitor/0,1,2, system_profile/0,2, trace/3, trace_delivered/1,
  trace_info/2, trace_pattern/2,3
- `init` (erts, 3, 8 done): boot/1, restart/0,1
- `code` (kernel, 51, 8 done): add_path/1,2, add_patha/1,2, add_paths/1,2, add_pathsa/1,2,
  add_pathsz/1,2, add_pathz/1,2, all_available/0, atomic_load/1, clash/0, clear_cache/0,
  compiler_dir/0, coverage_support/0, del_path/1, del_paths/1, delete/1, finish_loading/1,
  get_coverage/2, get_coverage_mode/0,1, get_debug_info/1, get_doc/1, get_mode/0, get_object_code/1,
  get_path/0, lib_dir/0,1, load_abs/1, load_binary/3, load_file/1, modified_modules/0,
  objfile_extension/0, prepare_loading/1, priv_dir/1, purge/1, replace_path/2,3, reset_coverage/1,
  root_dir/0, set_coverage_mode/1, set_path/1,2, soft_purge/1, stick_dir/1, unstick_dir/1,
  where_is_file/1; deprecated: lib_dir/2
- `disk_log` (kernel, 34): all/0, alog/2, alog_terms/2, balog/2, balog_terms/2, bchunk/2,3,
  block/1,2, blog/2, blog_terms/2, breopen/3, btruncate/2, change_header/2, change_notify/3,
  change_size/2, chunk/2,3, chunk_info/1, chunk_step/3, close/1, format_error/1, info/1, log/2,
  log_terms/2, next_file/1, open/1, pid2name/1, reopen/2,3, sync/1, truncate/1,2, unblock/1;
  deprecated: inc_wrap_file/1
- `erl_boot_server` (kernel, 7): add_slave/1, delete_slave/1, start/1,2, start_link/1,2,
  which_slaves/0
- `erl_ddll` (kernel, 15): demonitor/1, format_error/1, info/0,1,2, load/2, load_driver/2,
  loaded_drivers/0, monitor/2, reload/2, reload_driver/2, try_load/3, try_unload/2, unload/1,
  unload_driver/1
- `erl_debugger` (kernel, 13): breakpoint/3, breakpoints/1,3, instrumentations/0,
  peek_stack_frame_slot/4, peek_xreg/3, register/1, stack_frames/2, supported/0,
  toggle_instrumentations/1, unregister/2, whereis/0, xregs_count/1
- `erl_epmd` (kernel, 8): address_please/3, listen_port_please/2, names/1, port_please/2,3,
  register_node/2,3, start_link/0
- `file` (kernel, 18, 44 done): advise/4, allocate/3, change_group/2, change_mode/2,
  change_owner/2,3, change_time/2,3, format_error/1, pread/2,3, pwrite/2,3, sendfile/2,5,
  truncate/1, write_file_info/2,3
- `gen_sctp` (kernel, 23): abort/2, close/1, connect/3,4,5, connect_init/3,4,5, connectx_init/3,4,5,
  controlling_process/2, eof/2, error_string/1, listen/2, open/0,1,2, peeloff/2, recv/1,2, send/3,4
- `gen_tcp` (kernel, 1, 11 done): connect/2
- `gen_udp` (kernel, 5, 7 done): connect/2,3, send/2,3,5
- `heart` (kernel, 8): clear_callback/0, clear_cmd/0, get_callback/0, get_cmd/0, get_options/0,
  set_callback/2, set_cmd/1, set_options/1
- `inet` (kernel, 19, 20 done): cancel_monitor/1, format_error/1, get_rc/0, gethostbyaddr/1,
  getifaddrs/0,1, getopts/2, getstat/1,2, i/0,1,2, info/1, ipv4_mapped_ipv6_address/1, monitor/1,
  peernames/1,2, socknames/1,2
- `inet_res` (kernel, 20): getbyname/2,3,4, gethostbyaddr/1,2,3, gethostbyname/1,2,3,4,
  lookup/3,4,5, nnslookup/4,5, nslookup/3,4, resolve/3,4,5
- `logger_disk_log_h` (kernel, 1): filesync/1
- `net` (kernel, 15): getaddrinfo/1,2, gethostname/0, getifaddrs/0,1,2, getnameinfo/1,2,
  getservbyname/1,2, getservbyport/1,2, if_index2name/1, if_name2index/1, if_names/0
- `os` (kernel, 4, 15 done): cmd/2, find_executable/1,2, set_signal/2
- `socket` (kernel, 79): accept/1,2, bind/2,3, cancel/2, cancel_monitor/1, close/1, connect/1,2,3,
  getopt/2,3, getopt_native/3, i/0,1,2, info/0,1, ioctl/2,3,4, is_supported/1,2, listen/1,2,
  monitor/1, number_of/0, open/1,2,3,4, peeloff/2,3, peername/1, peernames/2, recv/1,2,3,4,
  recvfrom/1,2,3,4, recvmmsg/6, recvmsg/1,2,3,4,5, rest_iov/2, send/2,3,4, sendfile/2,3,4,5,
  sendmmsg/4, sendmsg/2,3,4, sendto/3,4,5, sendv/2,3,4, setopt/3,4, setopt_native/3, shutdown/2,
  sockname/1, socknames/2, supports/0,1,2, use_registry/1, which_sockets/0,1
- `trace` (kernel, 11): delivered/2, function/4, info/3, port/4, process/4, recv/3, send/3,
  session_create/3, session_destroy/1, session_info/1, system/3
- `wrap_log_reader` (kernel, 5): chunk/1,2, close/1, open/1,2
- `argparse` (stdlib, 6): format_error/1, help/1,2, parse/2,3, run/3
- `beam_lib` (stdlib, 19): all_chunks/1, build_module/1, chunks/2,3, clear_crypto_key_fun/0, cmp/2,
  cmp_dirs/2, crypto_key_fun/1, diff_dirs/2, format_error/1, info/1, md5/1, strip/1,2,
  strip_files/1,2, strip_release/1,2, version/1
- `c` (stdlib, 44): bt/1, c/1,2,3, cd/1, erlangrc/1, flush/0, h/1,2,3, hcb/1,2,3, help/0, ht/1,2,3,
  i/0,3, l/1, lc/1, lm/0, ls/0,1, m/0,1, memory/0,1, mm/0, nc/1,2, ni/0, nl/1, nregs/0, pi/1,3,
  pid/3, pwd/0, q/0, regs/0, uptime/0, xm/1, y/1,2
- `dets` (stdlib, 2, 42 done): table/1,2
- `edlin` (stdlib, 1): keymap/0
- `edlin_expand` (stdlib, 2): expand/1,2
- `epp` (stdlib, 18): close/1, default_encoding/0, encoding_to_string/1, format_error/1, open/1,2,3,
  parse_erl_form/1, parse_file/2,3, read_encoding/1,2, read_encoding_from_binary/1,2,
  scan_erl_form/1, scan_file/2, set_encoding/1,2
- `erl_lint` (stdlib, 5): format_error/1, is_guard_test/1, module/1,2,3
- `erl_tar` (stdlib, 14): add/3,4, close/1, create/2,3, extract/1,2, format_error/1, init/3, open/2,
  t/1, table/1,2, tt/1
- `escript` (stdlib, 3): create/2, extract/2, script_name/0
- `ets` (stdlib, 2, 68 done): table/1,2
- `file_sorter` (stdlib, 14): check/1,2, keycheck/2,3, keymerge/3,4, keysort/2,3,4, merge/2,3,
  sort/1,2,3
- `filelib` (stdlib, 16): ensure_dir/1, ensure_path/1, file_size/1, find_file/2,3,
  find_source/1,2,3, fold_files/5, is_dir/1, is_file/1, is_regular/1, last_modified/1,
  safe_relative_path/2, wildcard/1,2
- `io` (stdlib, 11, 37 done): columns/0,1, get_password/0, getopts/0,1, printable_range/0, rows/0,1,
  setopts/1,2, write/1,2
- `io_ansi` (stdlib, 153): alternate_character_set_mode/0, alternate_character_set_mode_off/0,
  alternate_screen/0, alternate_screen_off/0, background/1,3, black/0, black_background/0,
  black_underline/0, blink/0, blink_off/0, blue/0, blue_background/0, blue_underline/0, bold/0,
  bold_off/0, clear/0, color/1,3, color_name_to_index/1, curly_underline/0, cursor/2,
  cursor_backward/0,1, cursor_down/0,1, cursor_forward/0,1, cursor_get_position/0, cursor_hide/0,
  cursor_home/0, cursor_horizontal_absolute/1, cursor_horizontal_vertical/2, cursor_move/2,
  cursor_next_line/0, cursor_previous_line/0, cursor_report_position/0, cursor_restore/0,
  cursor_save/0, cursor_show/0, cursor_up/0,1, cursor_vertical_absolute/1, cyan/0,
  cyan_background/0, cyan_underline/0, dashed_underline/0, default_background/0, default_color/0,
  default_underline_color/0, delete_character/0,1, delete_line/0,1, device_report_attributes/0,
  dim/0, dim_off/0, dotted_underline/0, double_underline/0, enabled/0,1, erase_character/1,
  erase_display/0, erase_line/0, format/1,2,3, fwrite/1,2,3,4, green/0, green_background/0,
  green_underline/0, hyperlink_reset/0, hyperlink_start/1,2, insert_character/1, insert_line/0,1,
  inverse/0, inverse_off/0, invisible/0, invisible_off/0, italic/0, italic_off/0,
  keypad_transmit_mode/0, keypad_transmit_mode_off/0, light_black/0, light_black_background/0,
  light_black_underline/0, light_blue/0, light_blue_background/0, light_blue_underline/0,
  light_cyan/0, light_cyan_background/0, light_cyan_underline/0, light_green/0,
  light_green_background/0, light_green_underline/0, light_magenta/0, light_magenta_background/0,
  light_magenta_underline/0, light_red/0, light_red_background/0, light_red_underline/0,
  light_white/0, light_white_background/0, light_white_underline/0, light_yellow/0,
  light_yellow_background/0, light_yellow_underline/0, magenta/0, magenta_background/0,
  magenta_underline/0, modify_color/4, overline/0, overline_off/0, red/0, red_background/0,
  red_underline/0, render/1,2, reset/0, reverse_index/0, scan/1, scroll_backward/0,1,
  scroll_change_region/2, scroll_forward/0,1, strikethrough/0, strikethrough_off/0, tab/0,
  tab_backward/0, tab_clear/0, tab_clear_all/0, tab_set/0, tigetflag/1, tigetnum/1, tinfo/0,
  tput/1,2, underline/0, underline_color/1,3, underline_off/0, white/0, white_background/0,
  white_underline/0, yellow/0, yellow_background/0, yellow_underline/0
- `peer` (stdlib, 11): call/4,5, cast/4, get_state/1, random_name/0,1, send/3, start/1,
  start_link/0,1, stop/1
- `pool` (stdlib, 8): attach/1, get_node/0, get_nodes/0, pspawn/3, pspawn_link/3, start/1,2, stop/0
- `qlc` (stdlib, 26): append/1,2, cursor/1,2, delete_cursor/1, e/1,2, eval/1,2, fold/3,4,
  format_error/1, info/1,2, keysort/2,3, next_answers/1,2, q/1,2, sort/1,2, string_to_handle/1,2,3,
  table/2

- `re` (stdlib, 1, 10 done): import/1
- `shell` (stdlib, 18): catch_exception/1, default_multiline_prompt/1, erl_pp_format_func/1,
  format_shell_func/1, hints/1, history/1, inverted_space_prompt/1, multiline_prompt_func/1,
  prompt_func/1, prompt_width/1,2, results/1, start_interactive/0,1, start_restricted/1,
  stop_restricted/0, strings/1, whereis/0
- `shell_default` (stdlib, 1): help/0
- `shell_docs` (stdlib, 15): normalize/1, render/2,3,4,5, render_callback/2,3,4,5,
  render_type/2,3,4,5, supported_tags/0, validate/1
- `slave` (stdlib, 0): none; deprecated: pseudo/1,2, relay/1, start/1,2,3, start_link/1,2,3, stop/1
- `unicode` (stdlib, 12, 7 done): category/1, characters_to_nfc_binary/1, characters_to_nfc_list/1,
  characters_to_nfd_binary/1, characters_to_nfd_list/1, characters_to_nfkc_binary/1,
  characters_to_nfkc_list/1, characters_to_nfkd_binary/1, characters_to_nfkd_list/1,
  is_id_continue/1, is_id_start/1, is_whitespace/1
- `win32reg` (stdlib, 13): change_key/2, change_key_create/2, close/1, current_key/1, delete_key/1,
  delete_value/2, expand/1, format_error/1, open/1, set_value/3, sub_keys/1, value/2, values/1
- `zip` (stdlib, 22): create/2,3, extract/1,2, foldl/3, list_dir/1,2, t/1, table/1,2, tt/1,
  unzip/1,2, zip/2,3, zip_close/1, zip_get/1,2, zip_get_crc32/2, zip_list_dir/1, zip_open/1,2
- `zstd` (stdlib, 17): close/1, compress/1,2, context/1,2, decompress/1,2, dict/2,3, finish/2,
  flush/1, get_dict_id/1, get_frame_header/1, get_parameter/2, reset/1, set_parameter/3, stream/2

## Appendix B: luerl library tracing (from the luerl work, still usable)

luerl was the reference real-world program for library coverage. All its gaps are fixed (plan 11
steps 41T–41ZC in `00-finished.md`); its remaining speed work is in plan 20. The method is still
useful for finding which undocumented functions OTP-sourced modules reach:

- `CLAUSE_CALL_TRACE=FILE` (`runtime/src/process/call_trace.{hpp,cpp}`) logs boundary calls with
  arguments and `RET`/`EXC`. `CLAUSE_CALL_TRACE_FIRST=1` adds the first entry of each function, and
  `CLAUSE_CALL_TRACE_ALL=1` logs every entry.
- `tools/luerl-trace/ctrace.erl` is the OTP twin, which prints by the same rules. `norm.py`,
  `mismatch.py` and `firsts.py` diff the traces and list stubs reached.
- Stand-ins live only in a scratch copy of `library/`: OTP code must not enter the repository.
