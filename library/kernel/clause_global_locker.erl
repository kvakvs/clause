%% global's locker (plan 30 step DIST8a, docs/distribution.md#global): the process that joins two partitions under
%% global's lock. For each connect session the global_name_server tells it of ({his_the_locker, ...}), it locks this
%% node's partition with a lock id it shares with the other node's locker, so that the nodes common to both partitions
%% are locked by both; the two lockers confirm with lock_set messages passed through the servers. Once both hold the
%% lock it tells the server (lock_is_set) and keeps the lock until the server cancels the session. Requests of nodes on
%% this host are taken first, and an idle locker waits a moment before it tries, so that another node of the partition
%% may lock it instead.
-module(clause_global_locker).
-export([start_link/0, can_set_lock/1, delete_global_lock/2, the_boss/1, get_known/0]).

-define(SERVER, global_name_server).
-define(GLOBAL_RID, global).
-define(VERIFY_CONNECT_VSN, 8).

%% The locker's state: the sessions waiting for a lock, of nodes on this host (local) and the others (remote); a copy
%% of the server's known nodes and the greatest of them and node() (the boss, locked first); whether a lock was just
%% taken, so that the next attempt need not wait.
-record(multi, {local = [], remote = [], known = [], the_boss, just_synced = false}).
%% A session waiting for a lock: the other node and its locker, its protocol version, and both nodes' session tags.
-record(him, {node, locker, vsn, my_tag, his_tag}).

