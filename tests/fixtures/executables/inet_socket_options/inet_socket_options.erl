-module(inet_socket_options).
-moduledoc "Socket options, statistics, status and monitors of inet, and gen_udp's connected sends (plan 21 LIB60).".
-export([main/1]).

% Options every platform reports the same way; the system's buffer sizes are only checked to be integers.
% OTP on Windows reads multicast_if from uninitialized memory, so it is left out.
-define(PORTABLE, [
    active,
    packet,
    mode,
    header,
    reuseaddr,
    nodelay,
    keepalive,
    deliver,
    exit_on_close,
    send_timeout,
    send_timeout_close,
    delay_send,
    high_watermark,
    low_watermark,
    high_msgq_watermark,
    low_msgq_watermark,
    packet_size,
    show_econnreset,
    linger,
    dontroute,
    tos,
    buffer,
    broadcast,
    read_packets,
    multicast_ttl,
    multicast_loop,
    recvtos,
    recvttl
]).

-define(COLUMNS, [module, recv, sent, state, type]).

% Sockets bind to loopback only, so that Windows' firewall does not ask about the test program.
-define(LOOPBACK, {ip, {127, 0, 0, 1}}).

main(_) ->
    io:format("no sockets~n"),
    inet:i(),
    options(),
    statistics(),
    listing(),
    monitors(),
    datagrams(),
    helpers(),
    io:format("inet_socket_options: ok~n").

options() ->
    {ok, Listen} = gen_tcp:listen(0, [
        ?LOOPBACK,
        {active, false},
        {reuseaddr, true},
        {nodelay, true},
        {send_timeout, 55}
    ]),
    {ok, Port} = inet:port(Listen),
    show(listen_defaults, inet:getopts(Listen, ?PORTABLE)),
    {ok, Client} = gen_tcp:connect({127, 0, 0, 1}, Port, [
        {keepalive, true}, {header, 3}, {deliver, port}
    ]),
    {ok, Accepted} = gen_tcp:accept(Listen),
    show(client_defaults, inet:getopts(Client, ?PORTABLE)),
    show(accepted_inherits, inet:getopts(Accepted, [reuseaddr, nodelay, send_timeout, keepalive])),
    {ok, Sizes} = inet:getopts(Client, [sndbuf, recbuf]),
    show(sizes, [{Name, is_integer(Size)} || {Name, Size} <- Sizes]),
    Settings = [
        {nodelay, true},
        {keepalive, false},
        {linger, {true, 5}},
        {dontroute, true},
        {buffer, 3000},
        {header, 2},
        {deliver, term},
        {exit_on_close, false},
        {send_timeout, 1000},
        {send_timeout_close, true},
        {delay_send, true},
        {high_watermark, 20000},
        {low_watermark, 1000},
        {high_msgq_watermark, 9000},
        {low_msgq_watermark, 4000},
        {packet_size, 100},
        {show_econnreset, true},
        {tos, 16},
        {mode, binary},
        {mode, list},
        {packet, 2},
        {active, once}
    ],
    [
        show({set, Setting}, {
            inet:setopts(Client, [Setting]), inet:getopts(Client, [element(1, Setting)])
        })
     || Setting <- Settings
    ],
    show(set_bogus, inet:setopts(Client, [{bogus, 1}])),
    show(set_not_boolean, inet:setopts(Client, [{nodelay, 7}])),
    show(set_mixed, {
        inet:setopts(Client, [{nodelay, false}, {bogus, 1}]), inet:getopts(Client, [nodelay])
    }),
    show(set_not_list, catch_error(fun() -> inet:setopts(Client, id(nodelay)) end)),
    show(get_unreadable, [
        inet:getopts(Client, [Name])
     || Name <- [line_delimiter, add_membership, ip, port, binary]
    ]),
    show(get_unknown, inet:getopts(Client, [active, bogus])),
    show(get_duplicates, inet:getopts(Client, [tos, tos, packet])),
    show(get_empty, inet:getopts(Client, [])),
    show(get_not_list, catch_error(fun() -> inet:getopts(Client, id(active)) end)),
    {ok, Raised} = gen_tcp:connect({127, 0, 0, 1}, Port, [{recbuf, 20000}]),
    show(recbuf_raises_buffer, inet:getopts(Raised, [buffer])),
    [gen_tcp:close(Socket) || Socket <- [Raised, Client, Accepted, Listen]],
    show(closed_getopts, inet:getopts(Client, [active])),
    show(closed_setopts, inet:setopts(Client, [{active, true}])).

