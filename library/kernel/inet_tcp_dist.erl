%% Clause's inet_tcp_dist (plan 30 step DIST4b1a, docs/distribution.md#starting-distribution): the IPv4 TCP carrier of
%% distribution over gen_tcp. It listens on a port of the kernel parameters inet_dist_listen_min/max and registers
%% that port with epmd; it accepts and sets up connections through dist_util's handshake, each with a controller
%% process over its socket (DIST4b2a, docs/distribution.md#carriers). Its gen_* functions take a driver, inet_tcp or
%% inet6_tcp, and its fam_* functions an address family, so that inet6_tcp_dist runs over them (DIST4b2c).
-module(inet_tcp_dist).
-export([
    accept/1,
    accept_connection/5,
    accept_loop/3,
    address/0,
    close/1,
    do_accept/7,
    do_setup/7,
    fam_address/1,
    fam_listen/4,
    fam_select/2,
    fam_setup/4,
    gen_accept/2,
    gen_accept_connection/6,
    gen_address/1,
    gen_hs_data/2,
    gen_listen/3,
    gen_select/2,
    gen_setup/6,
    getopts/2,
    getstat/1,
    is_node_name/1,
    listen/1,
    listen/2,
    merge_options/2,
    merge_options/3,
    nodelay/0,
    select/1,
    setopts/2,
    setup/5,
    tick/2
]).

-include_lib("kernel/include/net_address.hrl").
-include_lib("kernel/include/dist_util.hrl").

% The backlog of the listen socket.
-define(BACKLOG, 128).

listen(Name) ->
    {ok, Host} = inet:gethostname(),
    listen(Name, Host).

listen(Name, Host) -> gen_listen(inet_tcp, Name, Host).

% Listen for distribution on Host and register the port under the alive name Name and Driver with the epmd module:
% {ok, {Socket, Address, Creation}}, or the error of either.
gen_listen(Driver, Name, Host) ->
    case listen_options(family(Driver)) of
        {ok, Options} -> listen_on(Driver, Name, Host, Options);
        Error -> Error
    end.

listen_on(Driver, Name, Host, Options) ->
    Family = family(Driver),
    case listen_socket(listen_ports(), Options) of
        {ok, Socket} ->
            {ok, {IP, Port}} = inet:sockname(Socket),
            Address = #net_address{address = {IP, Port}, host = Host, protocol = tcp, family = Family},
            case register_node(net_kernel:epmd_module(), Name, Port, Driver) of
                {ok, Creation} ->
                    {ok, {Socket, Address, Creation}};
                Error ->
                    gen_tcp:close(Socket),
                    Error
            end;
        Error ->
            Error
    end.

% erl_epmd is called by name, so that programs link it.
register_node(erl_epmd, Name, Port, Driver) -> erl_epmd:register_node(Name, Port, Driver);
register_node(Module, Name, Port, Driver) -> Module:register_node(Name, Port, Driver).

% The address family of a driver (IPv6 carrier).
family(inet_tcp) -> inet;
family(inet6_tcp) -> inet6.

% The ports to try, in order: inet_dist_listen_min to inet_dist_listen_max (min alone is one port), else any.
listen_ports() ->
    case application:get_env(kernel, inet_dist_listen_min) of
        {ok, Min} when is_integer(Min) ->
            case application:get_env(kernel, inet_dist_listen_max) of
                {ok, Max} when is_integer(Max), Max >= Min -> lists:seq(Min, Max);
                _ -> [Min]
            end;
        _ ->
            [0]
    end.

% The first port of Ports a listen socket takes; a port in use moves to the next one.
listen_socket([Port | Ports], Options) ->
    case gen_tcp:listen(Port, Options) of
        {error, eaddrinuse} when Ports =/= [] -> listen_socket(Ports, Options);
        Result -> Result
    end.

% The kernel parameter inet_dist_listen_options under the carrier's own options and inet_dist_use_interface.
listen_options(Family) ->
    Interface =
        case application:get_env(kernel, inet_dist_use_interface) of
            {ok, IP} -> [{ip, IP}];
            undefined -> []
        end,
    Forced = Interface ++ [Family, binary, {active, false}, {packet, 2}],
    kernel_options(inet_dist_listen_options, Forced, [{backlog, ?BACKLOG}]).

% The options of a kernel parameter under Forced and over Additional: {ok, Options}, or badarg for one with an address
% family, which the carrier sets itself, as OTP's does.
kernel_options(Parameter, Forced, Additional) ->
    Options =
        case application:get_env(kernel, Parameter) of
            {ok, List} when is_list(List) -> List;
            _ -> []
        end,
    case lists:member(family, option_keys(Options)) of
        true -> {error, badarg};
        false -> {ok, merge_options(Options, Forced, Additional)}
    end.

%% Socket options (plan 30 step DIST4b2c): Forced options replace the options of their key in Options, which replace
%% those of Additional. A key is a pair's first element, mode for binary and list, family for inet, inet6 and local;
%% other options have none and stay.
merge_options(Options, Forced) when is_list(Options), is_list(Forced) ->
    Keys = option_keys(Forced),
    Forced ++ [Option || Option <- Options, not lists:member(option_key(Option), Keys)].

merge_options(Options, Forced, Additional) when is_list(Options), is_list(Forced), is_list(Additional) ->
    Keys = option_keys(Options),
    merge_options(Options, Forced) ++ [Option || Option <- Additional, not lists:member(option_key(Option), Keys)].

option_keys(Options) -> [Key || Key <- [option_key(Option) || Option <- Options], Key =/= none].

option_key({Key, _}) -> Key;
option_key(Mode) when Mode =:= binary; Mode =:= list -> mode;
option_key(Family) when Family =:= inet; Family =:= inet6; Family =:= local -> family;
option_key(_) -> none.

% Options of future connections on the listen socket; those the carrier sets itself are refused, as OTP's are.
setopts(Listen, Options) ->
    case [Option || {Key, _} = Option <- Options, lists:member(Key, [active, deliver, packet])] of
        [] -> inet:setopts(Listen, Options);
        Refused -> {error, {badopts, Refused}}
    end.

getopts(Listen, Options) -> inet:getopts(Listen, Options).

% The address node names of this host take, before the carrier listens.
address() -> gen_address(inet_tcp).

gen_address(Driver) -> fam_address(family(Driver)).

fam_address(Family) ->
    {ok, Host} = inet:gethostname(),
    #net_address{host = Host, protocol = tcp, family = Family}.

close(Socket) -> gen_tcp:close(Socket).

% Whether Node is an atom of a name part and a host part around one @.
is_node_name(Node) when is_atom(Node) -> length(string:split(atom_to_list(Node), "@", all)) =:= 2;
is_node_name(_) -> false.

select(Node) -> gen_select(inet_tcp, Node).

gen_select(Driver, Node) -> fam_select(family(Driver), Node).

% Whether a carrier of Family reaches Node: both parts present and the host has an address of the family.
fam_select(Family, Node) ->
    case string:split(to_string(Node), "@", all) of
        [[_ | _], [_ | _] = Host] -> element(1, inet:getaddr(Host, Family)) =:= ok;
        _ -> false
    end.

to_string(Node) when is_atom(Node) -> atom_to_list(Node);
to_string(Node) -> Node.

%% Accepting connections (plan 30 step DIST4b2a): an acceptor process at max priority, linked to net_kernel, hands each
%% accepted socket's controller to net_kernel and the connection supervisor net_kernel answers with.
accept(Listen) -> gen_accept(inet_tcp, Listen).

gen_accept(Driver, Listen) ->
    spawn_opt(?MODULE, accept_loop, [self(), Listen, family(Driver)], [link, {priority, max}]).

accept_loop(Kernel, Listen, Family) ->
    case gen_tcp:accept(Listen) of
        {ok, Socket} ->
            Controller = start_controller(Socket),
            Kernel ! {accept, self(), Controller, Family, tcp},
            receive
                {Kernel, controller, Supervisor} ->
                    call(Controller, {supervisor, Supervisor}),
                    Supervisor ! {self(), controller};
                {Kernel, unsupported_protocol} ->
                    exit(unsupported_protocol)
            end,
            accept_loop(Kernel, Listen, Family);
        Error ->
            exit(Error)
    end.

accept_connection(Acceptor, Controller, MyNode, Allowed, SetupTime) ->
    gen_accept_connection(inet_tcp, Acceptor, Controller, MyNode, Allowed, SetupTime).

gen_accept_connection(Driver, Acceptor, Controller, MyNode, Allowed, SetupTime) ->
    spawn_opt(
        ?MODULE,
        do_accept,
        [self(), Acceptor, Controller, MyNode, Allowed, SetupTime, Driver],
        dist_util:net_ticker_spawn_options()
    ).

% The connection supervisor of an accepted connection, once the acceptor made it the controller's supervisor.
do_accept(Kernel, Acceptor, Controller, MyNode, Allowed, SetupTime, Driver) ->
    receive
        {Acceptor, controller} ->
            Timer = dist_util:start_timer(SetupTime),
            HSData = gen_hs_data(Driver, Controller),
            dist_util:handshake_other_started(HSData#hs_data{
                kernel_pid = Kernel, this_node = MyNode, timer = Timer, allowed = Allowed
            })
    end.

