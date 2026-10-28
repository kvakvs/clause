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
  headline speedup to `00-finished.md` (Plan 21). Rejected or reverted steps go to `00-reject.md`
  with the measurement behind the decision.

## Status (2026-10-27)

- **Source of truth:** the doc chunks of the installed OTP 29.1.1 (`code:get_doc/1`), recorded in
  `library/documented.terms` by `tests/compiler/library/observe_docs.escript`;
  `library_modules.py coverage .` prints the progress per module. 120 documented modules, 2,888
  documented functions.
- **Done:** every documented module but the ones below (`00-finished.md`, Plan 21).
- **Left:** `erlang` (20 declined functions, LIB9 remainder, Appendix A); OTP-free builds
  (LIB78 on request; LIB77 transforms without the host are done);
  applications beyond the core that emqx needs (LIB81D3–D5, LIB82–LIB83, Appendix B; `crypto` (LIB79),
  `public_key` (LIB80), `ssl`'s utility functions (LIB81A), libssl engine (LIB81B), core connections
  (LIB81C1), the hello pause and closing (LIB81C2), renegotiation, key updates, exporters and secrets
  (LIB81D1), TLS 1.2 session reuse (LIB81D2a), TLS 1.3 session tickets (LIB81D2b) and early data (LIB81D2c) are
  done).
- **Callback-only modules** (no functions to implement, only behaviours to honour): `erl_tracer`
  (`logger_handler` is provided).

## Working rules for plan 21 steps

- Commit each step when its gate is green: subject `[plan21] LIBn. Title`. Finished steps move to
  `00-finished.md` (Plan 21) with their hash.
- The plan 11 gate applies: fast CTest, `check-quality` (clang-tidy and Lizard, no suppressions),
  clang-format, erlfmt for `.erl`/`.terms`, docs (`docs/libraries/library.md` row,
  `docs/builtins.md` section, `docs/differences.md`, `.agents/arch.md`, `.agents/files.md`).
