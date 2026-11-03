%% mnesia's transaction manager, registered as mnesia_tm as in OTP (plan 21 step MIM1B,
%% docs/libraries/mnesia.md#transactions). The server gives each transaction its {tid, Counter, Pid} id and the ets
%% stores of its locks and writes, and releases the locks of a transaction whose process exits. Transactions run in
%% the caller with the client functions here: nesting, restarts after refused locks, aborts and the commit.
-module(clause_mnesia_tm).
-behaviour(gen_server).
-export([start_link/0, transaction/6, non_transaction/5, transactions/0]).
-export([init/1, handle_call/3, handle_cast/2, handle_info/2]).

-include("clause_mnesia.hrl").

%% The server's state: each transaction's stores (innermost first) and the transaction of each process running one.
%% The last transaction serial is OTP's {trans_tid, serial, N} row of mnesia_decision, kept in DECISION_TAB.LOG.
-record(state, {stores = #{}, tids = #{}}).

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
            gen_server:cast(mnesia_tm, {delete_transaction, Tid}),
            left(),
            {aborted, fix_error(Reason)};
        [{OldMod, Outer} | Rest] ->
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

% The commit: mnesia_tm applies an outer transaction's writes and releases its locks, so that a commit completes even
% if the transaction's process dies meanwhile; an inner transaction hands its store to the outer one. A sync
% transaction logs synchronously (MIM1H1).
commit(Kind) ->
    case get(mnesia_activity_state) of
        {_, Tid, #tidstore{store = Store, up_stores = []}} ->
            check_snmp_keys(Store),
            committed = request({commit, Tid, Kind}),
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

% Each written record's operations, in the order the transaction made them, after the tables it clears; those of disc
% tables logged first (MIM1D). A transaction that wrote, or changed the schema, completes an activity (MIM1E).
apply_writes(Tid, Store, Kind) ->
    Clears = ets:match_object(Store, {'_', '_', clear_table}),
    Oids = lists:usort(ets:select(Store, [{{'$1', '_', '_'}, [{is_tuple, '$1'}, {'=:=', {size, '$1'}, 2}], ['$1']}])),
    Ops = Clears ++ [Op || Oid <- Oids, Op <- ets:lookup(Store, Oid), element(3, Op) =/= clear_table],
    Updates = [U || Op <- Ops, U <- [table_update(Op)], U =/= none],
    safe(fun() -> clause_mnesia_disc:log(Updates, Kind) end),
    [safe(fun() -> clause_mnesia_access:update(Tid, Tab, Key, Update) end) || {Tab, Key, Update} <- Updates],
    (Updates =/= [] orelse ets:member(Store, op)) andalso clause_mnesia_subscr:report_activity(Tid),
    ok.

table_update({{Tab, Key}, Val, write}) -> {Tab, Key, {write, Val}};
table_update({{Tab, Key}, _, delete}) -> {Tab, Key, delete};
table_update({{Tab, Key}, Val, delete_object}) -> {Tab, Key, {delete_object, Val}};
table_update({{Tab, Key}, Pattern, clear_table}) -> {Tab, Key, {clear_table, Pattern}};
table_update(_) -> none.

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
handle_call({commit, #tid{pid = Pid} = Tid, Kind}, _From, #state{stores = Stores} = State) ->
    [Store | _] = maps:get(Tid, Stores),
    apply_writes(Tid, Store, Kind),
    clause_mnesia_locker:release_tid(Tid),
    unlink(Pid),
    {reply, committed, forget(Tid, State)};
handle_call(info, _From, #state{stores = Stores} = State) ->
    {reply, [{C, Pid, coordinator} || #tid{counter = C, pid = Pid} <- lists:sort(maps:keys(Stores))], State}.

handle_cast({delete_transaction, Tid}, State) ->
    unlink(Tid#tid.pid),
    {noreply, forget(Tid, State)}.

% A process that exits in a transaction leaves no locks behind.
handle_info({'EXIT', Pid, _}, #state{tids = Tids} = State) ->
    case Tids of
        #{Pid := Tid} ->
            clause_mnesia_locker:release_tid(Tid),
            {noreply, forget(Tid, State)};
        _ ->
            {noreply, State}
    end;
handle_info(_, State) ->
    {noreply, State}.

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
