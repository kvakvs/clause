%% Clause's net_kernel (plan 21 step LIB25, docs/libraries/library.md): OTP's net_kernel server of a distributed node,
%% started by start/1,2 under erl_distribution's net_sup (plan 30 step DIST4b1a, docs/distribution.md#starting-
%% distribution). It names the node from the carrier's listen and epmd registration. Without it, requests are
%% ignored as OTP's are. Node status subscriptions are the runtime's (plan 30 step DIST2b). It accepts and sets up
%% connections through the carriers and dist_util's handshake, and ticks them (DIST4b2a, docs/distribution.md#connections).
-module(net_kernel).
-behaviour(gen_server).
-include_lib("kernel/include/inet.hrl").
-include_lib("kernel/include/net_address.hrl").
-export([
    allow/1,
    allowed/0,
    async_disconnect/1,
    aux_ticker/4,
    code_change/3,
    connect_node/1,
    connecttime/0,
    dflag_unicode_io/1,
    disconnect/1,
    do_spawn/3,
    epmd_module/0,
    get_net_ticktime/0,
    get_state/0,
    getopts/2,
    handle_call/3,
    handle_cast/2,
    handle_info/2,
    hidden_connect_node/1,
    i/0,
    i/1,
    init/1,
    kernel_apply/3,
    longnames/0,
    monitor_nodes/1,
    monitor_nodes/2,
    node_info/1,
    node_info/2,
    nodename/0,
    nodes_info/0,
    passive_cnct/1,
    passive_connect_monitor/2,
    protocol_childspecs/0,
    publish_on_node/1,
    set_net_ticktime/1,
    set_net_ticktime/2,
    setopts/2,
    spawn_func/6,
    start/1,
    start/2,
    start_link/1,
    stop/0,
    terminate/2,
    ticker/2,
    ticker_loop/2,
    verbose/1
]).

% The kernel parameters' defaults: net tick time in seconds and ticks per tick time.
-define(TICKTIME, 60).
-define(TICKINTENSITY, 4).
% The carrier of node names without --proto-dist.
-define(PROTO, "inet_tcp").

% A started node: its name (undefined until a peer names it), how it was named and started, its tick settings,
% whether it is hidden, its carriers' listen sockets as {Module, Socket, Address}, and the carrier modules that set up
% connections. Connections (DIST4b2a): the entries by node, their handshake or supervisor processes by pid, and the
% controllers by pid.
-record(state, {
    node,
    name_type,
    name_domain,
    started,
    ticktime,
    intensity,
    hidden,
    listen = [],
    carriers = [],
    connections = #{},
    owners = #{},
    controllers = #{}
}).

% A connection to another node (DIST4b2a): pending during its handshake, then up; the handshake process that becomes
% its supervisor, its dist handle, the connect_node callers waiting for it, the other node's address and its type.
-record(connection, {state = pending, owner, handle, waiting = [], address, type = normal}).

get_state() ->
    case request(get_state) of
        ignored -> #{started => no};
        State -> State
    end.

nodename() -> request(nodename).
longnames() -> request(longnames).
get_net_ticktime() -> request(ticktime).

%% Connections (plan 30 step DIST4b2a): true once the connection is up, false when it cannot be made.
connect_node(Node) when is_atom(Node) -> request({connect, normal, Node}).
hidden_connect_node(Node) when is_atom(Node) -> request({connect, hidden, Node}).

% End the connection to Node; true once it is down, false without one. erlang:disconnect_node/1 calls it, as OTP's.
disconnect(Node) -> request({disconnect, Node}).

%% Requests of a started node that DIST6 answers; ignored as without distribution until then.
allow(_) -> ignored.
allowed() -> ignored.
set_net_ticktime(_) -> ignored.
set_net_ticktime(_, _) -> ignored.
getopts(_, _) -> ignored.
setopts(_, _) -> ignored.

% A call to the net_kernel server; ignored without one, as OTP's.
request(Request) ->
    case whereis(net_kernel) of
        undefined -> ignored;
        _ -> gen_server:call(net_kernel, Request, infinity)
    end.

async_disconnect(_) -> ok.

% Whether this node is published to the nodes it connects to: not when hidden (DIST4b2a).
publish_on_node(Node) -> request({publish_on_node, Node}) =:= true.

