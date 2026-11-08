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
- **Left:** MongooseIM's missing applications (MIM2–MIM9, `test-mim.toml`; `mnesia` complete, MIM1H optional), optional `ssl` steps
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

### Phase: applications beyond erts/kernel/stdlib (survey 2026-10-26)

What `test-emqx.toml` still takes from OTP. Documented API from the OTP 29 doc chunks of the host's
`ssl-11.7.7`, `eunit-2.11`, `common_test-1.31.2` (full lists in the appendix; re-observe them into
`documented.terms` the way `observe_docs.escript` does for the three core applications). Call counts
are over `references/emqx/apps`, non-test sources unless noted. The same rules apply as for the
core: whole module or nothing in `library/`, OTP's terms and records, goldens from OTP, own headers
written from the documented records and macros.

- **LIB81 `ssl` optional steps** (`ssl` is complete; design in `21-finished.md`):
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
    - Local-node semantics: one node, `node()` (named or `nonode@nohost`); `ram_copies`, `disc_copies` and
      `disc_only_copies` there; other nodes get OTP's answers for nodes outside the schema. Since plan 30
      (distribution, 2026-11-02, DIST13) a Clause node can be distributed: MIM1A–MIM1G stay single-node, never
      hard-coding `nonode@nohost` (the `dir` default is `Mnesia.<node()>`, `schema/0` names `node()`), and keep
      the node lists of schema records, `{tid, N, Pid}` and lock owners as OTP's so that MIM1H adds nodes
      without changing formats.
    - MIM1H (optional, after MIM1G): several nodes, Clause and OTP nodes in one database. See
      **MIM1H Several nodes** below for its steps.
    - What programs observe stays OTP's: tables are public named `ets` tables (keypos 2) that
      code may read directly, `{aborted, Reason}` and `exit({aborted, Reason})` terms, the `{tid, N,
      Pid}` transaction ids, `table_info`/`system_info` items, event messages, the backup callback
      interface, registered names of the processes Clause has (`mnesia_sup`, `mnesia_monitor`,
      `mnesia_locker`, `mnesia_tm`, `mnesia_controller`, `mnesia_subscr`, `mnesia_event`).
    - `mnesia` is complete in `library/` since MIM1G (`add_backend_type/2` declined). Docs in
      `docs/libraries/mnesia.md`; one golden per step, generated from OTP.
    - Since MIM1A-MIM1G (`21-finished.md`): `mnesia_monitor` owns every table and applies the changes of schema
      transactions, whose coordinator takes OTP's locks and checks (`clause_mnesia_change`); `mnesia_gvar` holds
      OTP's `{Tab, Item}` rows; dirty access and transactions run in the caller, `mnesia_tm` commits,
      `mnesia_locker` keeps locks; on a disc schema `clause_mnesia_disc` keeps OTP's files (schema.DAT, LATEST.LOG,
      DCD/DCL, DAT, FALLBACK.BUP from create_schema), `clause_mnesia_db` dispatches table access to ets or dets;
      `mnesia_subscr` keeps event subscriptions, events are reported where they happen; checkpoints are processes
      under `mnesia_checkpoint_sup`, backups go through OTP's `mnesia_backup`, fragments through `mnesia_frag`.
      `mnesia_tm:get_info/1` (prometheus) goes to MIM7.
