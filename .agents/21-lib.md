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
- **Left:** `erlang` (20 declined functions, LIB9 remainder) and the declined modules' answer stubs
  (Appendix A); OTP-free builds (LIB77–LIB78: luerl's manifests use no OTP paths since LIB76);
  applications beyond the core that emqx needs (LIB79–LIB83, Appendix B).
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

### Phase: OTP-free builds

`test-luerl-euler.toml` and `test-luerl-benchmark.toml` use no Erlang/OTP paths since LIB76: all
non-luerl modules resolve from `library/`. `test-emqx.toml` keeps only the OTP applications outside
erts/kernel/stdlib (`eunit`, `common_test`, `public_key`, `ssl`) as `applications` roots; its
preprocessing diagnostics are unchanged (the rest are emqx's missing dependencies).

- **What still ties programs to OTP** (counted over `references/emqx/apps`): parse transforms
  (`ms_transform`, `qlc`) run on the host `erl`; `.yrl`/`.xrl` sources need OTP's `yecc`/`leex`
  (luerl ships generated `luerl_parse.erl`/`luerl_scan.erl`, whose `-file` lines only name OTP's
  `yeccpre.hrl`). The public headers emqx includes (`assert.hrl`, `ms_transform.hrl`, `inet.hrl`)
  are provided since LIB75.

Steps:

- **LIB77 Parse transforms without the host (design first):** `ms_transform` and `qlc_pt` exist in
  `library/` but run on the host OTP `erl` ([transforms](../docs/transforms.md)). Decide with the
  user between running library transforms inside `clau` (needs compile-time evaluation) and keeping
  the host requirement; until then, `ets:fun2ms` users still need OTP on the build host.
- **LIB78 (optional, beyond erts/kernel/stdlib) `parsetools`:** `yecc` and `leex` with
  `yeccpre.hrl`/`leexinc.hrl` equivalents, so projects that ship only `.yrl`/`.xrl` build without
  OTP. Generated parsers are clause-heavy: generate C++ tables or keep generated Erlang small (see
  the IMPORTANT note above).

#### Applications beyond erts/kernel/stdlib (survey 2026-10-26)

What `test-emqx.toml` still takes from OTP. Documented API from the OTP 29 doc chunks of the host's
`crypto-5.10`, `public_key-1.21.7`, `ssl-11.7.7`, `eunit-2.11`, `common_test-1.31.2` (full lists in
Appendix B; re-observe them into `documented.terms` the way `observe_docs.escript` does for the
three core applications). Call counts are over `references/emqx/apps`, non-test sources unless
noted. The same rules apply as for the core: whole module or nothing in `library/`, OTP's terms and
records, goldens from OTP, own headers written from the documented records and macros.

- **LIB79 `crypto` (75 functions in one module, +4 deprecated; design agreed 2026-10-26):** emqx
  uses `hash/2` (28), `strong_rand_bytes/1` (20), `pbkdf2_hmac/5`, `hash_equals/2`, `mac/3,4`,
  `hash_init/update/final`, `exor/2`, `verify/5`, `supports/1`.
  - **OpenSSL:** pinned 4.1.0 (`cmake/OpenSSLDependencies.cmake`); any OpenSSL >= 3.1.2 (latest
    FIPS-validated provider) also works: the crypto C++ uses only API present since 3.1.2 and no
    compile-time version branches (runtime `OpenSSL_version_num()` checks), so one archive links
    with any such libcrypto. Order: `CLAUSE_OPENSSL_ROOT` (an installed prefix or a source tree,
    told apart by `include/openssl/opensslv.h` vs `Configure`), an installed copy
    (`CLAUSE_OPENSSL_SYSTEM`, default ON except Windows), else the pinned source built at configure
    time into `thirdparty/` (one install per architecture and CRT). Options: `no-shared no-module
    no-apps no-tests no-docs no-makedepend enable-ec_explicit_curves` (`no-module` puts the legacy
    provider into libcrypto; it is loaded as OTP loads it). Windows builds need native Perl and
    NASM: pinned Strawberry Perl portable and NASM zips into `thirdparty/tools/` (Git's MSYS perl
    breaks Configure's paths), then `nmake`; POSIX hosts `./Configure` and `make -j`.
  - **Separate runtime archive `clause_runtime_crypto`** (+ `clause_runtime_crypto.link` listing
    libcrypto and system libraries) beside `clause_runtime`, exported as `Clause::crypto` for CMake
    consumers. `clau` links it when the batch contains the library `crypto` module or with
    `--with-crypto[=PATH]` (also adds `crypto` to the batch, for dynamic callers such as
    `beam_lib`). PATH is an OpenSSL >= 3.1.2 install prefix or source tree (a source tree is
    configured and built once into clau's build cache with Perl/NASM from PATH or the build's
    `thirdparty/tools`); without PATH the `CLAUSE_OPENSSL_ROOT` environment variable, else the
    libcrypto the build recorded in `clause_runtime_crypto.link`. The startup descriptor lists
    builtin extensions: codegen emits a pointer to `CLAUSE_crypto_extension_v1`
    (runtime_symbols.hpp), so the linker pulls the archive and the runtime registers its families.
    Programs without crypto are unchanged. Configure option `CLAUSE_RUNTIME_CRYPTO` (ON) builds it.
  - **Probing:** one table (OTP name, kind, OpenSSL fetch name, flags, OTP's order); probed once per
    program on the first crypto call (`std::call_once`, as OTP probes at NIF load). `supports/0,1`,
    `hash_info`, `cipher_info`, `ec_curves`, `info`, `info_lib` read it: programs see what the
    linked OpenSSL provides.
  - **Runtime (`runtime/src/builtins/crypto/`):** `openssl.hpp` (RAII, errors), `algorithms.cpp`,
    `hash.cpp`, `mac.cpp` (+ pbkdf2, hash_equals), `cipher.cpp`, `aead.cpp`, `pkey.cpp`, `kem.cpp`,
    `curves.cpp` (`ec_curve/1` from OpenSSL's group data), `math.cpp`, `rand.cpp`. States are handle
    references with finalizers; long inputs run in portions. Errors keep OTP's
    `{badarg, {File, Line}, Description}` shape and descriptions with Clause's own file and line
    (`differences.md` row).
  - **Erlang (`library/crypto/`, new application):** `crypto.erl` wrappers, `crypto.terms`,
    `crypto.app` (`application:start(crypto)`), headers if documented. Refused: `engine_*` (notsup)
    as an OTP built without engines; FIPS (`info_fips/0` gives `not_supported`). `rand_seed*` plug
    into `rand`'s plugin handler.
  - **Tests:** goldens from OTP 29.1.1 (OpenSSL 3.5.5) for deterministic vectors and error terms;
    probe lists checked as a required core subset plus "every listed algorithm works", `info_lib`
    masked.
  - **Steps:** LIB79A, LIB79B, LIB79F done (`00-finished.md`; FIPS and OpenSSL in `docs/crypto.md`). LIB79C public keys
    (`sign`/`verify`, `generate_key`, `compute_key`, RSA encrypt/decrypt, `privkey_to_pubkey`),
    `ec_curve(s)`, ML-KEM, SRP; then crypto leaves `PARTIAL` in `library_modules.py`.
- **LIB80 `public_key` (54 functions, after LIB79):** PEM, DER codec of the PKIX/PKCS ASN.1 types
  into OTP's record tuples (`'Certificate'`, `'OTPCertificate'`, `'RSAPrivateKey'`,
  `'ECPrivateKey'`, ~250 records of `public_key.hrl` and its four included headers), signing,
  verification, path validation, CRLs, OCSP, hostname checks, `cacerts_*` (OS trust stores). OTP
  generates 30 ASN.1 modules with `asn1ct` and decodes through `asn1rt_nif` (the `asn1` application
  has no documented functions): the codec is C++ over type tables generated once from the ASN.1
  specifications (clause-heavy, see the IMPORTANT note), crypto through LIB79. emqx uses
  `pem_decode/1` (14), `der_decode/2`, `der_encode/2`, `pem_entry_decode/1,2`, `pem_encode/1`,
  `pkix_decode_cert/2`, `pkix_sign/2`, `generate_key/1`, `verify/4`,
  `pkix_verify_hostname_match_fun/1`.
- **LIB81 `ssl` (design first, after LIB80; `ssl` 52 functions, `ssl_crl_cache` 3, callback-only
  `ssl_crl_cache_api`, `ssl_session_cache_api`):** OTP implements TLS 1.0–1.3 and DTLS in Erlang
  (`gen_statem` per connection, `ssl_app` supervision tree, PEM and session caches). Decide between
  Clause's own Erlang protocol implementation over LIB79/LIB80 (OTP's options, alerts, errors and
  `connection_information/1` items) and a C++ connection over OpenSSL's libssl behind the same API
  (smaller, but option and alert coverage would differ: `differences.md` rows). Options are the bulk
  of the API (`tls_client_option()`, `tls_server_option()` types). emqx's `emqx_ocsp_cache` and
  `emqx_ssl_crl_cache` include OTP-internal `ssl/src/ssl_handshake.hrl` and `ssl_internal.hrl`: not
  public, not provided (projects keep OTP's `ssl` application root for them).
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

1. The declined modules' answer stubs.
2. LIB77 after a design decision; LIB78 only on request.
3. LIB79 → LIB80 → LIB81 (each after its design decision).
4. LIB82, LIB83 last (test support).

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
- `peer` (stdlib, 11): call/4,5, cast/4, get_state/1, random_name/0,1, send/3, start/1,
  start_link/0,1, stop/1
- `pool` (stdlib, 8): attach/1, get_node/0, get_nodes/0, pspawn/3, pspawn_link/3, start/1,2, stop/0
- `slave` (stdlib, 0): none; deprecated: pseudo/1,2, relay/1, start/1,2,3, start_link/1,2,3, stop/1

## Appendix B: documented API of the applications beyond the core (OTP 29 doc chunks)

Observed 2026-10-26 on the host's OTP 29 (`code:get_doc/1`, not hidden, not deprecated), for LIB79–
LIB83. Format: `module` (functions): name/arities.

- `crypto` (crypto-5.10, 75, +4 deprecated): bytes_to_integer/1, cipher_info/1, compute_key/4,
  crypto_final/1, crypto_get_data/1, crypto_init/3,4, crypto_one_time/4,5,
  crypto_one_time_aead/4,6,7, crypto_one_time_aead_init/4, crypto_update/2, decapsulate_key/3,
  ec_curve/1, ec_curves/0, encapsulate_key/2, engine_add/1, engine_by_id/1,
  engine_ctrl_cmd_string/3,4, engine_get_all_methods/0, engine_get_id/1, engine_get_name/1,
  engine_list/0, engine_load/3, engine_register/2, engine_remove/1, engine_unload/1,
  engine_unregister/2, ensure_engine_loaded/2, exor/2, generate_key/2,3, hash/2, hash_equals/2,
  hash_final/1, hash_info/1, hash_init/1, hash_update/2, hash_xof/3, info/0, info_fips/0,
  info_lib/0, mac/3,4, macN/4,5, mac_final/1, mac_finalN/2, mac_init/2,3, mac_update/2, mod_pow/3,
  pbkdf2_hmac/5, private_decrypt/4, private_encrypt/4, privkey_to_pubkey/2, public_decrypt/4,
  public_encrypt/4, rand_seed/0,1, rand_seed_alg/1,2, rand_seed_alg_s/1,2, rand_seed_s/0, sign/4,5,
  strong_rand_bytes/1, strong_rand_range/1, supports/1, verify/5,6
- `public_key` (public_key-1.21.7, 54): cacerts_clear/0, cacerts_get/0, cacerts_load/0,1,
  compute_key/2,3, decrypt_private/2,3, decrypt_public/2,3, der_decode/2, der_encode/2,
  dh_gex_group/4, encrypt_private/2,3, encrypt_public/2,3, generate_key/1, pem_decode/1,
  pem_encode/1, pem_entry_decode/1,2, pem_entry_encode/2,3, pkix_crl_issuer/1, pkix_crl_verify/2,
  pkix_crls_validate/3, pkix_decode_cert/2, pkix_dist_point/1, pkix_dist_points/1, pkix_encode/3,
  pkix_hash_type/1, pkix_is_fixed_dh_cert/1, pkix_is_issuer/2, pkix_is_self_signed/1,
  pkix_issuer_id/2, pkix_match_dist_point/2, pkix_normalize_name/1, pkix_ocsp_validate/5,
  pkix_path_validation/3, pkix_sign/2, pkix_sign_types/1, pkix_subject_id/1, pkix_test_data/1,
  pkix_test_root_cert/2, pkix_verify/2, pkix_verify_hostname/2,3, pkix_verify_hostname_match_fun/1,
  short_name_hash/1, sign/3,4, verify/4,5. Headers: `public_key.hrl` with `OTP-PUB-KEY.hrl`,
  `PKCS-FRAME.hrl`, `PKIXCMP.hrl`, `PKIXCRMF.hrl`. `asn1-5.5.2`: no documented functions
  (`asn1rt_nif` only).
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
