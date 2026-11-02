%% The distribution controller of Clause's carriers (plan 30 step DIST4b2a, docs/distribution.md#carriers): a process
%% owning a connection's socket that answers its connection supervisor's handshake requests, then moves the runtime's
%% packets out and the other node's packets in. Moved out of inet_tcp_dist for TLS distribution (DIST12a), so that
%% inet_tcp_dist runs it over gen_tcp and inet_tls_dist over ssl; a transport map names the socket functions.
-module(clause_dist_controller).
-export([start/2, call/2, hs_data/4, tcp/0]).

-include_lib("kernel/include/net_address.hrl").
-include_lib("kernel/include/dist_util.hrl").

%% A transport (TLS distribution) is a map: data (the module of send/2, recv/3 and controlling_process/2), options (the
%% module of setopts/2, getopts/2, peername/1 and getstat/2), tags (the data, closed and error message tags of an active
%% socket), protocol (the #net_address{} protocol), and pre_nodeup and post_nodeup (the socket options set before and
%% after the connection comes up).

% The transport of the TCP carriers over gen_tcp: packets of 2 bytes until the connection is up, then of 4.
tcp() ->
    #{
        data => gen_tcp,
        options => inet,
        tags => {tcp, tcp_closed, tcp_error},
        protocol => tcp,
        pre_nodeup => [{packet, 4}],
        post_nodeup => []
    }.

