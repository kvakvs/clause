%% Clause's global (plan 21 step LIB25, plan 30 step DIST8a, docs/distribution.md#global): global names and locks
%% replicated between connected nodes, over OTP's protocol version 8 between the global_name_server processes, written
%% from OTP's documentation and the messages its nodes exchange. The server starts under kernel_sup at the first use.
%% When two nodes connect, each partition's locker (clause_global_locker) locks the partition, the servers exchange
%% their names, resolve conflicts once on the smaller node, tell the rest of their partition about the new nodes and
%% connect everyone. Names and locks of a process go when it ends, those of a node when the connection goes, and a lost
%% connection disconnects the nodes that would otherwise form overlapping partitions (prevent_overlapping_partitions).
-module(global).
-behaviour(gen_server).
-export([
    start/0,
    start_link/0,
    stop/0,
    sync/0,
    sync/1,
    whereis_name/1,
    register_name/2,
    register_name/3,
    register_name_external/2,
    register_name_external/3,
    unregister_name_external/1,
    re_register_name/2,
    re_register_name/3,
    unregister_name/1,
    registered_names/0,
    send/2,
    set_lock/1,
    set_lock/2,
    set_lock/3,
    del_lock/1,
    del_lock/2,
    trans/2,
    trans/3,
    trans/4,
    random_exit_name/3,
    random_notify_name/3,
    notify_all_name/3,
    disconnect/0
]).
-export([init/1, handle_call/3, handle_cast/2, handle_info/2, terminate/2, code_change/3, resolve_it/4, get_locker/0]).
-export([info/0]).

-define(SERVER, global_name_server).
%% The protocol version this node speaks, and the versions that brought init_connect_ack and cancel_connect, the
%% overlapping-partitions messages with known nodes' versions, and partition prevention under global groups.
-define(VSN, 8).
-define(VERIFY_CONNECT_VSN, 8).
-define(POP_VSN, 7).
-define(PGPV_VSN, 7).
-define(GG_POP_VSN, 8).
%% The resource of global's own lock, taken by name registrations and by lockers joining partitions.
-define(GLOBAL_RID, global).
%% How long a lost_connection report is remembered, so that its repetitions are recognized.
-define(LOST_CONNECTION_KEEP, 60 * 60 * 1000).
-define(MIN_SMALL, -(1 bsl 59)).
-define(MAX_SMALL, (1 bsl 59) - 1).

%% The kernel parameters connect_all and prevent_overlapping_partitions as the server started with them.
-record(conf, {connect_all :: boolean(), prevent_over_part :: boolean()}).
%% The server's state, which info/0 returns. known maps a known node to its protocol version, and holds
%% {pending, Node} => Vsn while a connect runs, {removing, Node} => yes while partition prevention removes Node and
%% {connection_id, Node} => Id for each visible connection. synced lists the nodes whose names match this node's;
%% resolvers the {Node, MyTag, Pid} of each connect session's resolver; syncers the processes of sync/0,1 calls.
%% trace stays no_trace: OTP's high-level trace for troubleshooting is not provided.
-record(state, {
    conf = #conf{},
    known = #{},
    synced = [],
    resolvers = [],
    syncers = [],
    node_name = node(),
    the_locker,
    the_registrar,
    trace = no_trace,
    global_lock_down = false
}).