- **MIM1H Several nodes (optional; replanned 2026-11-03):** Clause and OTP 29 nodes share one mnesia database:
  same wire protocol (protocol version `{8,6}`, accepting `{8,5}`), same files in each node's directory, so that a
  Clause node can join an OTP cluster, be replaced by an OTP node on the same directory, and the reverse.
  - **Rules for every step:**
    - Observe before writing. Run the step's scenario on two OTP nodes in scratch (`erl -sname`, `sys:trace`/`dbg`
      on `mnesia_monitor`, `mnesia_controller`, `mnesia_recover`, `mnesia_tm`, `mnesia_locker`, `mnesia_loader`) and
      record the message shapes, the `rpc` calls and the file terms as observations in
      `docs/libraries/mnesia-protocol.md` (message tables, not OTP code). Write Clause's side from that table and
      OTP's documentation, never from OTP's sources.
    - Records that cross nodes or files stay OTP's, field for field: `#cstruct{}`, `#commit{}`, `#log_header{}`,
      `#tid{}`, `#decision{}`, `#transient_decision{}`, `#participant{}`, `#checkpoint_args{}`, `#retainer{}`,
      `#frag_state{}`, `#hash_state{}`, `#cyclic{}`. Clause's `#checkpoint_args{}` has fewer fields today: align it
      in H2.
    - OTP nodes reach a peer through registered processes and `rpc` into OTP's internal module names
      (`mnesia_lib:is_running/0`, `mnesia_controller:get_remote_cstructs/0`, `mnesia_schema:remote_read_schema/0`,
      `mnesia_monitor:call/1`, `mnesia_recover:call/1`, `mnesia_checkpoint:tm_prepare/1`, `mnesia_rpc`'s
      `{apply, M, F, A}`, `mnesia_lib:db_get/2`, `mnesia_tm:fixtable/3`, ...). Clause provides those module names as
      thin library modules over the `clause_mnesia_*` servers, each with an inventory whose functions no peer calls
      are `declined` ("internal, not reached by other nodes").
    - Servers that OTP runs as raw receive loops (`mnesia_tm`, `mnesia_locker`, `mnesia_checkpoint`) receive OTP's
      raw `{From, Request}` messages and answer in OTP's reply form (`{mnesia_locker, Node, Reply}`,
      `{mnesia_tm, Node, Reply}`, ...), in `handle_info` where the Clause server is a `gen_server`.
    - Tests: one golden per step, `host_otp`, multi-node runs whose peers are Clause nodes and OTP nodes
      (`runs[].nodes[].kind = otp`), generated with every node on OTP. Each scenario runs both ways where it can
      (the Clause node as the one that starts the operation, and as the one that answers). Disc scenarios give each
      node its own `dir`.
  - **MIM1H1 done** (`21-finished.md`): OTP's files and terms in the directory, observations in
    `docs/libraries/mnesia-protocol.md`.
  - **MIM1H2 done** (`21-finished.md`): schema transactions logged as OTP's operations. The op forms of
    `add_table_copy`, `del_table_copy` of another copy and `move_table_copy` are not seen on one node: verify them in
    H9. `#checkpoint_args{}` alignment moves to H11, `#cstruct{}` as a record on the wire to H5.
  - **MIM1H3 done** (`21-finished.md`): OTP's process tree and the queries peers make (`mnesia_lib`,
    `mnesia_schema`, `mnesia_controller`, `mnesia_rpc`). `mnesia_recover` and `mnesia_late_loader` are idle servers
    until H4 and H9; `mnesia:kill/0` still multicalls `clause_mnesia_app` (OTP peers call `mnesia_sup:kill/0`).
  - **MIM1H4 done** (`21-finished.md`): negotiation, monitor links, `connect_nodes`, serial exchange, `mnesia_down` and
    partition detection. Left for later steps: decision records in `{decisions, ...}` are not merged (H8), OTP's
    `early_connects` (reject before mnesia_tm runs) and `busy` while a down is processed are not reproduced (Clause
    processes a down at once), `{mnesia_down, ...}` records are not written to DECISION_TAB.LOG (H10).
  - **MIM1H5 done** (`21-finished.md`): `change_config(extra_db_nodes)` and the start's `extra_db_nodes` merge
    schemas with OTP's transaction (raw remote schema locks, `get_remote_cstructs`, `announce_im_running` and
    `merge_schema` ops committed with asym_trans `ask_commit`/`vote_yes`/`pre_commit`/`committed`), `im_running`,
    `adopt_orphans`, `i_have_tab`, `schema_is_merged`, `disc_load_intents`, `log_mnesia_up`; active replicas drive
    where_to_read/write/commit; cookie clashes fail as OTP's. Left for later steps: participants vote no on any other
    schema op and schema changes after a merge stay local (H8); a table held only elsewhere is not read and its writes
    reach no node (H6 done, H8); a replica loaded elsewhere starts empty at a restart (H9); a disc schema's merge logs ops
    but no disc-node rules (H10); the merge asks only the first node to merge for its definitions per transaction.
  - **MIM1H6 done** (`21-finished.md`): dirty reads of a table held elsewhere through the holder's `mnesia_rpc`
    (OTP's `mnesia_lib`, `mnesia` and `mnesia_index` applies, chunk continuations repaired on the holder), dirty
    writes as `sync_dirty`/`async_dirty` commits to its `mnesia_tm`. Left for later steps: transactions on such tables
    (H7, H8); replicated tables (both nodes holding one) are not created over nodes yet (H8, H9), so `async_dirty`
    commits to other replicas are sent and received but not tested; snmp reads (`mnesia:dirty_rpc` of
    `clause_mnesia_snmp`) reach only Clause holders (H12).
  - **MIM1H7 done** (`21-finished.md`): locks on the lock managers of the nodes keeping them in OTP's raw form,
    grants carrying records, `ix_read`, waiting and refusals by tid across nodes, `simple_commit`/`release_tid`/
    `sync_release_tid` ends, `what_happened` + `do_abort` for a dead transaction, waiters restarting when a node goes
    down; `mnesia_tm:fixtable/3` answered for OTP peers (Clause walks without fixing). Left for later steps: OTP's
    `{switch, ...}` reply and unsticking replicated tables need tables held on several nodes (H8, H9); `what_happened`
    answers the asker's default until decisions are kept (H8); a transaction writing another node's table (H8).
  - **MIM1H8 done** (`21-finished.md`): commits over nodes with OTP's `sym_trans`, `sync_sym_trans` and `asym_trans`
    (data and every schema change), votes and refusals, restarts when a participant goes down before voting, aborts on
    participants when the coordinator goes, transient outcomes for `what_happened`. Left for later steps: participants
    vote no on table copy, restore, fragment and SNMP operations (H9, H11, H12); decisions are not logged in
    `DECISION_TAB.LOG` nor merged in `{decisions, ...}`, and a disc node's commit is not logged under its `#decision{}`
    (H10); OTP's `wait_for_decision`/`max_wait_for_decision` (a participant with no node to ask aborts at once); the
    coordinator dying between `pre_commit` and `committed` is checked only in scratch runs (OTP has no release test
    hook).
  - **MIM1H9 done** (`21-finished.md`): OTP's table transfer both ways (`send_table`, `{first, Size}`, chunks and
    `more_z`, `no_more`, events during the transfer, `update_where_to_write`/`block_table`/`add_active_replica`),
    copies added, deleted and moved over nodes (move as `add_table_copy` + `sync_trans` + `del_table_copy`), replicas
    loaded at start from a running node (`{active_remote, Node}`). Left for later steps: `change_table_copy_type/3` on
    another node needs disc nodes (H10); `load_order`, `no_table_loaders` and `force_load_table/1` of a replica held
    elsewhere are not applied (every pending replica loads at once); OTP's table blocks during a transfer are answered
    without blocking; disc replicas (`{no_more, Bin}` of a DCD file) come with H10; Clause's `mnesia_late_loader`
    stays idle.
  - **MIM1H10 Disc nodes** (split 2026-11-08 into a, b, c):
    - **MIM1H10a done** (`21-finished.md`): `create_schema/1` over nodes (OTP's fallback receivers both ways), disc
      nodes started together and in turn (connecting to the schema's nodes, merges of differing definitions only,
      loads decided by down records: `initial`, `ram_only`, `local_only`, waiting until `i_have_tab`), disc replica
      transfers (DCD written, dets chunks), commits over disc nodes logged under their decision with `im_certain`,
      up/down records in the log and `DECISION_TAB.LOG`, index changes refused with a disc node down. Left for later:
      OTP's `{adopt_orphan, Node}` load of a waiting replica when the other node comes up without it; `{decisions,
      ...}` of other nodes still not merged.
    - **MIM1H10b done** (`21-finished.md`): master nodes deciding loads (also set while stopped),
      `force_load_table/1`, `{no_majority, Tab}` write locks, disc copies added, moved and converted on another node,
      a stopped disc node removed and rejoined. Golden `mnesia_disc_masters`. Majority over more than two replicas
      ("more than half active") is not observed with two nodes.
    - **MIM1H10c Partitions of disc nodes:** `inconsistent_database` with `running_partitioned_network` and
      `starting_partitioned_network` between disc nodes, master nodes resolving a partition at start, adopted orphans.
      Golden `mnesia_partitions`.
  - **MIM1H11 Checkpoints, backups and fallbacks over nodes:** retainers on other nodes (`max`/`min` redundancy,
    `allow_remote`, `most_local_node`, `tm_prepare`, `collect_pending` and `{activate, Pending}`, retainers moving
    with `add_table_copy`), backups reading remote retainers (`tab_copier`/`tab_receiver`), global fallbacks
    installed on every disc node (`fallback_receiver`), `uninstall_fallback/0` everywhere, `restore/2` with
    `recreate_tables` over nodes, `{mnesia_checkpoint_deactivated, Name}` when the last retainer node goes.
    - Golden `mnesia_remote_backups`.
  - **MIM1H12 Events and fragments over nodes:** table subscriptions where the replica is elsewhere
    (`not_active_local`), `{mnesia_down, Node}` and `{mnesia_up, Node}` system events, activity events of
    distributed transactions; fragmented tables spread over a `node_pool` of OTP and Clause nodes (`frag_dist`,
    `remote_select`/`local_select`), `del_table_copy(schema, Node)` removing the node from pools
    (`mnesia_frag:remove_node/2`). Golden `mnesia_remote_frag`.
  - **Order and size:** H1 and H2 are single-node refactors and unlock the rest; H3–H5 make the nodes meet; H6–H8
    are the transaction core; H9–H12 build on it. Each step is one commit with its golden; H8 and H9 are the largest
    and may split further once their message tables are written.
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

1. MIM9's record wildcard fix, MIM6, MIM7 (MIM1A-MIM1G, MIM1H1-H9, MIM1H10a-b done 2026-11-02/08; MIM1H10c-H12 optional, in order)
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
