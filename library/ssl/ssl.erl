%% Clause's replacement for OTP's ssl (docs/libraries/ssl.md, plan 21 step LIB81), written from OTP's documentation:
%% the cipher suite table and name walks are builtins and the crypto-dependent lists clause_ssl_algorithms' (LIB81A);
%% connections over OpenSSL's libssl follow in LIB81B-D and raise not_implemented until then.
-module(ssl).
-export([
    append_cipher_suites/2,
    cipher_suites/2,
    cipher_suites/3,
    clear_pem_cache/0,
    close/1,
    close/2,
    connect/2,
    connect/3,
    connect/4,
    connection_information/1,
    connection_information/2,
    controlling_process/2,
    eccs/0,
    eccs/1,
    export_key_materials/4,
    export_key_materials/5,
    filter_cipher_suites/2,
    format_error/1,
    getopts/2,
    getstat/1,
    getstat/2,
    groups/0,
    groups/1,
    handle_trace/3,
    handshake/1,
    handshake/2,
    handshake/3,
    handshake_cancel/1,
    handshake_continue/2,
    handshake_continue/3,
    listen/2,
    negotiated_protocol/1,
    peercert/1,
    peername/1,
    prepend_cipher_suites/2,
    prf/5,
    recv/2,
    recv/3,
    renegotiate/1,
    send/2,
    setopts/2,
    shutdown/2,
    signature_algs/2,
    sockname/1,
    start/0,
    start/1,
    stop/0,
    str_to_suite/1,
    suite_to_openssl_str/1,
    suite_to_str/1,
    tls_version/1,
    transport_accept/1,
    transport_accept/2,
    update_keys/2,
    versions/0
]).

-deprecated([
    {prf, 5,
        "Use export_key_materials/4 instead. "
        "Note that in OTP 28 the 'testing' way of calling this function will no longer be supported."}
]).

-include("ssl.types.hrl").

%% The application.
start() -> start(temporary).
start(Type) ->
    case application:ensure_all_started(ssl, Type) of
        {ok, _} -> ok;
        Other -> Other
    end.
stop() -> application:stop(ssl).
clear_pem_cache() -> clause_ssl_pem_cache:clear().

%% Cipher suites: the table and name walks are builtins (runtime/src/builtins/ssl/).
cipher_suites(_Description, _Version) -> clause_builtin_placeholder.
cipher_suites(_Description, _Version, _StringType) -> clause_builtin_placeholder.
suite_to_str(_CipherSuite) -> clause_builtin_placeholder.
suite_to_openssl_str(_CipherSuite) -> clause_builtin_placeholder.
str_to_suite(_CipherSuiteName) -> clause_builtin_placeholder.
filter_cipher_suites(Suites, Filters) -> clause_ssl_algorithms:filter_cipher_suites(Suites, Filters).

% Put Preferred (suites, or the filters choosing them from Suites) first.
prepend_cipher_suites([First | _] = Preferred, Suites) when is_map(First) ->
    Preferred ++ (Suites -- Preferred);
prepend_cipher_suites(Filters, Suites) ->
    Preferred = filter_cipher_suites(Suites, Filters),
    Preferred ++ (Suites -- Preferred).

% Put Deferred (suites, or the filters choosing them from Suites) last.
append_cipher_suites([First | _] = Deferred, Suites) when is_map(First) ->
    (Suites -- Deferred) ++ Deferred;
append_cipher_suites(Filters, Suites) ->
    Deferred = filter_cipher_suites(Suites, Filters),
    (Suites -- Deferred) ++ Deferred.

%% Algorithms and versions of the linked crypto.
signature_algs(Description, Version) -> clause_ssl_algorithms:signature_algs(Description, Version).
eccs() -> clause_ssl_algorithms:eccs().
eccs(Version) -> clause_ssl_algorithms:eccs(Version).
groups() -> clause_ssl_algorithms:groups().
groups(default) -> clause_ssl_algorithms:groups(default).
versions() -> clause_ssl_algorithms:versions().

%% Errors.

% The text of an error an ssl function returned.
format_error({error, Reason}) -> error_text(Reason);
format_error(Reason) -> error_text(Reason).

error_text(Reason) when is_list(Reason) ->
    Reason;
error_text(closed) ->
    "TLS connection is closed";
error_text({tls_alert, {_, Description}}) ->
    Description;
error_text({options, {FileType, File, Reason}}) when
    FileType == cacertfile; FileType == certfile; FileType == keyfile; FileType == dhfile
->
    file_description(FileType) ++ File ++ ": " ++ file_error(Reason);
error_text({options, {socket_options, Option, Error}}) ->
    lists:flatten(io_lib:format("Invalid transport socket option ~p: ~s", [Option, error_text(Error)]));
