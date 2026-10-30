# Plan 21 completed work (from 2026-10-17)

Compact record of the library plan, one bullet per step;
[the index](00-finished.md) lists the other plans.

Clause's own erts, kernel and stdlib; open steps are in [21-lib.md](21-lib.md).

- **Status 2026-10-25:** 106 of 120 documented modules complete (all functions, or the rest
  declined): data, front-end, process/behaviour, file, kernel I/O, `ets`/`dets` and tracing modules.
- **qlc chain** (user direction 2026-10-19): LIB30, LIB31, LIB54A, LIB32, LIB34A, LIB73 with LIB12C
  in order; `erl_scan`/`erl_parse` got their own C++ scanner and parser in the runtime.
- **luerl tracing method** (gaps fixed in plan 11 steps 41T–41ZC): `CLAUSE_CALL_TRACE=FILE` and
  its OTP twin `tools/luerl-trace/ctrace.erl` find undocumented functions OTP-sourced modules reach.

- **LIB0** `library/documented.terms`: 2,888 documented functions observed from OTP's doc chunks;
  inventory and coverage checks use it — `e3e44460`
- **LIB1** `ordsets`, `orddict`, `proplists`: list walks as C++ builtins mirroring OTP's clauses,
  fun-taking functions Erlang — `04329156`
- **LIB2** `dict`, `sets`: OTP's linear hash records and version 2 maps; `phash/2`, `phash2/1,2`
  now give OTP's values — `162eab2f`
- **LIB3** `gb_trees`, `gb_sets`, `queue`: OTP's shapes from a shared balanced tree engine —
  `66e0ab0c`
- **LIB3B** `array`: OTP 29's cached tuple tree record in C++, folds in Erlang — `5f197abb`
- **LIB5A** `base64`: C++ encoders and OTP's strict and mime decoders, errors included — `547ae828`
- **LIB5B** `calendar`: Gregorian arithmetic in C++, local time and RFC 3339 Erlang — `a70297a6`
- **LIB5C** `filename`: Erlang walks by OTP's Unix and Windows rules; golden checks both —
  `fa6d4a3c`
- **LIB5D** `uri_string`: Erlang walker over RFC 3986 with OTP's error fragments — `f91bbcb6`
- **LIB5E** `json`: C++ `decode/1` and escapers; encoders, formatters and callback decoders
  Erlang — `28f84686`
- **LIB6** `rand`, `random`: OTP's algorithms in C++, plug-in handlers Erlang; constants observed
  from OTP — `a543519e`
- **LIB5F** `json` default `encode/1`, `format/1,2` in C++ with OTP's iodata — `084ed9f9`
- **LIB5G** `filename` in C++ — `19896937`; `uri_string` in C++ — `17cf492d`
- **LIB4A** `string`: every function in C++ over a new `unicode` code-point and normalisation
  layer; `strings` golden — `fc6022ce`
- **LIB4B** `unicode` forms and properties, `unicode_util` walks as builtins; `unicode_forms`
  golden — `c25f8153`
- **LIB4C** `io_lib` in C++: writers, checks and the format engine in OTP's shapes; `io:format`
  runs on it — `d1c64893`
- **LIB4D** `io_lib_pretty` in C++: `print`, `~p`/`~P`, limited `write`, crash reports in OTP's
  shapes; `pretty_print` golden — `c8d01d0d`
- **LIB4E** `io_lib:fread/2,3` in C++ and `io:fread/2,3`; `fread_scanning` golden — `6bc5b45f`
- **LIB8A** `erl_internal` and `erl_anno` in C++ with OTP's tables and errors; `compiler_tables`
  golden — `6c9447df`
- **LIB8B** `records` and `erl_features` (ABI 24): record definitions folded at compile time and
  reflected at run time; `records_reflection` golden — `3847ccb6`
- **LIB9A** `erlang` gaps: checksums, tuple edits, cookies, option-taking sends, node-naming
  spawns, `process_flag/3`, `system_flag/2`, `hibernate/0`; `erlang_gaps` golden — `d24cdb75`
- **LIB9C** `decode_packet/3` in C++ including OTP's http line parser; `packet_decoding` golden —
  `e4f1081c`
