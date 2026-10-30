# Plan 21: Clause's own erts, kernel and stdlib

The general plan for the missing standard library. Goal: every public module that OTP 29 documents
for `erts`, `kernel` and `stdlib` has a Clause implementation of its documented functions, so
programs need no OTP sources on `source_search_paths`. Native C++ runtime builtins are preferred;
Erlang is used where a function takes funs, runs a process protocol or is glue. Undocumented and
internal functions stay out unless a public function needs them (user direction 2026-10-17).

- When adding plan steps and extending plan step that is too broad, prefer bullet lists (nested if
  necessary) for easier reading.
- IMPORTANT: Prefer compatibility with Erlang/OTP at all times, if something can be faster at the
  cost of compatibility, add that as a future task and we will create a new API that's for the end
  user to choose
- IMPORTANT: When implementing erlang functions with many clauses like those generated from yrl,
  they are very heavy on inference engine, consider making C++ versions of those instead.

- When a step is done, remove it from this file and add a 1-2 line bullet with its commit hash and
  headline speedup to `21-finished.md`. Rejected or reverted steps go to `00-reject.md`
  with the measurement behind the decision.

## Status (2026-10-28)

- **Source of truth:** the doc chunks of the installed OTP 29.1.1 (`code:get_doc/1`), recorded in
  `library/documented.terms` by `tests/compiler/library/observe_docs.escript`;
  `library_modules.py coverage .` prints the progress per module. 120 documented modules, 2,888
  documented functions.
- **Done:** every documented module of `erts`, `kernel` and `stdlib`, `crypto` (LIB79),
  `public_key` (LIB80) and `ssl` (LIB81A–D5) (`21-finished.md`).
- **Left:** MongooseIM's missing applications (MIM1–MIM9, `test-mim.toml`), optional `ssl` steps
  on request (LIB81D3c, LIB81D4b, LIB81E) and test support (LIB82 `eunit`, LIB83 `common_test`,
  the appendix).
- **Declined** (`00-reject.md`): the 20 `erlang` functions of the LIB9 remainder, LIB78
  `yecc`/`leex`, `ssl`'s SRP suites.
- **Callback-only modules** (no functions to implement, only behaviours to honour): `erl_tracer`
  (`logger_handler` is provided).

## Working rules for plan 21 steps

- Commit each step when its gate is green: subject `[plan21] LIBn. Title`. Finished steps move to
  `21-finished.md` with their hash.
- The plan 11 gate applies: `check-quality` first (clang-tidy and Lizard, no suppressions), then
  fast CTest, clang-format, erlfmt for `.erl`/`.terms`, docs (`docs/libraries/library.md` row,
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

### Phase: OTP-free builds

`test-luerl-euler.toml` and `test-luerl-benchmark.toml` use no Erlang/OTP paths since LIB76: all
non-luerl modules resolve from `library/`. `test-emqx.toml` keeps only the OTP applications outside
erts/kernel/stdlib (`eunit`, `common_test`, `public_key`, `ssl`) as `applications` roots; its
preprocessing diagnostics are unchanged (the rest are emqx's missing dependencies).

- **What still ties programs to OTP** (counted over `references/emqx/apps`): parse transforms run in
  Clause's transform host since LIB77D (the host `erl` only for `.beam`s without `debug_info`, OTP
  modules the library lacks such as `eunit_autoexport`, or `--erl PATH`); `.yrl`/`.xrl` sources need
  OTP's `yecc`/`leex`, which Clause will not provide (LIB78 cancelled; luerl ships generated
  `luerl_parse.erl`/`luerl_scan.erl`). The public headers emqx includes (`assert.hrl`,
  `ms_transform.hrl`, `inet.hrl`) are provided since LIB75.

#### Applications beyond erts/kernel/stdlib (survey 2026-10-26)

What `test-emqx.toml` still takes from OTP. Documented API from the OTP 29 doc chunks of the host's
`ssl-11.7.7`, `eunit-2.11`, `common_test-1.31.2` (full lists in the appendix; re-observe them into
`documented.terms` the way `observe_docs.escript` does for the three core applications). Call counts
are over `references/emqx/apps`, non-test sources unless noted. The same rules apply as for the
core: whole module or nothing in `library/`, OTP's terms and records, goldens from OTP, own headers
written from the documented records and macros.

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
  - **LIB81D TLS features:** done but these optional steps (`ssl` is complete, not PARTIAL).
    - **LIB81D3c (optional, on request)** `log_level` output: OTP's `debug` protocol traces of
      records and handshake messages (`ssl_logger`); libssl's messages differ from OTP's, so the
      traces cannot match.
    - **LIB81D4b (optional, skipped for now 2026-10-28)** OCSP stapling: the client `stapling`
      option (`staple | no_staple | #{ocsp_nonce, ...}`), the staple checked as OTP's
      `ssl_certificate` checks it (`public_key:pkix_ocsp_validate/5`), `missing_ocsp_staple`.
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

