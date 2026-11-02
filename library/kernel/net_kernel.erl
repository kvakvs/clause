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
% The seconds of set_net_ticktime/1's transition period (DIST6).
-define(TRANSITION_PERIOD, 60).
% The milliseconds a connection setup may take without the kernel parameter net_setuptime.
-define(SETUPTIME, 7000).
% The carrier of node names without --proto-dist.
-define(PROTO, "inet_tcp").

% A started node: its name (undefined until a peer names it), how it was named and started, its tick settings (tick
% time in milliseconds, ticks per tick time, the ticker process, and longer or shorter while set_net_ticktime/2
% changes the tick time, DIST6), whether it is hidden, its carriers' listen sockets as {Module, Socket, Address}, and
% the carrier modules that set up connections. Connections (DIST4b2a): the entries by node, their handshake or
% supervisor processes by pid, and the controllers by pid. The nodes of allow/1 (DIST4b2b). A dynamic name (DIST4b2b):
% the host it is asked with, the node whose connection gave it; and the names this node gives others: a random seed,
% and the last creation of each. DIST6: verbose/1's level, the requests to connection supervisors waiting for their
% answers by monitor reference, and the nodes dist_auto_connect once keeps from connecting again.
-record(state, {
    node,
    name_type,
    name_domain,
    started,
    ticktime,
    intensity,
    ticker,
    change,
    hidden,
    listen = [],
    carriers = [],
    connections = #{},
    owners = #{},
    controllers = #{},
    allowed = [],
    host,
    named_by,
    seed,
    creations = #{},
    verbose = 0,
    requests = #{},
    barred = []
}).

% A connection to another node (DIST4b2a): pending during its handshake, then up; the handshake process that becomes
% its supervisor, its dist handle, the connect_node callers waiting for it, the other node's address and its type, and
% whether this node set it up or accepted it (simultaneous connects, DIST4b2b).
-record(connection, {
    state = pending, owner, handle, waiting = [], address = #net_address{}, type = normal, direction = accept
}).

get_state() ->
    case request(get_state) of
        ignored -> #{started => no};
        State -> State
    end.

nodename() -> request(nodename).
longnames() -> request(longnames).

%% Tick time changes (plan 30 step DIST6): the tick time in seconds, or {ongoing_change_to, Seconds} while a change
%% runs; set_net_ticktime/1,2 answers unchanged, change_initiated or {ongoing_change_to, Seconds}.
get_net_ticktime() -> ticktime_res(request(ticktime)).

set_net_ticktime(Time, Period) when is_integer(Time), Time > 0, is_integer(Period), Period >= 0 ->
    ticktime_res(request({new_ticktime, Time * 1000, Period * 1000})).

set_net_ticktime(Time) when is_integer(Time) -> set_net_ticktime(Time, ?TRANSITION_PERIOD).

ticktime_res({Atom, Milliseconds}) when is_atom(Atom), is_integer(Milliseconds) -> {Atom, Milliseconds div 1000};
ticktime_res(Milliseconds) when is_integer(Milliseconds) -> Milliseconds div 1000;
ticktime_res(Atom) when is_atom(Atom) -> Atom.

%% Connections (plan 30 step DIST4b2a): true once the connection is up, false when it cannot be made.
connect_node(Node) when is_atom(Node) -> request({connect, normal, Node}).
hidden_connect_node(Node) when is_atom(Node) -> request({connect, hidden, Node}).

% End the connection to Node; true once it is down, false without one. erlang:disconnect_node/1 calls it, as OTP's.
disconnect(Node) -> request({disconnect, Node}).

%% The nodes that may connect to and from this one (plan 30 step DIST4b2b): allow/1 adds to them, error for a list
%% with a non-atom; any node may while none were added.
allow(Nodes) -> request({allow, Nodes}).

allowed() -> request(allowed).

%% Socket options of distribution (plan 30 step DIST6): of an up connection through its carrier's mf_setopts and
%% mf_getopts, or with Node new of the listen sockets and the connections made from now on.
setopts(Node, Options) when is_atom(Node), is_list(Options) -> request({setopts, Node, Options}).

getopts(Node, Options) when is_atom(Node), is_list(Options) -> request({getopts, Node, Options}).

% Not applicable any longer, as OTP answers it.
kernel_apply(Module, Function, Arguments) -> request({apply, Module, Function, Arguments}).

% The level of info reports about connections, 0 for none; answers the level before.
verbose(Level) when is_integer(Level) -> request({verbose, Level}).

% A call to the net_kernel server; ignored without one, as OTP's.
request(Request) ->
    case whereis(net_kernel) of
        undefined -> ignored;
        _ -> gen_server:call(net_kernel, Request, infinity)
    end.

% disconnect/1 without waiting for the connection to end (DIST6).
async_disconnect(Node) -> gen_server:cast(net_kernel, {async_disconnect, Node}).

% Whether this node is published to Node: not when hidden (DIST4b2a), except to its global group (DIST8b).
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