%% Setting up a connection (plan 30 step DIST4b2a): the connection supervisor asks the other host's epmd for the
%% node's port, connects and runs the initiator's handshake.
setup(Node, Type, MyNode, LongOrShortNames, SetupTime) ->
    gen_setup(inet_tcp, Node, Type, MyNode, LongOrShortNames, SetupTime).

gen_setup(Driver, Node, Type, MyNode, LongOrShortNames, SetupTime) ->
    spawn_opt(
        ?MODULE,
        do_setup,
        [self(), Node, Type, MyNode, LongOrShortNames, SetupTime, Driver],
        dist_util:net_ticker_spawn_options()
    ).

do_setup(Kernel, Node, Type, MyNode, LongOrShortNames, SetupTime, Driver) ->
    Family = family(Driver),
    [Name, Host] = split_node(Node, LongOrShortNames),
    Timer = dist_util:start_timer(SetupTime),
    {IP, Port, Version} = node_port(Node, Name, Host, Family),
    dist_util:reset_timer(Timer),
    Socket = connect(Node, IP, Port, Family),
    Controller = start_controller(Socket),
    call(Controller, {supervisor, self()}),
    HSData = hs_data(Controller, Socket, Family),
    dist_util:handshake_we_started(HSData#hs_data{
        kernel_pid = Kernel,
        other_node = Node,
        this_node = MyNode,
        timer = Timer,
        other_version = Version,
        request_type = Type
    }).

