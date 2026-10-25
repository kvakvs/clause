# Plan 21: Clause's own erts, kernel and stdlib

The general plan for the missing standard library. Goal: every public module that OTP 29 documents
for `erts`, `kernel` and `stdlib` has a Clause implementation of its documented functions, so
programs need no OTP sources on `source_search_paths`. Native C++ runtime builtins are preferred;
Erlang is used where a function takes funs, runs a process protocol or is glue. Undocumented and
internal functions stay out unless a public function needs them (user direction 2026-10-17).

- IMPORTANT: Prefer compatibility with Erlang/OTP at all times, if something can be faster at the
  cost of compatibility, add that as a future task and we will create a new API that's for the end
  user to choose
- IMPORTANT: When implementing erlang functions with many clauses like those generated from yrl,
  they are very heavy on inference engine, consider making C++ versions of those instead.

- When a step is done, remove it from this file and add a 1-2 line bullet with its commit hash and
  headline speedup to `00-finished.md` (Plan 21).

## Discovery (2026-10-17, counts refreshed 2026-10-18)

- **Source of truth:** the doc chunks of the installed OTP 29.1.1 (`code:get_doc/1`). A module
  counts when its module doc is not `hidden`, a function when its doc is not `hidden` and it is not
  deprecated. The OTP checkout (pin `d199a49a01`) gave line counts and NIF use.
  `tests/compiler/library/observe_docs.escript` records the documented functions in
  `library/documented.terms`; `library_modules.py coverage .` prints the progress per module.
- **Result:** 120 documented modules (erts 9, kernel 37, stdlib 74) with 2,888 documented functions;
  784 are still to do (erts 23, kernel 185, stdlib 576). Appendix A lists them per module.
- **Complete already** (every documented function, or the rest declined): `atomics`, `counters`,
  `erl_prim_loader`, `persistent_term`, `prim_eval`, `zlib`, `binary`, `lists`, `maps`, `math`, `re`
  (`import/1` declined), `init` (`boot/1`, `restart/0,1` refused), `array`, `base64`, `calendar`,
  `dict`, `digraph`, `digraph_utils`, `erl_anno`, `erl_features`, `erl_internal`, `filename`,
  `gb_sets`, `gb_trees`, `graph`, `io_lib`, `json`, `ms_transform`, `erl_scan`, `erl_parse`,
  `orddict`, `ordsets`, `proplists`, `queue`, `rand`, `records`, `sets`, `sofs`, `string`, `timer`,
  `unicode`, `uri_string`, `erl_eval`, `erl_error`, `erl_pp`, `erl_expand_records`, `epp`,
  `proc_lib`, `erl_lint`, `sys`, `gen_server`, `gen_statem`, `gen_event`, `gen_fsm` (deprecated),
  `supervisor`, `supervisor_bridge`, `logger`, `logger_formatter`, `logger_std_h`, `logger_filters`,
  `error_logger`, `log_mf_h`, `error_handler`, `application`, `pg`, `data_publisher`, `global`,
  `global_group`, `rpc`, `erpc`, `net_adm`, `net_kernel` (distribution start declined), `auth`,
  `seq_trace` (tokens without tracing), `file`, `filelib`, `file_sorter`, `disk_log`,
  `wrap_log_reader`, `logger_disk_log_h`, `gen_tcp`, `gen_udp`, `net`, `os`, `code` (loading answers
  for linked code), `io`, `io_ansi` (no terminfo), `inet`, `inet_res` (no answer cache), `socket`,
  `gen_sctp` (no SCTP), `argparse`, `ets`, `dets`, `qlc`, `trace` and `erl_tracer` (all `notsup`:
  no tracing).
- **Partial:** `erlang` (22 left, all unavailable: code loading, distribution, tracing).
- **Missing:** 15 modules (`library_modules.py coverage`). Today a program gets the plain-Erlang
  ones (`zip`, `erl_tar`, ...) only by compiling OTP's sources from a search path.
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
- Tracing: `trace*`, `system_monitor/0,1,2`, `system_profile/0,2` raise `notsup` (LIB71).

## Tier B: ETS and what needs it

- Later: `select_delete/2` and `select_replace/2` over a whole table in portions (they run in one
  piece, each part under its lock).

## Tier C: processes and behaviours (Erlang)

Done: LIB20–LIB25 and `logger_disk_log_h` (LIB52) (`00-finished.md`).

## Tier D: Erlang front end as library modules

Done (`00-finished.md`): LIB32 `erl_eval`, LIB33 readers, LIB34 parts 1-4, LIB34C (C++ cores of
`erl_expand_records`, `erl_lint`, `erl_pp`, `epp`, `erl_eval`, `erl_error`) and with it LIB34A.
Nothing is left in this tier.

