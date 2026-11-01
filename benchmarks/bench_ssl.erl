%% Loopback TLS benchmarks (added with the distribution benchmarks): TLS 1.3 echo round trips, full handshakes and a
%% bulk transfer, server and client in this node; certificates generated once with public_key:pkix_test_data/1.
-module(bench_ssl).
-export([main/1]).

-include_lib("public_key/include/public_key.hrl").

-define(TLS_OPTIONS, [binary, {packet, 4}, {active, false}, {versions, ['tlsv1.3']}, {log_level, none}]).

main(_Args) ->
    ok = ssl:start(),
    Chain = chain(),
    Payload = bench_util:bytes(1024),
    bench_util:run("ssl.tls_echo", 3, fun() -> tls_echo(Chain, Payload, 5000) end),
    bench_util:run("ssl.tls_handshake", 3, fun() -> tls_handshakes(Chain, Payload, 50) end),
    bench_util:run("ssl.tls_bulk", 3, fun() -> tls_bulk(Chain, bench_util:bytes(16384), 1024) end),
    bench_util:done("bench_ssl").

%% A generated root and a server certificate naming localhost: the server options and the client options trusting it.
chain() ->
    AltName = #'Extension'{extnID = ?'id-ce-subjectAltName', extnValue = [{dNSName, "localhost"}], critical = false},
    Config = public_key:pkix_test_data(#{
        root => [{key, {namedCurve, secp256r1}}, {digest, sha256}],
        intermediates => [],
        peer => [{key, {namedCurve, secp256r1}}, {digest, sha256}, {extensions, [AltName]}]
    }),
    Root = lists:last(proplists:get_value(cacerts, Config)),
    Server = [{cert, proplists:get_value(cert, Config)}, {key, proplists:get_value(key, Config)}],
    Client = [{cacerts, [Root]}, {verify, verify_peer}, {server_name_indication, "localhost"}],
    {Server, Client}.

%% A listening socket on loopback and its port.
listen({Server, _}) ->
    {ok, Listen} = ssl:listen(0, [{reuseaddr, true}, {ip, {127, 0, 0, 1}} | Server ++ ?TLS_OPTIONS]),
    {ok, {_, Port}} = ssl:sockname(Listen),
    {Listen, Port}.

connect(Port, {_, Client}) ->
    {ok, Socket} = ssl:connect({127, 0, 0, 1}, Port, Client ++ ?TLS_OPTIONS),
    Socket.

accept(Listen) ->
    {ok, Transport} = ssl:transport_accept(Listen),
    {ok, Socket} = ssl:handshake(Transport),
    Socket.

%% Count echo round trips of Payload over one connection.
tls_echo(Chain, Payload, Count) ->
    {Listen, Port} = listen(Chain),
    Server = spawn_link(fun() -> echo(accept(Listen)) end),
    Socket = connect(Port, Chain),
    echo_loop(Socket, Payload, Count),
    ok = ssl:close(Socket),
    await_exit(Server),
    ssl:close(Listen).

echo(Socket) ->
    case ssl:recv(Socket, 0) of
        {ok, Data} ->
            ok = ssl:send(Socket, Data),
            echo(Socket);
        {error, closed} ->
            ssl:close(Socket)
    end.

echo_loop(_Socket, _Payload, 0) ->
    ok;
echo_loop(Socket, Payload, N) ->
    ok = ssl:send(Socket, Payload),
    {ok, Payload} = ssl:recv(Socket, 0),
    echo_loop(Socket, Payload, N - 1).

%% Count connections, each a full handshake and one echo round trip; the server closes first.
tls_handshakes(Chain, Payload, Count) ->
    {Listen, Port} = listen(Chain),
    Acceptor = spawn_link(fun() -> accept_n(Listen, Count) end),
    connect_n(Port, Chain, Payload, Count),
    await_exit(Acceptor),
    ssl:close(Listen).

accept_n(_Listen, 0) ->
    ok;
accept_n(Listen, N) ->
    Socket = accept(Listen),
    {ok, Data} = ssl:recv(Socket, 0),
    ok = ssl:send(Socket, Data),
    ok = ssl:close(Socket),
    accept_n(Listen, N - 1).

connect_n(_Port, _Chain, _Payload, 0) ->
    ok;
connect_n(Port, Chain, Payload, N) ->
    Socket = connect(Port, Chain),
    echo_loop(Socket, Payload, 1),
    {error, closed} = ssl:recv(Socket, 0),
    ok = ssl:close(Socket),
    connect_n(Port, Chain, Payload, N - 1).

%% Count chunks sent one way, then one acknowledgement back.
tls_bulk(Chain, Chunk, Count) ->
    {Listen, Port} = listen(Chain),
    Server = spawn_link(fun() -> sink(accept(Listen), Count) end),
    Socket = connect(Port, Chain),
    send_n(Socket, Chunk, Count),
    {ok, <<"done">>} = ssl:recv(Socket, 0),
    ok = ssl:close(Socket),
    await_exit(Server),
    ssl:close(Listen).

sink(Socket, 0) ->
    ok = ssl:send(Socket, <<"done">>),
    {error, closed} = ssl:recv(Socket, 0),
    ssl:close(Socket);
sink(Socket, N) ->
    {ok, _} = ssl:recv(Socket, 0),
    sink(Socket, N - 1).

send_n(_Socket, _Chunk, 0) ->
    ok;
send_n(Socket, Chunk, N) ->
    ok = ssl:send(Socket, Chunk),
    send_n(Socket, Chunk, N - 1).

%% Wait until a linked helper process has finished its work.
await_exit(Pid) ->
    Ref = erlang:monitor(process, Pid),
    receive
        {'DOWN', Ref, process, Pid, _} -> ok
    end.
