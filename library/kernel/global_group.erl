%% Clause's global_group (plan 21 step LIB25, plan 30 step DIST8b, docs/distribution.md#global_group): the global
%% groups of the kernel parameter global_groups, as OTP's server keeps them. global syncs only with the nodes of this
%% node's group: it waits for this server's group_nodeup, sent once the other node showed the same group, and this
%% server tells global (group_nodedown) and the other node's server ({disconnect_node, Node}) about nodes outside the
%% group, whose connections stay. Names of other groups are found by search processes asking each node's server. The
%% configuration is kept in a persistent term, so that global and net_kernel read it without the server.
-module(global_group).
-behaviour(gen_server).
-export([
    global_groups/0,
    monitor_nodes/1,
    own_nodes/0,
    registered_names/1,
    send/2,
    send/3,
    whereis_name/1,
    whereis_name/2,
    sync/0,
    info/0,
    member/1,
    participant/1,
    publish/2,
    group_configured/0,
    get_own_nodes/0,
    get_own_nodes_with_errors/0,
    global_groups_changed/1,
    global_groups_added/1,
    global_groups_removed/1,
    ng_add_check/2,
    ng_add_check/3,
    registered_names_test/1,
    send_test/2,
    whereis_name_test/1,
    start/0,
    start_link/0,
    stop/0,
    init/1,
    handle_call/3,
    handle_cast/2,
    handle_info/2,
    terminate/2,
    code_change/3
]).

%% The version of the conf_check messages sync/0 sends; replies of version 3 come to this server itself.
-define(CC_VSN, 3).
%% The node the test hooks name to make a search process fail.
-define(TEST_NODE, 'test3844zty').

%% The server's state: no_conf without groups, synced with them; this node's group, its publish type and the state
%% of each other node of it (sync, sync_error or no_contact); the other groups as {Name, Nodes}; the processes of
%% monitor_nodes/1; each connected visible node's connection id; the erpc requests checking other nodes'
%% configurations; and sync/0's conf_check session {Ref, #{Node => Monitor}}.
-record(state, {
    sync_state = no_conf,
    group_name = [],
    nodes = #{},
    other_grps = [],
    monitor = [],
    group_publish_type = normal,
    connections = #{},
    erpc_requests,
    config_check
}).

%% A configuration: the parameter value it was made of and the node name it was made for; this node's group, its
%% publish type, its sorted nodes (also as a map, all without groups) and the other groups; and no_conf, conf or
%% {error, Reason, Value} for an invalid value.
-record(gconf, {
    parameter_value = invalid,
    node_name,
    group_name = [],
    group_publish_type = normal,
    group_list = [],
    group_map = all,
    other_groups = [],
    state = no_conf
}).

%% Requests of the server.

global_groups() -> request(global_groups).

monitor_nodes(Flag) when is_boolean(Flag) -> request({monitor_nodes, Flag});
monitor_nodes(_) -> {error, not_boolean}.

own_nodes() -> request(own_nodes).

registered_names(Where) -> request({registered_names, Where}).

send(Name, Message) -> request({send, Name, Message}).

send(Where, Name, Message) -> request({send, Where, Name, Message}).

whereis_name(Name) -> request({whereis_name, Name}).

whereis_name(Where, Name) -> request({whereis_name, Where, Name}).

global_groups_changed(Value) -> request({global_groups_changed, Value}).

global_groups_added(Value) -> request({global_groups_added, Value}).

global_groups_removed(Value) -> request({global_groups_removed, Value}).

sync() -> request(sync).

info() -> request(info, 3000).

ng_add_check(Node, OtherNodes) -> ng_add_check(Node, normal, OtherNodes).

ng_add_check(Node, PublishType, OtherNodes) -> request({ng_add_check, Node, PublishType, OtherNodes}).

registered_names_test(Where) -> request({registered_names_test, Where}).

send_test(Name, Message) -> request({send_test, Name, Message}).

whereis_name_test(Name) -> request({whereis_name_test, Name}).

% The server starts with kernel_sup at the first use of a kernel service (plan 30 step DIST4b1d); OTP's misspelt
% error answers when it does not run.
request(Request) -> request(Request, infinity).

request(Request, Timeout) ->
    ensure_started(),
    case whereis(global_group) of
        Pid when is_pid(Pid) -> gen_server:call(global_group, Request, Timeout);
        _ -> {error, global_group_not_runnig}
    end.

ensure_started() ->
    case whereis(global_group) of
        undefined ->
            ok = clause_kernel:ensure_started(),
            % Excluded from kernel_sup (exclude_kernel_services), the server still starts unlinked at its first use.
            _ = whereis(global_group) =:= undefined andalso start(),
            ok;
        _ ->
            ok
    end.

start() -> gen_server:start({local, global_group}, ?MODULE, [], []).

start_link() -> gen_server:start_link({local, global_group}, ?MODULE, [], []).

