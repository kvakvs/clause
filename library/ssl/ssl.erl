%% Clause's replacement for OTP's ssl (docs/libraries/ssl.md, plan 21 step LIB81), written from OTP's documentation:
%% the cipher suite table and name walks are builtins and the crypto-dependent lists clause_ssl_algorithms' (LIB81A);
%% connections run over OpenSSL's libssl (LIB81B engine, LIB81C1 connections, LIB81C2 pauses and closing); the rest
%% of LIB81D raises not_implemented until then.
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
-include("clause_ssl.hrl").

-define(IS_TIMEOUT(T), ((is_integer(T) andalso T >= 0) orelse T == infinity)).

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

%% Connections (LIB81C1): each one a clause_ssl_connection process over the clause_ssl_engine builtins.

% A connection to Host:Port, or a TLS client over an already connected transport socket.
connect(Socket, Options) -> connect(Socket, Options, infinity).

connect(Socket, Options, Timeout) when is_list(Options) ->
    upgrade(client, Socket, Options, Timeout);
connect(Host, Port, Options) ->
    connect(Host, Port, Options, infinity).

connect(Host, Port, Options, Timeout) when
    is_list(Options), (is_integer(Timeout) andalso Timeout >= 0) orelse Timeout == infinity
->
    case clause_ssl_options:client(Options, Host) of
        {ok, #{cb_info := {Transport, _, _, _, _}, socket := SocketOptions} = Config} ->
            case Transport:connect(Host, Port, transport_options(SocketOptions), Timeout) of
                {ok, Socket} -> start_connection(client, Socket, Config, undefined, Timeout);
                Error -> Error
            end;
        Error ->
            Error
    end.

% A TLS client or server over a connected transport socket, whose emulated options start as the socket's own.
upgrade(Role, Socket, Options0, Timeout) ->
    Options =
        case inet:getopts(Socket, [mode, packet, packet_size, header, active]) of
            {ok, Inherited} -> Inherited ++ Options0;
            _ -> Options0
        end,
    Configured =
        case Role of
            client -> clause_ssl_options:client(Options, undefined);
            server -> clause_ssl_options:server(Options)
        end,
    case Configured of
        {ok, #{cb_info := {Transport, _, _, _, _}, socket := SocketOptions} = Config} ->
            case (socket_module(Transport)):setopts(Socket, transport_options(SocketOptions)) of
                ok -> start_connection(Role, Socket, Config, undefined, Timeout);
                Error -> Error
            end;
        Error ->
            Error
    end.

% The transport's own options under the ones the connection process reads with.
transport_options(SocketOptions) ->
    [{mode, binary}, {packet, 0}, {header, 0}, {active, false}, {packet_size, 0} | SocketOptions].

% Start a connection process owning Socket and run its handshake; a server's from transport_accept (with its
% listening socket's engine context) waits for handshake/1,2,3.
start_connection(Role, Socket, Config0, Context, Timeout) ->
    #{cb_info := {Transport, _, _, _, _}} = Config0,
    case clause_ssl_options:load_files(Config0) of
        {ok, Config} ->
            Args = #{role => Role, socket => Socket, config => Config, owner => self(), context => Context},
            {ok, Pid} = clause_ssl_connection:start(Args),
            ok = Transport:controlling_process(Socket, Pid),
            SslSocket = sslsocket(Socket, Pid, Transport),
            case Context of
                undefined -> call(SslSocket, {handshake, Timeout, []});
                _ -> {ok, SslSocket}
            end;
        Error ->
            Transport:close(Socket),
            Error
    end.

sslsocket(Socket, Pid, Transport) ->
    #sslsocket{
        socket_handle = Socket,
        connection_handler = Pid,
        payload_sender = Pid,
        transport_cb = Transport,
        connection_cb = tls_gen_connection
    }.

% A listening socket: its transport socket, and the configuration and engine context of its connections.
listen(_Port, []) ->
    {error, nooptions};
listen(Port, Options) ->
    case clause_ssl_options:server(Options) of
        {ok, Config0} ->
            case clause_ssl_options:load_files(Config0) of
                {ok, Config} -> listen_with(Port, Config);
                Error -> Error
            end;
        Error ->
            Error
    end.

listen_with(Port, #{cb_info := {Transport, _, _, _, _}, socket := SocketOptions} = Config) ->
    case clause_ssl_engine:context(server, clause_ssl_options:engine_options(Config)) of
        {ok, Context} ->
            case Transport:listen(Port, transport_options(SocketOptions)) of
                {ok, Listen} ->
                    {ok, #sslsocket{
                        socket_handle = Listen, transport_cb = Transport, listener_config = {Config, Context}
                    }};
                Error ->
                    Error
            end;
        {error, {Key, Text}} ->
            {error, {options, {Key, Text}}}
    end.

transport_accept(ListenSocket) -> transport_accept(ListenSocket, infinity).

transport_accept(#sslsocket{socket_handle = Listen, listener_config = {Config, Context}}, Timeout) ->
    #{cb_info := {Transport, _, _, _, _}} = Config,
    case Transport:accept(Listen, Timeout) of
        {ok, Socket} -> start_connection(server, Socket, Config, Context, Timeout);
        Error -> Error
    end.

% The server handshake of an accepted connection, or of a transport socket upgraded to TLS.
handshake(SslSocket) -> handshake(SslSocket, infinity).

handshake(#sslsocket{} = SslSocket, Timeout) when is_integer(Timeout); Timeout == infinity ->
    handshake(SslSocket, [], Timeout);
handshake(Socket, Options) ->
    handshake(Socket, Options, infinity).

handshake(#sslsocket{} = SslSocket, Options, Timeout) ->
    call(SslSocket, {handshake, Timeout, Options});
handshake(Socket, Options, Timeout) ->
    upgrade(server, Socket, Options, Timeout).

send(#sslsocket{connection_handler = undefined}, _Data) -> {error, enotconn};
send(SslSocket, Data) -> call(SslSocket, {send, Data}).

recv(SslSocket, Length) -> recv(SslSocket, Length, infinity).

recv(#sslsocket{connection_handler = undefined}, _Length, _Timeout) -> {error, enotconn};
recv(SslSocket, Length, Timeout) -> call(SslSocket, {recv, Length, Timeout}).

close(#sslsocket{connection_handler = undefined, socket_handle = Listen, transport_cb = Transport}) ->
    Transport:close(Listen);
close(SslSocket) ->
    case call(SslSocket, close) of
        {error, closed} -> ok;
        Result -> Result
    end.

controlling_process(#sslsocket{connection_handler = undefined, socket_handle = Listen, transport_cb = T}, Pid) ->
    T:controlling_process(Listen, Pid);
controlling_process(SslSocket, Pid) when is_pid(Pid) ->
    call(SslSocket, {controlling_process, Pid}).

setopts(#sslsocket{connection_handler = undefined, socket_handle = Listen, transport_cb = T}, Options) ->
    {_Emulated, SocketOptions} = clause_ssl_options:split_socket(Options),
    case SocketOptions of
        [] -> ok;
        _ -> (socket_module(T)):setopts(Listen, SocketOptions)
    end;
setopts(SslSocket, Options) when is_list(Options) ->
    call(SslSocket, {setopts, Options}).

getopts(#sslsocket{connection_handler = undefined} = ListenSocket, Names) ->
    #sslsocket{socket_handle = Listen, transport_cb = T, listener_config = {#{emulated := Emulated}, _}} = ListenSocket,
    {EmulatedNames, SocketNames} = clause_ssl_options:split_socket(Names),
    Values = lists:reverse([{N, maps:get(N, Emulated)} || N <- EmulatedNames]),
    case SocketNames of
        [] ->
            {ok, Values};
        _ ->
            case (socket_module(T)):getopts(Listen, SocketNames) of
                {ok, SocketValues} -> {ok, SocketValues ++ Values};
                {error, Reason} -> {error, {options, {socket_options, Names, {badmatch, {error, Reason}}}}}
            end
    end;
getopts(SslSocket, Names) when is_list(Names) ->
    call(SslSocket, {getopts, Names}).

peername(#sslsocket{connection_handler = undefined}) -> {error, enotconn};
peername(#sslsocket{socket_handle = Socket, transport_cb = T}) -> (socket_module(T)):peername(Socket).

sockname(#sslsocket{socket_handle = Socket, transport_cb = T}) -> (socket_module(T)):sockname(Socket).

getstat(SslSocket) -> getstat(SslSocket, inet:stats()).

getstat(#sslsocket{socket_handle = Socket, transport_cb = T}, Options) -> (socket_module(T)):getstat(Socket, Options).

peercert(#sslsocket{connection_handler = undefined}) -> {error, enotconn};
peercert(SslSocket) -> call(SslSocket, peercert).

negotiated_protocol(#sslsocket{connection_handler = Pid} = SslSocket) when is_pid(Pid) ->
    call(SslSocket, negotiated_protocol).

connection_information(#sslsocket{connection_handler = undefined}) -> {error, enotconn};
connection_information(SslSocket) -> call(SslSocket, {connection_information, all}).

connection_information(#sslsocket{connection_handler = undefined}, _Items) -> {error, enotconn};
connection_information(SslSocket, Items) when is_list(Items) -> call(SslSocket, {connection_information, Items}).

% A call to the connection process; a finished connection is closed.
call(#sslsocket{connection_handler = Pid}, Request) ->
    try
        gen_server:call(Pid, Request, infinity)
    catch
        exit:{Reason, _} when Reason == noproc; Reason == normal; Reason == shutdown -> {error, closed}
    end.

socket_module(gen_tcp) -> inet;
socket_module(Transport) -> Transport.

%% Pauses and closing (LIB81C2).

% Continue or cancel a handshake paused by {handshake, hello}.
handshake_continue(SslSocket, Options) -> handshake_continue(SslSocket, Options, infinity).

handshake_continue(SslSocket, Options, Timeout) when is_list(Options), ?IS_TIMEOUT(Timeout) ->
    call(SslSocket, {handshake_continue, Options, Timeout}).

handshake_cancel(SslSocket) -> call(SslSocket, cancel).

% Close with a timeout, which OTP's close leaves to its default, or downgrade to the transport socket, handed to
% NewOwner once the peer has answered close_notify.
close(#sslsocket{connection_handler = undefined} = ListenSocket, _How) ->
    close(ListenSocket);
close(#sslsocket{connection_handler = Pid} = SslSocket, {NewOwner, Timeout}) when
    is_pid(Pid), is_pid(NewOwner), ?IS_TIMEOUT(Timeout)
->
    call(SslSocket, {downgrade, NewOwner, Timeout});
close(#sslsocket{connection_handler = Pid} = SslSocket, Timeout) when is_pid(Pid), ?IS_TIMEOUT(Timeout) ->
    close(SslSocket).

shutdown(#sslsocket{connection_handler = undefined, transport_cb = T}, _How) when T == gen_udp; T == gen_sctp ->
    {error, notsup};
shutdown(#sslsocket{connection_handler = undefined}, _How) ->
    {error, enotconn};
shutdown(SslSocket, How) ->
    call(SslSocket, {shutdown, How}).

% The TLS version of a protocol version tuple (a DTLS one's counterpart); internal, as in OTP.
tls_version(Version) when element(1, Version) == 3 -> Version;
tls_version({254, 255}) -> {3, 2};
tls_version({254, 253}) -> {3, 3}.

%% Connection features of LIB81D.
export_key_materials(_, _, _, _) ->
    erlang:error({not_implemented, export_key_materials, 4}).
export_key_materials(_, _, _, _, _) ->
    erlang:error({not_implemented, export_key_materials, 5}).
handle_trace(_, _, _) -> erlang:error({not_implemented, handle_trace, 3}).
prf(_, _, _, _, _) -> erlang:error({not_implemented, prf, 5}).
renegotiate(_) -> erlang:error({not_implemented, renegotiate, 1}).
update_keys(_, _) -> erlang:error({not_implemented, update_keys, 2}).
