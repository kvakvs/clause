%% mnesia's transaction manager, registered as mnesia_tm as in OTP (plan 21 step MIM1B,
%% docs/libraries/mnesia.md#transactions). The server gives each transaction its {tid, Counter, Pid} id and the ets
%% stores of its locks and writes, and releases the locks of a transaction whose process exits. Transactions run in
%% the caller with the client functions here: nesting, restarts after refused locks, aborts and the commit. Other
%% nodes' coordinators send it OTP's raw commit requests (MIM1H5, clause_mnesia_commit), and other nodes' dirty
%% writes come to it as OTP's commits (MIM1H6). A transaction that took locks on other nodes ends there too: a commit
%% sends their transaction managers simple_commit, an abort their lock managers release_tid, a restart sync_release_tid
%% (MIM1H7). A transaction writing tables held on other nodes commits there with OTP's protocols (MIM1H8,
%% clause_mnesia_commit); this server votes on and applies other nodes' sym_trans commits and starts the participants
%% of their asym_trans commits (clause_mnesia_participant).
-module(clause_mnesia_tm).
-behaviour(gen_server).
-export([start_link/0, transaction/6, non_transaction/5, transactions/0, send_dirty/5, dirty_results/1]).
-export([apply_commit/3]).
-export([init/1, handle_call/3, handle_cast/2, handle_info/2]).

-include("clause_mnesia.hrl").

