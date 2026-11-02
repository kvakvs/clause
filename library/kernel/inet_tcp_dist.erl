%% Clause's inet_tcp_dist (plan 30 step DIST4b1a, docs/distribution.md#starting-distribution): the IPv4 TCP carrier of
%% distribution over gen_tcp. It listens on a port of the kernel parameters inet_dist_listen_min/max and registers
%% that port with epmd; it accepts and sets up connections through dist_util's handshake, each with a controller
%% process over its socket (DIST4b2a, docs/distribution.md#carriers; clause_dist_controller since DIST12a). Its gen_*
%% functions take a driver, inet_tcp or inet6_tcp, and its fam_* functions an address family, so that inet6_tcp_dist
%% (DIST4b2c) and the TLS carriers (DIST12a) run over them.
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

% Listen for distribution on Host and register the port under the alive name Name with the epmd module:
% {ok, {Socket, Address, Creation}}, or the error of either.
gen_listen(Driver, Name, Host) ->
    Family = family(Driver),
    Listen = fun(First, Last, Options) ->
        listen_socket(lists:seq(First, Last), merge_options(Options, [Family, binary, {active, false}, {packet, 2}]))
    end,
    case fam_listen(Family, Name, Host, Listen) of
        {ok, {Socket, Address, Creation}} ->
            {ok, {Socket, #net_address{address = Address, host = Host, protocol = tcp, family = Family}, Creation}};
        Error ->
            Error
    end.

%% OTP's fam_listen/4, which the TLS carriers share (plan 30 step DIST12a): ListenFun(First, Last, Options) listens on
%% the port range with the kernel's listen options, then the port is registered with the epmd module:
%% {ok, {Socket, {IP, Port}, Creation}}, or the error of either.
fam_listen(Family, Name, Host, ListenFun) ->
    case listen_options() of
        {ok, Options} ->
            {First, Last} = port_range(Name, Host),
            registered(ListenFun(First, Last, Options), Name, Family);
        Error ->
            Error
    end.

registered({ok, Socket}, Name, Family) ->
    {ok, {IP, Port}} = inet:sockname(Socket),
    case register_node(net_kernel:epmd_module(), Name, Port, Family) of
        {ok, Creation} ->
            {ok, {Socket, {IP, Port}, Creation}};
        Error ->
            gen_tcp:close(Socket),
            Error
    end;
registered(Error, _, _) ->
    Error.

% erl_epmd is called by name, so that programs link it.
register_node(erl_epmd, Name, Port, Family) -> erl_epmd:register_node(Name, Port, Family);
register_node(Module, Name, Port, Family) -> Module:register_node(Name, Port, Family).

% The address family of a driver (IPv6 carrier).
family(inet_tcp) -> inet;
family(inet6_tcp) -> inet6.

% The ports to try: the epmd module's listen port, else inet_dist_listen_min to inet_dist_listen_max (min alone is one
% port), else any.
port_range(Name, Host) ->
    case listen_port_please(net_kernel:epmd_module(), Name, Host) of
        {ok, Port} when Port > 0 -> {Port, Port};
        _ -> kernel_port_range()
    end.

listen_port_please(erl_epmd, Name, Host) -> erl_epmd:listen_port_please(Name, Host);
listen_port_please(Module, Name, Host) -> call_optional(Module, listen_port_please, [Name, Host], fun() -> {ok, 0} end).

kernel_port_range() ->
    case application:get_env(kernel, inet_dist_listen_min) of
        {ok, Min} when is_integer(Min) ->
            case application:get_env(kernel, inet_dist_listen_max) of
                {ok, Max} when is_integer(Max), Max >= Min -> {Min, Max};
                _ -> {Min, Min}
            end;
        _ ->
            {0, 0}
    end.

% Module:Function(Arguments...) when the module exports it, else Default().
call_optional(Module, Function, Arguments, Default) ->
    _ = code:ensure_loaded(Module),
    case erlang:function_exported(Module, Function, length(Arguments)) of
        true -> apply(Module, Function, Arguments);
        false -> Default()
    end.

% The first port of Ports a listen socket takes; a port in use moves to the next one.
listen_socket([Port | Ports], Options) ->
    case gen_tcp:listen(Port, Options) of
        {error, eaddrinuse} when Ports =/= [] -> listen_socket(Ports, Options);
        Result -> Result
    end.

% The kernel parameter inet_dist_listen_options under inet_dist_use_interface.
listen_options() ->
    Interface =
        case application:get_env(kernel, inet_dist_use_interface) of
            {ok, IP} -> [{ip, IP}];
            undefined -> []
        end,
    kernel_options(inet_dist_listen_options, Interface, [{backlog, ?BACKLOG}]).

% The options of a kernel parameter under Forced and over Additional: {ok, Options}, or badarg for one with an address
% family, which the carrier sets itself, as OTP's does.
kernel_options(Parameter, Forced, Additional) ->
    Options = kernel_list(Parameter),
    case lists:member(family, option_keys(Options)) of
        true -> {error, badarg};
        false -> {ok, merge_options(Options, Forced, Additional)}
    end.

kernel_list(Parameter) ->
    case application:get_env(kernel, Parameter) of
        {ok, List} when is_list(List) -> List;
        _ -> []
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
    Timer = dist_util:start_timer(SetupTime),
    ParseAddress = fun(Host) -> inet:parse_strict_address(Host, Family) end,
    {#net_address{address = {IP, Port}}, Options, Version} = fam_setup(Family, Node, LongOrShortNames, ParseAddress),
    dist_util:reset_timer(Timer),
    Socket = connect(Node, IP, Port, Family, Options),
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

%% OTP's fam_setup/4, which the TLS carriers share (plan 30 step DIST12a): the address of Node's host and the port and
%% protocol version the epmd module gives for it, as {#net_address{}, ConnectOptions, Version}, the kernel's connect
%% options under passive packets of 2 bytes. ParseAddress checks an address host of long names. An illegal name or an
%% unknown node ends the caller.
fam_setup(Family, Node, LongOrShortNames, ParseAddress) ->
    [Name, Host] = split_node(Node, LongOrShortNames, ParseAddress),
    {IP, Port, Version} = node_port(Node, Name, Host, Family),
    Address = #net_address{address = {IP, Port}, host = Host, protocol = tcp, family = Family},
    {Address, merge_options(kernel_list(inet_dist_connect_options), [{active, false}, {packet, 2}]), Version}.

% The name and host parts of Node, as the node's naming allows them; an illegal one is logged and ends the setup.
split_node(Node, LongOrShortNames, ParseAddress) ->
    case string:split(atom_to_list(Node), "@", all) of
        [[_ | _] = Name, [_ | _] = Host] ->
            check_host(Node, Host, LongOrShortNames, lists:member($., Host), ParseAddress),
            [Name, Host];
        [_] ->
            error_logger:error_msg("** Nodename ~p illegal, no '@' character **~n", [Node]),
            ?shutdown(Node);
        _ ->
            error_logger:error_msg("** Nodename ~p illegal **~n", [Node]),
            ?shutdown(Node)
    end.

% Long names need a dotted host or an IP address; short names a host without dots.
check_host(Node, Host, longnames, false, ParseAddress) ->
    case ParseAddress(Host) of
        {ok, _} ->
            ok;
        _ ->
            error_logger:error_msg(
                "** System running to use fully qualified hostnames **~n** Hostname ~ts is illegal **~n", [Host]
            ),
            ?shutdown(Node)
    end;
check_host(Node, Host, shortnames, true, _) ->
    error_logger:error_msg(
        "** System NOT running to use fully qualified hostnames **~n** Hostname ~ts is illegal **~n", [Host]
    ),
    ?shutdown(Node);
check_host(_, _, _, _, _) ->
    ok.

% The address of Host and the port and protocol version for Name there: the epmd module's address_please/3 when it
% gives them all, else its port_please/2 at the address.
node_port(Node, Name, Host, Family) ->
    Module = net_kernel:epmd_module(),
    case address_please(Module, Name, Host, Family) of
        {ok, IP, Port, Version} ->
            {IP, Port, Version};
        {ok, IP} ->
            case port_please(Module, Name, IP) of
                {port, Port, Version} -> {IP, Port, Version};
                _ -> ?shutdown(Node)
            end;
        _ ->
            ?shutdown(Node)
    end.

% erl_epmd is called by name, so that programs link it.
address_please(erl_epmd, Name, Host, Family) ->
    erl_epmd:address_please(Name, Host, Family);
address_please(Module, Name, Host, Family) ->
    call_optional(Module, address_please, [Name, Host, Family], fun() -> inet:getaddr(Host, Family) end).

port_please(erl_epmd, Name, IP) -> erl_epmd:port_please(Name, IP);
port_please(Module, Name, IP) -> Module:port_please(Name, IP).

% The connect options under the carrier's own, over nodelay; an address family among them ends the setup.
connect(Node, IP, Port, Family, Options) ->
    case lists:member(family, option_keys(Options)) of
        true ->
            ?shutdown(Node);
        false ->
            Merged = merge_options(Options, [Family, binary, {active, false}, {packet, 2}], [nodelay()]),
            connected(Node, gen_tcp:connect(IP, Port, Merged, net_kernel:connecttime()))
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

hs_data(Controller, Socket, Family) ->
    clause_dist_controller:hs_data(Controller, Socket, clause_dist_controller:tcp(), Family).

% A controller over Socket (clause_dist_controller, DIST12a) and the requests to it.
start_controller(Socket) -> clause_dist_controller:start(Socket, clause_dist_controller:tcp()).

call(Controller, Request) -> clause_dist_controller:call(Controller, Request).

%% Not provided: OTP's getstat/1 and tick/2 are undocumented, and the carrier does without.
getstat(_) -> erlang:error({not_implemented, getstat, 1}).
tick(_, _) -> erlang:error({not_implemented, tick, 2}).