%% Whether net_kernel holds a connection to Node up for erlang:dmonitor_node/3; false while no process is registered
%% as net_kernel (plan 30 step DIST2c).
passive_cnct(Node) ->
    case request({passive_cnct, Node}) of
        ignored -> false;
        Up -> Up
    end.

dflag_unicode_io(_) -> true.

protocol_childspecs() -> [].

% The epmd client module, the kernel parameter epmd_module.
epmd_module() ->
    case application:get_env(kernel, epmd_module) of
        {ok, Module} when is_atom(Module) -> Module;
        _ -> erl_epmd
    end.

connecttime() -> 7000.

stop() -> erl_distribution:stop().

%% Starting distribution: the documented start/1 lists as start/2's options, which are checked before net_sup starts.
start([Name]) when is_atom(Name) ->
    start([Name, longnames, 15000]);
start([Name, NameDomain]) when is_atom(Name), is_atom(NameDomain) ->
    start([Name, NameDomain, 15000]);
start([Name, NameDomain, TickTime]) when is_atom(Name), is_atom(NameDomain), is_integer(TickTime), TickTime > 0 ->
    % TickTime is the time between ticks at intensity 4, net_ticktime seconds rounded up.
    start(Name, #{name_domain => NameDomain, net_ticktime => ((TickTime * 4 - 1) div 1000) + 1, net_tickintensity => 4}).

start(Name, Options) when is_atom(Name), is_map(Options) ->
    lists:foreach(
        fun({Key, Value}) ->
            start_option(Key, Value) orelse erlang:error({invalid_option, Key, Value}, [Name, Options])
        end,
        maps:to_list(Options)
    ),
    erl_distribution:start(Options#{name => Name});
start(Name, Options) when is_map(Options) ->
    erlang:error(invalid_name, [Name, Options]);
start(Name, Options) ->
    erlang:error(invalid_options, [Name, Options]).

% Whether Value is a valid value of start/2's option Key.
start_option(name_domain, Domain) -> Domain =:= shortnames orelse Domain =:= longnames;
start_option(net_ticktime, Time) -> is_integer(Time) andalso Time > 0;
start_option(net_tickintensity, Intensity) -> is_integer(Intensity) andalso Intensity >= 4 andalso Intensity =< 1000;
start_option(dist_listen, Listen) -> is_boolean(Listen);
start_option(hidden, Hidden) -> is_boolean(Hidden);
start_option(_, _) -> false.

% The net_kernel server as net_sup's child; a server that cannot start ends the start with nodistribution.
start_link(Options) ->
    case gen_server:start_link({local, net_kernel}, ?MODULE, Options, []) of
        {ok, Pid} -> {ok, Pid};
        {error, {already_started, Pid}} -> {ok, Pid};
        _ -> exit(nodistribution)
    end.

%% The server: names the node, listening and registering with epmd first unless dist_listen is false. dist_listen
%% and hidden default to the node start options --dist-listen and --hidden.
init(#{name := Name} = Options) ->
    process_flag(trap_exit, true),
    Listen =
        maps:get(dist_listen, Options, init:get_argument(dist_listen) =/= {ok, [["false"]]}) andalso
            Name =/= undefined,
    State = #state{
        name_domain = maps:get(name_domain, Options, longnames),
        started = started(Options),
        ticktime = maps:get(net_ticktime, Options, kernel_ticktime()),
        intensity = maps:get(net_tickintensity, Options, ?TICKINTENSITY),
        hidden = maps:get(hidden, Options, init:get_argument(hidden) =/= error) orelse not Listen
    },
    case Name of
        undefined -> dynamic_name(State);
        _ -> name_node(node_name(atom_to_list(Name), State#state.name_domain), Listen, State)
    end.

% The node of net_kernel:start/1,2 is dynamic, the node of the node start options static.
started(#{supervisor := net_sup_dynamic}) -> dynamic;
started(_) -> static.

% The kernel parameter net_ticktime: an invalid value is silently replaced, as OTP's.
kernel_ticktime() ->
    case application:get_env(kernel, net_ticktime) of
        {ok, Time} when is_integer(Time), Time > 0 -> Time;
        {ok, Time} when is_integer(Time) -> 1;
        _ -> ?TICKTIME
    end.

% A node that asks the first peer for its name, which then comes with the handshake (DIST4b2).
dynamic_name(State) ->
    erts_internal:dynamic_node_name(true),
    {ok, State#state{name_type = dynamic}}.

% The node name of the name part and the host part of Text, or OTP's log text of a name it refuses: a host is taken
% as given, else this host's short name or its fully qualified name.
node_name(Text, Domain) ->
    [Alive | Rest] = string:split(Text, "@"),
    case {valid_alive(Alive), Rest, Domain} of
        {false, _, _} -> {error, "Invalid node name!\nPlease check your configuration\n"};
        {true, [[_ | _] = Host], shortnames} -> short_name(Alive, Host);
        {true, [[_ | _] = Host], longnames} -> {ok, Alive, Host};
        {true, _, shortnames} -> short_name(Alive, hd(string:split(hostname(), ".")));
        {true, _, longnames} -> long_name(Alive, full_hostname())
    end.

% Whether a name part is letters, digits, - and _, as OTP accepts it.
valid_alive(Alive) ->
    Alive =/= [] andalso
        lists:all(
            fun(C) ->
                (C >= $a andalso C =< $z) orelse (C >= $A andalso C =< $Z) orelse
                    (C >= $0 andalso C =< $9) orelse C =:= $- orelse C =:= $_
            end,
            Alive
        ).

short_name(Alive, Host) ->
    case lists:member($., Host) of
        true -> {error, "Can't set short node name!\nPlease check your configuration\n"};
        false -> {ok, Alive, Host}
    end.

long_name(Alive, Host) ->
    case lists:member($., Host) of
        true -> {ok, Alive, Host};
        false -> {error, "Can't set long node name!\nPlease check your configuration\n"}
    end.

hostname() ->
    {ok, Host} = inet:gethostname(),
    Host.

% This host's fully qualified name: its host name with a dot, else the name its address resolves to.
full_hostname() ->
    Host = hostname(),
    case {lists:member($., Host), inet:gethostbyname(Host)} of
        {true, _} -> Host;
        {false, {ok, #hostent{h_name = Name}}} -> Name;
        {false, _} -> Host
    end.

% Name the node once its carriers listen and registered it (dist_listen false: a random creation, no registration).
name_node({error, Text}, _, _) ->
    error_logger:info_msg(Text),
    {stop, {error, badarg}};
name_node({ok, Alive, Host}, Listen, State) ->
    Node = list_to_atom(Alive ++ "@" ++ Host),
    case listen_carriers(protocols(), Alive, Host, Node, Listen) of
        {ok, Listeners, Creation} ->
            true = erlang:setnode(Node, Creation),
            Carriers = [list_to_atom(Proto ++ "_dist") || Proto <- protocols()],
            {ok, connecting(State#state{node = Node, name_type = static, listen = Listeners, carriers = Carriers})};
        {error, Proto, Error} ->
            log_listen_error(Proto, Node, Error),
            {stop, {error, badarg}}
    end.

% The carriers of --proto-dist, else inet_tcp; the first one listens.
protocols() ->
    case init:get_argument(proto_dist) of
        {ok, [Protos | _]} when Protos =/= [] -> Protos;
        _ -> [?PROTO]
    end.

listen_carriers(_, _, _, _, false) ->
    {ok, [], rand:uniform(16#fffffff0) + 3};
listen_carriers([Proto | _], Alive, Host, Node, true) ->
    Module = list_to_atom(Proto ++ "_dist"),
    case Module:is_node_name(Node) of
        false ->
            {error, Proto, invalid_node_name};
        true ->
            case listen(Module, Alive, Host) of
                {ok, {Socket, Address, Creation}} -> {ok, [{Module, Socket, Address}], Creation};
                {error, Reason} -> {error, Proto, Reason}
            end
    end.

% The built-in carrier is called by name, so that programs link it.
listen(inet_tcp_dist, Alive, Host) -> inet_tcp_dist:listen(Alive, Host);
listen(Module, Alive, Host) -> Module:listen(Alive, Host).

log_listen_error(Proto, Node, invalid_node_name) ->
    error_logger:info_msg("Protocol '~ts': invalid node name: ~ts~n", [Proto, atom_to_list(Node)]);
log_listen_error(Proto, Node, duplicate_name) ->
    error_logger:info_msg("Protocol '~ts': the name ~ts seems to be in use by another Erlang node~n", [
        Proto, atom_to_list(Node)
    ]);
log_listen_error(Proto, _, Reason) ->
    error_logger:info_msg("Protocol '~ts': register/listen error: ~p~n", [Proto, Reason]).

handle_call(get_state, _From, State) ->
    {reply, state_map(State), State};
handle_call(nodename, _From, State) ->
    {reply, node(), State};
handle_call(longnames, _From, #state{name_domain = Domain} = State) ->
    {reply, Domain =:= longnames, State};
handle_call(ticktime, _From, #state{ticktime = Time} = State) ->
    {reply, Time, State};
handle_call({connect, Type, Node}, From, State) ->
    connect(Type, Node, From, State);
handle_call({disconnect, Node}, _From, #state{connections = Connections} = State) ->
    case Connections of
        #{Node := Connection} -> {reply, true, disconnected(Node, Connection, State)};
        #{} -> {reply, false, State}
    end;
handle_call({publish_on_node, _Node}, _From, #state{hidden = Hidden} = State) ->
    {reply, not Hidden, State};
handle_call({passive_cnct, _Node}, _From, State) ->
    {reply, false, State}.

state_map(#state{node = Node, name_type = Type, name_domain = Domain, started = Started}) ->
    #{started => Started, name => Node, name_type => Type, name_domain => Domain}.

handle_cast(_, State) -> {noreply, State}.

% A node monitor's connection attempt sets the connection up; one that cannot be made is aborted, so the monitor
% gets nodedown.
handle_info({auto_connect, Node, DHandle}, #state{connections = Connections} = State) ->
    case Connections of
        #{Node := _} ->
            {noreply, State};
        #{} ->
            case setup(Node, normal, [], State) of
                {ok, NewState} ->
                    {noreply, NewState};
                error ->
                    erts_internal:abort_pending_connection(Node, DHandle),
                    {noreply, State}
            end
    end;
handle_info({accept, Acceptor, Controller, Family, Protocol}, State) ->
    {noreply, accepted(Acceptor, Controller, Family, Protocol, State)};
handle_info({Pid, {accept_pending, Node, Address, Type}}, State) ->
    {noreply, accept_pending(Pid, Node, Address, Type, State)};
handle_info({Pid, {nodeup, Node, Address, Type}}, State) ->
    {noreply, nodeup(Pid, Node, Address, Type, State)};
handle_info({dist_ctrlr, Controller, Node, _SetupPid}, #state{controllers = Controllers} = State) ->
    link(Controller),
    {noreply, State#state{controllers = Controllers#{Controller => Node}}};
handle_info(tick, #state{connections = Connections} = State) ->
    _ = [Owner ! {self(), tick} || _ := #connection{state = up, owner = Owner} <- Connections],
    {noreply, State};
handle_info({'EXIT', Pid, Reason}, State) ->
    exited(Pid, Reason, State);
handle_info(_, State) ->
    {noreply, State}.

% A node still waiting for its dynamic name is no longer alive once the server ends.
terminate(_, #state{listen = Listeners, name_type = Type}) ->
    Type =:= dynamic andalso erts_internal:dynamic_node_name(false),
    lists:foreach(fun({Module, Socket, _}) -> Module:close(Socket) end, Listeners).

%% Connections (plan 30 step DIST4b2a). A named node accepts on each listen socket and ticks its up connections
%% net_tickintensity times per net_ticktime.
connecting(#state{listen = Listeners, ticktime = Time, intensity = Intensity} = State) ->
    lists:foreach(fun({Module, Socket, _}) -> accept(Module, Socket) end, Listeners),
    spawn_opt(?MODULE, ticker, [self(), Time * 1000 div Intensity], [link, {priority, max}]),
    State.

% The built-in carrier is called by name, so that programs link it.
accept(inet_tcp_dist, Socket) -> inet_tcp_dist:accept(Socket);
accept(Module, Socket) -> Module:accept(Socket).

% connect_node/1: this node is connected; a connection up answers at once, a pending one once its handshake ends.
connect(_, Node, _, #state{node = Node} = State) ->
    {reply, true, State};
connect(_, _, _, #state{node = undefined} = State) ->
    {reply, false, State};
connect(Type, Node, From, #state{connections = Connections} = State) ->
    case Connections of
        #{Node := #connection{state = up}} ->
            {reply, true, State};
        #{Node := #connection{waiting = Waiting} = Connection} ->
            {noreply, State#state{
                connections = Connections#{Node := Connection#connection{waiting = [From | Waiting]}}
            }};
        #{} ->
            case setup(Node, Type, [From], State) of
                {ok, NewState} -> {noreply, NewState};
                error -> {reply, false, State}
            end
    end.

% Set up a connection to Node with the first carrier that reaches it; error for a node no carrier reaches.
setup(Node, Type, Waiting, #state{node = MyNode, name_domain = Domain, carriers = Carriers} = State) ->
    case [Module || Module <- Carriers, Module:select(Node)] of
        [Module | _] ->
            case catch erts_internal:new_connection(Node) of
                {'EXIT', _} ->
                    error;
                Handle ->
                    Owner = Module:setup(Node, Type, MyNode, Domain, connecttime()),
                    Connection = #connection{owner = Owner, handle = Handle, waiting = Waiting, type = Type},
                    {ok, insert(Node, Connection, State)}
            end;
        [] ->
            error
    end.

insert(Node, #connection{owner = Owner} = Connection, #state{connections = Connections, owners = Owners} = State) ->
    State#state{connections = Connections#{Node => Connection}, owners = Owners#{Owner => Node}}.

% An acceptor's connection: the carrier of its family and protocol starts the handshake process.
accepted(Acceptor, Controller, Family, Protocol, #state{node = MyNode, listen = Listeners} = State) ->
    case [Module || {Module, _, #net_address{family = F, protocol = P}} <- Listeners, F =:= Family, P =:= Protocol] of
        [Module | _] ->
            Supervisor = Module:accept_connection(Acceptor, Controller, MyNode, [], connecttime()),
            Acceptor ! {self(), controller, Supervisor};
        [] ->
            Acceptor ! {self(), unsupported_protocol}
    end,
    State.

% An accepted handshake asks to go on: ok when Node has no connection, which turns pending; refused otherwise, as the
% statuses of simultaneous and existing connections come in DIST4b2b.
accept_pending(Pid, Node, Address, Type, #state{node = MyNode, connections = Connections} = State) ->
    case Connections of
        #{Node := _} ->
            Pid ! {self(), {accept_pending, nok_pending}},
            State;
        #{} when Node =:= MyNode ->
            Pid ! {self(), {accept_pending, nok_pending}},
            State;
        #{} ->
            Handle = erts_internal:new_connection(Node),
            Pid ! {self(), {accept_pending, ok}},
            insert(Node, #connection{owner = Pid, handle = Handle, address = Address, type = Type}, State)
    end.

% A handshake ended: the connection is up and its callers get true.
nodeup(Pid, Node, Address, Type, #state{connections = Connections, intensity = Intensity} = State) ->
    case Connections of
        #{Node := #connection{owner = Pid, waiting = Waiting} = Connection} ->
            lists:foreach(fun(From) -> gen_server:reply(From, true) end, Waiting),
            Pid ! {self(), {inserted, Intensity}},
            Up = Connection#connection{state = up, waiting = [], address = Address, type = Type},
            State#state{connections = Connections#{Node := Up}};
        #{} ->
            exit(Pid, bad_nodeup),
            State
    end.

% A connection's process ended: a pending connection is aborted and its callers get false; the runtime takes an up
% one down with its controller. Other processes' ends, such as a failed accepted handshake's, change nothing.
exited(Pid, _Reason, #state{owners = Owners, controllers = Controllers} = State) ->
    case {Owners, Controllers} of
        {#{Pid := Node}, _} -> {noreply, dropped(Node, State)};
        {_, #{Pid := _}} -> {noreply, State#state{controllers = maps:remove(Pid, Controllers)}};
        _ -> {noreply, State}
    end.

dropped(Node, #state{connections = Connections, owners = Owners} = State) ->
    #{Node := #connection{state = Status, owner = Owner, handle = Handle, waiting = Waiting}} = Connections,
    Status =:= pending andalso erts_internal:abort_pending_connection(Node, Handle),
    lists:foreach(fun(From) -> gen_server:reply(From, false) end, Waiting),
    State#state{connections = maps:remove(Node, Connections), owners = maps:remove(Owner, Owners)}.

% disconnect/1: the connection's processes end with reason disconnect, and the answer waits for their ends, so the
% connection is down by then.
disconnected(Node, #connection{owner = Owner}, #state{controllers = Controllers} = State) ->
    Ended = [Owner | [Controller || Controller := ControlledNode <- Controllers, ControlledNode =:= Node]],
    lists:foreach(fun(Pid) -> exit(Pid, disconnect) end, Ended),
    lists:foreach(
        fun(Pid) ->
            receive
                {'EXIT', Pid, _} -> ok
            end
        end,
        Ended
    ),
    Remaining = maps:without(Ended, Controllers),
    dropped(Node, State#state{controllers = Remaining}).

code_change(_, State, _) -> {ok, State}.

%% A subscription to node status messages is a process flag of the runtime.
monitor_nodes(Flag) ->
    try process_flag(monitor_nodes, Flag) of
        _ -> ok
    catch
        error:badarg -> error
    end.

monitor_nodes(Flag, Options) when is_boolean(Flag) ->
    case subscription(Options) of
        {ok, Map} ->
            _ = process_flag({monitor_nodes, Map}, Flag),
            ok;
        Error ->
            Error
    end;
monitor_nodes(_, _) ->
    error.

%% The process flag's map of monitor_nodes/2's options (a list's map holds list => true), or the error for them: bad
%% values first, then unknown options.
subscription(Options) when is_list(Options) -> list_options(Options, #{list => true}, []);
subscription(Options) when is_map(Options) -> map_options(Options);
subscription(Options) -> {error, {invalid_options, Options}}.

list_options([], Map, []) ->
    {ok, Map};
list_options([], _, Unknown) ->
    {error, {unknown_options, lists:reverse(Unknown)}};
list_options([connection_id | Rest], Map, Unknown) ->
    list_options(Rest, Map#{connection_id => true}, Unknown);
list_options([nodedown_reason | Rest], Map, Unknown) ->
    list_options(Rest, Map#{nodedown_reason => true}, Unknown);
list_options([{node_type, Type} = Option | Rest], Map, Unknown) ->
    case {node_type(Type), Map} of
        {false, _} -> {error, {bad_option_value, Option}};
        {true, #{node_type := Old}} when Old =/= Type -> {error, {option_value_mismatch, [Option, {node_type, Old}]}};
        {true, _} -> list_options(Rest, Map#{node_type => Type}, Unknown)
    end;
list_options([Option | Rest], Map, Unknown) ->
    list_options(Rest, Map, [Option | Unknown]).

map_options(Options) ->
    Keys = [connection_id, node_type, nodedown_reason],
    Bad = maps:filter(fun(Key, Value) -> not option_value(Key, Value) end, maps:with(Keys, Options)),
    Unknown = maps:without(Keys, Options),
    if
        map_size(Bad) > 0 -> {error, {bad_option_value, Bad}};
        map_size(Unknown) > 0 -> {error, {unknown_options, Unknown}};
        true -> {ok, Options}
    end.

option_value(node_type, Type) -> node_type(Type);
option_value(_, Value) -> is_boolean(Value).

node_type(Type) -> lists:member(Type, [visible, hidden, all]).

%% The ticker (plan 30 step DIST4b2a): a linked process, spawned at max priority, that sends net_kernel tick every
%% Interval milliseconds.
ticker(Kernel, Interval) -> ticker_loop(Kernel, Interval).

ticker_loop(Kernel, Interval) ->
    receive
    after Interval ->
        Kernel ! tick,
        ticker_loop(Kernel, Interval)
    end.

%% The rest of the distribution's processes come with net_kernel's complete API (DIST6).
aux_ticker(_, _, _, _) -> erlang:error({not_implemented, aux_ticker, 4}).
do_spawn(_, _, _) -> erlang:error({not_implemented, do_spawn, 3}).
i() -> erlang:error({not_implemented, i, 0}).
i(_) -> erlang:error({not_implemented, i, 1}).
kernel_apply(_, _, _) -> erlang:error({not_implemented, kernel_apply, 3}).
node_info(_) -> erlang:error({not_implemented, node_info, 1}).
node_info(_, _) -> erlang:error({not_implemented, node_info, 2}).
nodes_info() -> erlang:error({not_implemented, nodes_info, 0}).
passive_connect_monitor(_, _) -> erlang:error({not_implemented, passive_connect_monitor, 2}).
spawn_func(_, _, _, _, _, _) -> erlang:error({not_implemented, spawn_func, 6}).
verbose(_) -> erlang:error({not_implemented, verbose, 1}).
