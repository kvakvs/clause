%% Loopback socket benchmarks (plan step 69A): TCP echo throughput, connect/accept rate, UDP round trips.
-module(bench_socket).
-export([main/1]).

-define(TCP_OPTIONS, [binary, {packet, 4}, {active, false}]).
%% Only listening sockets reuse addresses: on Windows a reusing client may get eaddrinuse.
-define(LISTEN_OPTIONS, [{reuseaddr, true}, {ip, {127, 0, 0, 1}} | ?TCP_OPTIONS]).

main(_Args) ->
    Payload = bench_util:bytes(1024),
    bench_util:run("socket.tcp_echo", 3, fun() -> tcp_echo(Payload, 10000) end),
    bench_util:run("socket.tcp_connect", 3, fun() -> tcp_connect(Payload, 300, 25) end),
    bench_util:run("socket.udp_round_trip", 3, fun() -> udp_round_trip(Payload, 9300) end),
    bench_util:done("bench_socket").

%% Send Count framed messages to an echo server and wait for each reply.
tcp_echo(Payload, Count) ->
    {ok, Listen} = gen_tcp:listen(0, ?LISTEN_OPTIONS),
    {ok, Port} = inet:port(Listen),
    Server = spawn_link(fun() -> echo_server(Listen) end),
    {ok, Socket} = gen_tcp:connect({127, 0, 0, 1}, Port, ?TCP_OPTIONS),
    echo_loop(Socket, Payload, Count),
    ok = gen_tcp:close(Socket),
    await_exit(Server),
    gen_tcp:close(Listen).

echo_server(Listen) ->
    {ok, Socket} = gen_tcp:accept(Listen),
    echo(Socket).

echo(Socket) ->
    case gen_tcp:recv(Socket, 0) of
        {ok, Data} ->
            ok = gen_tcp:send(Socket, Data),
            echo(Socket);
        {error, closed} ->
            gen_tcp:close(Socket)
    end.

echo_loop(_Socket, _Payload, 0) ->
    ok;
echo_loop(Socket, Payload, N) ->
    ok = gen_tcp:send(Socket, Payload),
    {ok, Payload} = gen_tcp:recv(Socket, 0),
    echo_loop(Socket, Payload, N - 1).

%% Open Count short sessions of Exchanges echo round trips against one listening socket; more exchanges, not
%% more connections, keep the socket count low. The server closes first, so TIME_WAIT stays on the listening
%% port instead of using up the client's ephemeral ports.
tcp_connect(Payload, Count, Exchanges) ->
    {ok, Listen} = gen_tcp:listen(0, [{backlog, 128} | ?LISTEN_OPTIONS]),
    {ok, Port} = inet:port(Listen),
    Acceptor = spawn_link(fun() -> accept_n(Listen, Count, Exchanges) end),
    connect_n(Port, Payload, Count, Exchanges),
    await_exit(Acceptor),
    gen_tcp:close(Listen).

accept_n(_Listen, 0, _Exchanges) ->
    ok;
accept_n(Listen, N, Exchanges) ->
    {ok, Socket} = gen_tcp:accept(Listen),
    reply_n(Socket, Exchanges),
    ok = gen_tcp:close(Socket),
    accept_n(Listen, N - 1, Exchanges).

%% Echo Count framed messages back to the client of one session.
reply_n(_Socket, 0) ->
    ok;
reply_n(Socket, N) ->
    {ok, Data} = gen_tcp:recv(Socket, 0),
    ok = gen_tcp:send(Socket, Data),
    reply_n(Socket, N - 1).

connect_n(_Port, _Payload, 0, _Exchanges) ->
    ok;
connect_n(Port, Payload, N, Exchanges) ->
    {ok, Socket} = gen_tcp:connect({127, 0, 0, 1}, Port, ?TCP_OPTIONS),
    echo_loop(Socket, Payload, Exchanges),
    {error, closed} = gen_tcp:recv(Socket, 0),
    ok = gen_tcp:close(Socket),
    connect_n(Port, Payload, N - 1, Exchanges).

%% Bounce one datagram between two UDP sockets Count times.
udp_round_trip(Payload, Count) ->
    Options = [binary, {active, false}, {ip, {127, 0, 0, 1}}],
    {ok, Server} = gen_udp:open(0, Options),
    {ok, ServerPort} = inet:port(Server),
    Echo = spawn_link(fun() -> udp_echo(Server, Count) end),
    {ok, Client} = gen_udp:open(0, Options),
    udp_loop(Client, ServerPort, Payload, Count),
    await_exit(Echo),
    ok = gen_udp:close(Client),
    gen_udp:close(Server).

udp_echo(_Socket, 0) ->
    ok;
udp_echo(Socket, N) ->
    {ok, {Address, Port, Data}} = gen_udp:recv(Socket, 0),
    ok = gen_udp:send(Socket, Address, Port, Data),
    udp_echo(Socket, N - 1).

udp_loop(_Socket, _Port, _Payload, 0) ->
    ok;
udp_loop(Socket, Port, Payload, N) ->
    ok = gen_udp:send(Socket, {127, 0, 0, 1}, Port, Payload),
    {ok, {_, _, Payload}} = gen_udp:recv(Socket, 0),
    udp_loop(Socket, Port, Payload, N - 1).

%% Wait until a linked helper process has finished its work.
await_exit(Pid) ->
    Ref = erlang:monitor(process, Pid),
    receive
        {'DOWN', Ref, process, Pid, _} -> ok
    end.
