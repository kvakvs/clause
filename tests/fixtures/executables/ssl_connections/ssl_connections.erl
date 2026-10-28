%% TLS connections over loopback (plan 21 step LIB81C1): option errors, a listening socket, clients and servers in
%% passive and active modes, packet framing, options, connection information, controlling processes, certificate
%% verification failures and callbacks, client certificates, TLS 1.2, upgraded gen_tcp sockets and closing.
-module(ssl_connections).
-export([main/1]).

-include_lib("public_key/include/public_key.hrl").

main(_) ->
    ok = ssl:start(),
    Server = chain("localhost"),
    Client = chain("client"),
    option_errors(Server),
    Listen = listening(Server),
    passive(Listen, Server),
    active(Listen, Server),
    framing(Listen, Server),
    owners(Listen, Server),
    verification(Listen, Server),
    mutual(Server, Client),
    tls12(Listen, Server),
    upgrade(Server),
    ok = ssl:close(Listen),
    io:format("transport_accept closed ~p~n", [ssl:transport_accept(Listen, 100)]),
    io:format("ssl_connections: ok~n").

%% A generated root and peer whose certificate names Host: the peer's certificate, its key and the root.
chain(Host) ->
    AltName = #'Extension'{extnID = ?'id-ce-subjectAltName', extnValue = [{dNSName, Host}], critical = false},
    Config = public_key:pkix_test_data(#{
        root => [{key, {namedCurve, secp256r1}}, {digest, sha256}],
        intermediates => [],
        peer => [{key, {namedCurve, secp256r1}}, {digest, sha256}, {extensions, [AltName]}]
    }),
    #{
        cert => proplists:get_value(cert, Config),
        key => proplists:get_value(key, Config),
        root => lists:last(proplists:get_value(cacerts, Config))
    }.