% The name and host parts of Node, as the node's naming allows them; an illegal one is logged and ends the setup.
split_node(Node, LongOrShortNames) ->
    case string:split(atom_to_list(Node), "@", all) of
        [[_ | _] = Name, [_ | _] = Host] ->
            check_host(Node, Host, LongOrShortNames, lists:member($., Host)),
            [Name, Host];
        [_] ->
            error_logger:error_msg("** Nodename ~p illegal, no '@' character **~n", [Node]),
            ?shutdown(Node);
        _ ->
            error_logger:error_msg("** Nodename ~p illegal **~n", [Node]),
            ?shutdown(Node)
    end.

% Long names need a dotted host or an IP address; short names a host without dots.
check_host(Node, Host, longnames, false) ->
    case inet:parse_address(Host) of
        {ok, _} ->
            ok;
        _ ->
            error_logger:error_msg(
                "** System running to use fully qualified hostnames **~n** Hostname ~ts is illegal **~n", [Host]
            ),
            ?shutdown(Node)
    end;
check_host(Node, Host, shortnames, true) ->
    error_logger:error_msg(
        "** System NOT running to use fully qualified hostnames **~n** Hostname ~ts is illegal **~n", [Host]
    ),
    ?shutdown(Node);
check_host(_, _, _, _) ->
    ok.

% The address of Host and the port and protocol version epmd gives for Name there.
node_port(Node, Name, Host, Family) ->
    case inet:getaddr(Host, Family) of
        {ok, IP} ->
            case port_please(net_kernel:epmd_module(), Name, IP) of
                {port, Port, Version} -> {IP, Port, Version};
                _ -> ?shutdown(Node)
            end;
        _ ->
            ?shutdown(Node)
    end.

% erl_epmd is called by name, so that programs link it.
port_please(erl_epmd, Name, IP) -> erl_epmd:port_please(Name, IP);
port_please(Module, Name, IP) -> Module:port_please(Name, IP).

% The kernel parameter inet_dist_connect_options under the carrier's own options, over nodelay.
connect(Node, IP, Port, Family) ->
    case kernel_options(inet_dist_connect_options, [Family, binary, {active, false}, {packet, 2}], [nodelay()]) of
        {ok, Options} -> connected(Node, gen_tcp:connect(IP, Port, Options, net_kernel:connecttime()));
        _ -> ?shutdown(Node)
    end.

connected(_, {ok, Socket}) -> Socket;
connected(Node, _) -> ?shutdown(Node).

% TCP_NODELAY unless the kernel parameter dist_nodelay is false.
nodelay() ->
    case application:get_env(kernel, dist_nodelay) of
        {ok, false} -> {nodelay, false};
        _ -> {nodelay, true}
    end.