- **LIB9B** `suspend_process`, `resume_process` and `process_display/2`; `suspension` golden —
  `e1d597d2`
- **LIB7B** `graph`: queries and traversals in C++, updates in Erlang; `graph_module` golden —
  `424e0a2a`
- **LIB7A** `sofs`: typed sets, relations and families in C++, fun-taking functions in Erlang;
  `sofs_sets` golden — `67ee4fe9`
- **LIB10A–E** `ets` in C++ (own hash index, ordered trees, 64 locked parts, owners and heirs);
  match specs and files followed later; five `ets_*` goldens — `8a734394`
- **LIB4F** `maps_order` comparison funs in `io:format` writes; `io_lib_text` golden — `595c9574`
- **LIB11** match specifications with OTP's checks, ets `select*`/`match*` family and
  `erlang:match_spec_test/3`; `ets_match_specs` golden — `2998c62e`
- **LIB12A** `digraph`, `digraph_utils` over OTP's three ETS tables, C++ path searches;
  `digraph_tables` golden — `663405e0`
- **LIB12B** `ets:tab2file/2,3`, `file2tab/1,2`, `tabfile_info/1` in OTP's file format;
  `ets_table_files` golden — `4bf10136`
- **LIB10F** ETS big operations run in portions, owner-end frees outside the executor lock;
  `ets_portions` golden, Linux TSan clean — `d4d63f2c`
- **LIB72** `ms_transform` in Erlang over the abstract format; `ms_transform_forms` golden —
  `81589524`
- **LIB53** `dets` with files byte for byte as OTP writes them: a C++ engine under OTP's server
  in Erlang; `ets:to_dets/2`, `from_dets/2`; `dets_tables` golden compares file MD5s with OTP
- **LIB30** `erl_scan` (first step of the qlc chain): a runtime scanner clause for clause with
  OTP's; `erl_scan_tokens` golden
- **LIB31** `erl_parse` (second step of the qlc chain): a C++ parser of OTP's grammar and abstract
  format; `erl_parse_forms` golden, identical to OTP over 765 OTP sources
- **LIB22** `timer` without a timer server, `erlang:apply/2,3` builtins; `timers` golden —
  `b0562967`
- **LIB25** `pg`, `global`, `global_group`, `rpc`/`erpc` on one node, `net_adm`/`net_kernel`/`auth`
  non-alive answers, `seq_trace`; `process_groups`, `rpcs` goldens — `9ae39f56`
- **LIB24** `application`, `application_controller`; clau compiles `.app`/`.app.src` resources;
  `applications` golden, `project_applications` test — `5945f7f3`
- **LIB23** `logger` family, `error_logger`, `error_handler`, `kernel/include/logger.hrl`;
  `loggers` golden — `f2895564`
- **LIB20, LIB21** `proc_lib`, `sys`, `gen`, `gen_server`, `gen_statem`, `gen_event`, `gen_fsm`,
  `supervisor`, `supervisor_bridge` in Erlang with OTP's messages and reports; six goldens —
  `37a9c223`
- **LIB32** `erl_eval` (third step of the qlc chain): an Erlang interpreter of the abstract format
  with OTP's errors and bindings; `erl_eval_exprs` golden — `428ac91b`
- **LIB33** readers: `io:read/1..4`, `file:consult/1`, `eval`, `script`, `path_*` decode by coding
  comment with OTP's errors; `file_readers` golden — `0a1a0679`
- **LIB34 part 1** `erl_error`: `format_exception/3,4` in Erlang over `io_lib`; `erl_error_formats`
  golden — `04a5844e`
- **LIB34 part 2** `erl_pp`: OTP's layout algorithm; `erl_pp_forms` golden, no differences over
  357 OTP sources — `a8d88410`
- **LIB34 part 3** `erl_expand_records:module/2` in Erlang; golden, no differences over 396 OTP
  sources — `f07983e8`
- **LIB34 part 4** `epp` in Erlang; `file` servers monitor their opener as OTP; `epp_files`
  golden, no differences over 1,110 calls — `4a3e5b7b`
- **LIB34C** C++ cores for the clause-heavy front-end modules (user direction 2026-10-24) to cut
  compile time of programs linking them; six parts below, goldens unchanged
