%% TLS connections over loopback (plan 21 step LIB81C1): option errors, a listening socket, clients and servers in
%% passive and active modes, packet framing, options, connection information, controlling processes, certificate
%% verification failures and callbacks, client certificates, TLS 1.2, upgraded gen_tcp sockets and closing. LIB81C2:
%% handshakes paused at the hello, continued or canceled, close/2 downgrades, shutdown/2 and tls_version/1.
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
    hello(Server),
    downgrade(Listen, Server),
    shutdown(Listen, Server),
    show("tls_version", [ssl:tls_version(V) || V <- [{3, 4}, {3, 3}, {254, 253}, {254, 255}]]),
    show("close listen", ssl:close(Listen, 100)),
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

%% Handshakes paused at the hello ({handshake, hello}): the extensions each side sees (the peer's own extensions
%% differ between OTP and libssl, so only their keys and stable values), continuing with new options, canceling and a
%% refused continue option.
hello(Server) ->
    {ok, Listen} = ssl:listen(0, [{ip, {127, 0, 0, 1}}, {active, false}, {handshake, hello} | server_options(Server)]),
    Self = self(),
    Paused = fun(Then) ->
        spawn(fun() ->
            {ok, Socket} = ssl:transport_accept(Listen),
            case ssl:handshake(Socket, 5000) of
                {ok, Same, Extensions} ->
                    Self ! {hello, Same =:= Socket, Extensions},
                    Then(Same);
                Other ->
                    Self ! {hello, alert(Other)}
            end
        end)
    end,
    Paused(fun(Socket) ->
        {ok, Connected} = ssl:handshake_continue(Socket, [{alpn_preferred_protocols, [<<"h2">>]}], 5000),
        Self ! {server, ok, self()},
        echo(Self, Connected)
    end),
    Options = [{alpn_advertised_protocols, [<<"h2">>]}, {active, false} | client_options(Server)],
    {ok, Client, ClientExtensions} = connect(Listen, [{handshake, hello} | Options]),
    receive
        {hello, Same, Extensions} ->
            show("hello same socket", Same),
            show("hello keys", hello_keys(Extensions)),
            show("hello values", [maps:get(K, Extensions) || K <- [sni, alpn, client_hello_versions, cookie]])
    end,
    show("server hello keys", lists:sort(maps:keys(ClientExtensions))),
    show("server hello version", maps:get(server_hello_selected_version, ClientExtensions)),
    {ok, Connected} = ssl:handshake_continue(Client, [], 5000),
    show("continued server", server_result()),
    show("continued alpn", ssl:negotiated_protocol(Connected)),
    ok = ssl:send(Connected, "after hello"),
    show("continued recv", ssl:recv(Connected, 0, 2000)),
    ok = ssl:close(Connected),
    echoed(),
    Paused(fun(Socket) ->
        {ok, Connected12} = ssl:handshake_continue(Socket, [], 5000),
        Self ! {server, ok, self()},
        echo(Self, Connected12)
    end),
    {ok, Client12, ServerHello12} = connect(Listen, [{handshake, hello}, {versions, ['tlsv1.2']} | Options]),
    receive
        {hello, _, Hello12} -> show("tls12 hello keys", hello_keys(Hello12))
    end,
    show("tls12 server hello keys", lists:sort(maps:keys(ServerHello12))),
    show("tls12 server hello", [maps:get(K, ServerHello12) || K <- [renegotiation_info, alpn]]),
    {ok, Continued12} = ssl:handshake_continue(Client12, [], 5000),
    show("tls12 continued server", server_result()),
    show("tls12 continued", ssl:connection_information(Continued12, [protocol])),
    ok = ssl:close(Continued12),
    echoed(),
    Paused(fun(Socket) ->
        Self ! {cancel, ssl:handshake_cancel(Socket), ssl:handshake_cancel(Socket)}
    end),
    show("canceled client", alert(connect(Listen, Options))),
    receive
        {cancel, First, Again} -> show("cancel", [First, Again])
    end,
    flush_hello(),
    Paused(fun(Socket) ->
        Self ! {cancel, ssl:handshake_continue(Socket, [{verify, id(foo)}], 5000)}
    end),
    show("refused continue client", alert(connect(Listen, Options))),
    receive
        {cancel, Refused} -> show("refused continue", Refused)
    end,
    flush_hello(),
    ssl:close(Listen).