### Phase: MongooseIM (survey 2026-10-30)

`test-mim.toml` builds `references/MongooseIM` (esl/MongooseIM `master`, shallow clone) with its
73 rebar3 dependencies (`escript rebar3 get-deps`, in `_build/default/lib`) on
`source_search_paths`, no OTP paths. The `.yrl`/`.xrl` parsers of MongooseIM, `eini`, `graphql`,
`luerl` and `tomerl` are generated once with the host `erlc`. 1001 sources resolve
(`--print-inputs`); the build stops in the frontend (below), so later stages (unknown modules,
undefined functions, inference) are not surveyed yet. Call counts are over MongooseIM and its
dependencies, non-test sources.

- **MIM1 `mnesia`:** MongooseIM's default backends (sessions, roster, MUC, pubsub, offline, auth
  and more): 354 calls in 30 modules. Used: `transaction` (56), `write` (46), `dirty_read` (29),
  `delete`, `add_table_index`, `read`, `system_info`, `delete_object`, `match_object`, `wread`,
  `table_info`, `sync_dirty`, `dirty_select`, `foldl`, `async_dirty`, `select`, `dirty_index_read`,
  `add_table_copy`, `create_table`, `wait_for_tables`, `create_schema`, `transform_table`,
  `backup`/`restore`/`install_fallback`/`traverse_backup`, `load_textfile`, `change_config`, ...
  OTP 29.1.1's `mnesia-4.27` documents 138 functions in `mnesia` and 5 in `mnesia_frag_hash`.
  - **Design (all steps):**
    - Erlang over `ets`, `dets` and `disk_log` (processes and protocols; the hot paths are already
      C++ `ets`). Own structure, never OTP's sources.
    - Local-node semantics: one node `nonode@nohost`; `ram_copies`, `disc_copies` and
      `disc_only_copies` there; other nodes get OTP's non-distributed answers.
    - What programs observe stays OTP's: tables are public named `ets` tables (keypos 2) that
      code may read directly, `{aborted, Reason}` and `exit({aborted, Reason})` terms, the `{tid, N,
      Pid}` transaction ids, `table_info`/`system_info` items, event messages, the backup callback
      interface, registered names of the processes Clause has (`mnesia_sup`, `mnesia_monitor`,
      `mnesia_locker`, `mnesia_tm`, `mnesia_controller`, `mnesia_subscr`, `mnesia_event`).
    - `mnesia` enters `library/` in MIM1A as PARTIAL (`library_modules.py`) and leaves it in
      MIM1G. Docs in `docs/libraries/mnesia.md`; one golden per step, generated from OTP.
  - **MIM1A Foundation, schema in memory and dirty access:**
    - Observe `mnesia` and `mnesia_frag_hash` into `documented.terms`; inventories, stubs and
      `mnesia.types.hrl`; catalog rows; `mnesia.app` (4.27) with `clause_mnesia_app`.
    - `start/0`, `stop/0` with OTP's results and reports; answers when not started.
    - ram schema: `create_table/2` with option validation (`type`, `attributes`, `record_name`,
      `index`, `ram_copies`, `local_content`, `user_properties`, `storage_properties`,
      `load_order`, `access_mode`, `majority`), `delete_table/1`, `table_info/2`, `system_info/1`,
      `schema/0,1`, `wait_for_tables/2`, `error_description/1`.
    - Dirty functions: `dirty_read/1,2`, `dirty_write/1,2`, `dirty_delete/1,2`,
      `dirty_delete_object/1,2`, `dirty_update_counter/2,3`, `dirty_match_object/1,2`,
      `dirty_select/2`, `dirty_select_reverse/2`, `dirty_all_keys/1`, `dirty_first/last/next/prev`,
      `dirty_index_read/3`, `dirty_index_match_object/2,3`.
    - Indexes: `add_table_index/2`, `del_table_index/2`, index tables kept on every write.
    - Golden `mnesia_dirty`.
  - **MIM1B Transactions and activities:**
    - `mnesia_locker`: read and write locks on records and tables, OTP's age order (an older
      transaction waits, a younger one restarts with the same tid), release on commit, abort and
      exit.
    - `mnesia_tm`: tids, atomic commit with index upkeep, `transaction_commits`/`failures`/
      `restarts` counters.
    - `transaction/1,2,3`, `sync_transaction/1,2,3`, `abort/1`, `is_transaction/0`, nested
      transactions, retries, OTP's abort terms for errors, exits and throws.
    - In a transaction: `read/1,2,3`, `wread/1`, `write/1,3`, `s_write/1`, `delete/1,3`,
      `s_delete/1`, `delete_object/1,3`, `s_delete_object/1`, `match_object/1,3`,
      `select/1,2,3,4`, `select_reverse/1,2,3,4` (continuations), `all_keys/1`,
      `first/last/next/prev`, `foldl/3,4`, `foldr/3,4`, `index_read/3`, `index_match_object/2,4`,
      `lock/2`, `read_lock_table/1`, `write_lock_table/1`; reads see the transaction's own writes.
    - `activity/2,4` with access-module dispatch (`mnesia_activity_state`), `async_dirty/1,2`,
      `sync_dirty/1,2`, `ets/1,2`.
    - Golden `mnesia_transactions`.
  - **MIM1C Schema changes:**
    - `clear_table/1`, `transform_table/3,4`, `change_table_copy_type/3`, `add_table_copy/3`,
      `del_table_copy/2`, `move_table_copy/3` on the local node.
    - `read/write/delete_table_property`, `change_table_access_mode/2`,
      `change_table_load_order/2`, `change_table_majority/2`, `set_master_nodes/1,2`,
      `force_load_table/1`, `change_config/2`, `set_debug_level/1`.
    - OTP's abort terms for other nodes. Golden `mnesia_schema`.
  - **MIM1D Disc storage:**
    - `create_schema/1`, `delete_schema/1` for `[node()]`; the `dir` environment (default
      `Mnesia.nonode@nohost`); the schema on disc.
    - `disc_copies`: `ets` plus a transaction log replayed at start and dumped to table files
      (`dump_log/0`, `sync_log/0`, `dump_tables/1`, the dump thresholds).
    - `disc_only_copies` over `dets`.
    - Golden `mnesia_disc`: the program stops and starts mnesia and finds its data.
  - **MIM1E Events, text files, queries and info:**
    - `subscribe/1`, `unsubscribe/1`, `report_event/1`: `system`, `activity` and
      `{table, Tab, simple | detailed}` events with OTP's messages.
    - `load_textfile/1`, `dump_to_textfile/1`, `table/1,2` (qlc), `info/0`'s printout.
    - Golden `mnesia_events`.
  - **MIM1F Checkpoints and backups:**
    - `activate_checkpoint/1`, `deactivate_checkpoint/1` with retainers.
    - `backup/1,2`, `backup_checkpoint/2,3` through the backup callback module (default
      `mnesia_backup` over `disk_log`), `traverse_backup/4,6`, `restore/2`.
    - `install_fallback/1,2`, `uninstall_fallback/0,1` (the fallback replaces the database at the
      next start).
    - Golden `mnesia_backup`.
  - **MIM1G Fragmentation and completion:**
    - `mnesia_frag_hash` (5), `frag_properties`, `change_table_frag/2`, `activity` with
      `mnesia_frag`. Golden `mnesia_frag`.
    - `snmp_*` (5): decide (OTP's need the `snmp` application's `snmp_index`).
    - `mnesia` leaves PARTIAL; check `test-mim.toml` resolves `mnesia` from `library/`.
      `mnesia_tm:get_info/1` (prometheus) goes to MIM7.