- **LIB34C part 1** `erl_expand_records:module/2` and guard tests in C++; compile cost over a
  hello-world +1.2 s -> +0.00 s; OTP pin refreshed to `9710673e` — `c54f3cf4`
- **LIB34C part 2** `erl_lint` (last missing front-end module) in C++; `erl_lint_checks` golden,
  no differences from OTP over 481 sources and 4782 diagnostics — `5312fc45`
- **LIB34C part 3** `erl_pp` printing as a C++ builtin, identical to OTP over 347 sources;
  compile cost +0.77 s -> +0.00 s — `4c615b56`
- **LIB34C part 4** `epp` without `erl_eval`: `-if` conditions in C++ and strict liveness for
  `epp`, `file`, `erl_features`; compile cost +2.77 s -> +1.72 s (`-O0`) — `1b9150c1`
- **LIB34C part 6** `erl_error` compiled with strict liveness; compile 420 ms -> 293 ms
  (`-O0`) — `976360be`
- **LIB34C part 5** `erl_eval` pattern matching in C++; results equal the Erlang version over
  1,605 expression lists, compile about 12% faster — `3545d448`
- **LIB50** `file` complete: positioned I/O, `truncate`, `advise`, `allocate`, `sendfile`,
  `format_error`; `file_positioned` golden — `848d8dce`
- **LIB51** `filelib` complete: wildcards in C++, walks and queries in Erlang; `filelib_ops`
  golden — `0233d4d9`
- **LIB50C** `file` `compressed` modes, CRLF reads as LF; `file_compressed` golden — `d76b6c4d`
- **LIB54A** `file_sorter` complete (first step of the qlc chain): exact port of OTP's runs and
  merges; `file_sorter_ops` golden — `9046d2d4`
- **LIB52** `disk_log`, `wrap_log_reader`, `logger_disk_log_h` with OTP's file formats byte for
  byte; `disk_log_ops` golden — `c9f38431`
- **LIB54B** `zip` complete: OTP's archive layout byte for byte, memory and disk archives;
  `zip_ops` golden — `8a060c8b`
- **LIB54C** `file` `ram` mode answering as OTP's ram file driver; `file_ram` golden — `d390f609`
- **LIB54D** `erl_tar` complete: USTAR, pax, GNU and v7 formats byte for byte; `tar_ops` golden —
  `1a2e2cce`
- **LIB54E** `zstd` complete over libzstd 1.5.7, `file`'s `zstd` layer; `zstd_ops` golden —
  `c67bf634`
- **LIB54F** `beam_lib` complete; `beam_ops` golden — `c5b5ba04`
- **LIB55** `escript` and `win32reg` (over a Windows registry port driver); `escript_ops`,
  `win32reg_ops` goldens — `04c0bbc8`
- **LIB60A** sockets and names: socket options, counters, `inet` info and `getopts`, connected
  UDP, `net` complete; `inet_socket_options`, `net_names` goldens — `4e23e01b`
- **LIB61** `os` and `code` complete: `os:cmd/2`, `find_executable`, code path functions;
  `code_paths`, `os_commands` goldens — `97dfec2f`
- **LIB63** `io` complete and `io_ansi`: stdin server options, encodings, terminal queries;
  `io_options`, `io_ansi_codes` goldens — `a12b29c8`
- **LIB60B** `inet_res` complete: OTP's DNS client in Erlang with a C++ DNS codec;
  `inet_res_queries` golden against a loopback DNS server — `64f30bc9`
- **LIB62** `socket` and `gen_sctp`: socket in Erlang over driver ports with OTP's options and
  errors, gen_sctp without SCTP; `socket_ops` golden — `eb51924e`
- **LIB70** `argparse` complete with C++ validation, parser and help; a 9-module program compiles
  in 1.62 s instead of 2.6 s; `argparse_ops` golden — `02866a06`
- **LIB71** tracing not supported: tracing functions raise `notsup`; `tracing_unsupported` golden
  — `20759caf`
- **LIB73 part 1** `qlc_pt` in C++; identical to OTP over 562 `qlc_SUITE` queries and 1686 module
  transforms; `qlc_pt_transforms` golden — `63592558`