statistics() ->
    {ok, Listen} = gen_tcp:listen(0, [?LOOPBACK, binary, {active, false}]),
    {ok, Port} = inet:port(Listen),
    {ok, Client} = gen_tcp:connect({127, 0, 0, 1}, Port, [binary, {active, false}]),
    {ok, Accepted} = gen_tcp:accept(Listen),
    ok = gen_tcp:send(Client, <<"hello">>),
    ok = gen_tcp:send(Client, <<"hello world">>),
    timer:sleep(200),
    show(received, gen_tcp:recv(Accepted, 0)),
    show(client_stats, inet:getstat(Client)),
    show(accepted_stats, inet:getstat(Accepted)),
    show(chosen_stats, inet:getstat(Accepted, [send_oct, recv_cnt, recv_max])),
    show(bad_stat, inet:getstat(Accepted, [recv_cnt, bogus])),
    show(listen_stats, inet:getstat(Listen, [recv_oct, send_oct])),
    show(info_keys, lists:sort(maps:keys(inet:info(Client)))),
    #{owner := Owner, states := States, counters := Counters, links := Links} = inet:info(Client),
    show(info, {Owner =:= self(), States, maps:get(send_oct, Counters), Links =:= [self()]}),
    show(listen_info_states, maps:get(states, inet:info(Listen))),
    show(names, [
        hosts(Names)
     || Names <- [inet:socknames(Client), inet:peernames(Client), inet:socknames(Client, 0)]
    ]),
    show(listen_names, {hosts(inet:socknames(Listen)), inet:peernames(Listen)}),
    [gen_tcp:close(Socket) || Socket <- [Client, Accepted, Listen]],
    show(closed_info, inet:info(Client)),
    show(closed_stats, inet:getstat(Client)),
    show(closed_names, {inet:socknames(Client), inet:peernames(Client)}).

listing() ->
    {ok, Listen} = gen_tcp:listen(0, [?LOOPBACK, {active, false}]),
    {ok, Port} = inet:port(Listen),
    io:format("listening~n"),
    inet:i(tcp, ?COLUMNS),
    Self = self(),
    Acceptor = spawn(fun() ->
        {ok, Socket} = gen_tcp:accept(Listen),
        ok = gen_tcp:controlling_process(Socket, Self),
        Self ! {accepted, Socket}
    end),
    timer:sleep(100),
    io:format("accepting~n"),
    inet:i(tcp, ?COLUMNS),
    {ok, Client} = gen_tcp:connect({127, 0, 0, 1}, Port, [{active, false}]),
    Accepted =
        receive
            {accepted, Socket} -> Socket
        end,
    ok = gen_tcp:send(Client, "four"),
    {ok, "four"} = gen_tcp:recv(Accepted, 4),
    io:format("connected~n"),
    inet:i(tcp, ?COLUMNS),
    io:format("packet~n"),
    inet:i(tcp, [packet]),
    {ok, Udp} = gen_udp:open(0, [?LOOPBACK]),
    io:format("udp~n"),
    inet:i(udp, ?COLUMNS),
    ok = gen_udp:connect(Udp, {127, 0, 0, 1}, Port),
    io:format("udp connected~n"),
    inet:i(udp, [state, type]),
    io:format("sctp~n"),
    inet:i(sctp, ?COLUMNS),
    io:format("all columns~n"),
    inet:i([module, state]),
    _ = Acceptor,
    [inet:close(Open) || Open <- [Udp, Client, Accepted, Listen]].