% Start a controller over Socket and give it the socket.
start(Socket, #{data := Data} = Transport) ->
    Controller = spawn(fun() -> handshake_loop(Socket, Transport) end),
    ok = Data:controlling_process(Socket, Controller),
    Controller.

% A request to the controller and its answer; the controller's end ends the caller.
call(Controller, Request) ->
    Ref = erlang:monitor(process, Controller),
    Controller ! {Ref, self(), Request},
    receive
        {Ref, Result} ->
            erlang:demonitor(Ref, [flush]),
            Result;
        {'DOWN', Ref, process, Controller, Reason} ->
            exit({dist_controller_exit, Reason})
    end.

% The handshake callbacks of a controller over Socket: requests to the controller, statistics from the socket.
hs_data(Controller, Socket, #{options := Options}, Family) ->
    #hs_data{
        socket = Controller,
        f_send = fun(Ctrl, Data) -> call(Ctrl, {send, Data}) end,
        f_recv = fun(Ctrl, Length, Timeout) -> call(Ctrl, {recv, Length, Timeout}) end,
        f_setopts_pre_nodeup = fun(Ctrl) -> call(Ctrl, pre_nodeup) end,
        f_setopts_post_nodeup = fun(Ctrl) -> call(Ctrl, post_nodeup) end,
        f_getll = fun(Ctrl) -> {ok, Ctrl} end,
        f_address = fun(Ctrl, Node) -> call(Ctrl, {address, Node, Family}) end,
        f_handshake_complete = fun(Ctrl, _, DHandle) -> call(Ctrl, {handshake_complete, DHandle}) end,
        mf_tick = fun(Ctrl) -> Ctrl ! tick end,
        mf_getstat = fun(_) -> stat(Options:getstat(Socket, [recv_cnt, send_cnt, send_pend])) end,
        mf_setopts = fun(Ctrl, Opts) -> call(Ctrl, {setopts, Opts}) end,
        mf_getopts = fun(Ctrl, Opts) -> call(Ctrl, {getopts, Opts}) end
    }.

stat({ok, Stat}) ->
    {ok, proplists:get_value(recv_cnt, Stat, 0), proplists:get_value(send_cnt, Stat, 0),
        proplists:get_value(send_pend, Stat, 0)};
stat(Error) ->
    Error.

handshake_loop(Socket, #{options := Options} = Transport) ->
    receive
        {Ref, From, {handshake_complete, DHandle}} ->
            From ! {Ref, ok},
            ok = Options:setopts(Socket, [{active, once}]),
            ok = erlang:dist_ctrl_get_data_notification(DHandle),
            data_loop(Socket, DHandle, Transport);
        {Ref, From, Request} ->
            From ! {Ref, request(Socket, Request, Transport)},
            handshake_loop(Socket, Transport);
        _ ->
            handshake_loop(Socket, Transport)
    end.

request(_, {supervisor, Supervisor}, _) -> link(Supervisor);
request(Socket, socket, _) -> Socket;
request(Socket, {send, Data}, #{data := Module}) -> Module:send(Socket, Data);
request(Socket, {recv, Length, Timeout}, #{data := Module}) -> Module:recv(Socket, Length, Timeout);
request(Socket, pre_nodeup, #{options := Options, pre_nodeup := Opts}) -> setopts(Options, Socket, Opts);
request(Socket, post_nodeup, #{options := Options, post_nodeup := Opts}) -> setopts(Options, Socket, Opts);
request(Socket, {address, Node, Family}, Transport) -> peer_address(Socket, Node, Family, Transport);
request(Socket, {setopts, Opts}, #{options := Options}) -> carrier_setopts(Options, Socket, Opts);
request(Socket, {getopts, Opts}, #{options := Options}) -> active_options(Options:getopts(Socket, Opts)).

setopts(_, _, []) -> ok;
setopts(Options, Socket, Opts) -> Options:setopts(Socket, Opts).

% Options of an up connection; those the controller sets itself are refused, as OTP's are.
carrier_setopts(Options, Socket, Opts) ->
    case [Opt || {Key, _} = Opt <- Opts, lists:member(Key, [active, deliver, packet])] of
        [] -> Options:setopts(Socket, Opts);
        Refused -> {error, {badopts, Refused}}
    end.

% An up connection's socket options as OTP's port reads them: active, while this controller reads {active, once}
% (DIST6).
active_options({ok, Opts}) -> {ok, [active_option(Opt) || Opt <- Opts]};
active_options(Error) -> Error.

active_option({active, _}) -> {active, true};
active_option(Opt) -> Opt.

peer_address(Socket, Node, Family, #{options := Options, protocol := Protocol}) ->
    {ok, Address} = Options:peername(Socket),
    [_, Host] = string:split(atom_to_list(Node), "@"),
    #net_address{address = Address, host = Host, protocol = Protocol, family = Family}.

% The up connection: the runtime's packets go out when it signals dist_data, the other node's come in one at a time.
data_loop(Socket, DHandle, #{tags := {DataTag, ClosedTag, ErrorTag}, options := Options} = Transport) ->
    receive
        dist_data ->
            send_queued(Socket, DHandle, Transport);
        {DataTag, Socket, Data} ->
            put_data(DHandle, Data),
            ok = Options:setopts(Socket, [{active, once}]);
        tick ->
            send_packet(Socket, <<>>, Transport);
        {Ref, From, {Operation, _} = Request} when Operation =:= setopts; Operation =:= getopts ->
            From ! {Ref, request(Socket, Request, Transport)};
        {ClosedTag, Socket} ->
            exit(connection_closed);
        {ErrorTag, Socket, _} ->
            exit(connection_closed);
        _ ->
            ok
    end,
    data_loop(Socket, DHandle, Transport).

send_queued(Socket, DHandle, Transport) ->
    case erlang:dist_ctrl_get_data(DHandle) of
        none ->
            erlang:dist_ctrl_get_data_notification(DHandle);
        Packet ->
            send_packet(Socket, Packet, Transport),
            send_queued(Socket, DHandle, Transport)
    end.

send_packet(Socket, Packet, #{data := Module}) ->
    case Module:send(Socket, Packet) of
        ok -> ok;
        {error, _} -> exit(connection_closed)
    end.

% Data the runtime refuses ends the connection.
put_data(DHandle, Data) ->
    try
        erlang:dist_ctrl_put_data(DHandle, Data)
    catch
        error:_ -> exit(connection_closed)
    end.