- **LIB73 part 2** `qlc` complete: Clause's own `qlc.erl`, `ets:table/1,2`, `dets:table/1,2`,
  `qlc.hrl`; `qlc_queries` golden (~200 checks, identical to OTP) — `c03e883d`
- **LIB74A** interactive shell: own `shell` with history, records and restricted mode; `clau
  --shell` links the whole library; `shell_session` golden — `f90c7198`
- **LIB74B** `edlin_expand:expand/1,2` in Erlang for modules, functions, keywords, records and
  file paths; `shell_expand` golden — `15957f84`
- **LIB74C** `shell_docs` complete; identical to OTP over 10,643 markdown texts and 51,249
  renderings; `shell_docs_render` golden — `91d3f04d`
- **LIB75** OTP-free probe: luerl builds from the library alone with identical euler output, 37 MB
  instead of 85 MB; public headers `assert.hrl`, `ms_transform.hrl`, `inet.hrl` — `4f2d7449`
- **LIB76** OTP-free manifests: luerl builds from the library alone (97 modules instead of 174),
  `test-emqx.toml` drops OTP include dirs — `85fbfdbd`
- **LIB79A** `crypto` foundation over a pinned OpenSSL 4.1.0 in a separate archive linked only
  into programs that use it; hashes, MACs, PBKDF2, rand plugins; `crypto_digests` golden —
  `dda50d1e`
- **LIB79B** `crypto` ciphers and AEAD (AES, GCM, CCM, ChaCha20-Poly1305); `crypto_ciphers`
  golden — `2bc4228e`
- **LIB79F** `crypto` FIPS switchable as OTP; `clau --fips=PATH` pins a program to a user-supplied
  provider; `docs/libraries/crypto.md` — `0a0f9917`
- **LIB79C** `crypto` public keys, crypto complete: signatures, key generation and agreement, SRP,
  ML-KEM; `crypto_pubkey` golden (83 curves, 87 error cases) identical to OTP — `3777dc40`
- **LIB80A** ASN.1 compiler and BER/DER codec: clau compiles `.asn`/`.asn1` sources into
  asn1ct-identical modules; `asn1_ber`/`asn1_der` goldens — `69e27290`
- **LIB84** project `.asn` sources complete: PER/UPER engine; all emqx specs identical to asn1ct;
  `asn1_per`/`asn1_uper` goldens — `1e02283e`
- **LIB80B1** public_key specifications: 33 ASN.1 specs generating modules identical to OTP's, own
  `public_key.hrl`; `public_key_records`, `public_key_asn1` goldens — `6d3cf07a`
- **LIB80B2** public_key codec: DER/PEM, PKCS #8 keys, PEM encryption, certificates;
  `public_key_codec` golden identical to OTP — `c25240e3`
- **LIB80C** public_key keys and signatures over crypto; `public_key_keys` golden identical to
  OTP — `1db190b1`
- **LIB80D1** public_key certificate utilities and hostname verification; `public_key_certs`
  golden identical to OTP — `3b18c228`
- **LIB80D2** public_key path validation (RFC 5280); `public_key_path` golden identical to OTP —
  `1a5c6897`
- **LIB80D3** public_key CRL validation; `public_key_crls` golden identical to OTP — `1a5c6897`
- **LIB80D4** public_key OCSP validation; `public_key_ocsp` golden identical to OTP — `20a3d8bd`
- **LIB80D5** public_key test data generation; `public_key_test_data` golden — `93b46f75`
- **LIB80D6** public_key trust stores and `os_cacerts/0`; public_key no longer PARTIAL;
  `public_key_cacerts` golden identical to OTP — `294c4eee`
- **LIB80E1** public_key PEM text and PBE tables in C++, identical to OTP — `c63ce619`
- **LIB80E2** public_key otp form walk in C++; long user notice fix; `public_key_codec` golden
  extended — `76373830`
- **LIB80E3** public_key name functions in C++, identical to OTP — `51dddcc9`
- **LIB80E4** public_key policy tree in C++; all 211 PKITS path validations identical to OTP —
  `bde5daaa`