server_options(#{cert := Cert, key := Key}) ->
    [{cert, Cert}, {key, Key}, {log_level, none}].

client_options(#{root := Root}) ->
    [{cacerts, [Root]}, {log_level, none}].

id(X) -> X.

show(Label, Value) -> io:format("~s ~p~n", [Label, Value]).

%% Connection information with its suite map as a sorted list: map printing order is not Clause's.
sorted(Info) ->
    [
        {Key,
            if
                is_map(Value) -> lists:sort(maps:to_list(Value));
                true -> Value
            end}
     || {Key, Value} <- Info
    ].

%% Options ssl refuses before it connects or listens.
option_errors(#{cert := Cert, key := Key}) ->
    Connect = fun(Options) -> ssl:connect("localhost", 1, [{log_level, none} | Options], 1000) end,
    Listen = fun(Options) -> ssl:listen(0, [{log_level, none} | Options]) end,
    show("no cacerts", Connect([])),
    show("verify", Connect([{verify, id(bad)}])),
    show("versions", Connect([{verify, verify_none}, {versions, [x]}])),
    show("version gap", Connect([{verify, verify_none}, {versions, ['tlsv1.3', 'tlsv1.1']}])),
    show("ciphers", Connect([{verify, verify_none}, {ciphers, [foo]}])),
    show("server_name_indication", Connect([{verify, verify_none}, {server_name_indication, 1}])),
    show("alpn", Connect([{verify, verify_none}, {alpn_advertised_protocols, [<<>>]}])),
    show("server only", Connect([{verify, verify_none}, {alpn_preferred_protocols, [<<"a">>]}])),
    show("fail_if_no_peer_cert", Connect([{verify, verify_none}, {fail_if_no_peer_cert, true}])),
    show("depth", Connect([{verify, verify_none}, {depth, -1}])),
    show("log_level", ssl:connect("localhost", 1, [{verify, verify_none}, {log_level, foo}])),
    show("cacerts", Connect([{cacerts, foo}])),
    show("last wins", Connect([{verify, verify_none}, {verify, foo}])),
    show("no options", ssl:listen(0, [])),
    show("not a tuple", Listen([{cert, Cert}, {key, Key}, foo])),
    show("active", Listen([{cert, Cert}, {key, Key}, {active, foo}])),
    show("mode", Listen([{cert, Cert}, {key, Key}, {mode, foo}])),
    show("key", Listen([{cert, Cert}, {key, {bad, <<>>}}])),
    show("certfile", Listen([{certfile, "no_such_file.pem"}])),
    show("incompatible", Listen([{cert, Cert}, {key, Key}, {fail_if_no_peer_cert, true}])).

%% The listening socket: its address, options and the calls that need a connection.
listening(Server) ->
    {ok, Listen} = ssl:listen(0, [{ip, {127, 0, 0, 1}}, {active, false} | server_options(Server)]),
    {ok, {Address, _}} = ssl:sockname(Listen),
    show("listen address", Address),
    show("listen getopts", ssl:getopts(Listen, [active, mode, packet, header])),
    show("listen recv", ssl:recv(Listen, 0)),
    show("listen send", ssl:send(Listen, "x")),
    show("listen peername", ssl:peername(Listen)),
    show("listen peercert", ssl:peercert(Listen)),
    show("listen getstat", ssl:getstat(Listen, [recv_cnt])),
    show("transport_accept timeout", ssl:transport_accept(Listen, 10)),
    Listen.

port(Listen) ->
    {ok, {_, Port}} = ssl:sockname(Listen),
    Port.

%% Accept one connection in a new process that runs Serve on it after the handshake; the handshake's result
%% (tls_alert texts left out) arrives as {server, Result}.
serve(Listen, Serve) ->
    serve(Listen, [], Serve).

serve(Listen, Options, Serve) ->
    Self = self(),
    spawn(fun() ->
        {ok, Socket} = ssl:transport_accept(Listen),
        case ssl:handshake(Socket, Options, 5000) of
            {ok, Connected} ->
                Self ! {server, ok, self()},
                Serve(Connected);
            Error ->
                Self ! {server, alert(Error), self()}
        end
    end),
    ok.

server_result() ->
    receive
        {server, Result, Pid} ->
            put(server, Pid),
            Result
    after 5000 -> timeout
    end.

%% An error with its tls_alert text left out: OTP's texts name its own states and source lines.
alert({error, {tls_alert, {Description, _Text}}}) -> {error, {tls_alert, Description}};
alert(Other) -> Other.

connect(Listen, Options) ->
    ssl:connect("localhost", port(Listen), Options, 5000).

%% Echo every passive read until the peer closes; report the close.
echo(Parent, Socket) ->
    case ssl:recv(Socket, 0) of
        {ok, Data} ->
            ok = ssl:send(Socket, Data),
            echo(Parent, Socket);
        {error, Reason} ->
            Parent ! {echo, Reason}
    end.

echoed() ->
    receive
        {echo, Reason} -> Reason
    after 5000 -> timeout
    end.

%% A passive client: information, options, data both ways, timeouts and closing.
passive(Listen, Server) ->
    Self = self(),
    serve(Listen, fun(Socket) -> echo(Self, Socket) end),
    {ok, Socket} = connect(Listen, [binary, {active, false} | client_options(Server)]),
    show("server handshake", server_result()),
    show("sslsocket", element(1, Socket)),
    {ok, Info} = ssl:connection_information(Socket, [protocol, selected_cipher_suite, sni_hostname]),
    show("information", sorted(Info)),
    {ok, All} = ssl:connection_information(Socket),
    show("information has", [lists:keymember(Key, 1, All) || Key <- [protocol, selected_cipher_suite, verify]]),
    show("negotiated_protocol", ssl:negotiated_protocol(Socket)),
    show("peercert", ssl:peercert(Socket) =:= {ok, maps:get(cert, Server)}),
    {ok, {PeerAddress, _}} = ssl:peername(Socket),
    {ok, {Address, Port}} = ssl:sockname(Socket),
    show("peername", PeerAddress),
    show("sockname", {Address, Port =/= port(Listen)}),
    show("getopts", ssl:getopts(Socket, [active, mode, packet, header, nodelay])),
    ok = ssl:send(Socket, [<<"pi">>, "ng"]),
    show("recv", ssl:recv(Socket, 0, 2000)),
    ok = ssl:send(Socket, <<"abcdef">>),
    show("recv 4", ssl:recv(Socket, 4, 2000)),
    show("recv 2", ssl:recv(Socket, 2, 2000)),
    show("recv timeout", ssl:recv(Socket, 0, 50)),
    {ok, Stats} = ssl:getstat(Socket, [send_cnt, recv_cnt]),
    show("getstat", [Key || {Key, Count} <- Stats, Count > 0]),
    ok = ssl:setopts(Socket, [{mode, list}]),
    ok = ssl:send(Socket, "list"),
    show("list mode", ssl:recv(Socket, 0, 2000)),
    show("close", ssl:close(Socket)),
    show("server saw", echoed()),
    show("recv closed", ssl:recv(Socket, 0)),
    show("send closed", ssl:send(Socket, "x")),
    show("close again", ssl:close(Socket)).

%% Messages that arrived for Socket until a pause.
messages(Socket) ->
    receive
        {ssl, Socket, Data} -> [{ssl, Data} | messages(Socket)];
        {ssl_closed, Socket} -> [ssl_closed | messages(Socket)];
        {ssl_error, Socket, Reason} -> [{ssl_error, Reason} | messages(Socket)];
        {ssl_passive, Socket} -> [ssl_passive | messages(Socket)]
    after 300 -> []
    end.

%% Active modes: true, once, a count down to ssl_passive, then the server closing. Packets keep the server's sends
%% apart.
active(Listen, Server) ->
    Self = self(),
    serve(Listen, fun(Socket) ->
        ok = ssl:setopts(Socket, [{packet, 2}]),
        [ok = ssl:send(Socket, Text) || Text <- ["one", "two"]],
        receive
            more -> [ok = ssl:send(Socket, Text) || Text <- ["three", "four", "five"]]
        end,
        receive
            close -> Self ! {echo, ssl:close(Socket)}
        end
    end),
    {ok, Socket} = connect(Listen, [{active, true}, {packet, 2} | client_options(Server)]),
    server_result(),
    timer:sleep(100),
    show("active true", messages(Socket)),
    ok = ssl:setopts(Socket, [{active, false}]),
    show("recv active", ssl:recv(Socket, 0, 10)),
    ok = ssl:setopts(Socket, [{active, 2}]),
    get(server) ! more,
    timer:sleep(100),
    show("active 2", messages(Socket)),
    show("getopts passive", ssl:getopts(Socket, [active])),
    ok = ssl:setopts(Socket, [{active, once}]),
    show("active once", messages(Socket)),
    ok = ssl:setopts(Socket, [{active, true}]),
    get(server) ! close,
    show("server close", echoed()),
    show("after close", messages(Socket)).

%% Packet framing: 2-byte headers both ways, and a binary header.
framing(Listen, Server) ->
    Self = self(),
    serve(Listen, fun(Socket) ->
        ok = ssl:setopts(Socket, [{packet, 2}, {mode, binary}]),
        echo(Self, Socket)
    end),
    {ok, Socket} = connect(Listen, [{packet, 2}, {active, false}, binary | client_options(Server)]),
    server_result(),
    ok = ssl:send(Socket, <<"framed">>),
    show("packet 2", ssl:recv(Socket, 0, 2000)),
    ok = ssl:setopts(Socket, [{header, 2}]),
    ok = ssl:send(Socket, <<"header">>),
    show("header 2", ssl:recv(Socket, 0, 2000)),
    ok = ssl:setopts(Socket, [{mode, list}]),
    ok = ssl:send(Socket, <<"list">>),
    show("header list", ssl:recv(Socket, 0, 2000)),
    show("framing getopts", ssl:getopts(Socket, [packet, header, mode])),
    ok = ssl:close(Socket),
    show("framing server saw", echoed()).

%% Controlling processes: the new owner gets the messages; the owner's exit closes the connection.
owners(Listen, Server) ->
    Self = self(),
    serve(Listen, fun(Socket) -> echo(Self, Socket) end),
    {ok, Socket} = connect(Listen, [{active, true} | client_options(Server)]),
    server_result(),
    New = spawn(fun() ->
        receive
            go -> ok
        end,
        ok = ssl:send(Socket, "again"),
        receive
            {ssl, Socket, Data} -> Self ! {new, Data}
        after 2000 -> Self ! {new, timeout}
        end
    end),
    show("controlling_process", ssl:controlling_process(Socket, New)),
    New ! go,
    receive
        {new, Data} -> show("new owner", Data)
    end,
    show("owner messages", messages(Socket)),
    show("owner exit closes", echoed()).

%% Certificate checks: an unknown CA on either side, a host name the certificate lacks, a verify_fun accepting an
%% unknown CA, and a partial_chain trusting the root the server sends.
verification(Listen, Server) ->
    Self = self(),
    Other = chain("localhost"),
    serve(Listen, fun(_) -> ok end),
    show("unknown ca client", alert(connect(Listen, client_options(Other)))),
    show("unknown ca server", server_result()),
    serve(Listen, fun(_) -> ok end),
    Named = ssl:connect({127, 0, 0, 1}, port(Listen), [{server_name_indication, "other.test"} | client_options(Server)]),
    show("host name", alert(Named)),
    show("host name server", server_result()),
    Accept = fun
        (_, {bad_cert, unknown_ca} = Event, Count) ->
            Self ! {seen, Event},
            {valid, Count + 1};
        (_, {bad_cert, _} = Reason, _) ->
            {fail, Reason};
        (_, {extension, _}, Count) ->
            {unknown, Count};
        (_, Event, Count) ->
            Self ! {seen, Event},
            {valid, Count + 1}
    end,
    serve(Listen, fun(Socket) -> echo(Self, Socket) end),
    {ok, Accepted} = connect(Listen, [{verify_fun, {Accept, 0}} | client_options(Other)]),
    show("verify_fun server", server_result()),
    show("verify_fun", seen()),
    ssl:close(Accepted),
    echoed(),
    #{root := Root} = Server,
    {ok, Sending} = ssl:listen(0, [{ip, {127, 0, 0, 1}}, {active, false}, {cacerts, [Root]} | server_options(Server)]),
    Partial = fun(Chain) ->
        Self ! {seen, {partial_chain, length(Chain), lists:member(Root, Chain)}},
        {trusted_ca, Root}
    end,
    serve(Sending, fun(Socket) -> echo(Self, Socket) end),
    {ok, Trusted} = connect(Sending, [{partial_chain, Partial} | client_options(Other)]),
    show("partial_chain server", server_result()),
    show("partial_chain", seen()),
    ssl:close(Trusted),
    echoed(),
    ssl:close(Sending).

%% The {seen, Event} messages of a verification callback.
seen() ->
    receive
        {seen, Event} -> [Event | seen()]
    after 100 -> []
    end.

%% A server that asks for client certificates: accepted, and refused without one.
mutual(Server, Client) ->
    Options = [{verify, verify_peer}, {cacerts, [maps:get(root, Client)]}, {fail_if_no_peer_cert, true}],
    {ok, Listen} = ssl:listen(0, [{ip, {127, 0, 0, 1}}, {active, false} | Options ++ server_options(Server)]),
    Self = self(),
    serve(Listen, fun(Socket) ->
        Self ! {peer, ssl:peercert(Socket)},
        echo(Self, Socket)
    end),
    #{cert := Cert, key := Key} = Client,
    {ok, Socket} = connect(Listen, [{cert, Cert}, {key, Key}, {active, false} | client_options(Server)]),
    show("mutual server", server_result()),
    receive
        {peer, Peer} -> show("mutual peercert", Peer =:= {ok, Cert})
    end,
    ok = ssl:send(Socket, "mutual"),
    show("mutual recv", ssl:recv(Socket, 0, 2000)),
    ok = ssl:close(Socket),
    echoed(),
    serve(Listen, fun(_) -> ok end),
    {ok, Anonymous} = connect(Listen, [{active, false} | client_options(Server)]),
    show("no certificate server", server_result()),
    show("no certificate client", alert(ssl:recv(Anonymous, 0, 2000))),
    ssl:close(Listen).

%% TLS 1.2 with ALPN.
tls12(Listen, Server) ->
    Self = self(),
    serve(Listen, [{alpn_preferred_protocols, [<<"h2">>, <<"http/1.1">>]}], fun(Socket) -> echo(Self, Socket) end),
    {ok, Socket} = connect(Listen, [
        {versions, ['tlsv1.2']}, {alpn_advertised_protocols, [<<"http/1.1">>, <<"h2">>]} | client_options(Server)
    ]),
    server_result(),
    {ok, Info} = ssl:connection_information(Socket, [protocol, selected_cipher_suite]),
    show("tls12", sorted(Info)),
    show("tls12 alpn", ssl:negotiated_protocol(Socket)),
    ssl:close(Socket),
    echoed().

%% gen_tcp sockets upgraded to TLS by ssl:connect/3 and ssl:handshake/3.
upgrade(Server) ->
    {ok, Listen} = gen_tcp:listen(0, [{ip, {127, 0, 0, 1}}, {active, false}]),
    {ok, Port} = inet:port(Listen),
    Self = self(),
    spawn(fun() ->
        {ok, Tcp} = gen_tcp:accept(Listen),
        {ok, Socket} = ssl:handshake(Tcp, [{active, false} | server_options(Server)], 5000),
        Self ! {server, ok, self()},
        echo(Self, Socket)
    end),
    {ok, Tcp} = gen_tcp:connect({127, 0, 0, 1}, Port, [binary, {active, false}]),
    {ok, Socket} = ssl:connect(Tcp, [{server_name_indication, "localhost"} | client_options(Server)], 5000),
    show("upgrade server", server_result()),
    ok = ssl:send(Socket, "upgraded"),
    show("upgrade recv", ssl:recv(Socket, 0, 2000)),
    ok = ssl:close(Socket),
    show("upgrade server saw", echoed()),
    gen_tcp:close(Listen).
