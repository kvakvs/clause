# inets

Clause's `inets` (plan 21 MIM4) is OTP's HTTP client as MongooseIM and its dependencies call it: erlcloud sends AWS
requests with `httpc:request/4`, p1_utils reads and sets `max_sessions`, MongooseIM's metrics sender posts over TLS,
and erlcloud, lhttpc and fusco percent-encode with `http_uri`. The library is Erlang, in `library/inets/`, with the
[`inets.app`](../../library/inets/inets.app) resource (MIM6). The server side (`httpd`), `ftp` and `tftp` are not
provided.

Behaviour was observed on OTP 29 against local servers (request bytes as the server reads them, results, errors,
connections opened and closed) and is checked by the `httpc_requests` golden.

## httpc

`request/1,2,4,5` send a request through a profile's manager (`clause_httpc_manager`, registered `httpc_manager` for
the default profile, `httpc_<Profile>` for others) to a connection handler (`clause_httpc_handler`, under
`httpc_handler_sup`), over `gen_tcp`, or `ssl` for `https`.

- **Checks, in OTP's order:** the method (`get`, `head`, `options`, `trace`, `post`, `put`, `patch`, `delete`; else
  `{error, invalid_method}`), the request form (`{Url, Headers}` for `get`, `head`, `options`, `trace`, `put` and
  `delete`; `{Url, Headers, ContentType, Body}` for `post`, `patch`, `put` and `delete`, with a string content type
  and a string, binary, iolist or `{Fun, Acc}` body; else `{error, invalid_request}`), the headers (a name that is not
  a string is `{error, {headers_error, invalid_field}}`, a value neither a string nor a binary `invalid_value`; a
  header list that is not a list of pairs is `function_clause`), then the URL, normalized as
  `uri_string:normalize/2` does (`{error, invalid_uri}`, `{error, {no_scheme}}`, `{error, {bad_scheme, S}}`).
- **Options:** the HTTP options (`version`, `timeout`, `autoredirect`, `ssl`, `proxy_auth`, `relaxed`, `url_encode`,
  `connect_timeout`) and the request options (`sync`, `stream`, `body_format`, `full_result`, `headers_as_is`,
  `receiver`, `socket_opts`, `ipv6_host_with_brackets`) are checked in that order; an invalid value is reported as
  OTP's info report (`Invalid option timeout:-1 ignored`) and ignored, then the unknown options in the order given
  (`Invalid option {bogus,1} ignored`).
- **Request bytes:** the request line with the path and query (an empty query left out, unreserved characters
  decoded), or the absolute URI through a proxy. The headers OTP's header record holds are written in its field order
  from last to first (`last-modified` ... `content-type`, `content-length` ... `host` ... `connection`,
  `cache-control`), the user's value of each winning over the last given; the others follow as given, names in lower
  case. httpc adds `host` (with the port unless it is the scheme's) and `connection: keep-alive` for HTTP/1.1,
  `connection: close` and no `host` for HTTP/1.0, `authorization: Basic` from the URL's user, `proxy-authorization`
  through a proxy, `, TE` in `connection` when a `te` header is sent, and the content's type and length (a fun body's
  length is its `content-length` header, else `0`; `put` without content sends `content-length: 0`). With
  `headers_as_is` only the user's headers are written, last first.
- **Results:** `{ok, {{Version, Code, Reason}, Headers, Body}}`, or `{ok, {Code, Body}}` without `full_result`, with
  a string body unless `{body_format, binary}`. The response's headers come in OTP's response record order (the
  first value of each), then the others as received, names in lower case and values trimmed. Bodies are framed by
  `content-length`, the chunked coding (decoded, its length as `content-length`, trailers after the other headers)
  or the connection's close; `HEAD`, 204 and 304 have none and 1xx responses are skipped.
- **Errors:** `{error, {failed_connect, [{to_address, {Host, Port}}, {inet, [Family | Options], Reason}]}}` (TLS
  failures too), `{error, timeout}`, `{error, socket_closed_remotely}`, `{error, {invalid_version, V}}` for a status
  line not starting with `HTTP/`, `{error, {could_not_parse_as_http, Data}}` for a status code that is not a number.
- **Redirects** (`autoredirect`, at most five, then the last redirect response is the result): 300 with any method;
  301, 302 and 303 for `get`, `head` and `options`, and `post` as `get` with `content-length: 0`; 307 and 308 for
  `get`, `head`, `post` and `options`; others and 304 are results. A `Location` is resolved against the request's URI.
- **Sessions:** a request goes to the session of its server (and of the proxy it goes through) with the fewest
  requests, queued behind the one in progress, while it has fewer than `max_keep_alive_length`; else to a new
  connection. A new session takes requests once its first response came, unless that response was a redirect, as
  OTP's. A connection closes after a server error (5xx), `connection: close` from the server, an HTTP/1.0 response
  without keep-alive, a timeout or a parse error. HTTP/1.0 requests and requests with their own `socket_opts` open a
  connection of their own, ask `connection: close` and close it. A `connection: close` header of the user's leaves the
  connection unused, open until `keep_alive_timeout`. A reused connection the server closed before answering is
  retried once on a new one.
- **Asynchronous requests** (`{sync, false}`) return `{ok, RequestId}`; the result, with a binary body, goes to the
  receiver: the caller as `{http, {RequestId, Result}}`, a fun or `{M, F, A}` called with `{RequestId, Result}` (an
  exception there is ignored). `{stream, self}` sends `stream_start` with the headers as received, the body and
  `stream_end` with the final headers for 200 and 206; synchronous streaming is `{error, streaming_error}`.
  `cancel_request/1,2` drop a request and close its connection.
- **TLS:** the `ssl` option's TLS options, else `ssl_verify_host_options(true)` (the peer verified against
  `public_key:cacerts_get/0`, its host name checked as HTTPS does).
- **Proxy:** the `proxy` option's server carries `http` requests whose host is not in its list (exact names or
  `*suffix` patterns).

Profile options: `set_option/2,3` and `set_options/1,2` check every option first (`{error, {not_an_option, O}}`,
`{error, {error, {bad_option, Name, Value}}}`, `bad_ipfamily_unix_socket_combination`) and set none on an error;
`get_option/1,2` and `get_options/1,2` answer in OTP's order, `{error, {invalid_options, Names}}` for unknown names
and `{error, inets_not_started}` without the profile's manager. Profiles start as inets services
(`inets:start(httpc, [{profile, P}])`, `{error, no_profile}` without one) or stand-alone, and stop with
`inets:stop(httpc, Profile | Pid)`.

Not provided: cookies (`cookies` is kept as an option; `store_cookies`, `cookie_header`, `reset_cookies`,
`which_cookies` are declined), `which_sessions/0,1` and `info/0,1` (OTP's internal session records),
`stream_next/1` with streams `{self, once}` and to files (`{not_implemented, {option, {stream, S}}}`), pipelining
(`pipeline_timeout` and `max_pipeline_length` are kept as options), `https_proxy` tunnels and Unix sockets.

## inets

`start/0,1` and `stop/0` start and stop the application; `start/2,3` and `stop/2` start and stop a service through
its module (`httpc:start_service/1`, `start_standalone/1`, `stop_service/1`), a stand-alone one stopped by its
process. `services/0`, `services_info/0` and `service_names/0` list the httpc profiles' managers. Tracing,
`versions/0` and `print_version_info/0,1` are declined.

## http_uri

OTP 29's `http_uri` has only `encode/1` and `decode/1`, which answer as `uri_string:quote/1` and
`uri_string:percent_decode/1`.
