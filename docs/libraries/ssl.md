# ssl

Clause's `ssl` application (plan 21, LIB81) exports the OTP 29 interface of the `ssl` module. The
utility functions are implemented (LIB81A), and so are TLS 1.2 and 1.3 [connections](#connections)
(LIB81C1) over the libssl [engine](#engine) (LIB81B), with the `{handshake, hello}` pause, `close/2`,
`shutdown/2` and `tls_version/1` (LIB81C2), and [renegotiation, key updates, exporters](#renegotiation-key-updates-and-exporters)
and [secrets](#secrets) (LIB81D1). Session reuse, SNI, CRLs, OCSP, PSK and SRP (the rest of LIB81D) and
`ssl_crl_cache` follow. See the
[function inventory](../../library/ssl/ssl.terms) and
[differences](../differences.md#ssl).

## Design

Each TLS connection is an Erlang process with OTP's semantics: an owner,
`controlling_process/2`, active and passive modes, the `{ssl, Socket, Data}`, `{ssl_closed, Socket}`
and `{ssl_error, Socket, Reason}` messages, and OTP's option validation, error terms and alerts.
OpenSSL's libssl, linked from the same OpenSSL as [crypto](crypto.md), runs the record layer and
the handshake over memory buffers. The process moves bytes between libssl and the transport of the
`cb_info` option, `gen_tcp` by default. Certificates are verified as OTP verifies them: libssl
pauses the handshake in its verify callback, and the process runs
`public_key:pkix_path_validation/3` with the `verify_fun`, `partial_chain` and CRL options. TLS 1.2
and 1.3 come first; TLS 1.0, 1.1 and DTLS are optional (LIB81E).

## Connections

- **Options** (`clause_ssl_options`): `connect`, `listen` and `handshake` split their list as OTP does
  into ssl options, the socket options ssl emulates (`mode`, `packet`, `packet_size`, `header`,
  `active`, `line_delimiter`) and the transport's own. The ssl options are checked in OTP's order
  with OTP's errors: `{options, {Key, Value}}`, `{options, incompatible, [...]}` (a client with
  `verify_peer` and no `cacerts`), `{option, server_only | client_only, Key}`,
  `{options, missing_version, ...}`, `{options, {option_not_a_key_value_tuple, X}}`, and
  `{error, nooptions}` for `listen(Port, [])`. When an option repeats, the last one wins. A client's
  `server_name_indication` defaults to the host name it connects to. `ciphers` takes suite maps,
  tuples, codes, names or an OpenSSL string and keeps the suites its versions take. Certificate
  files are read when a server listens or a client has connected; their errors are
  `{options, {certfile, {File, Reason}}}`. Options the checks do not cover are kept as given.
- **Processes** (`clause_ssl_connection`): one `gen_server` per connection under `tls_connection_sup`
  owns the transport socket (in `{active, once}` only while data is wanted) and the engine. The
  `sslsocket` record keeps OTP's fields. `connect` runs the handshake before returning.
  `transport_accept` returns a server connection over the listening socket's engine context,
  and `handshake/1,2,3` runs it, with a context of its own when handshake options change the
  configuration. `connect/2,3` and `handshake/2,3` upgrade a connected `gen_tcp` socket, whose
  emulated options carry over.
- **Data**: `send` frames packets of type 1, 2 and 4. Passive `recv` takes an exact length in raw
  mode, `einval` in an active mode and `timeout` after its timeout. Active modes deliver
  `{ssl, S, Data}` as each packet completes (`erlang:decode_packet/3` for packet types). `once`
  and counts turn passive, a count with `{ssl_passive, S}`, and `{active, N}` adds to a count.
  `header` returns the first bytes of a binary as a list.
- **Hello pause** (`clause_ssl_hello`): with `{handshake, hello}` the connection holds the peer's first bytes
  until its hello message (a ClientHello to a server, a ServerHello to a client) is complete, before libssl sees
  them. The handshake then returns `{ok, SslSocket, Extensions}`, the hello's extensions as OTP decodes them
  ([builtins](../builtins.md#ssl)). `handshake_continue/2,3` feeds the held bytes to libssl under the updated
  options: a server gets a new engine when they change, a client's engine moves to a new context for its
  certificate and verification. A refused option returns its error and ends the handshake with an
  `internal_error` alert; `handshake_cancel/1` sends `user_canceled`. Both alerts go out as plaintext records,
  since no keys are in use yet. The peer's extensions are libssl's when the peer is a Clause connection
  ([differences](../differences.md#ssl)).
- **Reading ahead**: a passive connection reads from the transport while its buffer is empty, as OTP's does, so it
  answers renegotiations and key updates of an idle peer. Like OTP's, it acts on the peer's close_notify only once it
  is read from or turns active.
- **Closing**: `close/1` sends close_notify and closes the transport; the owner's exit does the same.
  `close/2` with a timeout closes the same way. `close(S, {NewOwner, Timeout})` sends close_notify, waits for the
  peer's, and hands the transport socket to `NewOwner` in passive binary mode as `{ok, Socket}`, or
  `{ok, Socket, Data}` with the bytes that came after the peer's close_notify; `{error, timeout}` when it does not
  come in time. A passive connection that already has the peer's close_notify hands the socket over at once. `shutdown(S, read | write)` shuts the transport down without close_notify, as OTP's does, and
  `read_write` sends close_notify first; a listening socket gives `{error, enotconn}`. A handshake whose peer
  closes the transport after an alert libssl ignored (TLS 1.3's `user_canceled`) fails with that alert.
  When the peer closes, the connection answers its close_notify; an active owner then gets
  `{ssl_closed, S}`, or `{ssl_error, S, {tls_alert, ...}}` after an alert, and a passive reader
  gets `{error, closed}` once the buffered data is read. A finished connection answers every call
  with `{error, closed}`.
- **Information**: `peername`, `sockname` and `getstat` ask the transport. `getopts` lists the
  transport's options and then the emulated ones, the last asked first, as OTP's does.
  `connection_information/1,2` gives the negotiated items (a server also gives `sni_hostname`) and the main
  options; `connection_information/2` lists them in OTP's order and adds the [secrets](#secrets) asked for. `peercert` gives the peer's certificate,
  and `negotiated_protocol` gives the ALPN protocol or `{error, protocol_not_negotiated}`. The internal
  `tls_version/1` maps a version tuple to its TLS version, a DTLS one to its counterpart.
- **Alerts** (`clause_ssl_alert`): a failed handshake returns `{error, {tls_alert, {Description,
  Text}}}` with OTP's description atoms and texts, and logs the alert at `notice` unless
  `log_level` is above it. The text leaves out OTP's source location
  ([differences](../differences.md#ssl)).

## Renegotiation, key updates and exporters

LIB81D1, over the engine's [feature builtins](../builtins.md#ssl-engine).

- **Renegotiation** (TLS 1.2): `renegotiate/1` starts a new full handshake, a client's with a ClientHello, a
  server's with a HelloRequest, and returns `ok` when it is done; the peer's chain is verified again.
  `{error, already_renegotiating}` while one is running, `{error, notsup}` on TLS 1.3, `{error, enotconn}` on a
  listening socket. Sends wait until the handshake lets libssl write again. A server with `client_renegotiation`
  (the default) accepts a client's renegotiation, then refuses others for 12 seconds as OTP's does, with a
  `no_renegotiation` warning; the client gets `{error, renegotiation_rejected}` (libssl then ends its connection,
  [differences](../differences.md#ssl)).
- **Key updates** (TLS 1.3): `update_keys(S, write | read_write)` sends a KeyUpdate; `read_write` asks the peer to
  update its keys too, which the peer does when it reads it. Another type gives `{error, {illegal_parameter, Type}}`
  and a listening socket `{error, not_supported}`.
- **Exporters**: `export_key_materials/4,5` run libssl's exporter (RFC 5705 for TLS 1.2, RFC 8446 for TLS 1.3, where
  `no_context` is an empty context). TLS 1.3 takes lists of labels, contexts and lengths, gives `{error, bad_input}`
  for lists of other lengths or bad contexts, and with `ConsumeSecret` (the default of `/4`) leaves no exporter for
  later calls: `{error, exporter_master_secret_already_consumed}`. TLS 1.2 exports one label, unwrapped, as OTP's
  does; other requests end the connection with `{error, closed}`. The deprecated `prf/5` exports with
  `master_secret` and `[client_random, server_random | [Context]]`, and runs TLS 1.2's PRF on its arguments under
  the negotiated suite's hash otherwise.

## Secrets

`clause_ssl_secrets` keeps the secrets of a connection whose `keep_secrets` option is not `false` (LIB81D1). libssl
logs them as NSS key log lines, which the engine queues as events; the TLS 1.3 traffic secrets after a KeyUpdate
are derived in Erlang (RFC 8446 section 7.2).

- `connection_information/2` gives `client_random`, `server_random` and the TLS 1.2 `master_secret` when one of
  them or `keylog` is asked for, as OTP's does, even without `keep_secrets`. `keylog` needs `{keep_secrets, true}`:
  OTP's NSS lines in upper-case hexadecimal, `CLIENT_RANDOM` for TLS 1.2, and for TLS 1.3 the handshake secrets (until
  the peer updates its keys) and both sides' current `CLIENT_TRAFFIC_SECRET_N` and `SERVER_TRAFFIC_SECRET_N`. With a
  keylog fun, asking for a secret raises `{case_clause, {keep_secrets, Value}}` in the caller, as OTP's does.
- `{keylog, Fun}` gets `#{items => Lines, client_random => Random}` from the connection process: the TLS 1.3
  handshake secrets together, then each traffic secret and each one after a key update, or the TLS 1.2 line after
  every handshake. A failing fun is logged as a warning.
- `{keylog_hs, Fun}` gets the TLS 1.3 secrets a handshake had when it failed with a fatal alert.

## Verification

`clause_ssl_certificate` checks a peer's chain when the handshake pauses, as OTP's ssl does. A
sent root found in `cacerts`, or the issuer of the chain's top in `cacerts` (by name and signature),
becomes the trusted certificate. A single certificate is first completed from `cacerts`.
`partial_chain` may trust a certificate of the chain instead. Then
`public_key:pkix_path_validation/3` runs with `max_path_length` from `depth` and ssl's verify fun.
That fun fails on bad certificates and checks a client's server name against the peer's
certificate (`server_name_indication`, else the host, with `customize_hostname_check`); a user
`verify_fun` sees every event, `valid_peer` replaced by the host name failure. Failures become
OTP's alerts: `unknown_ca`, `certificate_expired`, `bad_certificate` (host names, signatures,
self-signed peers), `unsupported_certificate`, `certificate_revoked`, else `handshake_failure`.
Servers send the certificate chain built from `cacerts`, and their `cacerts` name the
certificate authorities of a certificate request.

## Engine

`clause_ssl_engine` runs the TLS side of one connection over libssl (LIB81B,
[builtins](../builtins.md#ssl-engine)). Its builtins live in a separate `clause_runtime_ssl`
archive, linked with libssl of the OpenSSL the [crypto](crypto.md#linking-programs) archive uses.
Only programs that call the engine link it. A context holds the options `ssl.erl` validated, and an
engine holds one connection over memory buffers. Each handshake, read, write, shutdown or key update
step returns the bytes to send. A client continuing a paused handshake moves its engine to a new context. When it pauses for verification, the peer's chain is available, and
Erlang answers with `ok` or the alert to send. libssl 4.1 and later pause on both sides. A server
linked with an older libssl verifies the client's chain after its handshake is done, and a
rejection then closes the connection without sending the alert (and does not verify it again in a renegotiation).
libssl's callbacks queue events that Erlang takes after each step: the secrets it logs, KeyUpdates sent and
received, and TLS 1.2 renegotiations started, finished or refused.

## Application

`ssl.app` names `clause_ssl_app` as the start module. `clau` adds an application's start module to
every program that uses a module of that application, because no module calls it.
`ssl:start/0,1` runs `application:ensure_all_started(ssl, Type)`, which also starts `crypto`,
`asn1` and `public_key`. The top supervisor is registered as `ssl_sup`. Its children are
`clause_ssl_pem_cache`, registered as `ssl_pem_cache` as in OTP, and `tls_connection_sup`, whose
temporary children are the connection processes. Connections started without the application
run unsupervised. `ssl:clear_pem_cache/0` calls it
the way OTP does: it sets the caller's `ssl_pem_cache` process dictionary key, and it exits with
`{noproc, {gen_server, call, [ssl_pem_cache, {unconditionally_clear_pem_cache, Caller}, infinity]}}`
when `ssl` is not started.

## Utility functions

- **Cipher suites:** `cipher_suites/2,3`, `suite_to_str/1`, `suite_to_openssl_str/1` and
  `str_to_suite/1` are builtins in the core runtime (`runtime/src/builtins/ssl/`,
  [builtins](../builtins.md#ssl)). The table holds OTP's 129 listed suites with their codes and the
  30 lists of each version (`tlsv1.3` to `tlsv1`, `dtlsv1.2`, `dtlsv1`) and description
  (`default`, `all`, `exclusive`, `anonymous`, `exclusive_anonymous`). As in OTP, these lists do not
  depend on crypto.
- **Names:** the name builtins follow `ssl_cipher_format` clause by clause, so odd maps and names
  get OTP's results. Like OTP, `str_to_suite/1` parses a name without checking it against the table:
  any name whose parts are existing atoms becomes a map, and anything else is
  `{error, {not_recognized, Name}}`.
- **Crypto-dependent lists:** `filter_cipher_suites/2`, `prepend_cipher_suites/2` and
  `append_cipher_suites/2` apply the user's filter funs and then crypto's support, part by part.
  `signature_algs/2`, `eccs/0,1`, `groups/0,1` and `versions/0` filter OTP's lists by
  `crypto:supports/1` (`clause_ssl_algorithms`).
- **Versions:** `versions/0` reads the `protocol_version` and `dtls_protocol_version` application
  variables as OTP does. When a variable is unset, it stores the default versions there.
- **Errors:** `format_error/1` returns OTP's texts for alerts, options, files and POSIX reasons.