% The handshake callbacks of an accepted connection's controller.
gen_hs_data(Driver, Controller) -> hs_data(Controller, call(Controller, socket), family(Driver)).

% The handshake callbacks of a controller over Socket: requests to the controller, statistics from the socket.
hs_data(Controller, Socket, Family) ->
    #hs_data{
        socket = Controller,
        f_send = fun(Ctrl, Data) -> call(Ctrl, {send, Data}) end,
        f_recv = fun(Ctrl, Length, Timeout) -> call(Ctrl, {recv, Length, Timeout}) end,
        f_setopts_pre_nodeup = fun(Ctrl) -> call(Ctrl, pre_nodeup) end,
        f_setopts_post_nodeup = fun(_) -> ok end,
        f_getll = fun(Ctrl) -> {ok, Ctrl} end,
        f_address = fun(Ctrl, Node) -> call(Ctrl, {address, Node, Family}) end,
        f_handshake_complete = fun(Ctrl, _, DHandle) -> call(Ctrl, {handshake_complete, DHandle}) end,
        mf_tick = fun(Ctrl) -> Ctrl ! tick end,
        mf_getstat = fun(_) -> stat(inet:getstat(Socket, [recv_cnt, send_cnt, send_pend])) end
    }.

stat({ok, [{recv_cnt, Read}, {send_cnt, Written}, {send_pend, Pending}]}) -> {ok, Read, Written, Pending};
stat(Error) -> Error.

%% The distribution controller (plan 30 step DIST4b2a): a process owning the socket that answers its supervisor's
%% handshake requests, then moves the runtime's packets out and the other node's packets in.
start_controller(Socket) ->
    Controller = spawn(fun() -> handshake_loop(Socket) end),
    ok = gen_tcp:controlling_process(Socket, Controller),
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

handshake_loop(Socket) ->
    receive
        {Ref, From, {handshake_complete, DHandle}} ->
            From ! {Ref, ok},
            ok = inet:setopts(Socket, [{active, once}]),
            ok = erlang:dist_ctrl_get_data_notification(DHandle),
            data_loop(Socket, DHandle);
        {Ref, From, Request} ->
            From ! {Ref, request(Socket, Request)},
            handshake_loop(Socket);
        _ ->
            handshake_loop(Socket)
    end.

request(_, {supervisor, Supervisor}) -> link(Supervisor);
request(Socket, socket) -> Socket;
request(Socket, {send, Data}) -> gen_tcp:send(Socket, Data);
request(Socket, {recv, Length, Timeout}) -> gen_tcp:recv(Socket, Length, Timeout);
request(Socket, pre_nodeup) -> inet:setopts(Socket, [{packet, 4}]);
request(Socket, {address, Node, Family}) -> peer_address(Socket, Node, Family).

peer_address(Socket, Node, Family) ->
    {ok, Address} = inet:peername(Socket),
    [_, Host] = string:split(atom_to_list(Node), "@"),
    #net_address{address = Address, host = Host, protocol = tcp, family = Family}.

% The up connection: the runtime's packets go out when it signals dist_data, the other node's come in one at a time.
data_loop(Socket, DHandle) ->
    receive
        dist_data ->
            send_queued(Socket, DHandle);
        {tcp, Socket, Data} ->
            put_data(DHandle, Data),
            ok = inet:setopts(Socket, [{active, once}]);
        tick ->
            send_packet(Socket, <<>>);
        {tcp_closed, Socket} ->
            exit(connection_closed);
        {tcp_error, Socket, _} ->
            exit(connection_closed);
        _ ->
            ok
    end,
    data_loop(Socket, DHandle).

send_queued(Socket, DHandle) ->
    case erlang:dist_ctrl_get_data(DHandle) of
        none ->
            erlang:dist_ctrl_get_data_notification(DHandle);
        Packet ->
            send_packet(Socket, Packet),
            send_queued(Socket, DHandle)
    end.

send_packet(Socket, Packet) ->
    case gen_tcp:send(Socket, Packet) of
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

%% Not provided: OTP's fam_listen/4, fam_setup/4, getstat/1 and tick/2 are undocumented, and the carrier does without.
fam_listen(_, _, _, _) -> erlang:error({not_implemented, fam_listen, 4}).
fam_setup(_, _, _, _) -> erlang:error({not_implemented, fam_setup, 4}).
getstat(_) -> erlang:error({not_implemented, getstat, 1}).
tick(_, _) -> erlang:error({not_implemented, tick, 2}).