% The milliseconds a connection setup may take: the kernel parameter net_setuptime in seconds, at most 120 (DIST6).
connecttime() ->
    case application:get_env(kernel, net_setuptime) of
        {ok, Time} when is_number(Time), Time >= 120 -> 120 * 1000;
        {ok, Time} when is_number(Time), Time > 0 -> round(Time * 1000);
        _ -> ?SETUPTIME
    end.

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
    % Other nodes' processes this node leads send their I/O to user (plan 30 step DIST7c).
    ok = clause_io:start_user(),
    Listen =
        maps:get(dist_listen, Options, init:get_argument(dist_listen) =/= {ok, [["false"]]}) andalso
            not dynamic(Name),
    Intensity = maps:get(net_tickintensity, Options, kernel_tickintensity()),
    State = #state{
        name_domain = maps:get(name_domain, Options, longnames),
        started = started(Options),
        ticktime = tick_multiple(maps:get(net_ticktime, Options, kernel_ticktime()) * 1000, Intensity),
        intensity = Intensity,
        hidden = maps:get(hidden, Options, init:get_argument(hidden) =/= error) orelse not Listen,
        seed = rand:uniform(1 bsl 62)
    },
    Text = atom_to_list(Name),
    case dynamic(Name) of
        true -> dynamic_name(node_name(Text, State#state.name_domain), State);
        false -> name_node(node_name(Text, State#state.name_domain), Listen, State)
    end.

% Whether a node name asks the first peer for a name: undefined, also with a host (undefined@Host), as OTP's.
dynamic(Name) -> hd(string:split(atom_to_list(Name), "@")) =:= "undefined".

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

% The kernel parameter net_tickintensity, kept within 4 to 1000 (DIST6).
kernel_tickintensity() ->
    case application:get_env(kernel, net_tickintensity) of
        {ok, Intensity} when is_integer(Intensity) -> min(max(Intensity, 4), 1000);
        _ -> ?TICKINTENSITY
    end.

% A tick time in milliseconds rounded up to a multiple of the tick intensity, as OTP keeps it.
tick_multiple(Time, Intensity) when Time rem Intensity =:= 0 -> Time;
tick_multiple(Time, Intensity) -> (Time div Intensity + 1) * Intensity.

% A node that asks the first peer for its name, which then comes with the handshake, giving this node's host
% (DIST4b2b).
dynamic_name({error, Text}, _) ->
    error_logger:info_msg(Text),
    {stop, {error, badarg}};
dynamic_name({ok, _, Host}, State) ->
    erts_internal:dynamic_node_name(true),
    {ok, connecting(State#state{name_type = dynamic, host = Host, carriers = carriers()})}.

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
            {ok, connecting(State#state{node = Node, name_type = static, listen = Listeners, carriers = carriers()})};
        {error, Proto, Error} ->
            log_listen_error(Proto, Node, Error),
            {stop, {error, badarg}}
    end.

carriers() -> [list_to_atom(Proto ++ "_dist") || Proto <- protocols()].

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

% The built-in carriers are called by name, so that programs link them.
listen(inet_tcp_dist, Alive, Host) -> inet_tcp_dist:listen(Alive, Host);
listen(inet6_tcp_dist, Alive, Host) -> inet6_tcp_dist:listen(Alive, Host);
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
handle_call(ticktime, _From, #state{ticktime = Time, change = undefined} = State) ->
    {reply, Time, State};
handle_call(ticktime, _From, #state{ticktime = Time} = State) ->
    {reply, {ongoing_change_to, Time}, State};
handle_call({new_ticktime, Time, Period}, _From, State) ->
    new_ticktime(Time, Period, State);
handle_call({connect, Type, Node}, From, State) ->
    verbose({connect, Type, Node}, 1, State),
    connect(Type, Node, From, State);
% disconnect/1 ends an up connection only (DIST6).
handle_call({disconnect, Node}, _From, #state{connections = Connections} = State) ->
    verbose({disconnect, Node}, 1, State),
    case Connections of
        #{Node := #connection{state = up} = Connection} ->
            {reply, true, disconnected(Node, Connection, disconnect, State)};
        #{} ->
            {reply, false, State}
    end;
handle_call({setopts, new, Options}, From, State) ->
    setopts_new(Options, From, State);
handle_call({Operation, Node, Options}, From, State) when Operation =:= setopts; Operation =:= getopts ->
    node_options(Operation, Node, Options, From, State);
handle_call({connection, Node}, _From, #state{connections = Connections} = State) ->
    {reply, maps:find(Node, Connections), State};
handle_call(connections, _From, #state{connections = Connections} = State) ->
    {reply, maps:to_list(Connections), State};
handle_call({verbose, Level}, _From, #state{verbose = Old} = State) ->
    {reply, Old, State#state{verbose = Level}};
handle_call({apply, _, _, _}, {Pid, _}, State) when is_pid(Pid), node(Pid) =:= node() ->
    {reply, not_implemented, State};
% The spawns of other nodes' spawn/4, spawn_link/4 and spawn_opt/5 before spawn requests (DIST6).
handle_call({spawn, Module, Function, Arguments, Leader}, {Pid, _} = From, State) when is_pid(Pid) ->
    do_spawn([no_link, From, Module, Function, Arguments, Leader], [], State);
handle_call({spawn_link, Module, Function, Arguments, Leader}, {Pid, _} = From, State) when is_pid(Pid) ->
    do_spawn([link, From, Module, Function, Arguments, Leader], [], State);
handle_call({spawn_opt, Module, Function, Arguments, Options, Link, Leader}, {Pid, _} = From, State) when
    is_pid(Pid)
->
    do_spawn([Link, From, Module, Function, Arguments, Leader], Options, State);
handle_call({publish_on_node, Host}, _From, #state{hidden = Hidden} = State) when not is_atom(Host) ->
    % A node asking for a dynamic name, known by its host only, is in no global group.
    {reply, not Hidden, State};
handle_call({publish_on_node, Node}, _From, #state{hidden = Hidden} = State) ->
    % A hidden global group's nodes are visible only to their group, a hidden node is visible to its group (DIST8b).
    Type =
        case Hidden of
            true -> hidden;
            false -> normal
        end,
    {reply, global_group:publish(Type, Node), State};
handle_call({allow, Nodes}, _From, #state{allowed = Allowed} = State) ->
    case is_list(Nodes) andalso lists:all(fun erlang:is_atom/1, Nodes) of
        true -> {reply, ok, State#state{allowed = lists:foldl(fun add_allowed/2, Allowed, Nodes)}};
        false -> {reply, error, State}
    end;
handle_call(allowed, _From, #state{allowed = Allowed} = State) ->
    {reply, {ok, Allowed}, State};
handle_call({passive_cnct, Node}, _From, #state{node = Node} = State) ->
    {reply, true, State};
handle_call({passive_cnct, Node}, From, State) ->
    verbose({passive_cnct, Node}, 1, State),
    passive_connect(Node, From, State);
% A call from another node comes through only from an authorized node: net_adm:ping/1 (plan 30 step DIST4b3b1).
handle_call({is_auth, _Node}, _From, State) ->
    {reply, yes, State};
% Other calls stay unanswered, as OTP's.
handle_call(_, _From, State) ->
    {noreply, State}.

state_map(#state{node = Node, name_type = Type, name_domain = Domain, started = Started}) ->
    #{started => Started, name => Node, name_type => Type, name_domain => Domain}.

% The allowed nodes in the order allow/1 added them, each once.
add_allowed(Node, Allowed) ->
    case lists:member(Node, Allowed) of
        true -> Allowed;
        false -> Allowed ++ [Node]
    end.

handle_cast({async_disconnect, Node}, #state{connections = Connections} = State) ->
    verbose({async_disconnect, Node}, 1, State),
    case Connections of
        #{Node := #connection{state = up, owner = Owner}} -> end_connection(Node, Owner, disconnect, State);
        #{} -> ok
    end,
    {noreply, State};
handle_cast(_, State) ->
    {noreply, State}.

% A node monitor's connection attempt sets the connection up; one that cannot be made is aborted, so the monitor
% gets nodedown. A dynamic node, dist_auto_connect never and a node dist_auto_connect once barred make none
% (DIST4b2b, DIST6).
handle_info({auto_connect, Node, DHandle}, #state{connections = Connections} = State) ->
    verbose({auto_connect, Node, DHandle}, 1, State),
    case {Connections, auto_connect(Node, State)} of
        {#{Node := _}, _} ->
            {noreply, State};
        {#{}, true} ->
            case setup(Node, normal, [], State) of
                {ok, NewState} ->
                    {noreply, NewState};
                error ->
                    erts_internal:abort_pending_connection(Node, DHandle),
                    {noreply, State}
            end;
        {#{}, false} ->
            erts_internal:abort_pending_connection(Node, DHandle),
            {noreply, State}
    end;
handle_info({accept, Acceptor, Controller, Family, Protocol}, State) ->
    {noreply, accepted(Acceptor, Controller, Family, Protocol, State)};
handle_info({Pid, {accept_pending, Request, Type}}, State) ->
    {Status, NewState} = accept_pending(Pid, Request, Type, State),
    Pid ! {self(), {accept_pending, Status}},
    {noreply, NewState};
handle_info({Pid, {replace, Node, Type}}, State) ->
    {Status, NewState} = replace(Pid, Node, Type, State),
    Pid ! {self(), {replace, Status}},
    {noreply, NewState};
handle_info({Pid, {set_name, Node, Creation}}, State) ->
    {Status, NewState} = set_name(Pid, Node, Creation, State),
    Pid ! {self(), {set_name, Status}},
    {noreply, NewState};
handle_info({Pid, {nodeup, Node, Address, Type}}, State) ->
    {noreply, nodeup(Pid, Node, Address, Type, State)};
handle_info({dist_ctrlr, Controller, Node, SetupPid}, #state{controllers = Controllers, owners = Owners} = State) when
    map_get(SetupPid, Owners) =:= Node
->
    link(Controller),
    {noreply, State#state{controllers = Controllers#{Controller => Node}}};
handle_info(tick, State) ->
    tick_connections(tick, State),
    {noreply, State};
handle_info(aux_tick, State) ->
    tick_connections(aux_tick, State),
    {noreply, State};
handle_info(transition_period_end, State) ->
    {noreply, transition_end(State)};
handle_info({Request, Reply}, #state{requests = Requests} = State) when is_map_key(Request, Requests) ->
    demonitor(Request, [flush]),
    {noreply, answered(Request, Reply, State)};
handle_info({'DOWN', Request, process, _, _}, #state{requests = Requests} = State) when is_map_key(Request, Requests) ->
    {noreply, answered(Request, {error, noconnection}, State)};
handle_info({'EXIT', Pid, Reason}, State) ->
    verbose({'EXIT', Pid, Reason}, 2, State),
    exited(Pid, Reason, State);
% Messages to a registered name that net_kernel passes on, and an old cookie's message, as OTP's (DIST6).
handle_info({_From, registered_send, To, Message}, State) ->
    _ = is_pid(whereis(To)) andalso (To ! Message),
    {noreply, State};
handle_info({From, badcookie, _To, _Message}, #state{connections = Connections} = State) ->
    Node = sender_node(From),
    error_logger:error_msg("~n** Got OLD cookie from ~w~n", [Node]),
    case Connections of
        #{Node := #connection{state = up} = Connection} -> {noreply, disconnected(Node, Connection, disconnect, State)};
        #{} -> {noreply, State}
    end;
% Anything else, a setnode/3 of a process that set up no connection too, is logged as OTP's (DIST4b3a).
handle_info(Message, State) ->
    error_logger:error_msg("Net kernel got ~tw~n", [Message]),
    {noreply, State}.

% A node still waiting for its dynamic name is no longer alive once the server ends.
terminate(_, #state{listen = Listeners, name_type = Type}) ->
    Type =:= dynamic andalso erts_internal:dynamic_node_name(false),
    lists:foreach(fun({Module, Socket, _}) -> Module:close(Socket) end, Listeners).

%% Connections (plan 30 step DIST4b2a). A named node accepts on each listen socket and ticks its up connections
%% net_tickintensity times per net_ticktime.
connecting(#state{listen = Listeners, ticktime = Time} = State) ->
    lists:foreach(fun({Module, Socket, _}) -> accept(Module, Socket) end, Listeners),
    State#state{ticker = start_ticker(Time div State#state.intensity)}.

start_ticker(Interval) -> spawn_opt(?MODULE, ticker, [self(), Interval], [link, {priority, max}]).

% The up connections' supervisors get Tick: tick, or aux_tick during a tick time change.
tick_connections(Tick, #state{connections = Connections}) ->
    _ = [Owner ! {self(), Tick} || _ := #connection{state = up, owner = Owner} <- Connections],
    ok.

%% Tick time changes (plan 30 step DIST6), as OTP's: the tick time stays a multiple of the tick intensity, at least one
%% millisecond per tick. An aux ticker ticks the connections at the shorter interval through the transition period;
%% a longer tick time takes effect at its start, a shorter one at its end.
new_ticktime(Time, _, #state{ticktime = Time, change = undefined} = State) ->
    {reply, unchanged, State};
new_ticktime(Time, Period, #state{ticktime = Old, intensity = Intensity, ticker = Ticker, change = undefined} = State) ->
    New = tick_multiple(max(Time, Intensity), Intensity),
    case New =:= Old of
        true ->
            {reply, unchanged, State};
        false ->
            Interval = New div Intensity,
            spawn_opt(?MODULE, aux_ticker, [self(), Interval, Old div Intensity, Period], [link, {priority, max}]),
            How = how(New > Old, Ticker, Interval),
            {reply, change_initiated, State#state{ticktime = New, change = How}}
    end;
new_ticktime(_, _, #state{ticktime = Time} = State) ->
    {reply, {ongoing_change_to, Time}, State}.

how(true, Ticker, Interval) ->
    Ticker ! {new_ticktime, Interval},
    longer;
how(false, _, _) ->
    shorter.

transition_end(#state{change = shorter, ticker = Ticker, ticktime = Time, intensity = Intensity} = State) ->
    Ticker ! {new_ticktime, Time div Intensity},
    State#state{change = undefined};
transition_end(State) ->
    State#state{change = undefined}.

% The built-in carriers are called by name, so that programs link them.
accept(inet_tcp_dist, Socket) -> inet_tcp_dist:accept(Socket);
accept(inet6_tcp_dist, Socket) -> inet6_tcp_dist:accept(Socket);
accept(Module, Socket) -> Module:accept(Socket).

% connect_node/1: this node is connected; a connection up answers at once, a pending one once its handshake ends.
connect(_, Node, _, #state{node = Node} = State) ->
    {reply, true, State};
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

% Set up a connection to Node with the first carrier that reaches it; error for a node no carrier reaches or that
% allow/1 does not allow.
setup(Node, Type, Waiting, #state{name_domain = Domain, carriers = Carriers} = State) ->
    case connect_allowed(Node, State) andalso [Module || Module <- Carriers, Module:select(Node)] of
        [Module | _] ->
            case catch erts_internal:new_connection(Node) of
                {'EXIT', _} ->
                    error;
                Handle ->
                    Owner = Module:setup(Node, Type, this_node(State), Domain, connecttime()),
                    Connection = #connection{
                        owner = Owner, handle = Handle, waiting = Waiting, type = Type, direction = setup
                    },
                    {ok, insert(Node, Connection, State)}
            end;
        _ ->
            error
    end.

% Whether this node may connect to Node: any node until allow/1 added some (DIST4b2b).
connect_allowed(_, #state{allowed = []}) ->
    true;
connect_allowed(Node, #state{allowed = Allowed}) ->
    dist_util:is_allowed(Node, Allowed) orelse
        begin
            error_logger:error_msg("** Connection attempt with disallowed node ~w ** ~n", [Node]),
            false
        end.

% This node's name for the carriers; a node waiting for its dynamic name gives its host with the name undefined.
this_node(#state{node = undefined, host = Host}) -> list_to_atom("undefined@" ++ Host);
this_node(#state{node = Node}) -> Node.

% Whether a node monitor may set up a connection: not from a dynamic node, nor under dist_auto_connect never, nor
% to a node dist_auto_connect once barred (DIST6).
auto_connect(_, #state{name_type = dynamic}) -> false;
auto_connect(Node, #state{barred = Barred}) -> auto_connect_env() =/= never andalso not lists:member(Node, Barred).

auto_connect_env() ->
    case application:get_env(kernel, dist_auto_connect) of
        {ok, Value} -> Value;
        undefined -> always
    end.

% A connection of Node; one set up again lifts dist_auto_connect once's bar, as OTP's entry replaces its bar.
insert(Node, #connection{owner = Owner} = Connection, #state{connections = Connections, owners = Owners} = State) ->
    State#state{
        connections = Connections#{Node => Connection},
        owners = Owners#{Owner => Node},
        barred = lists:delete(Node, State#state.barred)
    }.

%% passive_cnct (plan 30 step DIST6): erlang:monitor_node/3 with allow_passive_connect waits for a connection as an
%% auto-connect makes it; a barred node is waited for until the other node connects, at most connecttime().
passive_connect(Node, From, #state{connections = Connections, barred = Barred} = State) ->
    case {lists:member(Node, Barred), Connections} of
        {true, _} ->
            spawn(?MODULE, passive_connect_monitor, [From, Node]),
            {noreply, State};
        {false, #{Node := #connection{state = up}}} ->
            {reply, true, State};
        {false, #{Node := #connection{waiting = Waiting} = Connection}} ->
            {noreply, State#state{
                connections = Connections#{Node := Connection#connection{waiting = [From | Waiting]}}
            }};
        {false, #{}} ->
            case auto_connect(Node, State) andalso setup(Node, normal, [From], State) of
                {ok, NewState} -> {noreply, NewState};
                _ -> {reply, false, State}
            end
    end.

passive_connect_monitor(From, Node) ->
    ok = monitor_nodes(true, [{node_type, all}]),
    Reply =
        lists:member(Node, nodes([connected])) orelse
            receive
                {nodeup, Node, _} -> true
            after connecttime() -> false
            end,
    ok = monitor_nodes(false, [{node_type, all}]),
    gen_server:reply(From, Reply).

% An acceptor's connection: the carrier of its family and protocol starts the handshake process.
accepted(Acceptor, Controller, Family, Protocol, #state{node = MyNode, listen = Listeners} = State) ->
    case [Module || {Module, _, #net_address{family = F, protocol = P}} <- Listeners, F =:= Family, P =:= Protocol] of
        [Module | _] ->
            Allowed = State#state.allowed,
            Supervisor = Module:accept_connection(Acceptor, Controller, MyNode, Allowed, connecttime()),
            Acceptor ! {self(), controller, Supervisor};
        [] ->
            Acceptor ! {self(), unsupported_protocol}
    end,
    State.

% An accepted handshake asks to go on, as {Status, State}: ok when Node has no connection, which turns pending; alive
% when one is up; for this node's own setup, ok_simultaneous when the other node's name is greater, which takes it
% over, else nok. A node asking for a dynamic name gets one (DIST4b2b).
accept_pending(Pid, {name_me, Host}, Type, State) ->
    {Node, Creation, NewState} = dynamic_node(Host, State),
    Handle = erts_internal:new_connection(Node),
    {{named, Node, Creation}, insert(Node, #connection{owner = Pid, handle = Handle, type = Type}, NewState)};
accept_pending(Pid, Node, Type, #state{node = MyNode, connections = Connections} = State) ->
    case Connections of
        #{Node := #connection{state = up}} ->
            {alive, State};
        #{Node := #connection{direction = setup} = Own} when Node > MyNode ->
            {ok_simultaneous, take_over(Pid, Own, State)};
        #{Node := _} ->
            {nok, State};
        #{} ->
            new_pending(Pid, Node, Type, State)
    end.

% Node's connection turns pending with the handshake process Pid; this node's own name has none, as OTP logs.
new_pending(Pid, Node, Type, State) ->
    try erts_internal:new_connection(Node) of
        Handle -> {ok, insert(Node, #connection{owner = Pid, handle = Handle, type = Type}, State)}
    catch
        error:badarg ->
            error_logger:error_msg("~n** Cannot get connection id for node ~w~n", [Node]),
            {nok, State}
    end.

% Simultaneous connects: this node's own handshake ends and the accepted one Pid goes on with its callers.
take_over(Pid, #connection{owner = Own} = Connection, #state{connections = Connections, owners = Owners} = State) ->
    #{Own := Node} = Owners,
    exit(Own, shutdown),
    Accepted = Connection#connection{owner = Pid, direction = accept},
    State#state{connections = Connections#{Node := Accepted}, owners = (maps:remove(Own, Owners))#{Pid => Node}}.

% The other node answered alive with true: its up connection ends with wait_pending and Pid's turns pending.
replace(Pid, Node, Type, #state{connections = Connections} = State) ->
    case Connections of
        #{Node := #connection{state = up} = Old} ->
            new_pending(Pid, Node, Type, disconnected(Node, Old, wait_pending, State));
        #{Node := _} ->
            {nok, State};
        #{} ->
            new_pending(Pid, Node, Type, State)
    end.

% A dynamic name for a node on Host: the first free name of this node's seed, so that a node asking again gets its
% name back, with a creation one past the one given with that name before.
dynamic_node(Host, State) -> dynamic_node(Host, 0, State).

dynamic_node(Host, Attempt, #state{seed = Seed, connections = Connections, creations = Creations} = State) ->
    <<Number:64, _/binary>> = erlang:md5(term_to_binary({Seed, Host, Attempt})),
    Node = list_to_atom(integer_to_list(Number, 36) ++ "@" ++ Host),
    case Connections of
        #{Node := _} ->
            dynamic_node(Host, Attempt + 1, State);
        #{} ->
            Creation = next_creation(maps:get(Node, Creations, undefined)),
            {Node, Creation, State#state{creations = Creations#{Node => Creation}}}
    end.

next_creation(undefined) -> rand:uniform(16#fffffff0) + 3;
next_creation(Creation) -> max(4, (Creation + 1) band 16#ffffffff).

% The other node named this one, which takes the name; the connection Pid sets up is the one that gave it.
set_name(Pid, Node, Creation, #state{node = undefined, owners = Owners} = State) ->
    case maps:find(Pid, Owners) of
        {ok, Peer} ->
            case catch erlang:setnode(Node, Creation) of
                true -> {ok, State#state{node = Node, named_by = Peer}};
                _ -> {error, State}
            end;
        error ->
            {error, State}
    end;
set_name(_, _, _, State) ->
    {error, State}.

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
exited(Pid, _Reason, #state{owners = Owners, controllers = Controllers, ticker = Ticker} = State) ->
    case {Owners, Controllers} of
        {#{Pid := Node}, _} -> {noreply, dropped(Node, State)};
        {_, #{Pid := _}} -> {noreply, State#state{controllers = maps:remove(Pid, Controllers)}};
        _ when Pid =:= Ticker -> {noreply, restart_ticker(State)};
        _ -> {noreply, State}
    end.

% A ticker that ended starts again; the connections get an aux_tick meanwhile, as OTP's (DIST6).
restart_ticker(#state{ticktime = Time, intensity = Intensity} = State) ->
    self() ! aux_tick,
    State#state{ticker = start_ticker(Time div Intensity)}.

% An up connection's end bars the node from auto-connects under dist_auto_connect once (DIST6).
dropped(Node, #state{connections = Connections, owners = Owners, named_by = NamedBy, barred = Barred} = State) ->
    #{Node := #connection{state = Status, owner = Owner, handle = Handle, waiting = Waiting}} = Connections,
    Status =:= pending andalso erts_internal:abort_pending_connection(Node, Handle),
    lists:foreach(fun(From) -> gen_server:reply(From, false) end, Waiting),
    Node =:= NamedBy andalso lose_name(State),
    Bar = Status =:= up andalso Node =/= NamedBy andalso auto_connect_env() =:= once,
    State#state{
        connections = maps:remove(Node, Connections),
        owners = maps:remove(Owner, Owners),
        barred = [Node || Bar] ++ Barred
    }.

% The connection that gave this node its dynamic name ended: net_sup starts again, unnamed, as OTP's; the end of
% net_kernel takes the other connections down with net_kernel_terminated (DIST4b2b).
lose_name(#state{started = Started}) ->
    Child =
        case Started of
            static -> net_sup;
            dynamic -> net_sup_dynamic
        end,
    spawn(fun() ->
        _ = supervisor:terminate_child(kernel_sup, Child),
        supervisor:restart_child(kernel_sup, Child)
    end).

% disconnect/1: the connection's processes end with Reason, disconnect, and the answer waits for their ends, so the
% connection is down by then. A replaced connection ends with wait_pending (DIST4b2b).
disconnected(Node, #connection{owner = Owner}, Reason, #state{controllers = Controllers} = State) ->
    Ended = end_connection(Node, Owner, Reason, State),
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

% End Node's supervisor Owner and its controllers with Reason; the processes ended.
end_connection(Node, Owner, Reason, #state{controllers = Controllers}) ->
    Ended = [Owner | [Controller || Controller := ControlledNode <- Controllers, ControlledNode =:= Node]],
    lists:foreach(fun(Pid) -> exit(Pid, Reason) end, Ended),
    Ended.

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
%% Interval milliseconds; a tick time change gives it a new interval (DIST6).
ticker(Kernel, Interval) when is_integer(Interval) -> ticker_loop(Kernel, Interval).

ticker_loop(Kernel, Interval) ->
    receive
        {new_ticktime, NewInterval} -> ticker_loop(Kernel, NewInterval)
    after Interval ->
        Kernel ! tick,
        ticker_loop(Kernel, Interval)
    end.

%% The aux ticker of a tick time change (plan 30 step DIST6): aux_tick at once and every shorter interval of the two
%% until the transition period is covered, then transition_period_end.
aux_ticker(Kernel, NewInterval, OldInterval, Period) ->
    Interval = min(NewInterval, OldInterval),
    Ticks =
        case Period > 0 of
            true -> 1 + ((Period - 1) div Interval + 1);
            false -> 1
        end,
    aux_ticks(Kernel, Interval, Ticks).

aux_ticks(Kernel, _, 1) ->
    Kernel ! transition_period_end,
    Kernel ! aux_tick,
    bye;
aux_ticks(Kernel, Interval, Ticks) ->
    Kernel ! aux_tick,
    receive
    after Interval -> aux_ticks(Kernel, Interval, Ticks - 1)
    end.

%% Spawns through net_kernel (plan 30 step DIST6): the old spawn/4 protocol's calls, answered by the spawned process
%% with its pid, or with {'EXIT', {Reason, []}} when the spawn fails.
do_spawn(Arguments, Options, State) ->
    try spawn_opt(?MODULE, spawn_func, Arguments, Options) of
        _ -> {noreply, State}
    catch
        error:Reason -> {reply, {'EXIT', {Reason, []}}, State}
    end.

spawn_func(link, {Pid, _} = From, Module, Function, Arguments, Leader) ->
    link(Pid),
    spawn_func(no_link, From, Module, Function, Arguments, Leader);
spawn_func(_, From, Module, Function, Arguments, Leader) ->
    gen_server:reply(From, self()),
    group_leader(Leader, self()),
    apply(Module, Function, Arguments).

%% Connection information (plan 30 step DIST6), as OTP's: owner, state, address, type and the packets read and written
%% by an up connection, as its supervisor reads them; {error, bad_node} without a connection, badarg without
%% distribution (OTP's sys_dist table is missing then).
node_info(Node) ->
    case request({connection, Node}) of
        {ok, #connection{state = up, owner = Owner} = Connection} -> up_info(Owner, Connection);
        {ok, Connection} -> {ok, info(Connection, 0, 0)};
        error -> {error, bad_node};
        ignored -> erlang:error(badarg)
    end.

node_info(Node, Key) ->
    case node_info(Node) of
        {ok, Info} ->
            case lists:keyfind(Key, 1, Info) of
                {Key, Value} -> {ok, Value};
                false -> {error, invalid_key}
            end;
        Error ->
            Error
    end.

nodes_info() ->
    case request(connections) of
        ignored -> erlang:error(badarg);
        Connections -> {ok, lists:filtermap(fun node_info_of/1, Connections)}
    end.

node_info_of({Node, #connection{state = up, owner = Owner} = Connection}) ->
    case up_info(Owner, Connection) of
        {ok, Info} -> {true, {Node, Info}};
        _ -> false
    end;
node_info_of({Node, Connection}) ->
    {true, {Node, info(Connection, 0, 0)}}.

up_info(Owner, Connection) ->
    Monitor = monitor(process, Owner),
    Owner ! {self(), get_status},
    receive
        {Owner, get_status, {ok, Read, Write}} ->
            demonitor(Monitor, [flush]),
            {ok, info(Connection, Read, Write)};
        {'DOWN', Monitor, process, Owner, _} ->
            {error, bad_node}
    end.

info(#connection{owner = Owner, state = Status, address = Address, type = Type}, Read, Write) ->
    [{owner, Owner}, {state, Status}, {address, Address}, {type, Type}, {in, Read}, {out, Write}].

% The connections as a table on standard output, as OTP prints it.
i() ->
    nformat("Node", "State", "Type", "In", "Out", "Address"),
    {ok, Infos} = nodes_info(),
    {In, Out} = lists:foldl(fun display_info/2, {0, 0}, Infos),
    nformat("Total", "", "", integer_to_list(In), integer_to_list(Out), "").

i(Node) ->
    case node_info(Node) of
        {ok, Info} ->
            io:format(
                "Node     = ~p~nState    = ~p~nType     = ~p~nIn       = ~p~nOut      = ~p~nAddress  = ~s~n",
                [Node, fetch(state, Info), fetch(type, Info), fetch(in, Info), fetch(out, Info), address_text(Info)]
            );
        Error ->
            Error
    end.

display_info({Node, Info}, {In, Out}) ->
    Read = fetch(in, Info),
    Written = fetch(out, Info),
    nformat(
        atom_to_list(Node),
        atom_to_list(fetch(state, Info)),
        atom_to_list(fetch(type, Info)),
        integer_to_list(Read),
        integer_to_list(Written),
        address_text(Info)
    ),
    {In + Read, Out + Written}.

address_text(Info) ->
    case fetch(address, Info) of
        #net_address{family = inet, address = {IP, Port}} -> inet:ntoa(IP) ++ ":" ++ integer_to_list(Port);
        #net_address{family = inet6, address = {IP, Port}} -> inet:ntoa(IP) ++ "/" ++ integer_to_list(Port);
        #net_address{family = Family} when Family =:= inet; Family =:= inet6 -> "-";
        #net_address{address = Address} -> lists:flatten(io_lib:format("~p", [Address]))
    end.

fetch(Key, Info) ->
    case lists:keyfind(Key, 1, Info) of
        {_, Value} -> Value;
        false -> 0
    end.

nformat(Node, Status, Type, In, Out, Address) ->
    io:format("~-20s ~-7s ~-6s ~8s ~8s ~s~n", [Node, Status, Type, In, Out, Address]).

verbose(Term, Level, #state{verbose = Verbose}) when Verbose >= Level -> error_logger:info_report({net_kernel, Term});
verbose(_, _, _) -> ok.

% The node of a pid, or the name it is given as.
sender_node(Pid) when is_pid(Pid) -> node(Pid);
sender_node(Name) -> Name.

%% Socket options (plan 30 step DIST6). An up connection's supervisor answers {setopts | getopts, Options} with
%% {Request, Result}; a connection that ends first answers {error, noconnection}.
node_options(Operation, Node, Options, From, #state{connections = Connections, requests = Requests} = State) ->
    case Connections of
        #{Node := #connection{state = up, owner = Owner}} ->
            NewRequests = owner_request(Requests, {Operation, From}, Owner, {Operation, Options}),
            {noreply, State#state{requests = NewRequests}};
        #{} ->
            {reply, {error, noconnection}, State}
    end.

owner_request(Requests, Label, Owner, Message) ->
    Request = monitor(process, Owner),
    Owner ! {self(), Request, Message},
    Requests#{Request => Label}.

% setopts(new, Options): the listen sockets take them first, and an error there is the answer. Then the kernel
% parameters of the carriers' listen and connect options merge them, and the pending connections take them once up.
setopts_new(Options, From, #state{listen = Listeners} = State) ->
    case listen_options(Options, Listeners) of
        ok -> new_options(Options, From, State);
        Error -> {reply, Error, State}
    end.

listen_options(_, []) ->
    ok;
listen_options(Options, [{Module, Socket, _} | Listeners]) ->
    try Module:setopts(Socket, Options) of
        ok -> listen_options(Options, Listeners);
        Error -> Error
    catch
        error:undef -> {error, enotsup}
    end.

new_options(Options, From, #state{connections = Connections, requests = Requests} = State) ->
    merge_env(inet_dist_connect_options, Options),
    merge_env(inet_dist_listen_options, Options),
    case lists:keyfind(nodelay, 1, Options) of
        {nodelay, Delay} when is_boolean(Delay) -> application:set_env(kernel, dist_nodelay, Delay);
        _ -> ok
    end,
    Operation = make_ref(),
    Pending = [Owner || _ := #connection{state = pending, owner = Owner} <- Connections],
    Label = {setopts_new, Operation},
    NewRequests = lists:foldl(
        fun(Owner, Acc) -> owner_request(Acc, Label, Owner, {setopts, Options}) end, Requests, Pending
    ),
    case length(Pending) of
        0 -> {reply, ok, State};
        Count -> {noreply, State#state{requests = NewRequests#{Operation => {setopts_new, From, Count}}}}
    end.

merge_env(Parameter, Options) ->
    Old =
        case application:get_env(kernel, Parameter) of
            {ok, Value} -> Value;
            _ -> []
        end,
    application:set_env(kernel, Parameter, merge_opts(Options, Old)).

% Each option replaces the options of its key, in front; an option that is no pair, new or old, crashes net_kernel,
% as OTP's.
merge_opts([], Old) ->
    Old;
merge_opts([{Key, _} = Option | Options], Old) ->
    merge_opts(Options, [Option | lists:filter(fun({OldKey, _}) -> OldKey =/= Key end, Old)]).

% A supervisor answered Request, or ended first; setopts(new, ...) answers ok once every pending one did.
answered(Request, Reply, #state{requests = Requests} = State) ->
    case maps:take(Request, Requests) of
        {{setopts_new, Operation}, Rest} ->
            State#state{requests = new_answered(Operation, Rest)};
        {{_, From}, Rest} ->
            gen_server:reply(From, Reply),
            State#state{requests = Rest}
    end.

new_answered(Operation, Requests) ->
    case maps:get(Operation, Requests) of
        {setopts_new, From, 1} ->
            gen_server:reply(From, ok),
            maps:remove(Operation, Requests);
        {setopts_new, From, Count} ->
            Requests#{Operation := {setopts_new, From, Count - 1}}
    end.