stop() -> gen_server:call(global_group, stop, infinity).

%% Answers from the configuration, without the server.

get_own_nodes_with_errors() ->
    case lookup_group_conf(false) of
        #gconf{state = {error, Error, _}} -> {error, Error};
        #gconf{group_list = []} -> {ok, all};
        #gconf{group_list = Nodes} -> {ok, Nodes}
    end.

get_own_nodes() -> own_group_nodes(false).

own_group_nodes(Install) -> (lookup_group_conf(Install))#gconf.group_list.

group_configured() -> (lookup_group_conf(false))#gconf.state =/= no_conf.

%% Whether global takes part with Node: a node of this node's group, or any node without groups.
participant(Node) ->
    case lookup_group_conf(false) of
        #gconf{group_map = all} -> true;
        #gconf{group_map = #{Node := ok}} -> true;
        #gconf{} -> false
    end.

member(Node) -> member(false, Node).

member(Install, Node) ->
    case lookup_group_conf(Install) of
        #gconf{group_map = #{Node := ok}} -> true;
        #gconf{} -> false
    end.

%% Whether this node, of the publish type OwnType, is visible to Node (net_kernel's handshake asks).
publish(OwnType, Node) when (OwnType =:= normal orelse OwnType =:= hidden) andalso is_atom(Node) ->
    case lookup_group_conf(false) of
        #gconf{group_map = all} -> OwnType =:= normal;
        #gconf{group_publish_type = normal} when OwnType =:= normal -> true;
        #gconf{group_map = #{Node := ok}} -> true;
        #gconf{} -> false
    end.

%% The configuration: made from kernel's global_groups for this node's name, kept in a persistent term by the server.

lookup_group_conf(Install) ->
    try
        persistent_term:get(?MODULE)
    catch
        error:badarg -> fetch_new_group_conf(Install)
    end.

fetch_new_group_conf(Install) -> fetch_new_group_conf(Install, undefined).

fetch_new_group_conf(Install, NodeName) ->
    Value =
        case application:get_env(kernel, global_groups) of
            {ok, V} -> V;
            undefined -> undefined
        end,
    new_group_conf(Install, Value, NodeName).

new_group_conf(Install, Value) -> new_group_conf(Install, Value, undefined).

% The installed configuration when it was made of Value for this name; else a new one, installed when the server asks.
new_group_conf(Install, Value, NodeName) ->
    case persistent_term:get(?MODULE, #gconf{}) of
        #gconf{parameter_value = Value, node_name = Name} = GConf when NodeName =:= Name; NodeName =:= undefined ->
            GConf;
        #gconf{node_name = Name} ->
            Use =
                case NodeName of
                    undefined -> Name;
                    _ -> NodeName
                end,
            GConf = make_group_conf(Use, Value),
            _ = Install andalso persistent_term:put(?MODULE, GConf),
            GConf
    end.

% Without groups, or without a name to find this node's group by, there is no configuration.
make_group_conf(NodeName, Value) when Value =:= undefined; Value =:= []; NodeName =:= nonode@nohost ->
    #gconf{parameter_value = Value, node_name = NodeName};
make_group_conf(NodeName, Value) ->
    try config_scan(NodeName, Value) of
        {error, Error} ->
            #gconf{parameter_value = Value, node_name = NodeName, state = {error, Error, Value}};
        {Name, PublishType, OwnNodes, OtherGroups} ->
            Map =
                case OwnNodes of
                    [] -> all;
                    _ -> maps:from_keys(OwnNodes, ok)
                end,
            #gconf{
                parameter_value = Value,
                node_name = NodeName,
                group_name = Name,
                group_publish_type = PublishType,
                group_list = lists:sort(OwnNodes),
                group_map = Map,
                other_groups = OtherGroups,
                state = conf
            }
    catch
        _:_ -> #gconf{parameter_value = Value, node_name = NodeName, state = {error, invalid, Value}}
    end.

% This node's group (the first that lists it; a second is an error) and the other groups, in the parameter's order.
config_scan(Node, Groups) -> config_scan(Node, normal, Groups, no_name, [], []).

config_scan(_, PublishType, [], OwnName, OwnNodes, Others) ->
    {OwnName, PublishType, lists:sort(OwnNodes), lists:reverse(Others)};
config_scan(Node, PublishType, [Group | Groups], OwnName, OwnNodes, Others) ->
    {Name, GroupType, Nodes} = group_tuple(Group),
    case lists:member(Node, Nodes) of
        true when OwnName =:= no_name -> config_scan(Node, GroupType, Groups, Name, Nodes, Others);
        true -> {error, {'node defined twice', {OwnName, Name}}};
        false -> config_scan(Node, PublishType, Groups, OwnName, OwnNodes, [{Name, Nodes} | Others])
    end.

group_tuple({Name, Nodes}) -> {Name, normal, Nodes};
group_tuple({Name, hidden, Nodes}) -> {Name, hidden, Nodes};
group_tuple({Name, normal, Nodes}) -> {Name, normal, Nodes}.

% The configuration once this node's name changed (distribution started or stopped).
alive_state_change_group_conf(NodeName) ->
    case persistent_term:get(?MODULE, #gconf{}) of
        #gconf{parameter_value = Value} when Value =/= invalid -> new_group_conf(true, Value, NodeName);
        #gconf{} -> fetch_new_group_conf(true, NodeName)
    end.

%% The server.

init([]) ->
    _ = process_flag(async_dist, true),
    process_flag(priority, max),
    ok = net_kernel:monitor_nodes(true, #{connection_id => true}),
    process_flag(trap_exit, true),
    % OTP's dispatcher of config check replies from nodes older than version 3.
    Checker = spawn_link(fun check_dispatcher/0),
    register(global_group_check, Checker),
    put(global_group_check, Checker),
    put(searches, #{}),
    Connections = maps:from_list([{N, Id} || {N, #{connection_id := Id}} <- nodes(visible, #{connection_id => true})]),
    {ok, initial_group_setup(fetch_new_group_conf(true, node()), Connections, erpc:reqids_new())}.

% Nodes outside the group are disconnected from global; the group's connected nodes are asked for their groups.
initial_group_setup(#gconf{state = no_conf}, Connections, Requests) ->
    #state{connections = Connections, erpc_requests = Requests};
initial_group_setup(#gconf{state = {error, _, Value}}, _, _) ->
    exit({error, {'invalid global_groups definition', Value}});
initial_group_setup(#gconf{} = GConf, Connections, Requests) ->
    Own = lists:delete(GConf#gconf.node_name, GConf#gconf.group_list),
    Connected = maps:keys(Connections),
    disconnect_nodes(Connected -- Own, Connections),
    States = maps:from_list([{N, connection_state(N, Connections)} || N <- Own]),
    #state{
        group_publish_type = GConf#gconf.group_publish_type,
        sync_state = synced,
        group_name = GConf#gconf.group_name,
        nodes = States,
        other_grps = GConf#gconf.other_groups,
        connections = Connections,
        erpc_requests = conf_changed_checks(Own, Requests, Connections)
    }.

% A connected node's configuration is not verified yet.
connection_state(Node, Connections) ->
    case maps:is_key(Node, Connections) of
        true -> sync_error;
        false -> no_contact
    end.

handle_call(sync, _, S) ->
    {reply, ok, sync_group(S)};
handle_call(global_groups, _, #state{sync_state = no_conf} = S) ->
    {reply, undefined, S};
handle_call(global_groups, _, S) ->
    {reply, {S#state.group_name, [Name || {Name, _} <- S#state.other_grps]}, S};
handle_call({monitor_nodes, Flag}, {Pid, _}, S) ->
    {Reply, Next} = monitor_nodes(Flag, Pid, S),
    {reply, Reply, Next};
handle_call(own_nodes, _, #state{sync_state = no_conf} = S) ->
    {reply, [node() | nodes()], S};
handle_call(own_nodes, _, S) ->
    {reply, own_group_nodes(true), S};
handle_call({registered_names, Where}, From, S) ->
    registered_names_request(Where, From, S);
handle_call({send, Name, Message}, From, S) ->
    send_request(any, Name, Message, From, S);
handle_call({send, Where, Name, Message}, From, S) ->
    send_request(Where, Name, Message, From, S);
handle_call({whereis_name, Name}, From, S) ->
    whereis_request(any, Name, From, S);
handle_call({whereis_name, Where, Name}, From, S) ->
    whereis_request(Where, Name, From, S);
handle_call({global_groups_changed, Value}, _, S) ->
    {reply, ok, groups_changed(Value, S)};
handle_call({global_groups_added, Value}, _, S) ->
    {reply, ok, groups_added(Value, S)};
handle_call({global_groups_removed, _}, _, S) ->
    #gconf{state = no_conf} = GConf = new_group_conf(true, undefined),
    {reply, ok, unconfigured(GConf, S)};
handle_call({ng_add_check, Node, PublishType, OtherNodes}, _, #state{group_publish_type = OwnType} = S) ->
    case {PublishType, lists:sort(OtherNodes)} =:= {OwnType, own_group_nodes(true)} of
        true -> {reply, agreed, node_state(sync, Node, S)};
        false -> {reply, not_agreed, node_state(sync_error, Node, S)}
    end;
handle_call(info, _, S) ->
    {reply, info_reply(S), S};
handle_call(get, _, S) ->
    {reply, get(), S};
handle_call({registered_names_test, {node, ?TEST_NODE}}, From, S) ->
    {noreply, searching(names_test, ?TEST_NODE, {names, From}, S)};
handle_call({send_test, Name, ?TEST_NODE}, From, S) ->
    {noreply, searching(send_test, ?TEST_NODE, {send, From, Name, ?TEST_NODE}, S)};
handle_call({whereis_name_test, ?TEST_NODE}, From, S) ->
    {noreply, searching(whereis_test, ?TEST_NODE, {whereis, From}, S)};
handle_call({Test, _}, _, S) when Test =:= registered_names_test; Test =:= whereis_name_test ->
    {reply, {error, illegal_function_call}, S};
handle_call({send_test, _, _}, _, S) ->
    {reply, {error, illegal_function_call}, S};
handle_call(stop, _, S) ->
    {stop, normal, stopped, S};
handle_call(Call, _, S) ->
    {reply, {illegal_message, Call}, S}.

%% Another node's search asks for this node's names; a search process reports its result.
handle_cast({registered_names, User}, S) ->
    User ! {registered_names_res, global:registered_names()},
    {noreply, S};
handle_cast({find_name, User, Name}, S) ->
    User ! {find_name_res, global:whereis_name(Name)},
    {noreply, S};
handle_cast({registered_names_res, Result, Pid, From}, S) ->
    search_done(Pid, From, Result),
    {noreply, S};
handle_cast({find_name_res, Result, Pid, From}, S) ->
    search_done(Pid, From, Result),
    {noreply, S};
handle_cast({send_res, Result, Name, Message, Pid, From}, S) ->
    _ = Result =/= {badarg, {Name, Message}} andalso (Result ! Message),
    search_done(Pid, From, Result),
    {noreply, S};
handle_cast({conf_check, Vsn, Node, From, sync, Name, Nodes}, S) ->
    handle_cast({conf_check, Vsn, Node, From, sync, Name, normal, Nodes}, S);
handle_cast({conf_check, Vsn, Node, From, sync, Name, PublishType, Nodes}, S) ->
    {noreply, conf_check(Vsn, Node, From, {Name, PublishType, Nodes}, S)};
handle_cast(_, S) ->
    {noreply, S}.

handle_info(Message, #state{erpc_requests = Requests} = S) ->
    try erpc:check_response(Message, Requests, true) of
        NoMatch when NoMatch =:= no_request; NoMatch =:= no_response ->
            node_info(Message, S);
        {{response, Result}, Label, Rest} ->
            {noreply, erpc_response(ok, Result, Label, S#state{erpc_requests = Rest})}
    catch
        Class:{Reason, Label, Rest} ->
            {noreply, erpc_response(Class, Reason, Label, S#state{erpc_requests = Rest})}
    end.

terminate(_, _) -> ok.

code_change(_, S, _) -> {ok, S}.

%% sync/0: global on the nodes outside the group hears they are, and each node of the group is asked to confirm the
%% group (conf_check); confirmations come back as config_ok or config_error.
sync_group(#state{connections = Connections} = S) ->
    case lookup_group_conf(true) of
        #gconf{state = no_conf} = GConf ->
            unconfigured(GConf, S);
        #gconf{state = {error, _, Value}} ->
            exit({error, {'invalid global_groups definition', Value}});
        #gconf{} = GConf ->
            Own = lists:delete(node(), GConf#gconf.group_list),
            disconnect_nodes(nodes() -- Own, Connections),
            Session = make_ref(),
            Check =
                {conf_check, ?CC_VSN, node(), {self(), Session}, sync, GConf#gconf.group_name,
                    GConf#gconf.group_publish_type, GConf#gconf.group_list},
            Monitors = maps:from_list([{N, check_node(N, Check)} || N <- Own]),
            States = maps:from_list([{N, maps:get(N, S#state.nodes, no_contact)} || N <- Own]),
            (configured(GConf, S))#state{nodes = States, config_check = {Session, Monitors}}
    end.

check_node(Node, Check) ->
    Server = {global_group, Node},
    Monitor = erlang:monitor(process, Server),
    gen_server:cast(Server, Check),
    Monitor.

configured(GConf, S) ->
    S#state{
        sync_state = synced,
        group_name = GConf#gconf.group_name,
        other_grps = GConf#gconf.other_groups,
        group_publish_type = GConf#gconf.group_publish_type
    }.

unconfigured(GConf, S) ->
    S#state{
        sync_state = no_conf,
        group_name = GConf#gconf.group_name,
        nodes = #{},
        group_publish_type = GConf#gconf.group_publish_type,
        other_grps = GConf#gconf.other_groups,
        config_check = undefined
    }.

% A changed parameter: connections outside the new group go, the group's nodes are checked again.
groups_changed(Value, #state{connections = Connections} = S) ->
    GConf = checked_conf(new_group_conf(true, Value)),
    Own = lists:delete(node(), GConf#gconf.group_list),
    force_nodedown(nodes(connected) -- Own, Connections),
    States = maps:from_list([{N, maps:get(N, S#state.nodes, no_contact)} || N <- Own]),
    Requests = conf_changed_checks(Own, S#state.erpc_requests, Connections),
    (configured(GConf, S))#state{
        sync_state = S#state.sync_state, nodes = States, erpc_requests = Requests, config_check = undefined
    }.

% A parameter set where there was none: the group's nodes are asked whether they agree (ng_add_check).
groups_added(Value, #state{connections = Connections} = S) ->
    GConf = checked_conf(new_group_conf(true, Value)),
    force_nodedown(nodes(connected) -- GConf#gconf.group_list, Connections),
    Arguments = [node(), GConf#gconf.group_publish_type, GConf#gconf.group_list],
    {Requests, States} = lists:foldl(
        fun(N, {Acc, StateAcc}) ->
            Id = maps:get(N, Connections, not_connected),
            Next = erpc:send_request(N, ?MODULE, ng_add_check, Arguments, {ng_add_check, N, Id}, Acc),
            {Next, StateAcc#{N => connection_state(N, Connections)}}
        end,
        {S#state.erpc_requests, #{}},
        lists:delete(node(), GConf#gconf.group_list)
    ),
    (configured(GConf, S))#state{nodes = States, erpc_requests = Requests, config_check = undefined}.

checked_conf(#gconf{state = no_conf}) -> exit({error, 'no global_groups definiton'});
checked_conf(#gconf{state = {error, _, Value}}) -> exit({error, {'invalid global_groups definition', Value}});
checked_conf(GConf) -> GConf.

% Asks each node for its group (get_own_nodes/0); the answers come in erpc_response/4.
conf_changed_checks(Nodes, Requests, Connections) ->
    lists:foldl(
        fun(N, Acc) ->
            Label = {conf_changed_check, N, maps:get(N, Connections, not_connected)},
            erpc:send_request(N, ?MODULE, get_own_nodes, [], Label, Acc)
        end,
        Requests,
        Nodes
    ).

%% Another node's sync/0 asks whether this node has the same group: agreed, it takes part in global with it.
conf_check(Vsn, Node, From, Group, #state{connections = Connections} = S) ->
    case {maps:get(Node, Connections, undefined), reply_to(Vsn, Node, From)} of
        {undefined, _} ->
            S;
        {_, garbage} ->
            S;
        {Id, To} ->
            case {lookup_group_conf(true), Group} of
                {#gconf{state = no_conf}, _} ->
                    disconnect_nodes([Node], Connections),
                    To ! {config_error, Vsn, From, node()},
                    S;
                {#gconf{state = {error, _, _}}, _} ->
                    disconnect_nodes([Node], Connections),
                    To ! {config_error, Vsn, From, node()},
                    node_state(remove, Node, S);
                {#gconf{group_name = Name, group_list = Nodes, group_publish_type = Type}, {Name, Type, Nodes}} ->
                    global_name_server ! {group_nodeup, Node, Id},
                    To ! {config_ok, Vsn, From, node()},
                    node_state(sync, Node, S);
                {#gconf{}, _} ->
                    disconnect_nodes([Node], Connections),
                    To ! {config_error, Vsn, From, node()},
                    node_state(sync_error, Node, S)
            end
    end.

% Version 3 replies go to the asking server; older nodes still have their global_group_check process.
reply_to(Vsn, _, {Pid, _}) when is_integer(Vsn), Vsn >= 3, is_pid(Pid) -> Pid;
reply_to(Vsn, _, _) when is_integer(Vsn), Vsn >= 3 -> garbage;
reply_to(_, Node, _) -> {global_group_check, Node}.

%% Node status and the configuration checks' answers.

% This node started distribution (connection id undefined): its name now places it in a group.
node_info({nodeup, Node, #{connection_id := undefined}}, S) ->
    Next = initial_group_setup(alive_state_change_group_conf(Node), S#state.connections, S#state.erpc_requests),
    send_monitor(Next#state.monitor, {nodeup, Node}, Next#state.sync_state),
    {noreply, Next};
node_info({nodeup, Node, #{connection_id := Id}}, #state{sync_state = no_conf, connections = Connections} = S) ->
    send_monitor(S#state.monitor, {nodeup, Node}, no_conf),
    {noreply, S#state{connections = Connections#{Node => Id}}};
node_info({nodeup, Node, #{connection_id := Id}}, #state{connections = Connections0} = S) ->
    Connections = Connections0#{Node => Id},
    case member(true, Node) of
        false ->
            disconnect_nodes([Node], Connections),
            {noreply, S#state{connections = Connections}};
        true ->
            % A node of the group: it takes part once it showed the same group.
            Label = {nodeup_conf_check, Node, Id},
            Requests = erpc:send_request(Node, ?MODULE, get_own_nodes, [], Label, S#state.erpc_requests),
            {noreply, node_state(sync_error, Node, S#state{erpc_requests = Requests, connections = Connections})}
    end;
node_info({nodedown, Node, #{connection_id := undefined}}, S) ->
    % Distribution stopped: without a name there is no group.
    #gconf{state = no_conf} = GConf = alive_state_change_group_conf(nonode@nohost),
    send_monitor(S#state.monitor, {nodedown, Node}, no_conf),
    {noreply, unconfigured(GConf, S)};
node_info({nodedown, Node, _}, #state{sync_state = no_conf, connections = Connections} = S) ->
    send_monitor(S#state.monitor, {nodedown, Node}, no_conf),
    {noreply, S#state{connections = maps:remove(Node, Connections)}};
node_info({nodedown, Node, _}, #state{connections = Connections} = S) ->
    send_monitor(S#state.monitor, {nodedown, Node}, S#state.sync_state),
    {noreply, node_state(no_contact, Node, S#state{connections = maps:remove(Node, Connections)})};
node_info({disconnect_node, Node}, #state{nodes = Nodes, connections = Connections} = S) ->
    % Node no longer counts this node in its group.
    _ =
        S#state.sync_state =:= synced andalso maps:get(Node, Nodes, not_member) =:= sync andalso
            send_monitor(S#state.monitor, {nodedown, Node}, synced),
    global_name_server ! {group_nodedown, Node, maps:get(Node, Connections, not_connected)},
    {noreply, node_state(sync_error, Node, S)};
node_info({Reply, ?CC_VSN, {Pid, Session}, Node}, #state{config_check = {Session, _}} = S) when
    Pid =:= self(), (Reply =:= config_ok orelse Reply =:= config_error)
->
    {noreply, check_reply(Reply, Node, S)};
node_info({'DOWN', Monitor, process, {global_group, Node}, Reason}, #state{config_check = {_, Monitors}} = S) ->
    case Monitors of
        #{Node := Monitor} -> {noreply, check_down(Node, Reason, S)};
        _ -> {noreply, S}
    end;
node_info({'EXIT', Pid, Reason}, S) ->
    search_failed(Pid, Reason),
    _ = get(global_group_check) =:= Pid andalso exit(Reason),
    {noreply, S};
node_info(_, S) ->
    {noreply, S}.

% A node's answer to sync/0's conf_check.
check_reply(Reply, Node, #state{config_check = {Session, Monitors}, connections = Connections} = S0) ->
    case maps:take(Node, Monitors) of
        error ->
            S0;
        {Monitor, Rest} ->
            erlang:demonitor(Monitor),
            S = S0#state{config_check = check_session(Session, Rest)},
            check_reply(Reply, Node, maps:get(Node, Connections, not_connected), S)
    end.

check_reply(config_ok, _, not_connected, S) ->
    S;
check_reply(config_ok, Node, Id, S) ->
    global_name_server ! {group_nodeup, Node, Id},
    node_state(sync, Node, S);
check_reply(config_error, Node, Id, S) ->
    global_name_server ! {group_nodedown, Node, Id},
    log_sync_error(Node),
    node_state(sync_error, Node, S).

% A node's server ended before it answered sync/0's conf_check.
check_down(Node, Reason, #state{config_check = {Session, Monitors}, connections = Connections} = S0) ->
    S = S0#state{config_check = check_session(Session, maps:remove(Node, Monitors))},
    global_name_server ! {group_nodedown, Node, maps:get(Node, Connections, not_connected)},
    case Reason of
        noconnection ->
            node_state(no_contact, Node, S);
        _ ->
            log_sync_error(Node),
            node_state(sync_error, Node, S)
    end.

check_session(_, Monitors) when map_size(Monitors) =:= 0 -> undefined;
check_session(Session, Monitors) -> {Session, Monitors}.

log_sync_error(Node) ->
    Text = io_lib:format(
        "global_group: Could not synchronize with node ~p~nbecause global_groups parameter were not in agreement.~n",
        [Node]
    ),
    error_logger:error_report(Text),
    ok.

% The answers of get_own_nodes/0 (at a nodeup or a changed parameter) and of ng_add_check/3, for the connection that
% asked; one of an ended connection changes nothing.
erpc_response(Class, Result, {Check, Node, AskedId}, #state{connections = Connections} = S) ->
    case maps:get(Node, Connections, undefined) of
        Id when Id =:= AskedId; AskedId =:= not_connected, is_integer(Id) ->
            check_answer(Check, Class, Result, Node, Id, S);
        _ ->
            S
    end.

check_answer(_, error, {erpc, noconnection}, Node, _, S) ->
    node_state(no_contact, Node, S);
check_answer(ng_add_check, ok, agreed, Node, _, S) ->
    node_state(sync, Node, S);
check_answer(Check, ok, Nodes, Node, Id, S) when is_list(Nodes), Check =/= ng_add_check ->
    case lists:sort(Nodes) =:= own_group_nodes(true) of
        true when Check =:= nodeup_conf_check ->
            send_monitor(S#state.monitor, {nodeup, Node}, S#state.sync_state),
            global_name_server ! {group_nodeup, Node, Id},
            node_state(sync, Node, S);
        true ->
            node_state(sync, Node, S);
        false ->
            disconnect_nodes([Node], S#state.connections),
            node_state(sync_error, Node, S)
    end;
check_answer(_, _, _, Node, _, S) ->
    disconnect_nodes([Node], S#state.connections),
    node_state(sync_error, Node, S).

% A node of the group takes the state; another one leaves the map (removing a member is an error).
node_state(remove, Node, #state{nodes = Nodes} = S) ->
    case member(true, Node) of
        true -> error({removing_node_state_of_member_node, Node});
        false -> S#state{nodes = maps:remove(Node, Nodes)}
    end;
node_state(What, Node, #state{nodes = Nodes} = S) ->
    case member(true, Node) of
        true -> S#state{nodes = Nodes#{Node => What}};
        false -> S#state{nodes = maps:remove(Node, Nodes)}
    end.

%% global stops taking part with the nodes, and their servers hear that this node no longer counts them.
disconnect_nodes(Nodes, Connections) ->
    _ = [
        begin
            global_name_server ! {group_nodedown, Node, maps:get(Node, Connections, not_connected)},
            {global_group, Node} ! {disconnect_node, node()}
        end
     || Node <- Nodes
    ],
    ok.

force_nodedown(Nodes, Connections) ->
    _ = [
        begin
            global_name_server ! {group_nodedown, Node, maps:get(Node, Connections, not_connected)},
            erlang:disconnect_node(Node)
        end
     || Node <- Nodes
    ],
    ok.

info_reply(S) ->
    States = maps:to_list(S#state.nodes),
    [
        {state, S#state.sync_state},
        {own_group_name, S#state.group_name},
        {own_group_nodes, own_group_nodes(true)},
        {synced_nodes, lists:sort([N || {N, sync} <- States])},
        {sync_error, lists:sort([N || {N, sync_error} <- States])},
        {no_contact, lists:sort([N || {N, no_contact} <- States])},
        {other_groups, S#state.other_grps},
        {monitoring, S#state.monitor}
    ].

%% Node status subscribers of monitor_nodes/1: each gets {nodeup, Node} and {nodedown, Node} of the group's nodes (of
%% every node without groups).

monitor_nodes(true, Pid, S) ->
    link(Pid),
    {ok, S#state{monitor = [Pid | S#state.monitor]}};
monitor_nodes(false, Pid, S) ->
    Next = S#state{monitor = [P || P <- S#state.monitor, P =/= Pid]},
    _ = lists:member(Pid, Next#state.monitor) orelse unlink(Pid),
    {ok, Next}.

send_monitor(Pids, {_, Node} = Message, SyncState) ->
    _ = [Pid ! Message || Pid <- Pids, SyncState =:= no_conf orelse member(true, Node)],
    ok.

% The dispatcher of OTP's global_group_check: replies of older nodes go to the server.
check_dispatcher() ->
    receive
        {config_ok, _, _, _} = Message -> global_group ! Message;
        {config_error, _, _, _} = Message -> global_group ! Message;
        _ -> ok
    end,
    check_dispatcher().

%% Name searches: this node's own group through global; other nodes and groups through a search process, which asks
%% each node's server in turn and reports to this server. A search that fails answers {error, Reason}, or
%% {badarg, {Name, Message}} for a send.

registered_names_request({group, Group}, _, #state{group_name = Group} = S) ->
    {reply, global:registered_names(), S};
registered_names_request({group, Group}, From, S) ->
    case lists:keyfind(Group, 1, S#state.other_grps) of
        {Group, [_ | _] = Nodes} -> {noreply, searching(names, {group, Nodes}, {names, From}, S)};
        _ -> {reply, [], S}
    end;
registered_names_request({node, Node}, _, S) when Node =:= node() ->
    {reply, global:registered_names(), S};
registered_names_request({node, Node}, From, S) ->
    {noreply, searching(names, {node, Node}, {names, From}, S)}.

send_request(any, Name, Message, From, S) ->
    case global:whereis_name(Name) of
        undefined ->
            {noreply, searching(send, {any, S#state.other_grps, Name}, {send, From, Name, Message}, S)};
        Pid ->
            Pid ! Message,
            {reply, Pid, S}
    end;
send_request({group, Group}, Name, Message, _, #state{group_name = Group} = S) ->
    case global:whereis_name(Name) of
        undefined ->
            {reply, {badarg, {Name, Message}}, S};
        Pid ->
            Pid ! Message,
            {reply, Pid, S}
    end;
send_request({group, Group}, Name, Message, From, S) ->
    case lists:keyfind(Group, 1, S#state.other_grps) of
        {Group, [_ | _] = Nodes} -> {noreply, searching(send, {group, Nodes, Name}, {send, From, Name, Message}, S)};
        _ -> {reply, {badarg, {Name, Message}}, S}
    end;
send_request({node, Node}, Name, Message, From, S) ->
    {noreply, searching(send, {node, Node, Name}, {send, From, Name, Message}, S)}.

whereis_request(any, Name, From, S) ->
    case global:whereis_name(Name) of
        undefined -> {noreply, searching(whereis, {any, S#state.other_grps, Name}, {whereis, From}, S)};
        Pid -> {reply, Pid, S}
    end;
whereis_request({group, Group}, Name, _, #state{group_name = Group} = S) ->
    {reply, global:whereis_name(Name), S};
whereis_request({group, Group}, Name, From, S) ->
    case lists:keyfind(Group, 1, S#state.other_grps) of
        {Group, [_ | _] = Nodes} -> {noreply, searching(whereis, {group, Nodes, Name}, {whereis, From}, S)};
        _ -> {reply, undefined, S}
    end;
whereis_request({node, Node}, Name, From, S) ->
    {noreply, searching(whereis, {node, Node, Name}, {whereis, From}, S)}.

% A search process, linked, remembered with its caller until it reports or fails.
searching(Kind, Target, Pending, S) ->
    Pid = spawn_link(fun() -> search(Kind, Target, Pending) end),
    put(searches, (get(searches))#{Pid => Pending}),
    S.

search_done(Pid, From, Result) ->
    unlink(Pid),
    Pid ! kill,
    put(searches, maps:remove(Pid, get(searches))),
    gen_server:reply(From, Result).

search_failed(Pid, Reason) ->
    case maps:take(Pid, get(searches)) of
        {{send, From, Name, Message}, Rest} ->
            put(searches, Rest),
            gen_server:reply(From, {badarg, {Name, Message}});
        {{_, From}, Rest} ->
            put(searches, Rest),
            gen_server:reply(From, {error, Reason});
        error ->
            ok
    end.

% The search process: its result goes to the server, which then ends it.
search(Kind, _, _) when Kind =:= names_test; Kind =:= send_test; Kind =:= whereis_test ->
    timer:sleep(5000),
    exit(testing_exit);
search(names, Target, {names, From}) ->
    Names =
        case names_in(Target) of
            Found when is_list(Found) -> Found;
            _Down -> []
        end,
    gen_server:cast(global_group, {registered_names_res, Names, self(), From}),
    wait_kill();
search(whereis, Target, {whereis, From}) ->
    gen_server:cast(global_group, {find_name_res, found_pid(Target), self(), From}),
    wait_kill();
search(send, Target, {send, From, Name, Message}) ->
    Result =
        case found_pid(Target) of
            Pid when is_pid(Pid) -> Pid;
            undefined -> {badarg, {Name, Message}}
        end,
    gen_server:cast(global_group, {send_res, Result, Name, Message, self(), From}),
    wait_kill().

wait_kill() ->
    receive
        kill -> exit(normal)
    end.

% The pid of a name, as OTP searches: the groups in turn until one has it; in a group, the first node that can be
% reached answers for all of it; or one node. undefined when it is not found.
found_pid(Target) ->
    case found_in(Target) of
        Pid when is_pid(Pid) -> Pid;
        _ -> undefined
    end.

found_in({any, Groups, Name}) -> found_in_groups(Groups, Name);
found_in({group, Nodes, Name}) -> group_answer(Nodes, {find_name, self(), Name}, find_name_res);
found_in({node, Node, Name}) -> ask_node(Node, {find_name, self(), Name}, find_name_res).

found_in_groups([], _) ->
    undefined;
found_in_groups([{_, Nodes} | Rest], Name) ->
    case group_answer(Nodes, {find_name, self(), Name}, find_name_res) of
        Pid when is_pid(Pid) -> Pid;
        _ -> found_in_groups(Rest, Name)
    end.

% The names of a group (its first node that can be reached) or of one node.
names_in({group, Nodes}) -> group_answer(Nodes, {registered_names, self()}, registered_names_res);
names_in({node, Node}) -> ask_node(Node, {registered_names, self()}, registered_names_res).

% The answer of a group's first node that can be reached, or group_down.
group_answer([], _, _) ->
    group_down;
group_answer([Node | Rest], Question, Tag) ->
    case ask_node(Node, Question, Tag) of
        node_down -> group_answer(Rest, Question, Tag);
        Answer -> Answer
    end.

% One question to Node's server: node_down when Node cannot be reached or goes down before it answers.
ask_node(Node, Question, Tag) ->
    case net_adm:ping(Node) of
        pang ->
            node_down;
        pong ->
            monitor_node(Node, true),
            gen_server:cast({global_group, Node}, Question),
            receive
                {nodedown, Node} ->
                    node_down;
                {Tag, Result} ->
                    monitor_node(Node, false),
                    Result
            end
    end.
