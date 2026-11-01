%% Distribution benchmarks (added with plan 30's net_kernel step DIST6): message round trips, data exchange, remote
%% spawns and reconnects between this node and a peer node it starts, over TCP; then the same between two started
%% nodes over TLS (inet_tls_dist), measured by one of them and relayed here. Clause starts copies of its executable as
%% the nodes, OTP erl. Nodes listen on loopback; run.py gives every run its own epmd port (ERL_EPMD_PORT), which OTP
%% serves from an epmd this program starts and Clause from its bundled epmd. Clause has no TLS distribution before plan
%% 30 step DIST12 and skips those kernels.
-module(bench_dist).
-export([main/1, echo_once/1]).

-define(COOKIE, "bench").
-define(LOOPBACK, "{127,0,0,1}").
% Seconds a starting node gets to answer, and a peer without its driver's connection stays alive.
-define(START_SECONDS, 60).
-define(ORPHAN_MS, 10000).

main([]) ->
    Epmd = start_epmd(),
    tcp(),
    tls(),
    stop_epmd(Epmd),
    bench_util:done("bench_dist");
main(["peer"]) ->
    peer();
main(["driver", Prefix, Peer]) ->
    Node = list_to_atom(Peer),
    kernels(Prefix, Node, await_peer(Node)),
    stop_peer(Node),
    erlang:halt().

%% TCP distribution: this node against a started peer.
tcp() ->
    ok = application:set_env(kernel, inet_dist_use_interface, {127, 0, 0, 1}),
    {ok, _} = net_kernel:start(bench_dist@localhost, #{name_domain => shortnames}),
    true = erlang:set_cookie(list_to_atom(?COOKIE)),
    Port = start_node("bench_dist_peer", ["peer"], []),
    Node = bench_dist_peer@localhost,
    kernels("dist", Node, await_peer(Node)),
    stop_peer(Node),
    await_port(Port),
    ok = net_kernel:stop().

%% TLS distribution: a driver node measures against a peer node; its output, the kernel lines, is printed here.
tls() ->
    case tls_distribution() of
        false ->
            ok;
        true ->
            Flags = ["-proto_dist", "inet_tls", "-ssl_dist_optfile", tls_options()],
            Peer = start_node("bench_tls_peer", ["peer"], Flags),
            Driver = start_node("bench_tls_driver", ["driver", "dist_tls", "bench_tls_peer@localhost"], Flags),
            relay(Driver),
            await_port(Peer)
    end.

% Only OTP provides inet_tls_dist so far.
tls_distribution() -> otp() andalso element(1, code:ensure_loaded(inet_tls_dist)) =:= module.

kernels(Prefix, Node, Peer) ->
    Data = {bench_util:bytes(65536), [{N, integer_to_list(N), float(N)} || N <- lists:seq(1, 1000)]},
    bench_util:run(Prefix ++ ".ping_pong", 3, fun() -> ping_pong(Peer, 5000) end),
    bench_util:run(Prefix ++ ".data_exchange", 3, fun() -> exchange(Peer, Data, 200) end),
    bench_util:run(Prefix ++ ".spawn", 3, fun() -> remote_spawns(Node, 500) end),
    bench_util:run(Prefix ++ ".reconnect", 3, fun() -> reconnects(Node, Peer, 20) end).

%% Count round trips of a small message to the peer process.
ping_pong(_Peer, 0) ->
    ok;
ping_pong(Peer, N) ->
    Peer ! {ping, self(), N},
    receive
        {pong, N} -> ping_pong(Peer, N - 1)
    end.

%% Count round trips of Data, a 64 KiB binary and a list of 1000 small tuples, encoded and decoded both ways.
exchange(_Peer, _Data, 0) ->
    ok;
exchange(Peer, Data, N) ->
    Peer ! {ping, self(), {N, Data}},
    receive
        {pong, {N, _}} -> exchange(Peer, Data, N - 1)
    end.

%% Count processes spawned on Node one after another, each answering once.
remote_spawns(_Node, 0) ->
    ok;
remote_spawns(Node, N) ->
    Pid = spawn(Node, ?MODULE, echo_once, [self()]),
    receive
        {echoed, Pid} -> remote_spawns(Node, N - 1)
    end.

echo_once(Parent) -> Parent ! {echoed, self()}.

%% Count disconnects and connection setups (handshakes), each followed by one round trip.
reconnects(_Node, _Peer, 0) ->
    ok;
reconnects(Node, Peer, N) ->
    true = erlang:disconnect_node(Node),
    true = net_kernel:connect_node(Node),
    ping_pong(Peer, 1),
    reconnects(Node, Peer, N - 1).

%% The peer: answers pings; stops when told, or when its driver stays disconnected for ORPHAN_MS.
peer() ->
    ok = net_kernel:monitor_nodes(true),
    register(bench_peer, self()),
    peer_loop().

peer_loop() ->
    receive
        {ping, From, Message} ->
            From ! {pong, Message},
            peer_loop();
        {ready, From} ->
            From ! {peer, self()},
            peer_loop();
        {nodeup, _} ->
            peer_loop();
        {nodedown, _} ->
            receive
                {nodeup, _} -> peer_loop()
            after ?ORPHAN_MS -> erlang:halt()
            end;
        stop ->
            erlang:halt()
    end.

%% The peer process of Node once its node is up and connected.
await_peer(Node) -> await_peer(Node, erlang:monotonic_time(second) + ?START_SECONDS).

await_peer(Node, Deadline) ->
    erlang:monotonic_time(second) > Deadline andalso erlang:error({peer_not_started, Node}),
    case net_kernel:connect_node(Node) of
        true ->
            {bench_peer, Node} ! {ready, self()},
            receive
                {peer, Pid} -> Pid
            after 500 -> await_peer(Node, Deadline)
            end;
        _ ->
            timer:sleep(100),
            await_peer(Node, Deadline)
    end.

% Stop the peer and wait until its node is gone: halting does not flush distribution output, so the stop must arrive
% first.
stop_peer(Node) ->
    Monitor = erlang:monitor(process, {bench_peer, Node}),
    {bench_peer, Node} ! stop,
    receive
        {'DOWN', Monitor, process, _, _} -> ok
    end.

%% Start node Name@localhost listening on loopback, with the cookie and extra emulator Flags, running main(Args).
start_node(Name, Args, Flags) ->
    Node = Name ++ "@localhost",
    Options = [exit_status, {line, 4096}],
    case otp() of
        true ->
            Erl = os:find_executable("erl", filename:join(code:root_dir(), "bin")),
            Code = filename:dirname(code:which(?MODULE)),
            Emulator = [
                "-noshell",
                "-sname",
                Node,
                "-setcookie",
                ?COOKIE,
                "-kernel",
                "inet_dist_use_interface",
                ?LOOPBACK,
                "-pa",
                Code
                | Flags
            ],
            open_port({spawn_executable, Erl}, [
                {args, Emulator ++ ["-run", atom_to_list(?MODULE), "main" | Args]} | Options
            ]);
        false ->
            Clause =
                "--sname " ++ Node ++ " --setcookie " ++ ?COOKIE ++ " --env kernel inet_dist_use_interface " ++
                    ?LOOPBACK,
            open_port({spawn_executable, init:script_name()}, [
                {args, Args}, {env, [{"CLAUSE_FLAGS", Clause}]} | Options
            ])
    end.

% Print a node's output lines until it exits, which must be with status 0.
relay(Port) ->
    receive
        {Port, {data, {_, Line}}} ->
            io:format("~s~n", [Line]),
            relay(Port);
        {Port, {exit_status, 0}} ->
            ok;
        {Port, {exit_status, Status}} ->
            erlang:error({node_failed, Status})
    end.

await_port(Port) ->
    receive
        {Port, {data, _}} -> await_port(Port);
        {Port, {exit_status, _}} -> ok
    end.

% The TLS distribution options file of a generated root and node certificate naming localhost, in the run directory.
tls_options() ->
    AltName = {'Extension', {2, 5, 29, 17}, false, [{dNSName, "localhost"}]},
    Config = public_key:pkix_test_data(#{
        root => [{key, {namedCurve, secp256r1}}, {digest, sha256}],
        intermediates => [],
        peer => [{key, {namedCurve, secp256r1}}, {digest, sha256}, {extensions, [AltName]}]
    }),
    Own = [{cert, proplists:get_value(cert, Config)}, {key, proplists:get_value(key, Config)}],
    Trust = [{cacerts, [lists:last(proplists:get_value(cacerts, Config))]}, {verify, verify_peer}],
    File = filename:absname("bench_tls_dist.conf"),
    ok = file:write_file(File, io_lib:format("~p.~n", [[{server, Own ++ Trust}, {client, Own ++ Trust}]])),
    File.

%% OTP serves the run's epmd port from an epmd started here; Clause's bundled epmd starts with the first registration.
start_epmd() ->
    case otp() of
        false ->
            none;
        true ->
            Epmd = epmd(),
            Port = open_port({spawn_executable, Epmd}, [{args, ["-relaxed_command_check"]}, exit_status]),
            await_epmd(erlang:monotonic_time(second) + ?START_SECONDS),
            Port
    end.

await_epmd(Deadline) ->
    erlang:monotonic_time(second) > Deadline andalso erlang:error(epmd_not_started),
    case erl_epmd:names(localhost) of
        {ok, _} ->
            ok;
        _ ->
            timer:sleep(50),
            await_epmd(Deadline)
    end.

stop_epmd(none) ->
    ok;
stop_epmd(Port) ->
    Kill = open_port({spawn_executable, epmd()}, [{args, ["-kill"]}, exit_status]),
    await_port(Kill),
    await_port(Port).

epmd() -> os:find_executable("epmd", filename:join([code:root_dir(), "erts-" ++ erlang:system_info(version), "bin"])).

% Whether this is OTP: its emulator has a progname, a Clause executable none.
otp() -> init:get_argument(progname) =/= error.
