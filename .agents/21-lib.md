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

## Status (2026-10-25)

- **Source of truth:** the doc chunks of the installed OTP 29.1.1 (`code:get_doc/1`), recorded in
  `library/documented.terms` by `tests/compiler/library/observe_docs.escript`;
  `library_modules.py coverage .` prints the progress per module. 120 documented modules, 2,888
  documented functions.
- **Done:** every documented module but the ones below (`00-finished.md`, Plan 21).
- **Left:** `erlang` (20 declined functions, LIB9 remainder), `edlin_expand` and `shell_docs`
  (LIB74B, LIB74C; provided as partial modules) and the declined modules' answer stubs (Appendix A).
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

## Open steps

### LIB9 remainder: declined `erlang` functions

- D01: `load_module/2`, `load_nif/2`, `purge_module/1`, `check_*code`.
- Distribution: `dist_ctrl_*`.
- Tracing: `trace*`, `system_monitor/0,1,2`, `system_profile/0,2` raise `notsup` (LIB71).

### LIB74. Interactive modules

- LIB74B. `edlin_expand:expand/1,2`: completion of modules, functions (exports), shell bindings,
  records and fields, map keys, `fun M:F/` arities, file paths in strings; result shapes as OTP
  (legacy and sections). Type-spec hints need specs at run time, which linked code lacks: answer
  `{no, [], []}` there and record it in `differences.md`. Golden: observe `edlin_expand:expand/1,2`
  from the shell (the shell's bindings and records feed it through `shell:get_state/0`).
- LIB74C. `shell_docs` (15): `render/2..5`, `render_type/2..5`, `render_callback/2..5` of a given
  `docs_v1` chunk (`application/erlang+html` and `text/markdown`), `normalize/1`, `validate/1`,
  `supported_tags/0`; golden from OTP over chunks the fixture builds (`code:get_doc/1` has none for
  linked code). Then drop both from `PARTIAL` in `library_modules.py`.

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

### Order

1. LIB74B, LIB74C, then the declined modules' answer stubs.

## Later (optional: speed and features beyond OTP's documented functions)

- `ets`: `select_delete/2` and `select_replace/2` over a whole table in portions (they run in one
  piece, each part under its lock).
- Speed: `zip` and `erl_tar` header parsing, stored copies and checksums as C++; `file_sorter` chunk
  decoding and run sorts of the default formats and orders in C++; `ms_transform` in C++ so `qlc_pt`
  needs no replayed requests (each `try_ms` answer reruns the whole transform today); `qlc`'s merge
  joins and caches over C++ helpers.
- Sockets: `{active, N}`, `{packet, line | http | ...}` and `{raw, ...}` options; inet_drv's own
  options (`send_timeout`, `header`, `deliver`, `exit_on_close`, ...); `socket`'s `local` domain,
  `ioctl`, native options and descriptor adoption as OTP on Linux and macOS; SCTP (`gen_sctp`,
  socket's `seqpacket`).
- A cache of `inet_res` answers; terminfo for `io_ansi`.
- Real tracing (runtime trace points for sends, receives, calls and process events; `trace`,
  `erlang:trace*`, `seq_trace` tracer output), starting from plan 11's call tracer.
- Shell: `edlin` line editing in a raw terminal (cursor keys, history, search, tab completion over
  `edlin_expand`, the `..>` multiline prompt) and Ctrl+G job control; `current_function` and
  `initial_call` items for `c:i/0`; records of compiled modules for `rr(Module)`.

## Appendix A: documented functions still to do (OTP 29.1.1 doc chunks)

Format: `module` (application, functions to do): name/arities. Deprecated functions are listed
separately and are optional.

- `erlang` (erts, 20, all declined; tracing raises `notsup`): check_old_code/1,
  check_process_code/2,3, dist_ctrl_get_data/1, dist_ctrl_get_data_notification/1,
  dist_ctrl_get_opt/2, dist_ctrl_input_handler/2, dist_ctrl_put_data/2, dist_ctrl_set_opt/3,
  load_module/2, load_nif/2, purge_module/1, system_monitor/0,1,2, system_profile/0,2, trace/3,
  trace_delivered/1, trace_info/2, trace_pattern/2,3
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
- `edlin_expand` (stdlib, 2): expand/1,2
- `peer` (stdlib, 11): call/4,5, cast/4, get_state/1, random_name/0,1, send/3, start/1,
  start_link/0,1, stop/1
- `pool` (stdlib, 8): attach/1, get_node/0, get_nodes/0, pspawn/3, pspawn_link/3, start/1,2, stop/0
- `shell_docs` (stdlib, 15): normalize/1, render/2,3,4,5, render_callback/2,3,4,5,
  render_type/2,3,4,5, supported_tags/0, validate/1
- `slave` (stdlib, 0): none; deprecated: pseudo/1,2, relay/1, start/1,2,3, start_link/1,2,3, stop/1