%% The ETS tables, OTP's: global_names {Name, Pid, Method, Ref}, global_names_ext {Name, Pid, RegNode},
%% global_pid_names {Pid, Name} and {Ref, Name}, global_locks {ResourceId, RequesterId, [{Pid, Ref}]},
%% global_pid_ids {Pid, ResourceId} and {Ref, ResourceId}, global_lost_connections {{NodeA, NodeB}, {XCreation, OpId,
%% Timer}} and global_node_resources {Node, #{Ref => ok}} and {Ref, Node} for the monitors of each node's processes.

%% The server.

start() -> gen_server:start({local, ?SERVER}, ?MODULE, [], []).

start_link() -> gen_server:start_link({local, ?SERVER}, ?MODULE, [], []).

stop() -> gen_server:call(?SERVER, stop, infinity).

%% The server starts with kernel_sup at the first use of a kernel service (plan 30 step DIST4b1d).
ensure_started() ->
    case whereis(?SERVER) of
        undefined ->
            ok = clause_kernel:ensure_started(),
            % Excluded from kernel_sup (exclude_kernel_services), the server still starts unlinked at its first use.
            case whereis(?SERVER) of
                undefined -> _ = start();
                _ -> ok
            end,
            ok;
        _ ->
            ok
    end.

call(Request) ->
    ensure_started(),
    gen_server:call(?SERVER, Request, infinity).

%% Synchronisation: sync/0,1 return once this node exchanged names with the nodes, within this node's global group.

sync() ->
    case check_sync_nodes() of
        {error, _} = Error -> Error;
        Nodes -> call({sync, Nodes})
    end.

sync(Nodes) ->
    case check_sync_nodes(Nodes) of
        {error, _} = Error -> Error;
        SyncNodes -> call({sync, SyncNodes})
    end.

check_sync_nodes() ->
    case own_nodes() of
        {ok, all} -> nodes();
        {ok, GroupNodes} -> intersection(GroupNodes, nodes());
        {error, _} = Error -> Error
    end.

check_sync_nodes(Nodes) ->
    case own_nodes() of
        {ok, all} ->
            Nodes;
        {ok, GroupNodes} ->
            case Nodes -- [node() | intersection(GroupNodes, nodes())] of
                [] -> Nodes;
                Illegal -> {error, {"Trying to sync nodes not defined in the own global group", Illegal}}
            end;
        {error, _} = Error ->
            Error
    end.

own_nodes() ->
    case global_group:get_own_nodes_with_errors() of
        {error, Error} -> {error, {"global_groups definition error", Error}};
        Own -> Own
    end.

intersection(_, []) -> [];
intersection(L1, L2) -> L1 -- (L1 -- L2).

%% Names: lookups read this node's replica; changes run in the registrar under global's lock on every known node.

where(Name) ->
    ensure_started(),
    case ets:lookup(global_names, Name) of
        [{_, Pid, _, _}] when node(Pid) =:= node() ->
            case is_process_alive(Pid) of
                true -> Pid;
                false -> undefined
            end;
        [{_, Pid, _, _}] ->
            Pid;
        [] ->
            undefined
    end.

whereis_name(Name) -> where(Name).

send(Name, Message) ->
    case where(Name) of
        Pid when is_pid(Pid) ->
            Pid ! Message,
            Pid;
        undefined ->
            exit({badarg, {Name, Message}})
    end.

register_name(Name, Pid) when is_pid(Pid) -> register_name(Name, Pid, fun random_exit_name/3).

register_name(Name, Pid, Method0) when is_pid(Pid) ->
    Method = allow_tuple_fun(Method0),
    registrar(fun(Nodes) ->
        case where(Name) =:= undefined andalso check_dupname(Name, Pid) of
            true ->
                _ = gen_server:multi_call(Nodes, ?SERVER, {register, Name, Pid, Method}),
                yes;
            false ->
                no
        end
    end).

register_name_external(Name, Pid) when is_pid(Pid) -> register_name_external(Name, Pid, fun random_exit_name/3).

register_name_external(Name, Pid, Method) when is_pid(Pid) ->
    registrar(fun(Nodes) ->
        case where(Name) of
            undefined ->
                _ = gen_server:multi_call(Nodes, ?SERVER, {register_ext, Name, Pid, Method, node()}),
                yes;
            _ ->
                no
        end
    end).

unregister_name_external(Name) -> unregister_name(Name).

re_register_name(Name, Pid) when is_pid(Pid) -> re_register_name(Name, Pid, fun random_exit_name/3).

re_register_name(Name, Pid, Method0) when is_pid(Pid) ->
    Method = allow_tuple_fun(Method0),
    registrar(fun(Nodes) ->
        _ = gen_server:multi_call(Nodes, ?SERVER, {register, Name, Pid, Method}),
        yes
    end).

unregister_name(Name) ->
    case where(Name) of
        undefined ->
            ok;
        _ ->
            registrar(fun(Nodes) ->
                _ = gen_server:multi_call(Nodes, ?SERVER, {unregister, Name}),
                ok
            end)
    end.

registered_names() ->
    ensure_started(),
    [Name || [Name] <- ets:match(global_names, {'$1', '_', '_', '_'})].

registrar(Fun) -> call({registrar, Fun}).

allow_tuple_fun({M, F}) when is_atom(M), is_atom(F) -> fun M:F/3;
allow_tuple_fun(Fun) when is_function(Fun, 3) -> Fun.

%% A process may have one name unless kernel's global_multi_name_action is allow.
check_dupname(Name, Pid) ->
    case ets:lookup(global_pid_names, Pid) of
        [] ->
            true;
        PidNames ->
            case clause_logger:app_env(kernel, global_multi_name_action) of
                {ok, allow} ->
                    true;
                _ ->
                    Names = [Name | [Other || {_, Other} <- PidNames]],
                    logger:log(error, "global: ~w registered under several names: ~tw\n", [Pid, Names]),
                    false
            end
    end.

%% Name conflict resolvers, called on the node of the name's older pid when partitions with the same name meet.

random_exit_name(Name, Pid, Pid2) ->
    {Min, Max} = minmax(Pid, Pid2),
    logger:log(info, "global: Name conflict terminating ~tw\n", [{Name, Max}]),
    exit(Max, kill),
    Min.

random_notify_name(Name, Pid, Pid2) ->
    {Min, Max} = minmax(Pid, Pid2),
    Max ! {global_name_conflict, Name},
    Min.

notify_all_name(Name, Pid, Pid2) ->
    Pid ! {global_name_conflict, Name, Pid2},
    Pid2 ! {global_name_conflict, Name, Pid},
    none.

%% Ordered by node only: on one node the second pid wins, as in OTP.
minmax(P1, P2) when node(P1) < node(P2) -> {P1, P2};
minmax(P1, P2) -> {P2, P1}.

%% The resolver's rpc call on the node of the registered pid: the method's choice, or the exception it raised.
resolve_it(Method, Name, Pid1, Pid2) ->
    try
        Method(Name, Pid1, Pid2)
    catch
        throw:Value -> Value;
        exit:Reason -> {'EXIT', Reason};
        error:Reason:Stack -> {'EXIT', {Reason, Stack}}
    end.

%% Locks: held by a requester id, which several processes may share, on each node of a list.

set_lock(Id) -> set_lock(Id, [node() | nodes()], infinity, 1).

set_lock(Id, Nodes) -> set_lock(Id, Nodes, infinity, 1).

set_lock(Id, Nodes, Retries) when is_integer(Retries), Retries >= 0 -> set_lock(Id, Nodes, Retries, 1);
set_lock(Id, Nodes, infinity) -> set_lock(Id, Nodes, infinity, 1).

set_lock({_, _}, [], _, _) ->
    true;
set_lock({_, _} = Id, Nodes, Retries, Times) ->
    case set_lock_on_nodes(Id, Nodes) of
        true ->
            true;
        false when Retries =:= 0 ->
            false;
        false ->
            random_sleep(Times),
            set_lock(Id, Nodes, dec(Retries), Times + 1)
    end.

dec(infinity) -> infinity;
dec(N) -> N - 1.

del_lock(Id) -> del_lock(Id, [node() | nodes()]).

del_lock({_, _} = Id, Nodes) ->
    ensure_started(),
    _ = gen_server:multi_call(Nodes, ?SERVER, {del_lock, Id}),
    true.

trans(Id, Fun) -> trans(Id, Fun, [node() | nodes()], infinity).

trans(Id, Fun, Nodes) -> trans(Id, Fun, Nodes, infinity).

trans(Id, Fun, Nodes, Retries) ->
    case set_lock(Id, Nodes, Retries) of
        true ->
            try
                Fun()
            after
                del_lock(Id, Nodes)
            end;
        false ->
            aborted
    end.

%% One attempt on every node: when one refuses, the nodes that granted it release it again.
set_lock_on_nodes(_, []) ->
    true;
set_lock_on_nodes(Id, Nodes) ->
    ensure_started(),
    case local_lock_check(Id, Nodes) of
        true ->
            {Replies, _} = gen_server:multi_call(Nodes, ?SERVER, {set_lock, Id}),
            check_replies(Replies, Id);
        false ->
            false
    end.

% A lock on several nodes that this node would refuse is not asked of the others.
local_lock_check(_, [_]) -> true;
local_lock_check(Id, Nodes) -> not lists:member(node(), Nodes) orelse clause_global_locker:can_set_lock(Id) =/= false.

check_replies(Replies, Id) ->
    case lists:keymember(false, 2, Replies) of
        false ->
            true;
        true when length(Replies) =:= 1 ->
            false;
        true ->
            _ = gen_server:multi_call([Node || {Node, true} <- Replies], ?SERVER, {del_lock, Id}),
            false
    end.

%% Waits up to an eighth of 2^Times seconds, at most 8 seconds, as OTP does between lock attempts.
random_sleep(Times) ->
    _ = Times rem 10 =:= 0 andalso rand:seed(exsplus),
    Max =
        case Times > 5 of
            true -> 8000;
            false -> ((1 bsl Times) * 1000) div 8
        end,
    receive
    after rand:uniform(Max) -> ok
    end.

%% Other requests.

info() -> call(info).

get_locker() ->
    #state{the_locker = Locker} = info(),
    Locker.

disconnect() -> call(disconnect).

%% The server's start: OTP's tables, node status messages with connection ids, the locker and the registrar, and the
%% kernel parameters connect_all and prevent_overlapping_partitions.
init([]) ->
    _ = process_flag(async_dist, true),
    process_flag(trap_exit, true),
    ok = net_kernel:monitor_nodes(true, #{connection_id => true}),
    Connected = nodes(visible, #{connection_id => true}),
    Known = maps:from_list([{{connection_id, N}, Id} || {N, #{connection_id := Id}} <- Connected]),
    _ = ets:new(global_locks, [set, named_table, protected]),
    _ = ets:new(global_names, [set, named_table, protected, {read_concurrency, true}]),
    _ = ets:new(global_names_ext, [set, named_table, protected]),
    _ = ets:new(global_pid_names, [bag, named_table, protected]),
    _ = ets:new(global_pid_ids, [bag, named_table, protected]),
    _ = ets:new(global_lost_connections, [set, named_table, protected]),
    _ = ets:new(global_node_resources, [set, named_table, protected]),
    Conf = #conf{connect_all = connect_all(), prevent_over_part = boolean_parameter(prevent_overlapping_partitions)},
    _ = rand:seed(
        default, (erlang:monotonic_time(nanosecond) rem 1000000000) + (erlang:system_time(nanosecond) rem 1000000000)
    ),
    % This incarnation's part of each lost_connection report, so that a restarted node's reports are new.
    put(creation_extension, (rand:uniform(?MAX_SMALL - ?MIN_SMALL) - 1) band (bnot ((1 bsl 32) - 1))),
    {ok, #state{
        conf = Conf,
        known = Known,
        the_locker = clause_global_locker:start_link(),
        the_registrar = spawn_link(fun registrar_loop/0)
    }}.

%% The kernel parameter connect_all (plan 30 step DIST6), as OTP's server reads it: a boolean, else the server fails to
%% start; when unset, false only under the emulator flag -connect_all false, and set so.
connect_all() ->
    case application:get_env(kernel, connect_all) of
        undefined ->
            Value =
                case init:get_argument(connect_all) of
                    {ok, [["false" | _] | _]} -> false;
                    _ -> true
                end,
            ok = application:set_env(kernel, connect_all, Value, [{timeout, infinity}]),
            Value;
        _ ->
            boolean_parameter(connect_all)
    end.

% A boolean kernel parameter, true when unset; another value fails the start, as OTP's.
boolean_parameter(Name) ->
    case application:get_env(kernel, Name) of
        {ok, Value} when is_boolean(Value) -> Value;
        {ok, Value} -> erlang:error({invalid_parameter_value, Name, Value});
        undefined -> true
    end.

handle_call({registrar, Fun}, From, S) ->
    S#state.the_registrar ! {trans_all_known, Fun, From},
    {noreply, S};
handle_call({register, Name, Pid, Method}, {FromPid, _}, S) ->
    {reply, yes, ins_name(Name, Pid, Method, FromPid, [], S)};
handle_call({unregister, Name}, _, S) ->
    {reply, ok, delete_global_name2(Name, S)};
handle_call({register_ext, Name, Pid, Method, RegNode}, {FromPid, _}, S) ->
    {reply, yes, ins_name_ext(Name, Pid, Method, RegNode, FromPid, [], S)};
handle_call({set_lock, Lock}, {Pid, _}, S0) ->
    {Reply, S} = handle_set_lock(Lock, Pid, S0),
    {reply, Reply, S};
handle_call({del_lock, Lock}, {Pid, _}, S) ->
    {reply, true, handle_del_lock(Lock, Pid, S)};
handle_call(get_known, _, S) ->
    {reply, mk_known_list(0, S), S};
handle_call(get_synced, _, S) ->
    {reply, S#state.synced, S};
handle_call({sync, Nodes}, From, S) ->
    Syncer = start_sync(lists:delete(node(), Nodes) -- S#state.synced, From),
    {noreply, S#state{syncers = [Syncer | S#state.syncers]}};
handle_call(get_protocol_version, _, S) ->
    {reply, ?VSN, S};
handle_call(get_names_ext, _, S) ->
    {reply, get_names_ext(), S};
handle_call(info, _, S) ->
    {reply, S, S};
handle_call(disconnect, _, S) ->
    {Nodes, Disconnected} = disconnect_known(S),
    {reply, Nodes, Disconnected};
handle_call(stop, _, S) ->
    {stop, normal, stopped, S};
handle_call(Request, From, S) ->
    logger:log(
        warning,
        "The global_name_server received an unexpected message:\nhandle_call(~tp, ~tp, _)\n",
        [Request, From]
    ),
    {noreply, S}.

handle_cast({init_connect, Vsn, Node, InitMsg}, S) ->
    {noreply, init_connect(Vsn, Node, InitMsg, S)};
handle_cast({lock_is_set, Node, MyTag, LockId}, S) ->
    {noreply, in_session(Node, MyTag, S, fun() -> lock_is_set(Node, LockId, S) end)};
handle_cast({exchange, Node, NameList, _NameExtList, MyTag}, S) ->
    {noreply, in_session(Node, MyTag, S, fun() -> exchange(Node, NameList, S) end)};
handle_cast({exchange_ops, Node, MyTag, Ops, Resolved}, S) ->
    {noreply, in_session(Node, MyTag, S, fun() -> exchange_ops(Node, Ops, Resolved, S) end)};
handle_cast({resolved, Node, HisResolved, HisKnown, _Unused, NamesExt, MyTag}, S) ->
    {noreply, in_session(Node, MyTag, S, fun() -> his_resolved(Node, HisResolved, HisKnown, NamesExt, S) end)};
handle_cast({new_nodes, Node, Ops, NamesExt, Nodes, ExtraInfo}, S) ->
    {noreply, new_nodes(Ops, Node, NamesExt, Nodes, ExtraInfo, S)};
handle_cast({in_sync, Node, _IsKnown}, S) ->
    {noreply, in_sync(Node, S)};
handle_cast({async_del_name, _, _}, S) ->
    % Sent by nodes before R14A; the 'DOWN' message deletes the name.
    {noreply, S};
handle_cast({async_del_lock, _, _}, S) ->
    {noreply, S};
handle_cast({lock_set, Pid, _, _, MyTag} = Message, S) ->
    % The other locker's message for this node's locker, passed on while its session is current.
    _ = session(node(Pid), my_tag) =:= MyTag andalso (S#state.the_locker ! Message),
    {noreply, S};
handle_cast({lock_set, _, _, _} = Message, S) ->
    S#state.the_locker ! Message,
    {noreply, S};
handle_cast(Request, S) ->
    logger:log(warning, "The global_name_server received an unexpected message:\nhandle_cast(~tp, _)\n", [Request]),
    {noreply, S}.

handle_info({'EXIT', Locker, _} = Exit, #state{the_locker = Locker} = S) ->
    {stop, {locker_died, Exit}, S#state{the_locker = undefined}};
handle_info({'EXIT', Registrar, _} = Exit, #state{the_registrar = Registrar} = S) ->
    {stop, {registrar_died, Exit}, S#state{the_registrar = undefined}};
handle_info({'EXIT', Pid, _}, S) when is_pid(Pid) ->
    % A syncer, or a process an external node registered.
    {noreply, S#state{syncers = lists:delete(Pid, S#state.syncers)}};
handle_info({nodedown, Node, _}, #state{node_name = Node} = S) ->
    % Distribution stopped: this node's name is node() again.
    {noreply, S#state{node_name = node()}};
handle_info({nodedown, Node, _}, S) ->
    {noreply, forget_connection(Node, handle_nodedown(Node, S, participant_down(Node)))};
handle_info({extra_nodedown, Node}, S) ->
    notify_syncers_down(Node, S),
    {noreply, forget_connection(Node, handle_nodedown(Node, S, participant_down(Node)))};
handle_info({group_nodedown, Node, CId}, S) ->
    {noreply, group_nodedown(Node, CId, S)};
handle_info({nodeup, Node, _}, S) when Node =:= node() ->
    % Distribution started: this node has its name.
    {noreply, S#state{node_name = Node}};
handle_info({nodeup, Node, #{connection_id := CId}}, #state{known = Known} = S0) ->
    S = S0#state{known = Known#{{connection_id, Node} => CId}},
    case S#state.conf#conf.connect_all andalso not global_group:group_configured() of
        true -> {noreply, handle_nodeup(Node, S)};
        % With global groups, global_group sends group_nodeup for the nodes of this node's group.
        false -> {noreply, S}
    end;
handle_info({group_nodeup, Node, CId}, #state{known = Known} = S) ->
    case maps:get({connection_id, Node}, Known, not_connected) of
        CId -> {noreply, handle_nodeup(Node, S)};
        _ -> {noreply, S}
    end;
handle_info({whereis, Name, From}, S) ->
    do_whereis(Name, From),
    {noreply, S};
handle_info(
    {lost_connection, _, _, _, _} = Message, #state{conf = #conf{connect_all = true, prevent_over_part = true}} = S
) ->
    {noreply, lost_connection(Message, S)};
handle_info({lost_connection, _, _, _, _}, S) ->
    {noreply, S};
handle_info({timeout, _, {lost_connection, _}} = Timeout, S) ->
    forget_lost_connection(Timeout),
    {noreply, S};
handle_info({remove_connection, Node}, S) ->
    {noreply, remove_connection(Node, S)};
handle_info({cancel_connect, Node, MyTag}, S) ->
    {noreply, cancel_connect(Node, MyTag, S)};
handle_info({init_connect_ack, Node, HisMyTag, HisHisTag}, S) ->
    case session(Node, my_tag) =:= HisMyTag andalso session(Node, his_tag) =:= HisHisTag of
        true ->
            {noreply, S};
        false ->
            % The two nodes disagree on the session: cancel it and start over.
            send_cancel_connect_message(Node, HisHisTag),
            {noreply, restart_connect(Node, session(Node, my_tag), S)}
    end;
handle_info({prepare_shutdown, From, Ref}, S) ->
    % The node ends: connections going down then report no lost connections.
    From ! {Ref, ok},
    {noreply, S#state{conf = #conf{connect_all = false, prevent_over_part = false}}};
handle_info({'DOWN', Ref, process, _, _}, S0) ->
    delete_node_resource_info(Ref),
    S1 = delete_lock(Ref, S0),
    {noreply, del_name(Ref, S1)};
handle_info(Message, S) ->
    logger:log(warning, "The global_name_server received an unexpected message:\nhandle_info(~tp, _)\n", [Message]),
    {noreply, S}.

terminate(_, _) ->
    _ = [
        ets:delete(Table)
     || Table <- [global_names, global_names_ext, global_locks, global_pid_names, global_pid_ids]
    ],
    ok.

code_change(_, S, _) -> {ok, S}.

%% Connect sessions. A session with Node starts at its nodeup with this node's tag MyTag; its facts live in the
%% process dictionary under {Key, Node}: my_tag, his_tag, vsn (the protocol version both speak), pre_connect (an
%% init_connect that came before the nodeup), wait_lock (lock_is_set, or the names of an exchange that came first),
%% save_ops (this node's resolved operations, or Node's resolved message that came first) and lock_id.

session(Node, Key) -> get({Key, Node}).

session(Node, Key, Value) -> put({Key, Node}, Value).

take_session(Node, Key) -> erase({Key, Node}).

reset_session(Node) ->
    _ = [erase({Key, Node}) || Key <- [wait_lock, save_ops, pre_connect, vsn, my_tag, his_tag, lock_id]],
    ok.

%% Runs Continue for a message of Node's current session; one of an older session cancels that session's lock.
in_session(Node, MyTag, S, Continue) ->
    case session(Node, my_tag) of
        MyTag -> Continue();
        _ -> cancel_locker(Node, S, MyTag)
    end.

%% A visible node connected: this node offers it a session, init_connect carrying this node's locker.
handle_nodeup(Node, #state{known = Known, resolvers = Resolvers} = S) ->
    case maps:is_key(Node, Known) orelse lists:keymember(Node, 1, Resolvers) of
        true ->
            S;
        false ->
            resend_pre_connect(Node),
            MyTag = erlang:unique_integer([monotonic]),
            session(Node, my_tag, MyTag),
            Locker = {locker, no_longer_a_pid, Known, S#state.the_locker},
            gen_server:cast({?SERVER, Node}, {init_connect, {?VSN, MyTag}, node(), Locker}),
            Resolver = spawn(fun() -> resolver(Node, MyTag) end),
            S#state{resolvers = [{Node, MyTag, Resolver} | Resolvers]}
    end.

% An init_connect that came before the nodeup is taken again once the session started.
resend_pre_connect(Node) ->
    case take_session(Node, pre_connect) of
        {Vsn, InitMsg, HisTag} -> gen_server:cast(self(), {init_connect, {Vsn, HisTag}, Node, InitMsg});
        _ -> ok
    end.

%% Node's offer of a session. A newer node speaks this node's version; any tuple starting with a version and a tag is
%% taken from future nodes.
init_connect({HisVsn, HisTag}, Node, InitMsg, S) when HisVsn > ?VSN ->
    init_connect(?VSN, Node, InitMsg, HisTag, HisVsn, S);
init_connect({HisVsn, HisTag}, Node, InitMsg, S) ->
    init_connect(HisVsn, Node, InitMsg, HisTag, HisVsn, S);
init_connect(Vsn, Node, InitMsg, S) when is_tuple(Vsn) ->
    [HisVsn, HisTag | _] = tuple_to_list(Vsn),
    init_connect(?VSN, Node, InitMsg, HisTag, HisVsn, S);
init_connect(Vsn, Node, _, S) ->
    logger:log(info, lists:flatten(io_lib:format("Illegal global protocol version ~p Node: ~p\n", [Vsn, Node]))),
    S.

% A second offer while a session runs (possible with global groups) cancels both and starts over.
init_connect(Vsn, Node, InitMsg, HisTag, HisVsn, #state{known = Known} = S) ->
    case maps:is_key({pending, Node}, Known) of
        true when HisVsn < ?VERIFY_CONNECT_VSN ->
            erlang:disconnect_node(Node),
            logger:log(
                error,
                "'global' at node ~p got an out of sync connection attempt from old version ~p node ~p. "
                "Disconnecting from it.",
                [node(), HisVsn, Node]
            ),
            S;
        true ->
            send_cancel_connect_message(Node, HisTag),
            restart_connect(Node, session(Node, my_tag), S);
        false ->
            accept_init_connect(Vsn, Node, InitMsg, HisTag, HisVsn, S)
    end.

% The offer joins this node's session with Node: the locker learns the other locker and starts locking. Before this
% node's own nodeup the offer waits as pre_connect.
accept_init_connect(Vsn, Node, InitMsg, HisTag, HisVsn, S) ->
    session(Node, vsn, Vsn),
    session(Node, his_tag, HisTag),
    case lists:keyfind(Node, 1, S#state.resolvers) of
        {Node, MyTag, _} ->
            MyTag = session(Node, my_tag),
            {locker, _, _, HisLocker} = InitMsg,
            S#state.the_locker ! {his_the_locker, HisLocker, {Vsn, []}, HisTag, MyTag},
            _ = HisVsn >= ?VERIFY_CONNECT_VSN andalso volatile_send(Node, {init_connect_ack, node(), HisTag, MyTag}),
            S#state{known = (S#state.known)#{{pending, Node} => HisVsn}};
        false ->
            session(Node, pre_connect, {Vsn, InitMsg, HisTag}),
            S
    end.

% A session out of step ends; while Node is up (an integer tag) a new one starts.
restart_connect(Node, MyTag, S0) ->
    S1 = cancel_locker(Node, S0, MyTag),
    reset_session(Node),
    % The connection stays, so no 'DOWN' messages remove the locks and names of Node's processes.
    S2 = delete_node_resources(Node, S1#state{known = maps:remove({pending, Node}, S1#state.known)}),
    case is_integer(MyTag) of
        true -> handle_nodeup(Node, S2);
        false -> S2
    end.

cancel_connect(Node, MyTag, S0) ->
    case session(Node, my_tag) of
        MyTag ->
            S1 = cancel_locker(Node, S0, MyTag),
            reset_session(Node),
            restart_connect(Node, MyTag, S1#state{known = maps:remove({pending, Node}, S1#state.known)});
        _ ->
            S0
    end.

%% The lock is set in this node's partition: its names go to Node, and the exchange continues once Node's came too.
lock_is_set(Node, LockId, S) ->
    gen_server:cast({?SERVER, Node}, {exchange, node(), get_names(), [], session(Node, his_tag)}),
    session(Node, lock_id, LockId),
    case session(Node, wait_lock) of
        {exchange, NameList} ->
            session(Node, wait_lock, lock_is_set),
            exchange(Node, NameList, S);
        undefined ->
            session(Node, wait_lock, lock_is_set),
            S
    end.

%% Node's names: once this node's lock is set too, the session's resolver compares them with this node's.
exchange(Node, NameList, S) ->
    case take_session(Node, wait_lock) of
        lock_is_set ->
            {Node, _, Resolver} = lists:keyfind(Node, 1, S#state.resolvers),
            Resolver ! {resolve, NameList, Node};
        undefined ->
            session(Node, wait_lock, {exchange, NameList})
    end,
    S.

%% The resolver's operations: Resolved goes to Node, Ops wait for Node's resolved message unless it came first.
exchange_ops(Node, Ops, Resolved, S) ->
    Known = mk_known_list(node_vsn(Node, S), S),
    gen_server:cast(
        {?SERVER, Node}, {resolved, node(), Resolved, Known, unused, get_names_ext(), session(Node, his_tag)}
    ),
    Saved = session(Node, save_ops),
    session(Node, save_ops, Ops),
    case Saved of
        {resolved, HisKnown, NamesExt, HisResolved} -> resolved(Node, HisResolved, HisKnown, NamesExt, S);
        undefined -> S
    end.

his_resolved(Node, HisResolved, HisKnown, NamesExt, S) ->
    case session(Node, save_ops) of
        Ops when is_list(Ops) ->
            resolved(Node, HisResolved, HisKnown, NamesExt, S);
        undefined ->
            session(Node, save_ops, {resolved, HisKnown, NamesExt, HisResolved}),
            S
    end.

%% Both sides resolved: the operations run here, the nodes of Node's partition become known and connected, and the
%% locker tells this partition's nodes (new_nodes) before it releases the lock.
resolved(Node, HisResolved, HisKnown, NamesExt, S0) ->
    Ops = take_session(Node, save_ops) ++ HisResolved,
    Known = S0#state.known,
    NewNodes = make_node_vsn_list([Node | HisKnown], S0),
    HisKnownNodes = node_list(HisKnown),
    sync_others(HisKnownNodes),
    ExtraInfo = [{vsn, session(Node, vsn)}, {lock, session(Node, lock_id)}],
    S1 = do_ops(Ops, node(), NamesExt, ExtraInfo, S0),
    notify_syncers([Node], S1),
    S2 = lists:foldl(fun(Other, Acc) -> end_session(Other, no_fun, Acc) end, S1, HisKnownNodes),
    Announce = fun() -> announce_new_nodes(Known, {new_nodes, node(), Ops, NamesExt, NewNodes, ExtraInfo}) end,
    S3 = end_session(Node, Announce, S2),
    {Added, S4} = add_to_known(NewNodes, S3),
    S4#state.the_locker ! {add_to_known, Added},
    S4#state{synced = [Node | S0#state.synced]}.

% Ends Node's session after its names were taken; the locker runs LockerFun before it releases the lock.
end_session(Node, LockerFun, S0) ->
    Tag = session(Node, my_tag),
    S1 = cancel_locker(Node, S0#state{known = maps:remove({pending, Node}, S0#state.known)}, Tag, LockerFun),
    reset_session(Node),
    S1.

% new_nodes to each known node; nodes before version 7 take the new nodes without versions.
announce_new_nodes(Known, {new_nodes, Self, Ops, NamesExt, NewNodes, ExtraInfo} = Message) ->
    maps:foreach(
        fun
            (N, V) when is_atom(N), V >= ?PGPV_VSN ->
                gen_server:cast({?SERVER, N}, Message);
            (N, _) when is_atom(N) ->
                gen_server:cast({?SERVER, N}, {new_nodes, Self, Ops, NamesExt, node_list(NewNodes), ExtraInfo});
            (_, _) ->
                ok
        end,
        Known
    ).

%% A node of this partition joined another: its nodes become known here (each at most once) and connected.
new_nodes(Ops, ConnNode, NamesExt, Nodes, ExtraInfo, S0) ->
    {Added, S1} = add_to_known(Nodes, S0),
    sync_others(Added),
    S2 = do_ops(Ops, ConnNode, NamesExt, ExtraInfo, S1),
    S2#state.the_locker ! {add_to_known, Added},
    S2.

%% A node of the other partition reports that this node is known to it: no session with it is needed.
in_sync(Node, #state{known = Known, synced = Synced} = S0) ->
    notify_syncers([Node], S0),
    S1 = S0#state{known = maps:remove({pending, Node}, Known), synced = add_synced(Node, Synced)},
    S2 = cancel_locker(Node, S1, session(Node, my_tag)),
    reset_session(Node),
    S2.

add_synced(Node, Synced) ->
    case lists:member(Node, Synced) of
        true -> Synced;
        false -> [Node | Synced]
    end.

%% The session's resolver process: the operations for Node's names, conflicts resolved on this node when it is the
%% smaller one.
resolver(Node, Tag) ->
    receive
        {resolve, NameList, Node} ->
            {Ops, Resolved} = exchange_names(NameList, Node, [], []),
            gen_server:cast(?SERVER, {exchange_ops, Node, Tag, Ops, Resolved});
        _ ->
            resolver(Node, Tag)
    end.

% Ops change this node's names to Node's; Resolved tells Node which of this node's names it takes instead.
exchange_names([{Name, Pid, Method} | Tail], Node, Ops, Res) ->
    case ets:lookup(global_names, Name) of
        [{Name, Pid, _, _}] ->
            exchange_names(Tail, Node, Ops, Res);
        [{Name, Pid2, Method2, _}] when node() < Node ->
            {MoreOps, MoreRes} = resolve_conflict(Name, {Pid, Method}, {Pid2, Method2}),
            exchange_names(Tail, Node, MoreOps ++ Ops, MoreRes ++ Res);
        [{Name, _, _, _}] ->
            % The other node resolves this conflict.
            exchange_names(Tail, Node, Ops, Res);
        [] ->
            exchange_names(Tail, Node, [{insert, {Name, Pid, Method}} | Ops], Res)
    end;
exchange_names([], _, Ops, Res) ->
    {Ops, Res}.

% Both partitions registered Name: the method of this node's registration chooses, on the node of its pid.
resolve_conflict(Name, {Pid, Method}, {Pid2, Method2}) ->
    case rpc:call(node(Pid2), ?MODULE, resolve_it, [Method2, Name, Pid, Pid2]) of
        Pid ->
            {[{insert, {Name, Pid, Method}}], []};
        Pid2 ->
            {[], [{insert, {Name, Pid2, Method2}}]};
        none ->
            {[{delete, Name}], [{delete, Name}]};
        {badrpc, Badrpc} ->
            logger:log(info, "global: badrpc ~w received when conflicting name ~tw was found\n", [Badrpc, Name]),
            {[{insert, {Name, Pid, Method}}], []};
        Else ->
            logger:log(info, "global: Resolve method ~w for conflicting name ~tw returned ~tw\n", [Method, Name, Else]),
            {[{delete, Name}], [{delete, Name}]}
    end.

%% Runs a session's operations: external names first (those of Names_ext), then the others, then the deletions.
do_ops(Ops, ConnNode, NamesExt, ExtraInfo, S0) ->
    External = [
        {Name, Pid, RegNode, Method}
     || {Name, Pid, RegNode} <- NamesExt, {insert, {Name2, Pid2, Method}} <- Ops, Name =:= Name2, Pid =:= Pid2
    ],
    S1 = lists:foldl(
        fun({Name, Pid, RegNode, Method}, S) -> ins_name_ext(Name, Pid, Method, RegNode, ConnNode, ExtraInfo, S) end,
        S0,
        External
    ),
    ExternalNames = [Name || {Name, _, _, _} <- External],
    Inserts = [{Name, Pid, Method} || {insert, {Name, Pid, Method}} <- Ops, not lists:member(Name, ExternalNames)],
    S2 = lists:foldl(
        fun({Name, Pid, Method}, S) -> ins_name(Name, Pid, Method, ConnNode, ExtraInfo, S) end, S1, Inserts
    ),
    lists:foldl(fun(Name, S) -> delete_global_name2(Name, S) end, S2, [Name || {delete, Name} <- Ops]).

%% The nodes of the other partition become connected: each checks in with in_sync, or is reported down after the
%% kernel parameter global_connect_retries more attempts (an extra nodedown, since no nodeup came for it).
sync_others(Nodes) ->
    Retries =
        case application:get_env(kernel, global_connect_retries) of
            {ok, N} when is_integer(N), N >= 0 -> N;
            _ -> 0
        end,
    _ = [spawn(fun() -> sync_other(Node, Retries) end) || Node <- Nodes],
    ok.

sync_other(Node, Retries) ->
    erlang:monitor_node(Node, true, [allow_passive_connect]),
    receive
        {nodedown, Node} when Retries > 0 ->
            sync_other(Node, Retries - 1);
        {nodedown, Node} ->
            logger:log(warning, "'global' at ~w failed to connect to ~w\n", [node(), Node]),
            ?SERVER ! {extra_nodedown, Node}
    after 0 ->
        gen_server:cast({?SERVER, Node}, {in_sync, node(), true})
    end.

%% Node status.

% How a visible node's nodedown counts: a lost connection unless the node is outside this node's global group.
participant_down(Node) ->
    case global_group:participant(Node) of
        true -> disconnected;
        false -> ignore_node
    end.

forget_connection(Node, S) -> S#state{known = maps:remove({connection_id, Node}, S#state.known)}.

%% Node is gone from this partition: its session ends, and a lost connection is reported unless partition prevention
%% removed it (What: disconnected, remove_connection or ignore_node).
handle_nodedown(Node, #state{synced = Synced, known = Known0} = S0, What) ->
    S = cancel_locker(Node, S0, session(Node, my_tag)),
    S#state.the_locker ! {remove_from_known, Node},
    reset_session(Node),
    Known1 =
        case What of
            remove_connection ->
                Known0#{{removing, Node} => yes};
            ignore_node ->
                maps:remove({removing, Node}, Known0);
            disconnected ->
                case maps:take({removing, Node}, Known0) of
                    {yes, Rest} ->
                        Rest;
                    error ->
                        inform_connection_loss(Node, S),
                        Known0
                end
        end,
    S#state{known = maps:remove(Node, maps:remove({pending, Node}, Known1)), synced = lists:delete(Node, Synced)}.

%% global_group: Node left this node's group while its connection stays.
group_nodedown(Node, CId, #state{known = Known, conf = #conf{connect_all = CA, prevent_over_part = POP}} = S0) ->
    case maps:get({connection_id, Node}, Known, not_connected) of
        CId ->
            % No 'DOWN' messages come for its processes, whose locks and names go here.
            S1 = delete_node_resources(Node, S0),
            notify_syncers_down(Node, S1),
            What =
                case CA andalso POP andalso global_group:member(Node) of
                    true -> disconnected;
                    false -> ignore_node
                end,
            handle_nodedown(Node, S1, What);
        _ ->
            S0
    end.

%% The disconnect/0 request: every connection global knows within this node's group goes, without lost_connection
%% reports, and the nodes it took down are the reply.
disconnect_known(#state{known = Known} = S0) ->
    Nodes = [N || {connection_id, N} <- maps:keys(Known), is_atom(N), global_group:participant(N)],
    _ = [net_kernel:async_disconnect(N) || N <- Nodes],
    Disconnected = lists:foldl(
        fun(N, S) ->
            receive
                {nodedown, N, _} -> ok
            end,
            forget_connection(N, handle_nodedown(N, S, ignore_node))
        end,
        S0,
        Nodes
    ),
    {Nodes, Disconnected}.

%% Overlapping partitions (plan 30 step DIST8a): a node that lost a connection reports it to every node it knows;
%% each node that still knows the lost node removes it, so that fully connected partitions form.

inform_connection_loss(Node, #state{conf = #conf{connect_all = true, prevent_over_part = true}} = S) ->
    XCreation = get(creation_extension) + erlang:system_info(creation) - ?MIN_SMALL,
    Message = {lost_connection, node(), XCreation, erlang:unique_integer([monotonic]), Node},
    volatile_multicast(Message, Node, ?POP_VSN, true, S);
inform_connection_loss(_, #state{}) ->
    ok.

% NodeA reports losing NodeB: a new report goes on to every known node, and the node to remove (NodeB, or NodeA when
% NodeB is this node) is asked to remove this node.
lost_connection({lost_connection, NodeA, XCreationA, OpIdA, NodeB} = Message, S) ->
    Key = {NodeA, NodeB},
    case lost_connection_info(Key) of
        {XCreationA, OpId, _} when OpIdA =< OpId ->
            S;
        {_, _, Timer} ->
            volatile_multicast(Message, NodeA, ?POP_VSN, true, S),
            save_lost_connection_info(Key, XCreationA, OpIdA, Timer),
            Remove =
                case node() =:= NodeB of
                    true -> NodeA;
                    false -> NodeB
                end,
            remove_lost_node(Remove, S)
    end.

remove_lost_node(Node, S) ->
    case is_node_potentially_known(Node, S) andalso global_group:participant(Node) of
        true ->
            {What, Text} = ask_removal(Node, node_vsn(Node, S)),
            logger:log(
                warning, "'global' at node ~p ~s node ~p in order to prevent overlapping partitions", [
                    node(), Text, Node
                ]
            ),
            handle_nodedown(Node, S, What);
        false ->
            S
    end.

% A node before version 7 is disconnected; a newer one is asked to remove this node itself.
ask_removal(Node, Vsn) when Vsn < ?POP_VSN ->
    net_kernel:async_disconnect(Node),
    {remove_connection, "disconnected old"};
ask_removal(Node, Vsn) ->
    volatile_send(Node, {remove_connection, node()}),
    case global_group:member(Node) andalso Vsn >= ?GG_POP_VSN of
        true -> {ignore_node, "excluded global group member"};
        false -> {remove_connection, "requested disconnect from"}
    end.

%% Node asks to be removed from this node's partition: the connection goes, or only the group membership of a node
%% of this node's global group.
remove_connection(Node, S) ->
    case is_node_potentially_known(Node, S) of
        true ->
            {What, Text} =
                case global_group:member(Node) of
                    true ->
                        global_group ! {disconnect_node, Node},
                        {ignore_node, "excluded global group member"};
                    false ->
                        net_kernel:async_disconnect(Node),
                        {remove_connection, "disconnected"}
                end,
            Next = handle_nodedown(Node, S, What),
            logger:log(
                warning, "'global' at node ~p ~s node ~p in order to prevent overlapping partitions", [
                    node(), Text, Node
                ]
            ),
            Next;
        false ->
            S
    end.

lost_connection_info(Key) ->
    case ets:lookup(global_lost_connections, Key) of
        [{Key, Info}] -> Info;
        _ -> {undefined, undefined, undefined}
    end.

save_lost_connection_info(Key, XCreation, OpId, undefined) ->
    Timer = erlang:start_timer(?LOST_CONNECTION_KEEP, self(), {lost_connection, Key}),
    _ = ets:insert(global_lost_connections, {Key, {XCreation, OpId, Timer}}),
    ok;
save_lost_connection_info(Key, XCreation, OpId, OldTimer) ->
    _ = erlang:cancel_timer(OldTimer, [{async, true}, {info, false}]),
    save_lost_connection_info(Key, XCreation, OpId, undefined).

forget_lost_connection({timeout, Timer, {lost_connection, Key}}) ->
    case ets:lookup(global_lost_connections, Key) of
        [{Key, {_, _, Timer}}] -> _ = ets:delete(global_lost_connections, Key);
        _ -> ok
    end,
    ok.

%% Sends that neither connect nor wait for a connection, to the server on Node or on every known node (and pending
%% ones when AlsoPending) of at least version MinVsn but IgnoreNode.
volatile_send(Node, Message) ->
    _ = erlang:send({?SERVER, Node}, Message, [noconnect]),
    ok.

volatile_multicast(Message, IgnoreNode, MinVsn, AlsoPending, #state{known = Known}) ->
    maps:foreach(
        fun
            (Node, Vsn) when is_atom(Node), Node =/= IgnoreNode, Vsn >= MinVsn ->
                volatile_send(Node, Message);
            ({pending, Node}, Vsn) when AlsoPending, Node =/= IgnoreNode, Vsn >= MinVsn ->
                volatile_send(Node, Message);
            (_, _) ->
                ok
        end,
        Known
    ).

is_node_potentially_known(Node, #state{known = Known}) ->
    maps:is_key(Node, Known) orelse maps:is_key({pending, Node}, Known).

%% Known nodes and their protocol versions.

node_vsn(Node, #state{}) when Node =:= node() ->
    ?VSN;
node_vsn(Node, #state{known = Known}) ->
    case Known of
        #{Node := Vsn} -> Vsn;
        #{{pending, Node} := Vsn} -> Vsn;
        _ -> 0
    end.

node_list(Nodes) -> [node_name(N) || N <- Nodes].

node_name({N, _}) when is_atom(N) -> N;
node_name(N) when is_atom(N) -> N.

% {Node, Vsn} pairs: a node without a version (or version 0) takes the one this node knows.
make_node_vsn_list(Nodes, S) ->
    [
        case N of
            {Node, V} when is_atom(Node), is_integer(V), V =/= 0 -> N;
            _ -> {node_name(N), node_vsn(node_name(N), S)}
        end
     || N <- Nodes
    ].

% The known nodes, as names for a node before version 7, else as {Node, Vsn}; in OTP's order, the map's reversed.
mk_known_list(Vsn, #state{known = Known}) when Vsn < ?PGPV_VSN ->
    lists:reverse([N || {N, _} <- maps:to_list(Known), is_atom(N)]);
mk_known_list(_, #state{known = Known}) ->
    lists:reverse([NV || {N, _} = NV <- maps:to_list(Known), is_atom(N)]).

% Adds nodes (names, version unknown, or {Node, Vsn}) to the known ones; the newly known ones are returned.
add_to_known(Nodes, #state{known = Known0} = S) ->
    Add = fun(Entry, {Added, Known}) ->
        {N, V} =
            case Entry of
                {Node, Vsn} -> {Node, Vsn};
                Node -> {Node, 0}
            end,
        case Known of
            _ when N =:= node() -> {Added, Known};
            #{N := _} -> {Added, Known};
            #{} -> {[N | Added], Known#{N => V}}
        end
    end,
    {Added, Known} = lists:foldl(Add, {[], Known0}, Nodes),
    {Added, S#state{known = Known}}.

%% The locker: told about sessions and known nodes, it holds global's lock while a session exchanges names.

cancel_locker(Node, S, Tag) -> cancel_locker(Node, S, Tag, no_fun).

% The locker releases the session's lock (after running LockerFun); the other node hears of a cancelled session, and
% the session's resolver ends.
cancel_locker(Node, S, Tag, LockerFun) ->
    S#state.the_locker ! {cancel, Node, Tag, LockerFun},
    send_cancel_connect(Node, Tag, S),
    case lists:keytake(Node, 1, S#state.resolvers) of
        {value, {_, Tag, Resolver}, Resolvers} ->
            exit(Resolver, kill),
            S#state{resolvers = Resolvers};
        _ ->
            S
    end.

% A pending session with a node of version 8 and the current tag is cancelled on its side too.
send_cancel_connect(Node, MyTag, #state{known = Known}) ->
    case Known of
        #{{pending, Node} := Vsn} when Vsn >= ?VERIFY_CONNECT_VSN ->
            case session(Node, my_tag) of
                MyTag -> send_cancel_connect_message(Node, session(Node, his_tag));
                _ -> ok
            end;
        _ ->
            ok
    end.

send_cancel_connect_message(Node, HisTag) ->
    _ = erlang:send({?SERVER, Node}, {cancel_connect, node(), HisTag}, [noconnect]),
    ok.

%% Names and locks in the tables.

ins_name(Name, Pid, Method, FromPidOrNode, ExtraInfo, S) ->
    insert_global_name(Name, Pid, Method, FromPidOrNode, ExtraInfo, delete_global_name_keep_pid(Name, S)).

ins_name_ext(Name, Pid, Method, RegNode, FromPidOrNode, ExtraInfo, S0) ->
    S = delete_global_name_keep_pid(Name, S0),
    _ = RegNode =:= node() andalso link(Pid),
    true = ets:insert(global_names_ext, {Name, Pid, RegNode}),
    insert_global_name(Name, Pid, Method, FromPidOrNode, ExtraInfo, S).

insert_global_name(Name, Pid, Method, FromPidOrNode, ExtraInfo, S) ->
    Ref = erlang:monitor(process, Pid, [priority]),
    save_node_resource_info(node(Pid), Ref),
    true = ets:insert(global_names, {Name, Pid, Method, Ref}),
    true = ets:insert(global_pid_names, [{Pid, Name}, {Ref, Name}]),
    case lock_still_set(FromPidOrNode, ExtraInfo, S) of
        true -> S;
        % The node holding the lock went down and up again before this late operation came: the name goes again.
        false -> delete_global_name2(Name, S)
    end.

% Whether global's lock that covered the operation is still held: by the registering process, or by the session the
% new_nodes message names.
lock_still_set(PidOrNode, ExtraInfo, S) ->
    case ets:lookup(global_locks, ?GLOBAL_RID) of
        [{?GLOBAL_RID, _, PidRefs}] when is_pid(PidOrNode) ->
            lists:keymember(PidOrNode, 1, PidRefs);
        [{?GLOBAL_RID, LockReqId, _}] when is_atom(PidOrNode) ->
            {?GLOBAL_RID, LockId} = extra_info(lock, ExtraInfo),
            LockReqId =:= LockId;
        [] ->
            not S#state.global_lock_down
    end.

% ExtraInfo was a list of nodes before version 3.
extra_info(Tag, ExtraInfo) when is_list(ExtraInfo) ->
    case lists:keyfind(Tag, 1, ExtraInfo) of
        {Tag, Info} -> Info;
        false -> undefined
    end;
extra_info(_, _) ->
    undefined.

del_name(Ref, S) ->
    Names = [
        Name
     || {_, Name} <- ets:lookup(global_pid_names, Ref), {_, _, _, Ref1} <- ets:lookup(global_names, Name), Ref1 =:= Ref
    ],
    case Names of
        [Name] -> delete_global_name2(Name, S);
        [] -> S
    end.

% The name's monitor and helper entries go; its global_names entry stays for the insert that replaces it.
delete_global_name_keep_pid(Name, S) ->
    case ets:lookup(global_names, Name) of
        [{Name, Pid, _, Ref}] -> delete_global_name2(Name, Pid, Ref, S);
        [] -> S
    end.

delete_global_name2(Name, S) ->
    case ets:take(global_names, Name) of
        [{Name, Pid, _, Ref}] -> delete_global_name2(Name, Pid, Ref, S);
        [] -> S
    end.

delete_global_name2(Name, Pid, Ref, S) ->
    delete_node_resource_info(Ref),
    true = erlang:demonitor(Ref, [flush]),
    true = ets:delete_object(global_pid_names, {Pid, Name}),
    true = ets:delete_object(global_pid_names, {Ref, Name}),
    case ets:take(global_names_ext, Name) of
        [{Name, Pid, RegNode}] when RegNode =:= node() -> unlink_pid(Pid);
        _ -> ok
    end,
    S.

% An external process stays linked while it has other names or locks here.
unlink_pid(Pid) ->
    case ets:member(global_pid_names, Pid) orelse ets:member(global_pid_ids, Pid) of
        true -> ok;
        false -> unlink(Pid)
    end.

handle_set_lock(Id, Pid, S) ->
    case clause_global_locker:can_set_lock(Id) of
        {true, PidRefs} ->
            case lists:keymember(Pid, 1, PidRefs) of
                true -> {true, S};
                false -> {true, insert_lock(Id, Pid, PidRefs, S)}
            end;
        false ->
            {false, S}
    end.

insert_lock({ResourceId, LockRequesterId}, Pid, PidRefs, S) ->
    Ref = erlang:monitor(process, Pid, [priority]),
    save_node_resource_info(node(Pid), Ref),
    true = ets:insert(global_pid_ids, [{Pid, ResourceId}, {Ref, ResourceId}]),
    true = ets:insert(global_locks, {ResourceId, LockRequesterId, [{Pid, Ref} | PidRefs]}),
    S.

handle_del_lock({ResourceId, LockReqId}, Pid, S) ->
    case ets:lookup(global_locks, ResourceId) of
        [{ResourceId, LockReqId, PidRefs}] -> remove_lock(ResourceId, LockReqId, Pid, PidRefs, false, S);
        _ -> S
    end.

% Pid no longer holds the lock; the lock goes with its last holder. Down records whether global's own lock went with
% a holder's end.
remove_lock(ResourceId, _, Pid, [{Pid, Ref}], Down, S) ->
    delete_node_resource_info(Ref),
    true = erlang:demonitor(Ref, [flush]),
    true = ets:delete(global_locks, ResourceId),
    true = ets:delete_object(global_pid_ids, {Pid, ResourceId}),
    true = ets:delete_object(global_pid_ids, {Ref, ResourceId}),
    case ResourceId of
        ?GLOBAL_RID -> S#state{global_lock_down = Down};
        _ -> S
    end;
remove_lock(ResourceId, LockRequesterId, Pid, PidRefs0, _, S) ->
    PidRefs =
        case lists:keytake(Pid, 1, PidRefs0) of
            {value, {Pid, Ref}, Rest} ->
                delete_node_resource_info(Ref),
                true = erlang:demonitor(Ref, [flush]),
                true = ets:delete_object(global_pid_ids, {Ref, ResourceId}),
                Rest;
            false ->
                PidRefs0
        end,
    true = ets:insert(global_locks, {ResourceId, LockRequesterId, PidRefs}),
    true = ets:delete_object(global_pid_ids, {Pid, ResourceId}),
    S.

% The locks of the process whose monitor Ref fired.
delete_lock(Ref, S0) ->
    Locks = [
        Lock
     || {_, ResourceId} <- ets:lookup(global_pid_ids, Ref),
        {_, _, PidRefs} = Lock <- ets:lookup(global_locks, ResourceId),
        lists:keymember(Ref, 2, PidRefs)
    ],
    lists:foldl(
        fun({ResourceId, LockRequesterId, PidRefs}, S) ->
            {Pid, Ref} = lists:keyfind(Ref, 2, PidRefs),
            remove_lock(ResourceId, LockRequesterId, Pid, PidRefs, true, S)
        end,
        S0,
        Locks
    ).

%% The monitors of each node's processes, so that a node leaving the group while connected loses its names and locks.

save_node_resource_info(Node, Ref) ->
    Refs =
        case ets:lookup(global_node_resources, Node) of
            [] -> #{Ref => ok};
            [{Node, Old}] -> Old#{Ref => ok}
        end,
    true = ets:insert(global_node_resources, [{Node, Refs}, {Ref, Node}]),
    ok.

delete_node_resource_info(Ref) ->
    case ets:take(global_node_resources, Ref) of
        [] ->
            ok;
        [{Ref, Node}] ->
            [{Node, Old}] = ets:lookup(global_node_resources, Node),
            Refs = maps:remove(Ref, Old),
            true =
                case map_size(Refs) of
                    0 -> ets:delete(global_node_resources, Node);
                    _ -> ets:insert(global_node_resources, {Node, Refs})
                end,
            ok
    end.

delete_node_resources(Node, S) ->
    case ets:take(global_node_resources, Node) of
        [] ->
            S;
        [{Node, Refs}] ->
            maps:fold(
                fun(Ref, ok, Acc) ->
                    erlang:demonitor(Ref, [flush]),
                    true = ets:delete(global_node_resources, Ref),
                    del_name(Ref, delete_lock(Ref, Acc))
                end,
                S,
                Refs
            )
    end.

get_names() -> [{Name, Pid, Method} || {Name, Pid, Method, _} <- ets:tab2list(global_names)].

get_names_ext() -> ets:tab2list(global_names_ext).

%% A whereis request of an older node waits while global's lock is set, retried after a random sleep.
do_whereis(Name, From) ->
    case ets:member(global_locks, ?GLOBAL_RID) of
        false ->
            gen_server:reply(From, where(Name));
        true ->
            Server = self(),
            _ = spawn(fun() ->
                random_sleep(5),
                Server ! {whereis, Name, From}
            end),
            ok
    end.

%% Syncers: each sync/0,1 call's process waits until the nodes synced or went down.

notify_syncers(Nodes, S) ->
    _ = [Syncer ! {synced, Nodes} || Syncer <- S#state.syncers],
    ok.

notify_syncers_down(Node, S) ->
    _ = [Syncer ! {nodedown, Node} || Syncer <- S#state.syncers],
    ok.

start_sync(Nodes, From) ->
    spawn_link(fun() ->
        _ = [monitor_node(Node, true) || Node <- Nodes],
        sync_loop(Nodes, From)
    end).

sync_loop([], From) ->
    gen_server:reply(From, ok);
sync_loop(Nodes, From) ->
    receive
        {nodedown, Node} ->
            monitor_node(Node, false),
            sync_loop(lists:delete(Node, Nodes), From);
        {synced, Synced} ->
            _ = [monitor_node(Node, false) || Node <- Synced],
            sync_loop(Nodes -- Synced, From)
    end.

%% The registrar: runs name changes under global's lock on every node known to global, one at a time, so that a name
%% is registered on all of them or none.

registrar_loop() ->
    receive
        {trans_all_known, Fun, From} ->
            gen_server:reply(From, trans_all_known(Fun));
        {'EXIT', _, _} ->
            ok;
        Other ->
            logger:log(
                warning,
                "The global_name_server ~w process received an unexpected message:\n~tp\n",
                [register, Other]
            )
    end,
    registrar_loop().

trans_all_known(Fun) ->
    Id = {?GLOBAL_RID, self()},
    Nodes = set_lock_known(Id, 0),
    try
        Fun(Nodes)
    after
        clause_global_locker:delete_global_lock(Id, Nodes)
    end.

% Locks the known nodes, the boss first as the lockers do; it starts over when nodes became known meanwhile.
set_lock_known(Id, Times) ->
    Known = clause_global_locker:get_known(),
    Nodes = [node() | Known],
    Boss = clause_global_locker:the_boss(Nodes),
    case set_lock_on_nodes(Id, [Boss]) of
        true ->
            case set_lock_on_nodes(Id, Nodes) andalso (clause_global_locker:get_known() -- Known) =:= [] of
                true ->
                    Nodes;
                false ->
                    del_lock(Id, [Boss]),
                    random_sleep(Times),
                    set_lock_known(Id, Times + 1)
            end;
        false ->
            random_sleep(Times),
            set_lock_known(Id, Times + 1)
    end.