- **LIB80E5** public_key PBKDF1 rounds in C++ (2048 rounds 15.9 -> 0.84 ms Release) — `b8282d39`
- **LIB80E2/E3 reverted**: otp form walk and name functions back to Erlang (C++ was slower); PEM,
  policy tree and PBKDF1 ports kept — `3a3bed22`
- **LIB81A** ssl application and utilities (PARTIAL): suite tables, `format_error/1`, `ssl_sup`;
  `ssl_suites` golden identical to OTP — `e16028cf`
- **LIB81B** ssl engine over libssl in a separate archive; `ssl_engine` golden — `448a0c12`
- **LIB81C1** ssl connections: connect, listen, accept, handshake, send/recv, options with OTP's
  errors; `ssl_connections` golden identical to OTP — `ffee110f`
- **LIB81C2** ssl hello pause (`handshake_continue/2,3`, `handshake_cancel/1`), `close/2`,
  `shutdown/2`; golden identical — `41f771da`
- **LIB81D1** ssl renegotiation, `update_keys/2`, key export, `prf/5`, `keep_secrets`;
  `ssl_features` golden identical — `56470c50`
- **LIB81D2a** ssl TLS 1.2 session reuse; `ssl_sessions` golden identical — `f60504c8`
- **LIB81D2b** ssl TLS 1.3 session tickets; `ssl_tickets` golden identical — `383c1587`
- **LIB81D2c** ssl TLS 1.3 early data; `ssl_early_data` golden identical — `8647cad4`
- **LIB81D3a** ssl `sni_hosts`/`sni_fun`; `ssl_sni` golden identical — `c9ef88ab`
- **LIB81D3b** ssl automatic rekeys and custom `cb_info` transports; `ssl_rekeys`,
  `ssl_transports` goldens identical — `7ced7bf1`
- **LIB81D4a** ssl CRL checks and `ssl_crl_cache`; `ssl_crls` golden identical — `65202f81`
- **LIB81D5** ssl TLS 1.2 PSK; `ssl` complete for its documented functions; `ssl_psk` golden
  identical — `190eb0e9`
- **LIB77A** transform host: a loader linked with the whole library, built once and cached; runs
  library-only transform chains; qlc_queries transform 220 s -> 12.7 s — `e926cf97`
- **LIB85** library catalog compiled into `clau` as an X-macro list instead of read from disk —
  `05e7b1e0`
- **LIB77B** module interpreter `clause_interp`: modules from forms run as `erl_eval` funs behind
  the code server; `interpreted_modules` golden identical to OTP — `e081d8ab`
- **LIB77C** transform sources: `clau` sends project transform sources to the host; without erl the
  host interprets chains of sources and `.beam`s — `a8bbac69`
- **LIB77D** switch-over: transforms run in the transform host by default; emqx fun2ms/qlc users
  transform with `--erl none` — `0312b3f9`

- **LIB77E1** `compile` compiles: `file`/`forms` run OTP's front passes and return interpreter
  images; `compile_forms` golden identical to OTP — `3f5b6a34`

- **LIB77E2** `erl_syntax`: all 303 functions as C++ builtins; `erl_syntax_trees` golden identical
  to OTP — `9a92f0e7`

- **LIB77E3a** `prettypr`: Erlang constructors and a C++ `best/3`; `prettypr_layout` golden
  identical to OTP — `3f412427`
- **LIB77E3b** `erl_prettypr` in Erlang; `erl_prettypr_format` golden identical to OTP — `d7f20eb9`
- **LIB77E4a** `erl_syntax_lib`: folds in Erlang, analyses in C++; `syntax_tools_lib` golden
  identical to OTP — `5801ca75`
- **LIB77E4b** `erl_comment_scan` and `erl_recomment` in C++; `syntax_tools_comments` golden
  identical to OTP — `7c6c356a`
- **LIB77E4c** `epp_dodger`: Erlang reading, C++ token and form rewriting; `epp_dodger_forms`
  golden identical to OTP — `060e939e`
- **LIB77E5** `merl` with C++ templates, `merl.hrl`, `merl_transform`; `syntax_tools` complete;
  `merl_templates` golden identical to OTP — `c674dcba`