error_text({options, {socket_options, Option}}) ->
    lists:flatten(io_lib:format("Invalid socket option: ~p", [Option]));
error_text({options, incompatible, Options}) ->
    lists:flatten(io_lib:format("Options (or their values) can not be combined: ~p", [Options]));
error_text({option, Reason, Options}) ->
    lists:flatten(io_lib:format("Invalid option ~w ~w", [Options, Reason]));
error_text({options, Reason, Options}) ->
    lists:flatten(io_lib:format("Invalid option ~w ~w", [Options, Reason]));
error_text({options, {Reason, Options}}) when
    Reason == missing_version;
    Reason == option_not_a_key_value_tuple;
    Reason == no_supported_algorithms;
    Reason == no_supported_signature_schemes;
    Reason == insufficient_crypto_support
->
    lists:flatten(io_lib:format("Invalid option ~w ~w", [Options, Reason]));
error_text({options, Options}) ->
    lists:flatten(io_lib:format("Invalid TLS option: ~p", [Options]));
error_text(Error) ->
    case inet:format_error(Error) of
        "unknown POSIX" ++ _ -> lists:flatten(io_lib:format("Unexpected error: ~p", [Error]));
        Text -> Text
    end.

file_error({error, Error}) ->
    case file:format_error(Error) of
        "unknown POSIX error" ++ _ -> "decoding error";
        Text -> Text
    end;
file_error(_) ->
    "decoding error".

file_description(cacertfile) -> "Invalid CA certificate file ";
file_description(certfile) -> "Invalid certificate file ";
file_description(keyfile) -> "Invalid key file ";
file_description(dhfile) -> "Invalid DH params file ".

%% Connections (LIB81B-D).
close(_) -> erlang:error({not_implemented, close, 1}).
close(_, _) -> erlang:error({not_implemented, close, 2}).
connect(_, _) -> erlang:error({not_implemented, connect, 2}).
connect(_, _, _) -> erlang:error({not_implemented, connect, 3}).
connect(_, _, _, _) -> erlang:error({not_implemented, connect, 4}).
connection_information(_) -> erlang:error({not_implemented, connection_information, 1}).
connection_information(_, _) -> erlang:error({not_implemented, connection_information, 2}).
controlling_process(_, _) -> erlang:error({not_implemented, controlling_process, 2}).
export_key_materials(_, _, _, _) ->
    erlang:error({not_implemented, export_key_materials, 4}).
export_key_materials(_, _, _, _, _) ->
    erlang:error({not_implemented, export_key_materials, 5}).
getopts(_, _) -> erlang:error({not_implemented, getopts, 2}).
getstat(_) -> erlang:error({not_implemented, getstat, 1}).
getstat(_, _) -> erlang:error({not_implemented, getstat, 2}).
handle_trace(_, _, _) -> erlang:error({not_implemented, handle_trace, 3}).
handshake(_) -> erlang:error({not_implemented, handshake, 1}).
handshake(_, _) -> erlang:error({not_implemented, handshake, 2}).
handshake(_, _, _) -> erlang:error({not_implemented, handshake, 3}).
handshake_cancel(_) -> erlang:error({not_implemented, handshake_cancel, 1}).
handshake_continue(_, _) -> erlang:error({not_implemented, handshake_continue, 2}).
handshake_continue(_, _, _) -> erlang:error({not_implemented, handshake_continue, 3}).
listen(_, _) -> erlang:error({not_implemented, listen, 2}).
negotiated_protocol(_) -> erlang:error({not_implemented, negotiated_protocol, 1}).
peercert(_) -> erlang:error({not_implemented, peercert, 1}).
peername(_) -> erlang:error({not_implemented, peername, 1}).
prf(_, _, _, _, _) -> erlang:error({not_implemented, prf, 5}).
recv(_, _) -> erlang:error({not_implemented, recv, 2}).
recv(_, _, _) -> erlang:error({not_implemented, recv, 3}).
renegotiate(_) -> erlang:error({not_implemented, renegotiate, 1}).
send(_, _) -> erlang:error({not_implemented, send, 2}).
setopts(_, _) -> erlang:error({not_implemented, setopts, 2}).
shutdown(_, _) -> erlang:error({not_implemented, shutdown, 2}).
sockname(_) -> erlang:error({not_implemented, sockname, 1}).
tls_version(_) -> erlang:error({not_implemented, tls_version, 1}).
transport_accept(_) -> erlang:error({not_implemented, transport_accept, 1}).
transport_accept(_, _) -> erlang:error({not_implemented, transport_accept, 2}).
update_keys(_, _) -> erlang:error({not_implemented, update_keys, 2}).
