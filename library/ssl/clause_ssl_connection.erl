%% One TLS connection of ssl (plan 21 step LIB81C1, docs/libraries/ssl.md#connections): a process that owns the
%% transport socket and a clause_ssl_engine, runs the handshake (pausing for clause_ssl_certificate's verdict), and
%% then moves bytes between the transport and the engine. It keeps OTP's semantics for the owner: active modes and
%% {ssl, S, Data}, {ssl_closed, S}, {ssl_error, S, Reason} and {ssl_passive, S} messages, passive recv, emulated
%% packet framing, and closing when the owner exits. LIB81C2 adds the {handshake, hello} pause (clause_ssl_hello),
%% shutdown/2 and close/2's downgrade to the transport socket.
-module(clause_ssl_connection).
-behaviour(gen_server).
-export([start/1, start_link/1]).
-export([init/1, handle_call/3, handle_cast/2, handle_info/2]).

-include("clause_ssl.hrl").

-define(INTERNAL_ERROR, 80).
-define(USER_CANCELED, 90).

%% Start a connection process for #{role, socket, config, owner, context (optional)}, under tls_connection_sup when
%% ssl is started.
start(Args) ->
    case whereis(tls_connection_sup) of
        undefined -> gen_server:start(?MODULE, Args, []);
        _ -> supervisor:start_child(tls_connection_sup, [Args])
    end.

start_link(Args) ->
    gen_server:start_link(?MODULE, Args, []).

%% The state is a map: role, socket, transport, tags, ssl (options), emulated (socket options), owner, monitor,
%% sslsocket, engine, phase (idle | handshake | connection | closed), waiter and timer (the handshake's caller),
%% buffer (received plaintext), reader ({From, Length, Timer} of a passive recv), armed (a transport read pending),
%% end_reason (why the peer ended) and verify_reason (the reason of a rejected peer chain). For LIB81C2: pausing (the
%% handshake pauses at the peer's hello), held (the peer's bytes libssl has not seen yet; phase user_hello while
%% paused) and downgrade ({NewOwner, From, Timer} of close/2's downgrade; phase downgrade).
init(#{role := Role, socket := Socket, config := Config, owner := Owner} = Args) ->
    #{ssl := Ssl, emulated := Emulated, cb_info := {Transport, Data, Closed, Error, _}} = Config,
    SslSocket = #sslsocket{
        socket_handle = Socket,
        connection_handler = self(),
        payload_sender = self(),
        transport_cb = Transport,
        connection_cb = tls_gen_connection
    },
    {ok, #{
        role => Role,
        socket => Socket,
        transport => Transport,
        tags => {Data, Closed, Error},
        config => Config,
        ssl => Ssl,
        emulated => Emulated,
        owner => Owner,
        monitor => erlang:monitor(process, Owner),
        sslsocket => SslSocket,
        context => maps:get(context, Args, undefined),
        engine => undefined,
        phase => idle,
        waiter => undefined,
        timer => undefined,
        buffer => <<>>,
        reader => undefined,
        armed => false,
        end_reason => undefined,
        verify_reason => undefined,
        pausing => false,
        held => <<>>,
        downgrade => undefined
    }}.

%%--------------------------------------------------------------------------------------------------------------------
%% Calls
%%--------------------------------------------------------------------------------------------------------------------

handle_call({handshake, Timeout, Options}, From, #{phase := idle} = S) ->
    start_handshake(Timeout, Options, From, S);
handle_call({handshake, _, _}, _From, S) ->
    {reply, {error, closed}, S};
handle_call({send, Data}, _From, #{phase := connection} = S) ->
    {reply, send_data(Data, S), S};
handle_call({send, _}, _From, S) ->
    {reply, {error, closed}, S};
handle_call({recv, _, _}, _From, #{emulated := #{active := Active}} = S) when Active =/= false ->
    {reply, {error, einval}, S};
handle_call({recv, Length, Timeout}, From, #{reader := undefined} = S) ->
    deliver(S#{reader => {From, Length, start_timer(Timeout, recv_timeout)}});
handle_call({recv, _, _}, _From, S) ->
    {reply, {error, ealready}, S};
handle_call(close, _From, S) ->
    close_transport(S),
    {stop, normal, ok, S#{phase => closed}};
handle_call({handshake_continue, Options, Timeout}, From, #{phase := user_hello} = S) ->
    continue_handshake(Options, Timeout, From, S);
handle_call(cancel, _From, #{phase := user_hello} = S) ->
    {stop, normal, ok, end_paused(?USER_CANCELED, user_canceled, S)};
handle_call({shutdown, How}, _From, #{phase := connection} = S) ->
    shutdown_transport(How, S);
handle_call({downgrade, Owner, Timeout}, From, #{phase := connection} = S) ->
    start_downgrade(Owner, Timeout, From, S);
handle_call({controlling_process, Pid}, _From, #{monitor := Monitor} = S) ->
    erlang:demonitor(Monitor, [flush]),
    {reply, ok, S#{owner => Pid, monitor => erlang:monitor(process, Pid)}};
handle_call({setopts, Options}, _From, S) ->
    case set_options(Options, S) of
        {ok, S1} -> reply_and_deliver(ok, S1);
        Error -> {reply, Error, S}
    end;
handle_call({getopts, Names}, _From, S) ->
    {reply, get_options(Names, S), S};
handle_call(Request, _From, #{phase := Phase} = S) when Phase == connection; Phase == closed ->
    {reply, information(Request, S), S};
handle_call(_Request, _From, S) ->
    {reply, {error, closed}, S}.

handle_cast(_Request, S) ->
    {noreply, S}.

reply_and_deliver(Reply, S) ->
    case deliver(S) of
        {noreply, S1} -> {reply, Reply, S1};
        {stop, Reason, S1} -> {stop, Reason, Reply, S1}
    end.

%%--------------------------------------------------------------------------------------------------------------------
%% Messages
%%--------------------------------------------------------------------------------------------------------------------

handle_info({Tag, Socket, Bytes}, #{socket := Socket, tags := {Tag, _, _}} = S) ->
    received(iolist_to_binary(Bytes), S#{armed => false});
handle_info({Tag, Socket}, #{socket := Socket, tags := {_, Tag, _}} = S) ->
    transport_ended(closed, S#{armed => false});
handle_info({Tag, Socket, _Reason}, #{socket := Socket, tags := {_, _, Tag}} = S) ->
    transport_ended(closed, S#{armed => false});
handle_info({'DOWN', Monitor, process, _, _}, #{monitor := Monitor} = S) ->
    close_transport(S),
    {stop, normal, S#{phase => closed}};
handle_info({timeout, Timer, handshake_timeout}, #{timer := Timer} = S) ->
    handshake_ended({error, timeout}, S);
handle_info({timeout, Timer, downgrade_timeout}, #{downgrade := {_, _, Timer}} = S) ->
    downgrade_ended({error, timeout}, S);
handle_info({timeout, Timer, recv_timeout}, #{reader := {From, _, Timer}} = S) ->
    gen_server:reply(From, {error, timeout}),
    {noreply, S#{reader => undefined}};
handle_info(_Message, S) ->
    {noreply, S}.

received(Bytes, #{phase := handshake, pausing := true} = S) ->
    hello_received(Bytes, S);
received(Bytes, #{phase := handshake} = S) ->
    step(Bytes, S);
received(Bytes, #{phase := user_hello, held := Held} = S) ->
    {noreply, S#{held => <<Held/binary, Bytes/binary>>}};
received(Bytes, #{phase := downgrade} = S) ->
    downgrade_received(Bytes, S);
received(Bytes, #{phase := connection, engine := Engine} = S) ->
    {Status, Plain, Output} = clause_ssl_engine:read(Engine, Bytes),
    transmit(Output, S),
    S1 = S#{buffer => <<(maps:get(buffer, S))/binary, Plain/binary>>},
    case Status of
        ok -> deliver(S1);
        closed -> peer_ended(closed, S1);
        Failure -> peer_ended(failure_reason(Failure, connection, S1), S1)
    end;
received(_Bytes, S) ->
    {noreply, S}.

transport_ended(Reason, #{phase := handshake, engine := Engine} = S) ->
    % An alert libssl ignored, as a TLS 1.3 user_canceled, is why the peer ended.
    case clause_ssl_engine:info(Engine) of
        #{alert := {alert, received, _, _} = Alert} -> handshake_ended({error, failure_reason(Alert, handshake, S)}, S);
        _ -> handshake_ended({error, Reason}, S)
    end;
transport_ended(Reason, #{phase := connection} = S) ->
    peer_ended(Reason, S);
transport_ended(_Reason, #{phase := user_hello} = S) ->
    close_socket(S),
    {stop, normal, S#{phase => closed}};
transport_ended(_Reason, #{phase := downgrade, tags := {_, Closed, _}} = S) ->
    downgrade_ended({error, Closed}, S);
transport_ended(_Reason, S) ->
    {noreply, S}.

%%--------------------------------------------------------------------------------------------------------------------
%% Handshake
%%--------------------------------------------------------------------------------------------------------------------

start_handshake(Timeout, Options, From, #{config := Config0, role := Role} = S) ->
    case updated(Config0, Options, Role) of
        {ok, Config} ->
            case engine(Config, S) of
                {ok, Engine} ->
                    #{ssl := Ssl} = Config,
                    Pausing = maps:get(handshake, Ssl) == hello,
                    S1 = S#{config => Config, ssl => Ssl, engine => Engine, pausing => Pausing},
                    Timer = start_timer(Timeout, handshake_timeout),
                    step(<<>>, S1#{phase => handshake, waiter => From, timer => Timer});
                Error ->
                    close_transport(S),
                    {stop, normal, Error, S#{phase => closed}}
            end;
        Error ->
            close_transport(S),
            {stop, normal, Error, S#{phase => closed}}
    end.

updated(Config, [], _Role) ->
    {ok, Config};
updated(Config0, Options, Role) ->
    case clause_ssl_options:update(Config0, Options, Role) of
        {ok, Config} -> clause_ssl_options:load_files(Config);
        Error -> Error
    end.

% The engine of the connection: over the listening socket's context unless the handshake changed options.
engine(Config, #{context := Context, config := Config, role := Role} = S) when Context =/= undefined ->
    {ok, clause_ssl_engine:connection(Context, connection_options(Role, maps:get(ssl, S)))};
engine(#{role := Role, ssl := Ssl} = Config, _S) ->
    case clause_ssl_engine:context(Role, clause_ssl_options:engine_options(Config)) of
        {ok, Context} -> {ok, clause_ssl_engine:connection(Context, connection_options(Role, Ssl))};
        {error, {Key, Text}} -> {error, {options, {Key, Text}}}
    end.

connection_options(client, #{server_name_indication := [_ | _] = Name}) -> #{server_name => list_to_binary(Name)};
connection_options(_, _) -> #{}.

% One handshake step with the bytes received.
step(Received, #{engine := Engine} = S) ->
    case clause_ssl_engine:handshake(Engine, Received) of
        {want_read, Output} ->
            transmit(Output, S),
            {noreply, arm(S)};
        {done, Output} ->
            transmit(Output, S),
            handshake_done(S);
        {verify, Output} ->
            transmit(Output, S),
            verify(S);
        {Failure, Output} ->
            transmit(Output, S),
            handshake_ended({error, failure_reason(Failure, handshake, S)}, S)
    end.

% The peer's chain checked as OTP's ssl checks it; the engine sends the alert of a rejection.
verify(#{engine := Engine, ssl := Ssl, role := Role} = S) ->
    case clause_ssl_certificate:verify(clause_ssl_engine:peer_chain(Engine), Ssl, Role) of
        ok ->
            ok = clause_ssl_engine:verify(Engine, ok),
            step(<<>>, S);
        {alert, Code, Reason} ->
            ok = clause_ssl_engine:verify(Engine, Code),
            step(<<>>, S#{verify_reason => {Code, Reason}})
    end.

handshake_done(#{waiter := From, timer := Timer, sslsocket := SslSocket, engine := Engine} = S) ->
    cancel(Timer),
    gen_server:reply(From, {ok, SslSocket}),
    S1 = S#{phase => connection, waiter => undefined, timer => undefined},
    % Application data that came with the handshake's last flight.
    {Status, Plain, Output} = clause_ssl_engine:read(Engine, <<>>),
    transmit(Output, S1),
    S2 = S1#{buffer => Plain},
    case Status of
        ok -> deliver(S2);
        closed -> peer_ended(closed, S2);
        Failure -> peer_ended(failure_reason(Failure, connection, S2), S2)
    end.

handshake_ended(Reply, #{waiter := From, timer := Timer} = S) ->
    cancel(Timer),
    From =/= undefined andalso gen_server:reply(From, Reply),
    close_transport(S),
    {stop, normal, S#{phase => closed, waiter => undefined}}.

%% The {handshake, hello} pause (LIB81C2).

% The bytes of the peer's first flight, held until its hello message is complete; then the handshake pauses with the
% hello's extensions, unless they cannot be read (libssl then fails the handshake).
hello_received(Bytes, #{held := Held0, ssl := Ssl} = S) ->
    Held = <<Held0/binary, Bytes/binary>>,
    case clause_ssl_hello:message(Held) of
        more -> {noreply, arm(S#{held => Held})};
        {ok, Message} -> paused(clause_ssl_hello:extensions(Message, Ssl), S#{held => Held});
        invalid -> step(Held, S#{pausing => false, held => <<>>})
    end.

paused({ok, Extensions}, #{waiter := From, timer := Timer, sslsocket := SslSocket} = S) ->
    cancel(Timer),
    gen_server:reply(From, {ok, SslSocket, Extensions}),
    {noreply, S#{phase => user_hello, waiter => undefined, timer => undefined}};
paused(error, #{held := Held} = S) ->
    step(Held, S#{pausing => false, held => <<>>}).

% Go on with the held bytes under the options of handshake_continue/2,3; a refused option ends the handshake with an
% internal_error alert, as OTP's does.
continue_handshake(Options, Timeout, From, #{config := Config, role := Role} = S0) ->
    case reconfigured(updated(Config, Options, Role), S0) of
        {ok, #{held := Held} = S} ->
            Timer = start_timer(Timeout, handshake_timeout),
            step(Held, S#{phase => handshake, waiter => From, timer => Timer, pausing => false, held => <<>>});
        {error, Reason} = Error ->
            {stop, normal, Error, end_paused(?INTERNAL_ERROR, Reason, S0)}
    end.

% A server's engine has not seen the client's hello, so changed options get a new engine; a client's engine switches
% to a new context.
reconfigured({ok, Config}, #{config := Config} = S) ->
    {ok, S};
reconfigured({ok, #{ssl := Ssl} = Config}, #{role := server} = S) ->
    case engine(Config, S) of
        {ok, Engine} -> {ok, S#{config => Config, ssl => Ssl, engine => Engine}};
        Error -> Error
    end;
reconfigured({ok, #{ssl := Ssl} = Config}, #{role := client, engine := Engine} = S) ->
    case clause_ssl_engine:context(client, clause_ssl_options:engine_options(Config)) of
        {ok, Context} ->
            ok = clause_ssl_engine:configure(Engine, Context),
            {ok, S#{config => Config, ssl => Ssl}};
        {error, {Key, Text}} ->
            {error, {options, {Key, Text}}}
    end;
reconfigured(Error, _S) ->
    Error.

% End a paused handshake with a fatal alert, logged as OTP logs it, and close the transport.
end_paused(Code, Reason, #{role := Role, ssl := Ssl} = S) ->
    transmit(clause_ssl_hello:alert(Code), S),
    clause_ssl_alert:log(Code, sent, Role, {user_hello, 2, Reason}, maps:get(log_level, Ssl)),
    close_socket(S),
    S#{phase => closed}.

% The error term of an engine failure, logged as OTP logs alerts.
failure_reason({alert, Origin, Level, Code}, Phase, #{role := Role, ssl := Ssl} = S) ->
    {StateName, Reason} = alert_context(Origin, Code, Phase, S),
    Context = {StateName, Level, Reason},
    clause_ssl_alert:log(Code, Origin, Role, Context, maps:get(log_level, Ssl)),
    clause_ssl_alert:error_reason(Code, Origin, Role, Context);
failure_reason({error, Text}, _Phase, _S) ->
    {tls_alert, {internal_error, Text}}.

alert_context(sent, Code, handshake, #{verify_reason := {Code, Reason}}) -> {certify, Reason};
alert_context(_, _, handshake, _) -> {hello, undefined};
alert_context(_, _, connection, _) -> {connection, undefined}.

%%--------------------------------------------------------------------------------------------------------------------
%% Data
%%--------------------------------------------------------------------------------------------------------------------

send_data(Data, #{engine := Engine, emulated := #{packet := Packet}} = S) ->
    try clause_ssl_engine:write(Engine, framed(Packet, Data)) of
        {ok, Output} -> transmit(Output, S);
        _ -> {error, closed}
    catch
        error:badarg -> {error, badarg}
    end.

framed(Size, Data) when Size == 1; Size == 2; Size == 4 ->
    [<<(iolist_size(Data)):Size/unit:8>>, Data];
framed(_, Data) ->
    Data.

% Deliver what the buffer holds: to a waiting passive reader, or to the owner in active mode; then ask the
% transport for more when someone waits for it.
deliver(#{reader := {From, Length, Timer}} = S) ->
    case take(Length, S) of
        {ok, Data, S1} ->
            cancel(Timer),
            gen_server:reply(From, {ok, Data}),
            {noreply, arm(S1#{reader => undefined})};
        more when map_get(phase, S) == closed ->
            cancel(Timer),
            gen_server:reply(From, {error, maps:get(end_reason, S)}),
            {noreply, S#{reader => undefined}};
        more ->
            {noreply, arm(S)}
    end;
deliver(#{emulated := #{active := false}} = S) ->
    {noreply, S};
deliver(#{owner := Owner, sslsocket := SslSocket} = S) ->
    case take(0, S) of
        {ok, Data, S1} ->
            Owner ! {ssl, SslSocket, Data},
            deliver(count_down(S1));
        more when map_get(phase, S) == closed ->
            Owner ! end_message(maps:get(end_reason, S), SslSocket),
            {stop, normal, S};
        more ->
            {noreply, arm(S)}
    end.

end_message(closed, SslSocket) -> {ssl_closed, SslSocket};
end_message(Reason, SslSocket) -> {ssl_error, SslSocket, Reason}.

% One active message delivered: once turns passive; a count turns passive at zero with ssl_passive.
count_down(#{emulated := #{active := once} = Emulated} = S) ->
    S#{emulated => Emulated#{active => false}};
count_down(#{emulated := #{active := 1} = Emulated, owner := Owner, sslsocket := SslSocket} = S) ->
    Owner ! {ssl_passive, SslSocket},
    S#{emulated => Emulated#{active => false}};
count_down(#{emulated := #{active := N} = Emulated} = S) when is_integer(N) ->
    S#{emulated => Emulated#{active => N - 1}};
count_down(S) ->
    S.

% One packet of the buffer under the emulated packet option: {ok, Data, S} or more.
take(Length, #{buffer := Buffer, emulated := #{packet := Packet} = Emulated} = S) when
    Packet == raw; Packet == 0
->
    case Buffer of
        <<>> -> more;
        _ when Length == 0 -> {ok, format(Buffer, Emulated), S#{buffer => <<>>}};
        <<Data:Length/binary, Rest/binary>> -> {ok, format(Data, Emulated), S#{buffer => Rest}};
        _ -> more
    end;
take(_Length, #{buffer := <<>>}) ->
    more;
take(_Length, #{buffer := Buffer, emulated := #{packet := Packet} = Emulated} = S) ->
    case erlang:decode_packet(Packet, Buffer, packet_options(Packet, Emulated)) of
        {ok, Data, Rest} when is_binary(Data) -> {ok, format(Data, Emulated), S#{buffer => Rest}};
        {ok, Data, Rest} -> {ok, Data, S#{buffer => Rest}};
        _ -> more
    end.

packet_options(line, #{packet_size := Size, line_delimiter := Delimiter}) ->
    [{packet_size, Size}, {line_delimiter, Delimiter}];
packet_options(_, #{packet_size := Size}) ->
    [{packet_size, Size}].

% Data in the emulated mode: a list, or a binary whose first header bytes are a list.
format(Data, #{mode := list}) ->
    binary_to_list(Data);
format(Data, #{header := Header}) when Header > 0 ->
    Split = min(Header, byte_size(Data)),
    <<Head:Split/binary, Rest/binary>> = Data,
    binary_to_list(Head) ++ Rest;
format(Data, _) ->
    Data.

% The peer ended the connection: answer a close_notify, close the transport, and tell the reader or owner.
peer_ended(Reason, #{engine := Engine} = S) ->
    Reason == closed andalso transmit(element(2, clause_ssl_engine:shutdown(Engine)), S),
    close_socket(S),
    deliver(S#{phase => closed, end_reason => Reason}).

%%--------------------------------------------------------------------------------------------------------------------
%% Options and information
%%--------------------------------------------------------------------------------------------------------------------

set_options(Options, #{emulated := Emulated0} = S) ->
    {EmulatedList, SocketList} = clause_ssl_options:split_socket(Options),
    try clause_ssl_options:emulated(EmulatedList, Emulated0#{active => false}) of
        Emulated1 ->
            Active = active(proplists:get_value(active, EmulatedList, keep), maps:get(active, Emulated0)),
            S1 = S#{emulated => Emulated1#{active => Active}},
            case transport_setopts(SocketList, S1) of
                ok -> {ok, S1};
                {error, Reason} -> {error, {options, {socket_options, SocketList, Reason}}}
            end
    catch
        throw:{error, {options, Option}} -> {error, {options, {socket_options, Option}}}
    end.

% The new active mode: an added count over a count, as inet's, else the value set.
active(keep, Current) -> Current;
active(N, Current) when is_integer(N), is_integer(Current) -> count(N + Current);
active(N, _) when is_integer(N) -> count(N);
active(Value, _) -> Value.

count(N) when N =< 0 -> false;
count(N) -> min(N, 32767).

transport_setopts([], _) ->
    ok;
transport_setopts(Options, #{transport := Transport, socket := Socket}) ->
    (socket_module(Transport)):setopts(Socket, Options).

% Options of getopts: the transport's first, then the emulated ones, last asked first, as OTP's.
get_options(Names, #{emulated := Emulated} = S) ->
    {EmulatedNames, SocketNames} = clause_ssl_options:split_socket(Names),
    case transport_getopts(SocketNames, S) of
        {ok, SocketValues} ->
            {ok, SocketValues ++ lists:reverse([{N, maps:get(N, Emulated)} || N <- EmulatedNames])};
        {error, Reason} ->
            {error, {options, {socket_options, SocketNames, Reason}}}
    end.

transport_getopts([], _) ->
    {ok, []};
transport_getopts(Names, #{transport := Transport, socket := Socket}) ->
    (socket_module(Transport)):getopts(Socket, Names).

socket_module(gen_tcp) -> inet;
socket_module(Transport) -> Transport.

information(peercert, #{engine := Engine}) ->
    case clause_ssl_engine:peer_chain(Engine) of
        [Cert | _] -> {ok, Cert};
        [] -> {error, no_peercert}
    end;
information(negotiated_protocol, #{engine := Engine}) ->
    case clause_ssl_engine:info(Engine) of
        #{alpn := undefined} -> {error, protocol_not_negotiated};
        #{alpn := Protocol} -> {ok, Protocol}
    end;
information({connection_information, all}, S) ->
    {ok, connection_items(S) ++ option_items(S)};
information({connection_information, Items}, S) ->
    All = connection_items(S) ++ option_items(S),
    {ok, [Item || Key <- Items, {K, _} = Item <- All, K == Key]};
information(_Request, _S) ->
    {error, closed}.

% The negotiated items of connection_information/1,2.
connection_items(#{engine := Engine, role := Role, ssl := Ssl}) ->
    Info = clause_ssl_engine:info(Engine),
    Items = [
        {protocol, maps:get(protocol, Info)},
        {session_id, maps:get(session_id, Info)},
        {session_resumption, maps:get(resumed, Info)},
        {selected_cipher_suite, clause_ssl_algorithms:code_suite(maps:get(cipher_suite, Info))},
        {selected_group, group(maps:get(group, Info))},
        {sni_hostname, sni_hostname(Role, maps:get(server_name, Info))}
        | secrets(maps:get(keep_secrets, Ssl), Info)
    ],
    [{K, V} || {K, V} <- Items, V =/= undefined].

group(undefined) ->
    undefined;
group(Name) ->
    try
        binary_to_existing_atom(Name)
    catch
        error:badarg -> undefined
    end.

sni_hostname(server, Name) when is_binary(Name) -> binary_to_list(Name);
sni_hostname(_, _) -> undefined.

secrets(true, Info) ->
    [{client_random, maps:get(client_random, Info)}, {server_random, maps:get(server_random, Info)}];
secrets(_, _) ->
    [].

% The connection's ssl options of connection_information/1, as OTP lists its configuration.
option_items(#{ssl := Ssl}) ->
    Keys = [
        verify,
        versions,
        ciphers,
        cacerts,
        depth,
        fail_if_no_peer_cert,
        server_name_indication,
        supported_groups,
        signature_algs,
        honor_cipher_order,
        reuse_sessions,
        handshake,
        log_level,
        alpn_advertised_protocols,
        alpn_preferred_protocols
    ],
    [{K, option_value(K, V)} || K <- Keys, {ok, V} <- [maps:find(K, Ssl)], V =/= undefined].

option_value(ciphers, Codes) -> [clause_ssl_algorithms:code_suite(C) || C <- Codes];
option_value(_, Value) -> Value.

%%--------------------------------------------------------------------------------------------------------------------
%% Transport
%%--------------------------------------------------------------------------------------------------------------------

transmit(<<>>, _S) ->
    ok;
transmit(Output, #{transport := Transport, socket := Socket}) ->
    case Transport:send(Socket, Output) of
        ok -> ok;
        {error, _} -> {error, closed}
    end.

%%--------------------------------------------------------------------------------------------------------------------
%% Shutdown and downgrade (LIB81C2)
%%--------------------------------------------------------------------------------------------------------------------

% shutdown/2 as OTP's: read and write shut the transport down, read_write sends close_notify first; a failure ends
% the connection.
shutdown_transport(read_write, #{engine := Engine} = S) ->
    transmit(element(2, clause_ssl_engine:shutdown(Engine)), S),
    shutdown_reply(transport_shutdown(read_write, S), S);
shutdown_transport(How, S) ->
    shutdown_reply(transport_shutdown(How, S), S).

shutdown_reply(ok, S) ->
    {reply, ok, S};
shutdown_reply(Error, S) ->
    close_socket(S),
    {stop, normal, Error, S#{phase => closed}}.

transport_shutdown(How, #{transport := Transport, socket := Socket}) ->
    try
        Transport:shutdown(Socket, How)
    catch
        error:undef -> {error, notsup}
    end.

% close/2's downgrade: send close_notify and wait for the peer's, then hand the transport socket to the new owner.
start_downgrade(Owner, Timeout, From, #{engine := Engine} = S) ->
    transmit(element(2, clause_ssl_engine:shutdown(Engine)), S),
    Timer = start_timer(Timeout, downgrade_timeout),
    {noreply, arm(S#{phase => downgrade, downgrade => {Owner, From, Timer}})}.

downgrade_received(Bytes, #{engine := Engine} = S) ->
    case clause_ssl_engine:read(Engine, Bytes) of
        {ok, _Plain, _Output} -> {noreply, arm(S)};
        {closed, _Plain, _Output} -> hand_over(S);
        {_Failure, _Plain, _Output} -> downgrade_ended({error, closed}, S)
    end.

% The socket goes to the new owner with OTP's options, and with the bytes that came after the peer's close_notify.
hand_over(#{downgrade := {Owner, From, Timer}, transport := Transport, socket := Socket, engine := Engine} = S) ->
    cancel(Timer),
    (socket_module(Transport)):setopts(Socket, [{active, false}, {mode, binary}, {packet, 0}]),
    ok = Transport:controlling_process(Socket, Owner),
    Reply =
        case clause_ssl_engine:info(Engine) of
            #{unread := <<>>} -> {ok, Socket};
            #{unread := Data} -> {ok, Socket, Data}
        end,
    gen_server:reply(From, Reply),
    {stop, normal, S#{phase => closed, downgrade => undefined}}.

downgrade_ended(Reply, #{downgrade := {_, From, Timer}} = S) ->
    cancel(Timer),
    gen_server:reply(From, Reply),
    close_socket(S),
    {stop, normal, S#{phase => closed, downgrade => undefined}}.

% Ask the transport for the next bytes, once.
arm(#{armed := true} = S) ->
    S;
arm(#{phase := Phase} = S) when Phase == closed; Phase == idle; Phase == user_hello ->
    S;
arm(#{phase := connection, reader := undefined, emulated := #{active := false}} = S) ->
    S;
arm(#{transport := Transport, socket := Socket} = S) ->
    (socket_module(Transport)):setopts(Socket, [{active, once}]),
    S#{armed => true}.

close_transport(#{phase := connection, engine := Engine} = S) ->
    transmit(element(2, clause_ssl_engine:shutdown(Engine)), S),
    close_socket(S);
close_transport(S) ->
    close_socket(S).

close_socket(#{transport := Transport, socket := Socket}) ->
    Transport:close(Socket).

start_timer(infinity, _Tag) -> undefined;
start_timer(Timeout, Tag) -> erlang:start_timer(Timeout, self(), Tag).

cancel(undefined) -> ok;
cancel(Timer) -> erlang:cancel_timer(Timer, [{async, true}, {info, false}]).
