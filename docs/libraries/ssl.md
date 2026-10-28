# ssl

Clause's `ssl` application (plan 21, LIB81) exports the OTP 29 interface of the `ssl` module. The
utility functions are implemented (LIB81A), and so is the libssl [engine](#engine) that connections
will run on (LIB81B). The connection functions raise `{not_implemented, Name, Arity}` until LIB81C,
and `ssl_crl_cache` arrives in LIB81D. See the
[function inventory](../../library/ssl/ssl.terms) and
[differences](../differences.md#ssl).

## Design

Each TLS connection will be an Erlang process with OTP's semantics: an owner,
`controlling_process/2`, active and passive modes, the `{ssl, Socket, Data}`, `{ssl_closed, Socket}`
and `{ssl_error, Socket, Reason}` messages, and OTP's option validation, error terms and alerts.
OpenSSL's libssl, linked from the same OpenSSL as [crypto](crypto.md), runs the record layer and
the handshake over memory buffers. The process moves bytes between libssl and the transport of the
`cb_info` option, `gen_tcp` by default. Certificates are verified as OTP verifies them: libssl
pauses the handshake in its verify callback, and the process runs
`public_key:pkix_path_validation/3` with the `verify_fun`, `partial_chain` and CRL options. TLS 1.2
and 1.3 come first; TLS 1.0, 1.1 and DTLS are optional (LIB81E).

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
`asn1` and `public_key`. The top supervisor is registered as `ssl_sup`. Its child
`clause_ssl_pem_cache` is registered as `ssl_pem_cache`, as in OTP. `ssl:clear_pem_cache/0` calls it
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