## Tier E: files, archives and logs

Done (`00-finished.md`): LIB50-LIB52, LIB54 (`zip`, `file` `ram` mode, `erl_tar`, `zstd` with
`file`'s `{zstd, Options}`, `beam_lib`) and LIB55 (`escript`, `win32reg`). Nothing is left in this
tier. Later (speed): `zip` and `erl_tar` header parsing, stored copies and checksums as C++;
`file_sorter` (LIB54A) chunk decoding and run sorts of the default formats and orders in C++.

## Tier F: kernel I/O remainder

Every step is done (LIB60A, LIB60B, LIB61, LIB62, LIB63; `00-finished.md`). Later: `{active, N}`,
`{packet, line | http | ...}` and `{raw, ...}` socket options; applying inet_drv's own options
(`send_timeout`, `header`, `deliver`, `exit_on_close`, ...). Later for `socket`: `local` domain,
`ioctl`, native options and descriptor adoption as OTP on Linux and macOS; SCTP (`gen_sctp`,
socket's `seqpacket`); a cache of `inet_res` answers; terminfo for `io_ansi`.

## Tier G: tools, transforms and the interactive shell

Done: LIB70, LIB71 (`00-finished.md`). Later (optional): real tracing (runtime trace points for
sends, receives, calls and process events; `trace`, `erlang:trace*`, `seq_trace` tracer output),
starting from plan 11's call tracer. Later (speed): `ms_transform` in C++ so `qlc_pt` needs no
replayed requests (each `try_ms` answer reruns the whole transform today); `qlc`'s merge joins and
caches over C++ helpers.

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

Done: LIB30, LIB31, LIB54A, LIB32, LIB34A, then LIB73 `qlc` with LIB12C (`00-finished.md`).
`erl_scan` and `erl_parse` have their own C++ scanner and parser in `runtime/src/builtins/front_end/`
(the runtime cannot link the compiler's `clause_frontend`; user choice 2026-10-19).

### General order

Ranked by what real programs need first and by dependencies:

1. LIB74, then the declined modules' answer stubs.

## Appendix A: documented functions still to do (OTP 29.1.1 doc chunks)

Format: `module` (application, functions to do, ...): name/arities. Deprecated functions are listed
separately and are optional.

- `erlang` (erts, 20 left, all declined; tracing raises `notsup`): check_old_code/1,
  check_process_code/2,3, dist_ctrl_get_data/1, dist_ctrl_get_data_notification/1,
  dist_ctrl_get_opt/2, dist_ctrl_input_handler/2, dist_ctrl_put_data/2, dist_ctrl_set_opt/3,
  load_module/2, load_nif/2, purge_module/1, system_monitor/0,1,2, system_profile/0,2, trace/3,
  trace_delivered/1, trace_info/2, trace_pattern/2,3
- `init` (erts, 3, 8 done): boot/1, restart/0,1
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
- `heart` (kernel, 8): clear_callback/0, clear_cmd/0, get_callback/0, get_cmd/0, get_options/0,
  set_callback/2, set_cmd/1, set_options/1
- `beam_lib` (stdlib, 19): all_chunks/1, build_module/1, chunks/2,3, clear_crypto_key_fun/0, cmp/2,
  cmp_dirs/2, crypto_key_fun/1, diff_dirs/2, format_error/1, info/1, md5/1, strip/1,2,
  strip_files/1,2, strip_release/1,2, version/1
- `c` (stdlib, 44): bt/1, c/1,2,3, cd/1, erlangrc/1, flush/0, h/1,2,3, hcb/1,2,3, help/0, ht/1,2,3,
  i/0,3, l/1, lc/1, lm/0, ls/0,1, m/0,1, memory/0,1, mm/0, nc/1,2, ni/0, nl/1, nregs/0, pi/1,3,
  pid/3, pwd/0, q/0, regs/0, uptime/0, xm/1, y/1,2
- `edlin` (stdlib, 1): keymap/0
- `edlin_expand` (stdlib, 2): expand/1,2
- `erl_tar` (stdlib, 14): add/3,4, close/1, create/2,3, extract/1,2, format_error/1, init/3, open/2,
  t/1, table/1,2, tt/1
- `escript` (stdlib, 3): create/2, extract/2, script_name/0
- `peer` (stdlib, 11): call/4,5, cast/4, get_state/1, random_name/0,1, send/3, start/1,
  start_link/0,1, stop/1
- `pool` (stdlib, 8): attach/1, get_node/0, get_nodes/0, pspawn/3, pspawn_link/3, start/1,2, stop/0

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
