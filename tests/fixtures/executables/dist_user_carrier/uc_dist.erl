%% A user carrier (plan 30 step DIST12b), written for this test in the shape of OTP's carrier guide (alt_dist): TCP on
%% loopback through gen_tcp, a controller process per connection that moves the runtime's packets with the dist_ctrl
%% functions, and dist_util's handshake, which reads lists: the socket is in list mode until the connection is up.
%% It exports only the guide's mandatory callbacks, listen/1 among them, and names its addresses with protocol uc.
-module(uc_dist).
-export([listen/1, accept/1, accept_connection/5, setup/5, close/1, select/1, address/0]).

-include_lib("kernel/include/net_address.hrl").
-include_lib("kernel/include/dist_util.hrl").

-define(LOOPBACK, {127, 0, 0, 1}).

listen(Name) ->
    {ok, Socket} = gen_tcp:listen(0, [list, {active, false}, {packet, 2}, {ip, ?LOOPBACK}]),
    {ok, Port} = inet:port(Socket),
    case erl_epmd:register_node(Name, Port) of
        {ok, Creation} -> {ok, {Socket, (address())#net_address{address = {?LOOPBACK, Port}}, Creation}};
        Error -> Error
    end.

address() ->
    {ok, Host} = inet:gethostname(),
    #net_address{host = Host, protocol = uc, family = inet}.

close(Socket) -> gen_tcp:close(Socket).

select(Node) ->
    case string:split(atom_to_list(Node), "@") of
        [_, "localhost"] -> true;
        _ -> false
    end.

%% Accepting: each socket gets a controller, which net_kernel's handshake process takes over.
accept(Listen) ->
    Kernel = self(),
    spawn_opt(fun() -> accept_loop(Kernel, Listen) end, [link, {priority, max}]).

accept_loop(Kernel, Listen) ->
    {ok, Socket} = gen_tcp:accept(Listen),
    Controller = controller(Socket),
    Kernel ! {accept, self(), Controller, inet, uc},
    receive
        {Kernel, controller, Supervisor} ->
            Controller ! {supervisor, Supervisor},
            Supervisor ! {self(), controller};
        {Kernel, unsupported_protocol} ->
            exit(unsupported_protocol)
    end,
    accept_loop(Kernel, Listen).

accept_connection(Acceptor, Controller, MyNode, Allowed, SetupTime) ->
    Kernel = self(),
    spawn_opt(
        fun() ->
            receive
                {Acceptor, controller} ->
                    dist_util:handshake_other_started((hs_data(Controller))#hs_data{
                        kernel_pid = Kernel,
                        this_node = MyNode,
                        timer = dist_util:start_timer(SetupTime),
                        allowed = Allowed
                    })
            end
        end,
        dist_util:net_ticker_spawn_options()
    ).

%% Setting up: the port of the other node from its epmd, then the handshake over a new controller.
setup(Node, Type, MyNode, _LongOrShortNames, SetupTime) ->
    Kernel = self(),
    spawn_opt(fun() -> do_setup(Kernel, Node, Type, MyNode, SetupTime) end, dist_util:net_ticker_spawn_options()).

do_setup(Kernel, Node, Type, MyNode, SetupTime) ->
    Timer = dist_util:start_timer(SetupTime),
    [Name, _] = string:split(atom_to_list(Node), "@"),
    case erl_epmd:port_please(Name, ?LOOPBACK) of
        {port, Port, Version} ->
            dist_util:reset_timer(Timer),
            {ok, Socket} = gen_tcp:connect(?LOOPBACK, Port, [list, {active, false}, {packet, 2}]),
            Controller = controller(Socket),
            Controller ! {supervisor, self()},
            dist_util:handshake_we_started((hs_data(Controller))#hs_data{
                kernel_pid = Kernel,
                other_node = Node,
                this_node = MyNode,
                timer = Timer,
                other_version = Version,
                request_type = Type
            });
        _ ->
            ?shutdown(Node)
    end.

%% The handshake callbacks: calls to the controller, which owns the socket.
hs_data(Controller) ->
    #hs_data{
        socket = Controller,
        f_send = fun(Ctrl, Data) -> call(Ctrl, {send, Data}) end,
        f_recv = fun(Ctrl, Length, Timeout) -> call(Ctrl, {recv, Length, Timeout}) end,
        f_setopts_pre_nodeup = fun(Ctrl) -> call(Ctrl, {setopts, [{packet, 4}, binary]}) end,
        f_setopts_post_nodeup = fun(_) -> ok end,
        f_getll = fun(Ctrl) -> {ok, Ctrl} end,
        f_address = fun(Ctrl, Node) -> call(Ctrl, {address, Node}) end,
        f_handshake_complete = fun(Ctrl, _Node, DHandle) ->
            Ctrl ! {handshake_complete, DHandle},
            ok
        end,
        mf_tick = fun(Ctrl) -> Ctrl ! tick end,
        mf_getstat = fun(Ctrl) -> call(Ctrl, getstat) end
    }.

call(Controller, Request) ->
    Ref = make_ref(),
    Controller ! {call, self(), Ref, Request},
    receive
        {Ref, Reply} -> Reply
    end.

%% The controller: handshake requests, then the up connection's packets both ways.
controller(Socket) ->
    Controller = spawn(fun() -> handshake(Socket) end),
    ok = gen_tcp:controlling_process(Socket, Controller),
    Controller.

handshake(Socket) ->
    receive
        {supervisor, Supervisor} ->
            link(Supervisor),
            handshake(Socket);
        {call, From, Ref, Request} ->
            From ! {Ref, request(Socket, Request)},
            handshake(Socket);
        {handshake_complete, DHandle} ->
            ok = inet:setopts(Socket, [{active, once}]),
            ok = erlang:dist_ctrl_get_data_notification(DHandle),
            connected(Socket, DHandle)
    end.

request(Socket, {send, Data}) ->
    gen_tcp:send(Socket, Data);
request(Socket, {recv, Length, Timeout}) ->
    gen_tcp:recv(Socket, Length, Timeout);
request(Socket, {setopts, Options}) ->
    inet:setopts(Socket, Options);
request(Socket, getstat) ->
    stat(inet:getstat(Socket, [recv_cnt, send_cnt, send_pend]));
request(Socket, {address, Node}) ->
    {ok, Address} = inet:peername(Socket),
    [_, Host] = string:split(atom_to_list(Node), "@"),
    #net_address{address = Address, host = Host, protocol = uc, family = inet}.

stat({ok, [{recv_cnt, Received}, {send_cnt, Sent}, {send_pend, Pending}]}) -> {ok, Received, Sent, Pending}.

connected(Socket, DHandle) ->
    receive
        dist_data ->
            send_all(Socket, DHandle);
        tick ->
            ok = gen_tcp:send(Socket, <<>>);
        {tcp, Socket, Data} ->
            erlang:dist_ctrl_put_data(DHandle, Data),
            ok = inet:setopts(Socket, [{active, once}]);
        {call, From, Ref, Request} ->
            From ! {Ref, request(Socket, Request)};
        {tcp_closed, Socket} ->
            exit(connection_closed)
    end,
    connected(Socket, DHandle).

send_all(Socket, DHandle) ->
    case erlang:dist_ctrl_get_data(DHandle) of
        none ->
            ok = erlang:dist_ctrl_get_data_notification(DHandle);
        Data ->
            ok = gen_tcp:send(Socket, Data),
            send_all(Socket, DHandle)
    end.