- **Library docs live in `docs/libraries/`** (indexed in `docs/README.md`'s Libraries table). An
  application beyond erts/kernel/stdlib gets its own file there (`crypto.md`, `public_key.md`), and
  every step ends with a documentation pass over it.
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
  `{declined, Reason}`, and the library catalog (`compiler/src/driver/library_modules.inc`) records
  the missing modules.
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

### Phase: OTP-free builds

`test-luerl-euler.toml` and `test-luerl-benchmark.toml` use no Erlang/OTP paths since LIB76: all
non-luerl modules resolve from `library/`. `test-emqx.toml` keeps only the OTP applications outside
erts/kernel/stdlib (`eunit`, `common_test`, `public_key`, `ssl`) as `applications` roots; its
preprocessing diagnostics are unchanged (the rest are emqx's missing dependencies).

- **What still ties programs to OTP** (counted over `references/emqx/apps`): parse transforms run
  in Clause's transform host since LIB77D (the host `erl` only for `.beam`s without `debug_info`,
  OTP modules the library lacks such as `eunit_autoexport`, or `--erl PATH`); `.yrl`/`.xrl`
  sources need OTP's `yecc`/`leex`
  (luerl ships generated `luerl_parse.erl`/`luerl_scan.erl`, whose `-file` lines only name OTP's
  `yeccpre.hrl`). The public headers emqx includes (`assert.hrl`, `ms_transform.hrl`, `inet.hrl`)
  are provided since LIB75.

Steps:

- **LIB78 (optional, beyond erts/kernel/stdlib) `parsetools`:** `yecc` and `leex` with
  `yeccpre.hrl`/`leexinc.hrl` equivalents, so projects that ship only `.yrl`/`.xrl` build without
  OTP. Generated parsers are clause-heavy: generate C++ tables or keep generated Erlang small (see
  the IMPORTANT note above).

#### Applications beyond erts/kernel/stdlib (survey 2026-10-26)

What `test-emqx.toml` still takes from OTP. Documented API from the OTP 29 doc chunks of the host's
`ssl-11.7.7`, `eunit-2.11`, `common_test-1.31.2` (full lists in
Appendix B; re-observe them into `documented.terms` the way `observe_docs.escript` does for the
three core applications). Call counts are over `references/emqx/apps`, non-test sources unless
noted. The same rules apply as for the core: whole module or nothing in `library/`, OTP's terms and
records, goldens from OTP, own headers written from the documented records and macros.

- **LIB81 `ssl` (`ssl` 52 functions, `ssl_crl_cache` 3, callback-only `ssl_crl_cache_api`,
  `ssl_session_cache_api`).** Decided 2026-10-27 (user): a hybrid over OpenSSL's libssl. Each
  connection is an Erlang process with OTP's semantics: owner and `controlling_process`, active
  modes, `{ssl, S, Data}`/`{ssl_closed, S}`/`{ssl_error, S, R}` messages, OTP's option validation,
  error and alert terms. libssl does the record layer and handshake over memory BIOs, and the
  process moves bytes through the `cb_info` transport (`gen_tcp` by default). Certificate checks
  stay OTP's: the handshake pauses in libssl's verify callback (`SSL_set_retry_verify`) and the
  process runs `public_key:pkix_path_validation/3` with `verify_fun`, `partial_chain` and CRL
  options. TLS 1.2 and 1.3 come first. emqx's non-test code only calls the data functions
  (`cipher_suites`, `versions`, `str_to_suite`, `suite_to_str`). Its tests call `connect` 70 times,
  `close` 55, `send` 17 and `recv` 15. emqx's `emqx_ocsp_cache` and `emqx_ssl_crl_cache` include
  OTP-internal `ssl_handshake.hrl` and `ssl_internal.hrl`. These are not public and are not
  provided, so projects keep OTP's `ssl` application root for them.
  - **LIB81D TLS features** (D1 `renegotiate/1`, `update_keys/2`, `export_key_materials/4,5`, `prf/5` and
    `keep_secrets`, D2a TLS 1.2 session reuse, D2b TLS 1.3 session tickets, D2c early data are done). `ssl` stops
    being PARTIAL after D5.
    - **LIB81D3** (D3a `sni_hosts`/`sni_fun` is done) custom `cb_info` transports, `key_update_at`/`renegotiate_at` automatic
      rekeys (accepted and ignored today), `log_level` output.
    - **LIB81D4** CRL checks with `ssl_crl_cache` (`insert/1,2`, `delete/1`) and the cache callbacks, OCSP stapling.
    - **LIB81D5** PSK/SRP `user_lookup_fun`.
  - **LIB81E (optional) TLS 1.0/1.1 and DTLS 1.0/1.2** over `gen_udp` with libssl's DTLS methods.
- **LIB82 (far back: test support) `eunit` (4 functions: `eunit:test/1,2`, `start/0`, `stop/0`):**
  `eunit.hrl` (49 `-define`s: `?assert*`, `?_test`, `?_assert*`, `?debug*`, `?LET`, `?IF`, `NOTEST`)
  adds the `eunit_autoexport` and `eunit_striptests` parse transforms, so it depends on LIB77. The
  test representation (generators, fixtures `setup`/`foreach`, `{timeout, ...}`,
  `{inparallel, ...}`), the server and the default text listener and `eunit_surefire` reports;
  output as OTP's. emqx calls `eunit:test/1` once in tests; 140 test files include the header.
- **LIB83 (far back: test support) `common_test` (264 documented functions in 16 modules):** `ct`
  (70) is what suites call: emqx tests use `ct:pal` (1151), `fail` (409), `sleep` (302), `timetrap`
  (113), `print`, `log`, `get_config`, `comment` in 306 files; `ct.hrl` (19 `-define`s),
  `ct_event.hrl`, `ct_property_test.hrl`. Needs a test runner executable shape (`ct_run`-like:
  suites, groups, `init_per_*`/`end_per_*`, config, `priv_dir`/`data_dir`, `ct_hooks` (19
  callbacks), `ct_suite` (12 callbacks), HTML logs) and `ct_testspec`, `ct_property_test` (needs
  PropEr), `ct_doctest`. Decline on a non-distributed node or without the other applications:
  `ct_master`, `ct_rpc`, `ct_cover` (`cover`), `ct_ssh` (75, `ssh`), `ct_netconfc` (47, `ssh`),
  `ct_snmp` (18, `snmp`), `ct_telnet` (16), `unix_telnet` (2); `ct_ftp` and `ct_slave` are
  deprecated only.

### Order

1. LIB81D3–D5 (LIB81E on request); LIB78 only on request.
2. LIB82, LIB83 last (test support).

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

## Appendix B: documented API of the applications beyond the core (OTP 29 doc chunks)

Observed 2026-10-26 on the host's OTP 29 (`code:get_doc/1`, not hidden, not deprecated), for LIB81–
LIB83 (`crypto` and `public_key` are done). Format: `module` (functions): name/arities.

- `ssl` (ssl-11.7.7, 52, +1 deprecated): append_cipher_suites/2, cipher_suites/2,3,
  clear_pem_cache/0, close/1,2, connect/2,3,4, connection_information/1,2, controlling_process/2,
  eccs/0,1, export_key_materials/4,5, filter_cipher_suites/2, format_error/1, getopts/2,
  getstat/1,2, groups/0,1, handshake/1,2,3, handshake_cancel/1, handshake_continue/2,3, listen/2,
  negotiated_protocol/1, peercert/1, peername/1, prepend_cipher_suites/2, recv/2,3, renegotiate/1,
  send/2, setopts/2, shutdown/2, signature_algs/2, sockname/1, start/0,1, stop/0, str_to_suite/1,
  suite_to_openssl_str/1, suite_to_str/1, transport_accept/1,2, update_keys/2, versions/0
- `ssl_crl_cache` (3): delete/1, insert/1,2. Callback-only: `ssl_crl_cache_api` (4 callbacks),
  `ssl_session_cache_api` (8 callbacks).
- `eunit` (eunit-2.11, 4): start/0, stop/0, test/1,2. `eunit_surefire`: no functions (a listener).
  Header `eunit.hrl`.
- `ct` (common_test-1.31.2, 70): abort_current_testcase/1, add_config/2, break/1,2, capture_get/0,1,
  capture_start/0, capture_stop/0, comment/1,2, continue/0,1, decrypt_config_file/2,3,
  encrypt_config_file/2,3, fail/1,2, get_config/1,2,3, get_event_mgr_ref/0, get_progname/0,
  get_status/0, get_target_name/1, get_testspec_terms/0,1, get_timetrap_info/0, get_verbosity/1,
  install/1, listenv/1, log/1,2,3,4,5, make_priv_dir/0, notify/2, pal/1,2,3,4,5, parse_table/1,
  print/1,2,3,4,5, reload_config/1, remaining_test_procs/0, remove_config/2, require/1,2, run/1,2,3,
  run_test/1, run_testspec/1, set_verbosity/2, sleep/1, start_interactive/0, step/3,4,
  stop_interactive/0, sync_notify/2, testcases/2, timetrap/1, userdata/2,3
- `ct_cover` (3): add_nodes/1, cross_cover_analyse/2, remove_nodes/1
- `ct_doctest` (6): file/1,2,3, module/1,2,3
- `ct_master` (11): abort/0,1, basic_html/1, get_event_mgr_ref/0, progress/0, run/1,3,4,
  run_on_node/2,3, run_test/2
- `ct_netconfc` (47): action/2,3, close_session/1,2, connect/1,2, copy_config/3,4,
  create_subscription/2,3, delete_config/2,3, disconnect/1, edit_config/3,4,5, get/2,3,
  get_capabilities/1,2, get_config/3,4, get_event_streams/1,2,3, get_session_id/1,2, hello/1,2,3,
  kill_session/2,3, lock/2,3, only_open/1,2, open/1,2, send/2,3, send_rpc/2,3, session/1,2,3,
  unlock/2,3
- `ct_property_test` (7): cmnd_names/1, init_per_suite/1, num_calls/1, present_result/4,5,
  quickcheck/2, sequential_parallel/1
- `ct_rpc` (8): app_node/2,3,4, call/4,5,6, cast/4,5
- `ct_snmp` (18): get_next_values/3, get_values/3, load_mibs/1, register_agents/2, register_users/2,
  register_usm_users/2, set_info/1, set_values/4, start/2,3, stop/1, unload_mibs/1,
  unregister_agents/1,2, unregister_users/1,2, unregister_usm_users/1,2
- `ct_ssh` (75): apread/4,5, apwrite/4,5, aread/3,4, awrite/3,4, close/2,3, connect/1,2,3,
  del_dir/2,3, delete/2,3, disconnect/1, exec/2,3,4, get_file_info/2,3, list_dir/2,3, make_dir/2,3,
  make_symlink/3,4, open/3,4, opendir/2,3, position/3,4, pread/4,5, pwrite/4,5, read/3,4,
  read_file/2,3, read_file_info/2,3, read_link/2,3, read_link_info/2,3, receive_response/2,3,4,
  rename/3,4, send/3,4,5, send_and_receive/3,4,5,6, session_close/2, session_open/1,2,
  sftp_connect/1, shell/2,3, subsystem/3,4, write/3,4, write_file/3,4, write_file_info/3,4
- `ct_telnet` (16): close/1, cmd/2,3, cmdf/3,4, expect/2,3, get_data/1, open/1,2,3,4, send/2,3,
  sendf/3,4
- `ct_testspec` (1): get_tests/1
- `unix_telnet` (2): connect/7, get_prompt_regexp/0
- Callback-only: `ct_hooks` (19 callbacks), `ct_suite` (12 callbacks). Deprecated only: `ct_ftp`
  (12), `ct_slave` (5). Headers: `ct.hrl`, `ct_event.hrl`, `ct_property_test.hrl`.
