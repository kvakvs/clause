# ssl

Clause's `ssl` application (plan 21, LIB81) exports the OTP 29 interface of the `ssl` module. The
utility functions are implemented (LIB81A), and so are TLS 1.2 and 1.3 [connections](#connections)
(LIB81C1) over the libssl [engine](#engine) (LIB81B). The `{handshake, hello}` pause, `close/2`,
`shutdown/2` and `tls_version/1` (LIB81C2), the TLS features of LIB81D and `ssl_crl_cache` raise
`{not_implemented, Name, Arity}` until then. See the
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
- **Closing**: `close/1` sends close_notify and closes the transport; the owner's exit does the same.
  When the peer closes, the connection answers its close_notify; an active owner then gets
  `{ssl_closed, S}`, or `{ssl_error, S, {tls_alert, ...}}` after an alert, and a passive reader
  gets `{error, closed}` once the buffered data is read. A finished connection answers every call
  with `{error, closed}`.
- **Information**: `peername`, `sockname` and `getstat` ask the transport. `getopts` lists the
  transport's options and then the emulated ones, the last asked first, as OTP's does.
  `connection_information/1,2` gives the negotiated items (a server also gives `sni_hostname`, and
  `keep_secrets` adds the randoms) and the main options. `peercert` gives the peer's certificate,
  and `negotiated_protocol` gives the ALPN protocol or `{error, protocol_not_negotiated}`.
- **Alerts** (`clause_ssl_alert`): a failed handshake returns `{error, {tls_alert, {Description,
  Text}}}` with OTP's description atoms and texts, and logs the alert at `notice` unless
  `log_level` is above it. The text leaves out OTP's source location
  ([differences](../differences.md#ssl)).

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
step returns the bytes to send. When it pauses for verification, the peer's chain is available, and
Erlang answers with `ok` or the alert to send. libssl 4.1 and later pause on both sides. A server
linked with an older libssl verifies the client's chain after its handshake is done, and a
rejection then closes the connection without sending the alert.

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