%% The keys of a ClientHello's extensions but those only one of OTP's and libssl's hellos has.
hello_keys(Extensions) ->
    lists:sort(maps:keys(Extensions)) -- [renegotiation_info, signature_algs_cert].

flush_hello() ->
    receive
        {hello, _, _} -> ok
    after 2000 -> timeout
    end.

%% close/2: a timeout, and downgrades of both ends to their gen_tcp sockets once each has the other's close_notify;
%% a downgrade whose passive peer does not read times out.
downgrade(Listen, Server) ->
    Self = self(),
    serve(Listen, fun(Socket) ->
        {ok, Data} = ssl:recv(Socket, 0, 2000),
        Self ! {downgrading, Data},
        Downgraded = ssl:close(Socket, {self(), 2000}),
        Self ! {server_downgrade, element(1, Downgraded)},
        {ok, Tcp} = Downgraded,
        Self ! {plain, inet:getopts(Tcp, [active, mode, packet]), gen_tcp:recv(Tcp, 0, 2000)},
        ok = gen_tcp:send(Tcp, "plain back"),
        receive
            done -> gen_tcp:close(Tcp)
        end
    end),
    {ok, Socket} = connect(Listen, [{active, false} | client_options(Server)]),
    server_result(),
    ok = ssl:send(Socket, "before"),
    receive
        {downgrading, Data} -> show("downgrading", Data)
    end,
    {ok, Tcp} = ssl:close(Socket, {self(), 2000}),
    receive
        {server_downgrade, Result} -> show("server downgrade", Result)
    end,
    ok = gen_tcp:send(Tcp, "plain"),
    receive
        {plain, Options, Plain} -> show("server plain", [Options, Plain])
    end,
    show("client plain", gen_tcp:recv(Tcp, 0, 2000)),
    get(server) ! done,
    gen_tcp:close(Tcp),
    show("downgraded close", ssl:close(Socket)),
    serve(Listen, fun(Peer) ->
        receive
            read -> Self ! {echo, ssl:recv(Peer, 0, 1000)}
        end
    end),
    {ok, Unread} = connect(Listen, [{active, false} | client_options(Server)]),
    server_result(),
    show("downgrade timeout", ssl:close(Unread, {self(), 300})),
    get(server) ! read,
    show("downgrade timeout peer", echoed()),
    serve(Listen, fun(Peer) -> echo(Self, Peer) end),
    {ok, Timed} = connect(Listen, [{active, false} | client_options(Server)]),
    server_result(),
    show("close timeout", ssl:close(Timed, 300)),
    show("close timeout peer", echoed()).

%% shutdown/2: write shuts the transport down without close_notify, so the peer sees the transport close; a listening
%% socket is not connected.
shutdown(Listen, Server) ->
    Self = self(),
    serve(Listen, fun(Socket) ->
        First = ssl:recv(Socket, 0, 2000),
        Second = ssl:recv(Socket, 0, 2000),
        Self ! {shutdown_peer, [First, Second, ssl:send(Socket, "late")]}
    end),
    {ok, Socket} = connect(Listen, [{active, false} | client_options(Server)]),
    server_result(),
    ok = ssl:send(Socket, "hi"),
    show("shutdown write", ssl:shutdown(Socket, write)),
    receive
        {shutdown_peer, Peer} -> show("shutdown peer", Peer)
    end,
    show("recv after shutdown", ssl:recv(Socket, 0, 2000)),
    show("shutdown closed", ssl:shutdown(Socket, read_write)),
    show("shutdown close", ssl:close(Socket)),
    show("shutdown listen", ssl:shutdown(Listen, write)).