%% The server's state: each transaction's stores (innermost first) and the transaction of each process running one;
%% the other nodes' sym_trans commits voted on here and waiting for their outcome, as #pending{}, and the participant
%% process of each asym_trans commit (MIM1H8). The last transaction serial is OTP's {trans_tid, serial, N} row of
%% mnesia_decision, kept in DECISION_TAB.LOG.
-record(state, {stores = #{}, tids = #{}, pending = #{}, participants = #{}}).

%% A sym_trans commit voted on here (MIM1H8): its coordinator and the monitor of it, its protocol, this node's commit
%% and the nodes taking part.
-record(pending, {coordinator, monitor, protocol, commit, nodes}).

start_link() ->
    gen_server:start_link({local, mnesia_tm}, ?MODULE, [], []).

% system_info(transactions): {Counter, Pid, coordinator} for each transaction running.
transactions() ->
    gen_server:call(mnesia_tm, info, infinity).

%% Client side.

% A transaction: an outer one, an inner one nested in the caller's, or one inside a dirty activity, which then goes on.
transaction(OldState, Fun, Args, Retries, Mod, Kind) ->
    case OldState of
        undefined ->
            outer(Mod, Fun, Args, Retries, Kind);
        {_, _, non_transaction} ->
            Result = outer(Mod, Fun, Args, Retries, Kind),
            put(mnesia_activity_state, OldState),
            Result;
        {OldMod, Tid, #tidstore{} = Ts} ->
            inner(Mod, Tid, OldMod, Ts, Fun, Args, Retries, Kind);
        _ ->
            {aborted, nested_transaction}
    end.

outer(Mod, Fun, Args, Retries, Kind) ->
    case request(start_outer) of
        {new_tid, Tid, Store} ->
            put(mnesia_activity_state, {Mod, Tid, #tidstore{store = Store}}),
            execute(Fun, Args, 1, Retries, Kind);
        {error, Reason} ->
            {aborted, Reason}
    end.

% An inner transaction works on a copy of the outer store; its commit makes the copy the outer store.
inner(Mod, Tid, OldMod, #tidstore{store = Outer, up_stores = Up, level = Level} = Ts, Fun, Args, Retries, Kind) ->
    case request({add_store, Tid}) of
        {new_store, Store} ->
            ets:insert(Store, ets:tab2list(Outer)),
            Inner = Ts#tidstore{store = Store, up_stores = [{OldMod, Outer} | Up], level = Level + 1},
            put(mnesia_activity_state, {Mod, Tid, Inner}),
            execute(Fun, Args, 1, Retries, Kind);
        {error, Reason} ->
            {aborted, Reason}
    end.

execute(Fun, Args, Factor, Retries, Kind) ->
    try
        Result = apply(Fun, Args),
        {commit(Kind), Result}
    of
        {outer, Value} ->
            increment(trans_commits),
            left(),
            {atomic, Value};
        {inner, Value} ->
            increment(trans_commits),
            {atomic, Value}
    catch
        throw:Value -> return_abort({aborted, {throw, Value}});
        error:Reason:Stack -> check_exit(Fun, Args, Factor, Retries, Kind, {Reason, Stack});
        exit:Reason -> check_exit(Fun, Args, Factor, Retries, Kind, Reason)
    end.

% An ended outer transaction leaves no activity.
left() ->
    erase(mnesia_activity_state),
    safe(fun() -> unlink(whereis(mnesia_tm)) end).

% A refused lock restarts the outer transaction while retries are left; any other reason aborts.
check_exit(Fun, Args, Factor, Retries, Kind, Reason) ->
    case Reason of
        {aborted, #cyclic{} = Why} -> maybe_restart(Fun, Args, Factor, Retries, Kind, Why);
        {aborted, {node_not_running, _} = Why} -> maybe_restart(Fun, Args, Factor, Retries, Kind, Why);
        _ -> return_abort(Reason)
    end.

maybe_restart(Fun, Args, Factor, Retries, Kind, Why) ->
    {Mod, Tid, Ts} = get(mnesia_activity_state),
    case try_again(Retries) of
        true when Ts#tidstore.level =:= 1 -> restart(Mod, Tid, Ts, Fun, Args, Factor, decr(Retries), Kind, Why);
        true -> return_abort(Why);
        false -> return_abort({aborted, nomore})
    end.

try_again(infinity) -> true;
try_again(N) -> N > 1.

decr(infinity) -> infinity;
decr(N) -> max(N - 1, 0).

% The same transaction runs again after its locks go and a pause that grows with each restart; a node that stopped
% starts a new transaction.
restart(Mod, Tid, Ts, Fun, Args, Factor, Retries, Kind, Why) ->
    increment(trans_restarts),
    case Why of
        {node_not_running, _} ->
            return_abort(Why),
            timer:sleep(pause(1)),
            outer(Mod, Fun, Args, Retries, Kind);
        _ ->
            clause_mnesia_locker:release_tid_sync(Tid),
            clause_mnesia_locker:sync_release_remote(Tid, clause_mnesia_locker:lock_nodes(Ts#tidstore.store)),
            {restarted, Tid} = request({restart, Tid, Ts#tidstore.store}),
            timer:sleep(pause(Factor)),
            execute(Fun, Args, Factor + 1, Retries, Kind)
    end.

pause(Factor) ->
    Square = Factor * Factor,
    Square + rand:uniform(max(1, trunc(500 * (1 - 50 / (Square + 50))))).

% An aborted transaction: an outer one releases its locks, an inner one hands the outer store back and passes a
% restart on to the outer transaction.
return_abort(Reason) ->
    {_, Tid, #tidstore{store = Store, up_stores = Up, level = Level} = Ts} = get(mnesia_activity_state),
    safe(fun() -> increment(trans_failures) end),
    case Up of
        [] ->
            clause_mnesia_locker:release_tid(Tid),
            clause_mnesia_locker:release_remote(Tid, clause_mnesia_locker:lock_nodes(Store)),
            gen_server:cast(mnesia_tm, {delete_transaction, Tid}),
            left(),
            {aborted, fix_error(Reason)};
        [{OldMod, Outer} | Rest] ->
            ets:insert(Outer, [{nodes, Node} || Node <- clause_mnesia_locker:lock_nodes(Store)]),
            request({del_store, Tid, Outer, Store}),
            put(mnesia_activity_state, {OldMod, Tid, Ts#tidstore{store = Outer, up_stores = Rest, level = Level - 1}}),
            restarts(Reason) andalso exit({aborted, Reason}),
            {aborted, fix_error(Reason)}
    end.

restarts(#cyclic{}) -> true;
restarts({node_not_running, _}) -> true;
restarts(_) -> false.

% An abort reason as transaction/1 returns it.
fix_error({aborted, Reason}) -> Reason;
fix_error({abort, Reason}) -> Reason;
fix_error(Reason) -> Reason.

% The commit: mnesia_tm applies an outer transaction's writes held here and releases its locks, so that a commit
% completes even if the transaction's process dies meanwhile; the other nodes holding written tables take part
% (MIM1H8), the others it locked end it; an inner transaction hands its store to the outer one. A sync transaction
% logs synchronously (MIM1H1).
commit(Kind) ->
    case get(mnesia_activity_state) of
        {_, Tid, #tidstore{store = Store, up_stores = []}} ->
            check_snmp_keys(Store),
            Locked = clause_mnesia_locker:lock_nodes(Store),
            Local = fun(Decision) -> committed = request({commit, Tid, Kind, Decision}) end,
            Schema = [N || {_, N} <- ets:lookup(Store, schema_participant)],
            Participants = Schema ++ clause_mnesia_commit:commit(Tid, Store, Kind, Local),
            [{mnesia_tm, Node} ! {Tid, simple_commit} || Node <- Locked -- Participants],
            outer;
        {_, Tid, #tidstore{store = Store, up_stores = [{OldMod, Outer} | Rest], level = Level} = Ts} ->
            request({del_store, Tid, Store, Outer}),
            put(mnesia_activity_state, {OldMod, Tid, Ts#tidstore{up_stores = Rest, level = Level - 1}}),
            inner
    end.

% Each written key of a table with an SNMP index makes a row index, or the transaction aborts (MIM1G).
check_snmp_keys(Store) ->
    Oids = ets:select(Store, [{{'$1', '_', '_'}, [{is_tuple, '$1'}, {'=:=', {size, '$1'}, 2}], ['$1']}]),
    [clause_mnesia_snmp:check(Tab, Key) || {Tab, Key} <- lists:usort(Oids)],
    ok.

% Each written record's operations of the tables held here, after the tables it clears; those of disc tables logged
% first (MIM1D), under the transaction's decision when it wrote a majority table (MIM1H2) or committed with
% asym_trans (MIM1H10a). A transaction that wrote, or changed the schema, completes an activity (MIM1E).
apply_writes(Tid, Store, Kind, Decision) ->
    Updates = [U || {Tab, _, _} = U <- clause_mnesia_commit:updates(Store), held(Tab)],
    safe(fun() -> clause_mnesia_disc:log(Updates, Kind, decision(Tid, Updates, Decision)) end),
    [safe(fun() -> clause_mnesia_access:update(Tid, Tab, Key, Update) end) || {Tab, Key, Update} <- Updates],
    (Updates =/= [] orelse ets:member(Store, op)) andalso clause_mnesia_subscr:report_activity(Tid),
    ok.

decision(_, _, #decision{} = Decision) ->
    Decision;
decision(Tid, Updates, _) ->
    case lists:any(fun majority/1, lists:usort([Tab || {Tab, _, _} <- Updates])) of
        true -> Tid;
        false -> presume_commit
    end.

majority(Tab) -> (clause_mnesia_monitor:val({Tab, cstruct}))#cstruct.majority.

held(Tab) ->
    try
        clause_mnesia_monitor:val({Tab, storage_type}) =/= unknown
    catch
        error:_ -> false
    end.

% Another node's commit of a transaction (MIM1H8): its records applied as this node's own commits apply them, with
% the transaction's table events and its completed activity, logged under its asym_trans decision (MIM1H10a); a commit
% of schema operations alone is logged in mnesia_monitor.
apply_commit(Tid, #commit{ram_copies = Ram, disc_copies = Disc, disc_only_copies = DiscOnly} = Commit, Kind) ->
    Updates = [U || Item <- Ram ++ Disc ++ DiscOnly, U <- [clause_mnesia_commit:update(Item)], U =/= none],
    Decision = decision(Tid, Updates, Commit#commit.decision),
    (Updates =/= [] orelse Commit#commit.schema_ops =:= []) andalso
        safe(fun() -> clause_mnesia_disc:log(Updates, Kind, Decision) end),
    [safe(fun() -> clause_mnesia_access:update(Tid, Tab, Key, Update) end) || {Tab, Key, Update} <- Updates],
    Updates =/= [] andalso clause_mnesia_subscr:report_activity(Tid),
    ok.

% A dirty commit's updates of this node's replicas, logged first; the last update's result, ok when none succeeds.
apply_dirty(Tid, #commit{ram_copies = Ram, disc_copies = Disc, disc_only_copies = DiscOnly}, Kind) ->
    Updates = [U || Item <- Ram ++ Disc ++ DiscOnly, U <- [clause_mnesia_commit:update(Item)], U =/= none],
    safe(fun() -> clause_mnesia_disc:log(Updates, Kind, presume_commit) end),
    lists:foldl(fun(Update, Result) -> dirty_update(Tid, Update, Result) end, ok, Updates).

dirty_update(Tid, {Tab, Key, Update}, Result) ->
    try
        clause_mnesia_access:update(Tid, Tab, Key, Update)
    catch
        _:_ -> Result
    end.

% Runs F, ignoring its failure: a table or mnesia itself may have gone meanwhile.
safe(F) ->
    try
        F(),
        ok
    catch
        _:_ -> ok
    end.

% A dirty activity: mnesia's functions run dirty; inside a transaction it is a transaction whose abort is an exit.
non_transaction({_, _, Trans} = OldState, Fun, Args, ActivityKind, Mod) when Trans =/= non_transaction ->
    Kind =
        case ActivityKind of
            sync_dirty -> sync;
            _ -> async
        end,
    case transaction(OldState, Fun, Args, infinity, Mod, Kind) of
        {atomic, Result} -> Result;
        {aborted, Reason} -> exit(Reason)
    end;
non_transaction(OldState, Fun, Args, ActivityKind, Mod) ->
    put(mnesia_activity_state, {Mod, {ActivityKind, self()}, non_transaction}),
    try apply(Fun, Args) of
        {'EXIT', Reason} -> exit(Reason);
        {aborted, Reason} -> mnesia:abort(Reason);
        Result -> Result
    catch
        throw:Thrown -> throw(Thrown);
        error:Reason:Stack -> exit({Reason, Stack});
        exit:Reason -> exit(Reason)
    after
        case OldState of
            undefined -> erase(mnesia_activity_state);
            _ -> put(mnesia_activity_state, OldState)
        end
    end.

% A dirty update sent to other nodes' replicas as OTP's commits (MIM1H6): async_dirty ones are only sent, sync_dirty
% ones are awaited by dirty_results/1, each through a monitor whose alias takes the reply.
send_dirty(Protocol, Tab, Key, Update, Replicas) ->
    Tid = {dirty, self()},
    [
        send_commit(Protocol, Tid, Tab, Node, dirty_commit(Node, Storage, Tab, Key, Update))
     || {Node, Storage} <- Replicas
    ].

send_commit(async_dirty, Tid, Tab, Node, Commit) ->
    {mnesia_tm, Node} ! {self(), {async_dirty, Tid, Commit, Tab}},
    sent;
send_commit(sync_dirty, Tid, Tab, Node, Commit) ->
    Ref = erlang:monitor(process, {mnesia_tm, Node}, [{alias, reply_demonitor}]),
    {mnesia_tm, Node} ! {Ref, {sync_dirty, Tid, Commit, Tab}},
    {Node, Ref}.

% A commit of one update on Node, its item in the field of the replica's storage there.
dirty_commit(Node, Storage, Tab, Key, Update) ->
    Commit = #commit{node = Node, decision = presume_commit},
    Item = clause_mnesia_disc:item(Tab, Key, Update),
    case Storage of
        ram_copies -> Commit#commit{ram_copies = [Item]};
        disc_copies -> Commit#commit{disc_copies = [Item]};
        disc_only_copies -> Commit#commit{disc_only_copies = [Item]}
    end.

% The results of the sync_dirty commits sent; a node that goes meanwhile gives OTP's {'EXIT', {aborted, Reason}}
% result, not an abort.
dirty_results(Sent) ->
    [dirty_result(Node, Ref) || {Node, Ref} <- Sent].

dirty_result(Node, Ref) ->
    receive
        {mnesia_tm, Node, {dirty_res, Result}} -> Result;
        {'DOWN', Ref, process, _, _} -> {'EXIT', {aborted, {node_not_running, Node}}}
    end.

request(Request) ->
    case whereis(mnesia_tm) of
        undefined -> {error, {node_not_running, node()}};
        Pid -> gen_server:call(Pid, Request, infinity)
    end.

increment(Counter) ->
    clause_mnesia_monitor:increment(Counter).

%% Server side.

init([]) ->
    process_flag(trap_exit, true),
    {ok, #state{}}.

% A new outer transaction: a store and the next tid, and a link that releases its locks if its process exits.
handle_call(start_outer, {Pid, _}, #state{stores = Stores, tids = Tids} = State) ->
    link(Pid),
    Tid = #tid{counter = ets:update_counter(mnesia_decision, serial, {3, 1}), pid = Pid},
    Store = new_store(),
    {reply, {new_tid, Tid, Store}, State#state{stores = Stores#{Tid => [Store]}, tids = Tids#{Pid => Tid}}};
handle_call({add_store, Tid}, _From, #state{stores = Stores} = State) ->
    Store = new_store(),
    {reply, {new_store, Store}, State#state{stores = Stores#{Tid := [Store | maps:get(Tid, Stores)]}}};
handle_call({del_store, Tid, _Current, Obsolete}, _From, #state{stores = Stores} = State) ->
    ets:delete(Obsolete),
    {reply, store_erased, State#state{stores = Stores#{Tid := lists:delete(Obsolete, maps:get(Tid, Stores))}}};
handle_call({restart, Tid, Store}, _From, #state{stores = Stores} = State) ->
    [ets:delete(S) || S <- maps:get(Tid, Stores), S =/= Store],
    ets:delete_all_objects(Store),
    {reply, {restarted, Tid}, State#state{stores = Stores#{Tid := [Store]}}};
handle_call({commit, #tid{pid = Pid} = Tid, Kind, Decision}, _From, #state{stores = Stores} = State) ->
    [Store | _] = maps:get(Tid, Stores),
    apply_writes(Tid, Store, Kind, Decision),
    clause_mnesia_locker:release_tid(Tid),
    unlink(Pid),
    {reply, committed, forget(Tid, State)};
handle_call(info, _From, #state{stores = Stores} = State) ->
    {reply, [{C, Pid, coordinator} || #tid{counter = C, pid = Pid} <- lists:sort(maps:keys(Stores))], State}.

handle_cast({delete_transaction, Tid}, State) ->
    unlink(Tid#tid.pid),
    {noreply, forget(Tid, State)};
handle_cast({participant_ended, Tid}, #state{participants = Participants} = State) ->
    {noreply, State#state{participants = maps:remove(Tid, Participants)}}.

% A process that exits in a transaction leaves no locks behind.
handle_info({'EXIT', Pid, _}, #state{tids = Tids, stores = Stores} = State) ->
    case Tids of
        #{Pid := Tid} ->
            clause_mnesia_locker:release_tid(Tid),
            abort_remote(Tid, store_nodes(Tid, Stores)),
            {noreply, forget(Tid, State)};
        _ ->
            {noreply, State}
    end;
% Another node's coordinator asks this node to take part in a commit (MIM1H5, MIM1H8): an asym_trans participant
% process of its own, or a sym_trans vote here; or it ends a transaction that only took locks here. This node's next
% transactions come after it.
handle_info({From, {ask_commit, Protocol, #tid{counter = Counter} = Tid, Commit, DiscNodes, RamNodes}}, State) ->
    clause_mnesia_recover:sync_serial(Counter),
    Nodes = DiscNodes ++ RamNodes,
    case Protocol of
        asym_trans ->
            Pid = clause_mnesia_participant:start(From, Tid, Commit, Nodes),
            {noreply, State#state{participants = (State#state.participants)#{Tid => Pid}}};
        _ ->
            {noreply, vote(From, Protocol, Tid, Commit, Nodes, State)}
    end;
handle_info({#tid{} = Tid, do_commit}, #state{pending = Pending} = State) ->
    case maps:take(Tid, Pending) of
        {Voted, Rest} ->
            do_commit(Tid, Voted),
            {noreply, State#state{pending = Rest}};
        error ->
            {noreply, State}
    end;
handle_info({#tid{counter = Counter} = Tid, simple_commit}, State) ->
    clause_mnesia_recover:sync_serial(Counter),
    clause_mnesia_locker:release_tid(Tid),
    {noreply, State};
% Another node's transaction that took locks here ended without a decision (MIM1H7): its locks go, its sym_trans
% commit voted on here is dropped and its asym_trans participant hears the abort (MIM1H8).
handle_info({#tid{} = Tid, {do_abort, _} = Abort}, #state{pending = Pending, participants = Participants} = State) ->
    clause_mnesia_locker:release_tid(Tid),
    case maps:take(Tid, Pending) of
        {#pending{monitor = Ref}, Rest} ->
            erlang:demonitor(Ref, [flush]),
            clause_mnesia_recover:note_decision(Tid, aborted),
            {noreply, State#state{pending = Rest}};
        error ->
            [Pid ! {Tid, Abort} || Pid <- [maps:get(Tid, Participants, none)], is_pid(Pid)],
            {noreply, State}
    end;
% The coordinator of a sym_trans commit voted on here went before deciding it: a process asks the other nodes taking
% part, then tells this server the outcome.
handle_info({'DOWN', Ref, process, _, _}, #state{pending = Pending} = State) ->
    [resolve(Tid, Voted) || {Tid, #pending{monitor = R} = Voted} <- maps:to_list(Pending), R =:= Ref],
    {noreply, State};
% Another node's dirty write of this node's replicas (MIM1H6): applied and logged here, the result answered to a
% sync_dirty one.
handle_info({From, {sync_dirty, Tid, Commit, _Tab}}, State) ->
    From ! {mnesia_tm, node(), {dirty_res, apply_dirty(Tid, Commit, sync)}},
    {noreply, State};
handle_info({_From, {async_dirty, Tid, Commit, _Tab}}, State) ->
    _ = apply_dirty(Tid, Commit, async),
    {noreply, State};
handle_info(_, State) ->
    {noreply, State}.

% This node votes yes on another node's sym_trans commit and waits for its outcome, watching the coordinator.
vote(Coordinator, Protocol, Tid, Commit, Nodes, #state{pending = Pending} = State) ->
    Ref = erlang:monitor(process, Coordinator),
    Coordinator ! {mnesia_tm, node(), {vote_yes, Tid}},
    Voted = #pending{coordinator = Coordinator, monitor = Ref, protocol = Protocol, commit = Commit, nodes = Nodes},
    State#state{pending = Pending#{Tid => Voted}}.

% A voted sym_trans commit applied here; a sync one confirms it.
do_commit(Tid, #pending{coordinator = Coordinator, monitor = Ref, protocol = Protocol, commit = Commit}) ->
    erlang:demonitor(Ref, [flush]),
    Kind =
        case Protocol of
            sync_sym_trans -> sync;
            _ -> async
        end,
    apply_commit(Tid, Commit, Kind),
    clause_mnesia_recover:note_decision(Tid, committed),
    clause_mnesia_locker:release_tid(Tid),
    Protocol =:= sync_sym_trans andalso (Coordinator ! {mnesia_tm, node(), {committed, Tid}}),
    ok.

resolve(Tid, #pending{coordinator = Coordinator, nodes = Nodes}) ->
    _ = spawn(fun() ->
        Outcome =
            case clause_mnesia_participant:outcome(Tid, Nodes) of
                committed -> do_commit;
                aborted -> {do_abort, {mnesia_down, node(Coordinator)}}
            end,
        mnesia_tm ! {Tid, Outcome}
    end),
    ok.

% The other nodes where any of a transaction's stores took locks.
store_nodes(Tid, Stores) ->
    lists:usort(lists:append([clause_mnesia_locker:lock_nodes(Store) || Store <- maps:get(Tid, Stores, [])])).

% A transaction whose process died had locks on Nodes: each node is asked what happened to it, then told to abort it,
% as OTP's transaction manager does, from a process of its own (MIM1H7).
abort_remote(_, []) ->
    ok;
abort_remote(Tid, Nodes) ->
    _ = spawn(fun() ->
        [
            safe(fun() -> gen_server:call({mnesia_recover, Node}, {what_happened, unclear, Tid}, infinity) end)
         || Node <- Nodes
        ],
        [{mnesia_tm, Node} ! {Tid, {do_abort, {mnesia_down, node()}}} || Node <- Nodes]
    end),
    ok.

forget(#tid{pid = Pid} = Tid, #state{stores = Stores, tids = Tids} = State) ->
    [ets:delete(S) || S <- maps:get(Tid, Stores, [])],
    Remaining =
        case Tids of
            #{Pid := Tid} -> maps:remove(Pid, Tids);
            _ -> Tids
        end,
    State#state{stores = maps:remove(Tid, Stores), tids = Remaining}.

new_store() ->
    ets:new(mnesia_trans_store, [bag, public]).
