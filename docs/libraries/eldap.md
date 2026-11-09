# eldap

Clause's `eldap` (plan 21 MIM2) is OTP's LDAP client as MongooseIM calls it: `mongoose_ldap_worker` opens, binds and
closes connections, and `eldap_pool` searches, adds, deletes and changes passwords through them with filters built by
`eldap_utils` and `eldap_filter`. The module is Erlang, in [`library/eldap/`](../../library/eldap/), with the
[`eldap.app`](../../library/eldap/eldap.app) resource (MIM6) and OTP's
[`eldap.hrl`](../../library/eldap/include/eldap.hrl).

Behaviour was observed on OTP 29 against a local server printing each request (the BER bytes eldap sends, results,
errors, message identifiers, log calls, connections) and is checked by the `eldap_requests` golden.

## Codec

The LDAPv3 messages are encoded and decoded by `'ELDAPv3'`, which `clau` generates at build time from
[`ELDAPv3.asn1`](../../library/eldap/asn1/ELDAPv3.asn1) into `library-asn1/eldap/` with OTP's options (`ber`,
`legacy_erlang_types`: octet strings decode as lists). The specification is RFC 4511's module with RFC 3062's
password modify values and RFC 2696's paged results value, so the records eldap builds and logs are OTP's
(`'SearchRequest'`, `'AttributeValueAssertion'`, `'ModifyRequest_changes_SEQOF'`, `'Control'`, ...).

## Connections

`open/1,2` starts a connection process linked to the caller and tries the hosts in order, with the options `port`
(389, also with `ssl`), `timeout` (a positive number of milliseconds for each request's answer; `infinity` without
it), `log` (a fun of three arguments; other values are ignored), `ssl` (TLS from the start, with `sslopts`),
`tcpopts` and `anon_auth`. A wrong option is `{error, {wrong_option, Option}}`; the socket options eldap sets itself
(`active`, `binary`, `deliver`, `list`, `mode`, `packet`) are `{error, {{forbidden_tcp_option, Option}, Text}}`; when
no host connects, each failure is logged (`Connect: ~p failed ~p~n`) and the result is `{error, "connect failed"}`.

Calls send a message to the connection and wait for its answer without a timeout: a call to a closed handle waits
forever, as OTP's. `close/1` sends the unbind request (with the current message identifier) and returns `ok` without
waiting. `controlling_process/2` links the caller to the new process and unlinks the connection from the caller.
`info/1` is the socket and `socket_type` (`ssl` for `ssl` connections, `tcp` otherwise, also after StartTLS).
`getopts/2` answers `port`, `log`, `timeout` and `ssl`; another name is `{error, {error, {case_clause, Name}}}`.
`start_tls/2,3,4` asks the server with the current message identifier, then upgrades with `ssl`:
`{error, tls_already_started}`, `{error, {response, Code}}` for a refusal, the `ssl` error for a failed handshake
(the connection is then closed).

## Requests

- **Binds:** `simple_bind/3,4` answer `ok`, `{ok, {referral, Uris}}` or `{error, ResultCode}`; an empty password is
  `{error, anonymous_auth}` without `{anon_auth, true}` (nothing is sent). A lost connection is
  `{error, {gen_tcp_error, Reason}}`.
- **Searches:** `search/2,3` take `#eldap_search{}` or a list of options (later ones winning; another option is
  `{error, {unknown_arg, Option}}`), checked in the request's field order with OTP's errors (`"unknown scope: S"`,
  `"size_limit not positive integer: L"`, `"timeout not positive integer: T"`, `"not Boolean: B"`,
  `"unknown filter: F"`, `"attribute not String: A"`; an unknown `deref` raises inside the connection and answers
  `{error, {function_clause, Stack}}`). Defaults: base `[]`, scope `wholeSubtree`, deref `derefAlways`, no size or
  time limit. The result is `{ok, #eldap_search_result{}}` with entries and referral lists newest first and the
  result's controls (`asn1_NOVALUE` without), for success and `sizeLimitExceeded`; `{ok, {referral, Uris}}`; or
  `{error, ResultCode}` (`{asn1_enum, N}` for codes outside RFC 4511's).
- **Updates:** `add/3,4`, `modify/3,4` (with `mod_add/2`, `mod_delete/2`, `mod_replace/2`), `delete/2,3` and
  `modify_dn/5` answer as binds; attributes that are not `{Type, Values}` pairs throw `{error, attribute_values}`.
  `modify_password/3,4,5` sends RFC 3062's extended request: `ok`, `{ok, GeneratedPassword}`, a referral or
  `{error, {response, ResultCode}}`.
- **Lost connections:** requests other than binds answer `{error, ldap_closed}` for a closed socket and
  `{error, {gen_tcp_error, timeout}}` when the answer does not come in time.
- **Message identifiers:** each request takes the last identifier plus one; the connection keeps it after a success,
  a referral or any search result, not after an error. Answers are not matched to requests: a late answer is read as
  the next request's (`{error, {Operation, Result}}` for another operation, `{error, {ok, Message}}` for another
  identifier).
- **Log:** with a `log` fun, eldap calls it with level 2 and OTP's format strings for each request and answer
  (`"bind request = ~p~n"`, `"search reply = ~p~n"`, `"unbind request = ~p (has no reply)~n"`, ...).

Filters (`'and'/1`, `'or'/1`, `'not'/1`, `equalityMatch/2`, `substrings/2`, `present/1`, `approxMatch/2`,
`greaterOrEqual/2`, `lessOrEqual/2`, `extensibleMatch/2`), scopes and dereference values are OTP's terms; invalid
substrings and extensible match options throw `{error, {substring_arg, X}}` and
`{error, {extensibleMatch_arg, X}}`. `paged_result_control/1,2` and `paged_result_cookie/1` handle RFC 2696's paged
results.

Not provided: `parse_dn/1` and `parse_ldap_url/1` (OTP's DN and URL grammar) and the internal `loop/2`.