monitors() ->
    {ok, Socket} = gen_udp:open(0, [?LOOPBACK]),
    Monitor = inet:monitor(Socket),
    Cancelled = inet:monitor(Socket),
    show(cancel, {inet:cancel_monitor(Cancelled), inet:cancel_monitor(Cancelled)}),
    ok = gen_udp:close(Socket),
    receive
        {'DOWN', Ref, Type, Object, Reason} ->
            show(down, {Ref =:= Monitor, Type, Object =:= Socket, Reason})
    after 2000 -> show(down, none)
    end,
    Late = inet:monitor(Socket),
    receive
        {'DOWN', LateRef, port, Socket, LateReason} ->
            show(late_down, {LateRef =:= Late, LateReason})
    after 2000 -> show(late_down, none)
    end,
    show(monitor_pid, catch_error(fun() -> inet:monitor(id(self())) end)),
    show(cancel_unknown, inet:cancel_monitor(make_ref())),
    show(cancel_atom, catch_error(fun() -> inet:cancel_monitor(id(x)) end)).

datagrams() ->
    {ok, Receiver} = gen_udp:open(0, [?LOOPBACK, binary]),
    {ok, Port} = inet:port(Receiver),
    {ok, Sender} = gen_udp:open(0, [?LOOPBACK, binary, {broadcast, true}, {multicast_ttl, 4}]),
    % OTP on Windows reads a UDP socket's reuseaddr unreliably.
    show(udp_defaults, inet:getopts(Receiver, ?PORTABLE -- [reuseaddr])),
    show(udp_set, inet:getopts(Sender, [broadcast, multicast_ttl])),
    show(
        udp_settings,
        [
            {Setting, inet:setopts(Sender, [Setting]), inet:getopts(Sender, [element(1, Setting)])}
         || Setting <- [
                {multicast_loop, false},
                {read_packets, 9},
                {recvtos, true},
                {recvttl, true},
                {tos, 8}
            ]
        ]
    ),
    show(udp_peer_unconnected, inet:peername(Sender)),
    show(connect, gen_udp:connect(Sender, {127, 0, 0, 1}, Port)),
    show(peer, inet:peername(Sender) =:= {ok, {{127, 0, 0, 1}, Port}}),
    show(udp_names, {hosts(inet:socknames(Sender)), hosts(inet:peernames(Sender))}),
    Sends = [
        {send2, fun() -> gen_udp:send(Sender, <<"two">>) end},
        {send3_pair, fun() -> gen_udp:send(Sender, {{127, 0, 0, 1}, Port}, <<"pair">>) end},
        {send3_family, fun() ->
            gen_udp:send(Sender, {inet, {{127, 0, 0, 1}, Port}}, <<"family">>)
        end},
        {send3_map, fun() ->
            gen_udp:send(Sender, #{family => inet, addr => {127, 0, 0, 1}, port => Port}, <<"map">>)
        end},
        {send4, fun() -> gen_udp:send(Sender, {127, 0, 0, 1}, Port, <<"four">>) end},
        {send4_ancillary, fun() -> gen_udp:send(Sender, {{127, 0, 0, 1}, Port}, [], <<"anc">>) end},
        {send4_zero, fun() -> gen_udp:send(Sender, {{127, 0, 0, 1}, Port}, 0, <<"zero">>) end},
        {send5, fun() -> gen_udp:send(Sender, "localhost", Port, [], <<"five">>) end}
    ],
    [show(Name, {Send(), received(Receiver)}) || {Name, Send} <- Sends],
    [
        show(
            {send5_port, Bad},
            catch_error(fun() -> gen_udp:send(Sender, {127, 0, 0, 1}, id(Bad), [], <<"x">>) end)
        )
     || Bad <- [-1, 'bogus-service', "bogus-service"]
    ],
    show(udp_stats, inet:getstat(Sender, [send_oct, send_cnt, send_max, send_avg, recv_cnt])),
    show(
        receiver_stats, inet:getstat(Receiver, [recv_oct, recv_cnt, recv_max, recv_avg, recv_dvi])
    ),
    {ok, Other} = gen_udp:open(0, [?LOOPBACK, binary]),
    show(connect_map, gen_udp:connect(Other, #{family => inet, addr => loopback, port => Port})),
    show(other_send, {gen_udp:send(Other, <<"other">>), received(Receiver)}),
    show(connect_host, gen_udp:connect(Other, "localhost", Port)),
    [gen_udp:close(Socket) || Socket <- [Receiver, Sender, Other]].

received(Socket) ->
    receive
        {udp, Socket, {127, 0, 0, 1}, _, Data} -> Data
    after 2000 -> timeout
    end.

helpers() ->
    {ok, Listen} = gen_tcp:listen(0, [?LOOPBACK, {active, false}]),
    {ok, Port} = inet:port(Listen),
    {ok, ByMap} = gen_tcp:connect(#{family => inet, addr => {127, 0, 0, 1}, port => Port}, [
        {active, false}
    ]),
    show(connect_map, element(1, inet:peername(ByMap))),
    {ok, ByLoopback} = gen_tcp:connect(#{family => inet, addr => loopback, port => Port}, [], 5000),
    show(connect_map_loopback, element(1, inet:peername(ByLoopback))),
    [gen_tcp:close(Socket) || Socket <- [ByMap, ByLoopback, Listen]],
    show(sockaddr, inet:ensure_sockaddr(#{family => inet, port => 7})),
    show(sockaddr6, inet:ensure_sockaddr(#{family => inet6, addr => loopback})),
    show(
        sockaddr_keys, catch_error(fun() -> inet:ensure_sockaddr(#{family => inet, bogus => 1}) end)
    ),
    show(sockaddr_family, catch_error(fun() -> inet:ensure_sockaddr(#{port => 1}) end)),
    show(sockaddr_other, catch_error(fun() -> inet:ensure_sockaddr(id(x)) end)),
    Reasons = [
        exbadport,
        exbadseq,
        system_limit,
        econnrefused,
        eaddrinuse,
        nxdomain,
        timeout,
        closed,
        bogus
    ],
    show(format_error, [inet:format_error(Reason) || Reason <- Reasons]),
    show(mapped, {
        inet:ipv4_mapped_ipv6_address({192, 168, 1, 2}),
        inet:ipv4_mapped_ipv6_address({0, 0, 0, 0, 0, 16#ffff, 16#c0a8, 16#0102}),
        inet:ipv4_mapped_ipv6_address({1, 2, 3, 4, 5, 6, 7, 8})
    }),
    show(mapped_bad, catch_error(fun() -> inet:ipv4_mapped_ipv6_address(id({256, 0, 0, 0})) end)).

show(Label, Value) -> io:format("~p ~p~n", [Label, sorted(Value)]).

% Maps as sorted lists, so that the printed key order is OTP's and Clause's alike.
sorted(Map) when is_map(Map) -> lists:sort([{Key, sorted(Value)} || Key := Value <- Map]);
sorted(List) when is_list(List) -> [sorted(Item) || Item <- List];
sorted(Tuple) when is_tuple(Tuple) -> list_to_tuple(sorted(tuple_to_list(Tuple)));
sorted(Term) -> Term.

% The addresses of socknames/peernames, without their ports.
hosts({ok, Names}) -> {ok, [Address || {Address, _} <- Names]};
hosts(Error) -> Error.

catch_error(Fun) ->
    try Fun() of
        Value -> {value, Value}
    catch
        Class:Reason -> {Class, Reason}
    end.

id(Value) -> Value.