%% The locker of the server that calls this, linked to it.
start_link() ->
    spawn_link(fun() ->
        process_flag(trap_exit, true),
        loop(update_known({add, get_known()}, #multi{}))
    end).

%% The server's known nodes.
get_known() -> gen_server:call(?SERVER, get_known, infinity).

%% Whether this node would grant the lock: {true, PidRefs} with the requester's holders so far, or false when another
%% requester holds the resource.
can_set_lock({ResourceId, LockRequesterId}) ->
    case ets:lookup(global_locks, ResourceId) of
        [{ResourceId, LockRequesterId, PidRefs}] -> {true, PidRefs};
        [{ResourceId, _, _}] -> false;
        [] -> {true, []}
    end.

%% Releases a lock on the nodes, the boss last, so that another locker rarely fails after locking the boss.
delete_global_lock(LockId, Nodes) ->
    Boss = the_boss(Nodes),
    global:del_lock(LockId, lists:delete(Boss, Nodes)),
    global:del_lock(LockId, [Boss]).

the_boss(Nodes) -> lists:max(Nodes).

%% The locker's loop: messages first; with sessions waiting it tries a lock after a pause unless global's lock is set.
loop(S) ->
    receive
        Message -> message(Message, S)
    after 0 ->
        Timeout = pause(S),
        Next = S#multi{just_synced = false},
        receive
            Message -> message(Message, Next)
        after Timeout ->
            case ets:member(global_locks, ?GLOBAL_RID) of
                true -> loop(Next);
                false -> select_node(Next)
            end
        end
    end.

% The pause must be longer than nothing, or another node of the partition rarely gets to lock it after this node
% failed; none right after a success.
pause(#multi{local = [], remote = []}) -> infinity;
pause(#multi{just_synced = true}) -> 0;
pause(#multi{known = []}) -> 200;
pause(#multi{known = Known}) -> min(1000 + 100 * length(Known), 3000).

message({his_the_locker, HisLocker, {HisVsn, _}, HisTag, MyTag}, S) when HisVsn > 4 ->
    Him = #him{node = node(HisLocker), locker = HisLocker, vsn = HisVsn, my_tag = MyTag, his_tag = HisTag},
    loop(add_node(Him, S));
message({cancel, _, undefined, no_fun}, S) ->
    % A cancel without a session may come from an old nodedown, and must not cancel a newer session.
    loop(S);
message({cancel, Node, Tag, no_fun}, S) ->
    loop(remove_node(Node, Tag, S));
message({lock_set, _, false, _, _}, S) ->
    loop(S);
message({lock_set, _, false, _}, S) ->
    loop(S);
message({lock_set, Pid, true, _, MyTag}, S) ->
    asked_to_lock(Pid, MyTag, S);
message({lock_set, Pid, true, _}, S) ->
    case find_node_tag(node(Pid), S) of
        {true, MyTag, _, _} -> asked_to_lock(Pid, MyTag, S);
        false -> refuse(Pid, S)
    end;
message({add_to_known, Nodes}, S) ->
    loop(update_known({add, Nodes}, S));
message({remove_from_known, Node}, S) ->
    loop(update_known({remove, Node}, S));
message({do_trace, _}, S) ->
    loop(S);
message({get_state, _, _} = Request, S) ->
    get_state_reply(Request, the_locker_message, S),
    loop(S);
message({'EXIT', _, _}, S) ->
    % The server ended.
    loop(S);
message(Other, S) ->
    logger:log(warning, "The global_name_server ~w process received an unexpected message:\n~tp\n", [locker, Other]),
    loop(S).

%% The other locker locked its partition first and asks this one to lock its own.
asked_to_lock(Pid, MyTag, S) ->
    Node = node(Pid),
    case find_node_tag(Node, S) of
        {true, MyTag, HisVsn, HisTag} ->
            LockId = locker_lock_id(Pid, HisVsn),
            {IsLockSet, S1} = lock_nodes_safely(LockId, [], S),
            send_lock_set(S1, IsLockSet, Pid, HisVsn, HisTag),
            case IsLockSet of
                true ->
                    gen_server:cast(?SERVER, {lock_is_set, Node, MyTag, LockId}),
                    wait_cancel_lock(
                        Node, LockId, MyTag, [node() | S1#multi.known], the_locker_message_wait_cancel, S1
                    ),
                    loop(remove_node(Node, MyTag, S1#multi{just_synced = true}));
                false ->
                    loop(S1#multi{just_synced = false})
            end;
        _ ->
            refuse(Pid, S)
    end.

refuse(Pid, S) ->
    send_lock_set(S, false, Pid, 5, 0),
    loop(S).

%% Picks a waiting session (this host's first) whose node is not known yet, locks the partition and the other node,
%% and asks the other locker to lock its partition.
select_node(S) ->
    UseRemote = S#multi.local =:= [],
    Waiting =
        case UseRemote of
            true -> exclude_known(S#multi.remote, S#multi.known);
            false -> exclude_known(S#multi.local, S#multi.known)
        end,
    S1 =
        case UseRemote of
            true -> S#multi{remote = Waiting};
            false -> S#multi{local = Waiting}
        end,
    case Waiting of
        [] -> loop(S1);
        _ -> lock_with(random_element(Waiting), S1)
    end.

lock_with(#him{node = Node, locker = HisLocker, vsn = HisVsn, my_tag = MyTag, his_tag = HisTag} = Him, S0) ->
    LockId = locker_lock_id(HisLocker, HisVsn),
    % Locking the other node too prevents a deadlock with its locker.
    case lock_nodes_safely(LockId, [Node], S0) of
        {true, S} ->
            send_lock_set(S, true, HisLocker, HisVsn, HisTag),
            loop(lock_is_set(S, Him, MyTag, [node(), Node | S#multi.known], LockId));
        {false, S} ->
            loop(S)
    end.

% Both lockers use one requester id, the sorted pair of their pids.
locker_lock_id(Pid, Vsn) when Vsn > 4 -> {?GLOBAL_RID, lists:sort([self(), Pid])}.

% Through the other node's server since version 6, with the session tag since version 8.
send_lock_set(S, IsLockSet, HisLocker, Vsn, HisTag) ->
    Message =
        case Vsn < ?VERIFY_CONNECT_VSN of
            true -> {lock_set, self(), IsLockSet, S#multi.known};
            false -> {lock_set, self(), IsLockSet, S#multi.known, HisTag}
        end,
    case Vsn < 6 of
        true -> HisLocker ! Message;
        false -> gen_server:cast({?SERVER, node(HisLocker)}, Message)
    end,
    ok.

%% Locks the boss, then this node and Extra, then the other known nodes, as each step allows; a refusal releases what
%% was locked. This node is only probed before the boss, as locking it could stop a locker that holds the boss.
lock_nodes_safely(LockId, Extra, S0) ->
    First = delete_nonode([S0#multi.the_boss]),
    Probe = [node()] =:= First orelse can_set_lock(LockId) =/= false,
    case Probe andalso global:set_lock(LockId, First, 0) of
        true ->
            S = update_known(S0),
            Second = delete_nonode([node() | Extra] -- First),
            {lock_groups(LockId, [Second, S#multi.known -- First], First), S};
        false ->
            {false, S0}
    end.

lock_groups(_, [], _) ->
    true;
lock_groups(LockId, [Nodes | Rest], Locked) ->
    case global:set_lock(LockId, Nodes, 0) of
        true ->
            lock_groups(LockId, Rest, Locked ++ Nodes);
        false ->
            global:del_lock(LockId, Locked),
            false
    end.

delete_nonode(Nodes) -> lists:delete(nonode@nohost, Nodes).

%% Waits for the other locker's answer: both locked, so the server may exchange names; or a refusal or cancel.
lock_is_set(S, #him{node = Node} = Him, MyTag, Known, LockId) ->
    receive
        {lock_set, P, true, _, MyTag} when node(P) =:= Node ->
            both_locked(S, Him, MyTag, Known, LockId);
        {lock_set, P, true, _} when node(P) =:= Node ->
            both_locked(S, Him, MyTag, Known, LockId);
        {lock_set, P, false, _, MyTag} when node(P) =:= Node ->
            delete_global_lock(LockId, Known),
            S;
        {lock_set, P, false, _} when node(P) =:= Node ->
            delete_global_lock(LockId, Known),
            S;
        {lock_set, P, _, _, _} when node(P) =:= Node ->
            % An answer of an older session.
            lock_is_set(S, Him, MyTag, Known, LockId);
        {cancel, Node, MyTag, Fun} ->
            call_fun(Fun),
            delete_global_lock(LockId, Known),
            remove_node(Node, MyTag, S);
        {get_state, _, _} = Request ->
            get_state_reply(Request, lock_is_set, S),
            lock_is_set(S, Him, MyTag, Known, LockId)
    end.

both_locked(S, #him{node = Node} = Him, MyTag, Known, LockId) ->
    gen_server:cast(?SERVER, {lock_is_set, Node, MyTag, LockId}),
    wait_cancel_lock(Node, LockId, MyTag, Known, lock_is_set_wait_cancel, S),
    S#multi{just_synced = true, local = lists:delete(Him, S#multi.local), remote = lists:delete(Him, S#multi.remote)}.

%% Holds the lock until the server ends the session; its function (the new_nodes messages) runs before the release,
%% so that the other nodes take new_nodes before they see the lock go. Should the other node go down, the server's
%% nodedown cancels the session.
wait_cancel_lock(Node, LockId, MyTag, Known, Where, S) ->
    receive
        {cancel, Node, MyTag, Fun} ->
            call_fun(Fun),
            delete_global_lock(LockId, Known);
        {get_state, _, _} = Request ->
            get_state_reply(Request, Where, S),
            wait_cancel_lock(Node, LockId, MyTag, Known, Where, S)
    end.

call_fun(no_fun) -> ok;
call_fun(Fun) -> Fun().

% The reply to a test's {get_state, From, Ref}: the reference first.
get_state_reply({get_state, From, Ref}, Where, S) ->
    From ! {Ref, Where, S},
    ok.

%% Known nodes, kept in step with the server's add_to_known and remove_from_known messages.

update_known(S) ->
    receive
        {add_to_known, Nodes} -> update_known(update_known({add, Nodes}, S));
        {remove_from_known, Node} -> update_known(update_known({remove, Node}, S))
    after 0 -> S
    end.

update_known({add, Nodes}, S) -> known(Nodes ++ S#multi.known, S);
update_known({remove, Node}, S) -> known(lists:delete(Node, S#multi.known), S).

known(Known, S) -> S#multi{known = Known, the_boss = the_boss([node() | Known])}.

%% Sessions waiting for a lock.

add_node(#him{node = Node} = Him, S) ->
    case is_node_local(Node) of
        true -> S#multi{local = [Him | S#multi.local]};
        false -> S#multi{remote = [Him | S#multi.remote]}
    end.

remove_node(Node, Tag, S) ->
    S#multi{local = remove_node2(Node, Tag, S#multi.local), remote = remove_node2(Node, Tag, S#multi.remote)}.

remove_node2(Node, Tag, Hims) ->
    case lists:splitwith(fun(#him{node = N, my_tag = T}) -> {N, T} =/= {Node, Tag} end, Hims) of
        {Before, [_ | After]} -> Before ++ After;
        {Hims, []} -> Hims
    end.

find_node_tag(Node, S) ->
    case [Him || #him{node = N} = Him <- S#multi.local ++ S#multi.remote, N =:= Node] of
        [#him{my_tag = MyTag, vsn = Vsn, his_tag = HisTag} | _] -> {true, MyTag, Vsn, HisTag};
        [] -> false
    end.

exclude_known(Hims, Known) -> [Him || #him{node = Node} = Him <- Hims, not lists:member(Node, Known)].

random_element(List) ->
    Index = abs(erlang:monotonic_time() bxor erlang:unique_integer()) rem length(List),
    lists:nth(Index + 1, List).

% Whether Node runs on this host.
is_node_local(Node) ->
    {ok, Host} = inet:gethostname(),
    case string:split(atom_to_list(Node), "@", all) of
        [_, Host] -> true;
        _ -> false
    end.