- **MIM2 `eldap`** (`eldap.hrl` and module `eldap`): `open`, `simple_bind`, `search` with the
  filter constructors (`equalityMatch`, `substrings`, `present`, `approxMatch`,
  `greaterOrEqual`, `lessOrEqual`, `extensibleMatch`), `close`. LDAPv3 over `gen_tcp`/`ssl` with
  the ELDAPv3 BER codec (Clause's ASN.1 BER engine, LIB84). MongooseIM's
  `ejabberd_auth_ldap`, `eldap_pool`, `mod_shared_roster_ldap` and `mod_vcard_ldap` are in
  `ignore_modules` until then.
- **MIM3 `xmerl`:** `xmerl.hrl` records (`#xmlElement{}`, `#xmlText{}`, ...), `xmerl_scan:string/1,2`,
  `xmerl_xpath:string/2` (erlcloud), `xmerl_ucs:from_utf8/1`, `to_utf8/1`, `is_unicode/1`,
  `from_utf16be/1` (jiffy, rabbit_common). The scanner in C++, XPath over C++ helpers.
- **MIM4 `inets` client side:** `httpc:request/1,4` (erlcloud, MongooseIM) and
  `http_uri:encode/1`, `decode/1` (erlcloud, lhttpc, MongooseIM; OTP 29 exports only these two,
  erlcloud's `parse` calls sit behind `?OTP_RELEASE` guards). `httpc` over `gen_tcp`/`ssl` with OTP's profile,
  option and result terms; the `inets` server parts (`httpd`, `ftp`, `tftp`) stay out unless asked.
- **MIM5 `runtime_tools`:** `msacc:start/0,1`, `stop/0`, `stats/0` (rabbit_common) and `dbg`'s
  `tracer`, `p`, `tp`, `ctp`, `stop`, `trace_port` (hackney's debug helper). `dbg` needs the real
  tracing of "Later"; until then OTP's answers where tracing is unavailable.
- **MIM6 application resources** for `application:ensure_all_started(mongooseim)`
  (`mongooseim:start/0`, the target's entry): MongooseIM's `.app.src` lists `sasl`, `inets`,
  `tools`, `runtime_tools`, `asn1` and `eldap`, its dependencies `xmerl`, `compiler` and
  `syntax_tools`. Each needs an `.app` in `library/` (and the application callback module where OTP
  has one: `sasl`'s `alarm_handler`/`release_handler` processes, `inets`' supervisor) so the
  start order and `{error, {not_started, App}}` answers match OTP.
- **MIM7 undocumented modules real code calls** (plan rule: internals stay out unless needed; these
  are needed). Decide per function, provide with OTP's results or decline in `.terms`:
  `inet_parse:ntoa/1`, `address/1`, `domain/1` (amqp_client, hackney, lhttpc, rabbit_common,
  MongooseIM); `inet_db:res_option/1`, `gethostname/0` (MongooseIM); `io_lib_format:fwrite_g/1`
  (eredis); `io_lib_pretty:print/2` (parse_trans); `ram_file:open/2`, `get_size/1` (prometheus);
  `dist_util:split_node/1` (cets); `pubkey_cert:time_str_2_gregorian_sec/1` (MongooseIM);
  `pubkey_pbe:pbdkdf2/7` (credentials_obfuscation); header `kernel/include/net_address.hrl`
  (rabbit_common's `rabbit_net`). Not to provide: `pg2` (pooler's group functions call it
  unguarded; OTP removed it in 24, so they raise `undef` there too).
- **MIM8 NIF dependencies** (decision needed): `exml_nif` (hexml, MongooseIM's XML parser),
  `jiffy`, `stringprep`, `cache_tab`'s `ets_cache`, `fast_pbkdf2`, `re2`, `lz4`, `snappy`,
  `syslogger` load C NIFs (`-on_load`, `-nifs`, rejected by the compiler). Either Clause-owned C++
  replacements registered as library modules for the popular ones (`exml_nif`, `jiffy`,
  `fast_pbkdf2`, `stringprep`), or NIF loading through the runtime. `jwerl` and MongooseIM's
  `mongoose_mam_id` use `-on_load` without NIFs.
- **MIM9 compiler and project gaps** (not library: route to plan 11 when picked up):
  - `#r{a = 1, b = 2, _ = '_'}` with every field given is rejected (`record wildcard requires an
    omitted field`, `compiler/src/semantic/records.cpp`); OTP accepts it (checked with `erlc`).
    MongooseIM's `mod_muc_mnesia`. A difference from OTP: fix or record in `differences.md`.
  - `-on_load`, `-nifs`, `-compile(export_all)` (`mongoose_xmpp_errors`) and `no_native`
    (`ets_cache`) are rejected (plan 11, 65A–65C).
  - One include path per target: mysql and epgsql both `-include("protocol.hrl")` from their own
    `include/`, so one of them gets the other's header (rebar compiles each application with its
    own `include`). Needs per-application include directories (for example under
    `source_search_paths` roots, or `[[targets.apps]]`). `test-mim.toml` keeps epgsql's.
  - Transform-time calls: hackney's `ct_expand` evaluates `certifi:cacerts()` while transforming
    `hackney_ssl` and gets `undef` in the transform host, which only receives the transform's own
    callees.
  - `behaviour gen_fsm_compat undefined` warnings although the dependency's source on the search path
    defines its callbacks.

### Order

1. MIM1A–MIM1G (paused 2026-10-30 for plan 30, network distribution in `30-dist.md`; revisit the
   single-node design after it), then MIM9's record wildcard fix, MIM6, MIM7
   (small), MIM3, MIM4, MIM2, MIM5; MIM8 after a user decision.
2. LIB81D3c, LIB81D4b and LIB81E on request.
3. LIB82, LIB83 last (test support).

## Later (optional: speed and features beyond OTP's documented functions)

- `ets`: `select_delete/2` and `select_replace/2` over a whole table in portions (they run in one
  piece, each part under its lock).
- Speed: `zip` and `erl_tar` header parsing, stored copies and checksums as C++; `file_sorter` chunk
  decoding and run sorts of the default formats and orders in C++; `qlc`'s merge joins and caches
  over C++ helpers.
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

## Appendix: documented API of the applications beyond the core (OTP 29 doc chunks)

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
