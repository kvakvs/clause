%% Clause's pool (plan 21, docs/libraries/library.md#runtime-module-replacements), written from OTP's
%% documentation: the pool master is a gen_server registered globally as pool_master, as OTP's. A Clause node is not
%% distributed, so its pool is this node alone: start/1,2 read .hosts.erlang and start no slave nodes, attaching
%% another node fails as monitor_node/2 does on a non-distributed node, and processes spawn here.
-module(pool).
-behaviour(gen_server).
-export([
    attach/1,
    do_spawn/4,
    get_node/0,
    get_nodes/0,
    get_nodes_and_load/0,
    handle_call/3,
    handle_cast/2,
    handle_info/2,
    init/1,
    pspawn/3,
    pspawn_link/3,
    start/1,
    start/2,
    statistic_collector/0,
    stop/0,
    terminate/2
]).

start(Name) -> start(Name, []).

% Start the pool master, then a slave node per host of .hosts.erlang (none starts on a node without distribution).
start(Name, Args) when is_atom(Name) ->
    _ = gen_server:start({global, pool_master}, pool, [], []),
    Nodes = start_nodes(net_adm:host_file(), Name, Args),
    lists:foreach(fun attach/1, Nodes),
    Nodes.

start_nodes([], _, _) ->
    [];
start_nodes([Host | Hosts], Name, Args) ->
    case slave:start(Host, Name, Args) of
        {error, {already_running, Node}} ->
            io:format("Can't start node on host ~w due to ~w~n", [Host, {already_running, Node}]),
            [Node | start_nodes(Hosts, Name, Args)];
        {error, Reason} ->
            io:format("Can't start node on host ~w due to ~w~n", [Host, Reason]),
            start_nodes(Hosts, Name, Args);
        {ok, Node} ->
            [Node | start_nodes(Hosts, Name, Args)]
    end.

get_nodes() -> [Node || {_Load, Node} <- get_nodes_and_load()].

get_nodes_and_load() -> gen_server:call({global, pool_master}, get_nodes).

get_node() -> gen_server:call({global, pool_master}, get_node).

attach(Node) -> gen_server:call({global, pool_master}, {attach, Node}).

pspawn(M, F, A) -> gen_server:call({global, pool_master}, {spawn, group_leader(), M, F, A}).

pspawn_link(M, F, A) -> spawn_link(get_node(), M, F, A).

stop() -> gen_server:call({global, pool_master}, stop).

% Run a pool process under the caller's group leader.
do_spawn(GroupLeader, M, F, A) ->
    group_leader(GroupLeader, self()),
    apply(M, F, A).

%% The pool master: its state is the nodes with their expected loads, least loaded first.

init([]) ->
    process_flag(trap_exit, true),
    {ok, [{0, node()}]}.

handle_call(get_nodes, _From, Nodes) ->
    {reply, Nodes, Nodes};
handle_call(get_node, _From, [{Load, Node} | Nodes]) ->
    {reply, Node, Nodes ++ [{Load + 1, Node}]};
handle_call({attach, Node}, _From, Nodes) ->
    case lists:keymember(Node, 2, Nodes) of
        true ->
            {reply, already_attached, Nodes};
        false ->
            erlang:monitor_node(Node, true),
            {reply, attached, Nodes ++ [{999999, Node}]}
    end;
handle_call({spawn, GroupLeader, M, F, A}, From, Nodes) ->
    {reply, Node, Rest} = handle_call(get_node, From, Nodes),
    {reply, spawn(Node, pool, do_spawn, [GroupLeader, M, F, A]), Rest};
handle_call(stop, _From, Nodes) ->
    {stop, normal, stopped, Nodes}.

handle_cast(_, Nodes) ->
    {noreply, Nodes}.

% A pool node went down: drop it.
handle_info({nodedown, Node}, Nodes) ->
    {noreply, lists:keydelete(Node, 2, Nodes)};
handle_info(_, Nodes) ->
    {noreply, Nodes}.

terminate(_, _) ->
    ok.

statistic_collector() -> erlang:error({not_implemented, statistic_collector, 0}).
