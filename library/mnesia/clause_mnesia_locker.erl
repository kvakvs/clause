%% mnesia's lock manager, registered as mnesia_locker as in OTP (plan 21 step MIM1B, docs/libraries/mnesia.md#locks):
%% read and write locks on records, whole tables and global items for transactions. A conflicting request of an older
%% transaction waits in a queue; a younger one is refused with a #cyclic{} reason and its transaction restarts.
%% Transactions call the client functions here, in their own process; the server keeps the locks and the queue.
-module(clause_mnesia_locker).
-behaviour(gen_server).
-export([start_link/0, held_locks/0, lock_queue/0, release_tid/1, release_tid_sync/1]).
-export([rlock/3, rlock_table/3, wlock/3, wlock_table/3, wlock_no_exist/4, load_lock_table/3, rwlock/3]).
-export([sticky_wlock/3, sticky_wlock_table/3, sticky_rwlock/3, global_lock/5]).
-export([init/1, handle_call/3, handle_cast/2]).

-include("clause_mnesia.hrl").

%% The key of a whole table's lock and of a global item's table, as OTP spells them.
-define(ALL, '______WHOLETABLE_____').
-define(GLOBAL, '______GLOBAL_____').
-define(STICK, '______STICK_____').

%% A queued request: the lock it waits for, the transaction, its operation, who to answer and the holder it waits on.
-record(queue, {oid, tid, op, from, lucky}).
%% The server's state: the queue of waiting requests, oldest first, and the tables stuck to this node by sticky locks.
-record(state, {queue = [] :: [#queue{}], sticky = [] :: [atom()]}).

start_link() ->
    gen_server:start_link({local, mnesia_locker}, ?MODULE, [], []).

% system_info(held_locks): the {Oid, Lock, Tid} locks held, in their order.
held_locks() ->
    gen_server:call(mnesia_locker, held_locks, infinity).

% system_info(lock_queue): the {Oid, Op, Pid, Tid, WaitingFor} requests waiting.
lock_queue() ->
    gen_server:call(mnesia_locker, lock_queue, infinity).

% A transaction's locks go and the requests waiting for them are tried again.
release_tid(Tid) ->
    gen_server:cast(mnesia_locker, {release_tid, Tid}).

release_tid_sync(Tid) ->
    gen_server:call(mnesia_locker, {release_tid, Tid}, infinity).

%% Client side: the locks of a transaction, each asked for once and remembered in its store as {{locks, Tab, Key}, Lock}.

% A read lock on a record returns its records; on a table ok when taken, its node when held already, as OTP's do.
rlock(Tid, Store, {Tab, Key} = Oid) ->
    Node = readable(Tab),
    case need_lock(Store, Tab, Key, '_') of
        yes ->
            granted(request({read, Tid, Oid}), Oid),
            ets:insert(Store, {{locks, Tab, Key}, read}),
            case Oid of
                {_, ?ALL} -> ok;
                {?GLOBAL, _} -> ok;
                _ -> read_result(Oid, Node)
            end;
        no ->
            read_result(Oid, Node)
    end.

rlock_table(Tid, Store, Tab) ->
    rlock(Tid, Store, {Tab, ?ALL}).

% A write lock returns the nodes written; a lock already held returns [], or the table's majority flag for a table
% lock, as OTP's does.
wlock(Tid, Store, Oid) ->
    wlock(Tid, Store, Oid, write).

wlock(Tid, Store, {Tab, Key} = Oid, Op) ->
    case need_lock(Store, Tab, Key, write) of
        yes ->
            {Nodes, _Majority} = writers(Tab),
            ets:insert(Store, {{locks, Tab, Key}, write}),
            granted(request({Op, Tid, Oid}), Oid),
            Nodes;
        no when Key =/= ?ALL, Tab =/= ?GLOBAL ->
            [];
        no ->
            element(2, writers(Tab))
    end.

wlock_table(Tid, Store, Tab) ->
    wlock(Tid, Store, {Tab, ?ALL}).

% A table write lock taken while the table has active replicas, whether it is written anywhere or not, as
% change_table_access_mode/2 takes it (MIM1C).
wlock_no_exist(_, _, _, []) ->
    ok;
wlock_no_exist(Tid, Store, Tab, _Active) ->
    case need_lock(Store, Tab, ?ALL, write) of
        yes ->
            ets:insert(Store, {{locks, Tab, ?ALL}, write}),
            granted(request({write, Tid, {Tab, ?ALL}}), {Tab, ?ALL});
        no ->
            ok
    end.

load_lock_table(Tid, Store, Tab) ->
    wlock(Tid, Store, {Tab, ?ALL}).

% wread: a write lock that returns the record's records.
rwlock(Tid, Store, {Tab, Key} = Oid) ->
    Node = readable(Tab),
    case need_lock(Store, Tab, Key, write) of
        yes ->
            _ = writers(Tab),
            granted(request({read_write, Tid, Oid}), Oid),
            ets:insert(Store, {{locks, Tab, Key}, write}),
            read_result(Oid, Node);
        no when Key =:= ?ALL; Tab =:= ?GLOBAL ->
            element(2, writers(Tab));
        no ->
            read_result(Oid, Node)
    end.

% Sticky locks: the first one of a table also locks the table for reading and its sticky item, and sticks the table to
% this node; the table must have a replica here.
sticky_wlock(Tid, Store, Oid) ->
    sticky_lock(Tid, Store, Oid, write).

sticky_rwlock(Tid, Store, Oid) ->
    sticky_lock(Tid, Store, Oid, read_write).

sticky_wlock_table(Tid, Store, Tab) ->
    sticky_lock(Tid, Store, {Tab, ?ALL}, write).

sticky_lock(Tid, Store, {Tab, Key} = Oid, Lock) ->
    Node = node(),
    readable(Tab) =:= Node orelse mnesia:abort({not_local, Tab}),
    case need_lock(Store, Tab, Key, write) of
        yes ->
            _ = writers(Tab),
            stuck(Tab) orelse stick(Tid, Store, Oid),
            granted(request({Lock, Tid, Oid}), Oid),
            ets:insert(Store, [{sticky, true}, {{locks, Tab, Key}, write}]);
        no ->
            ok
    end,
    sticky_result(Oid, Lock, Node).

stick(Tid, Store, {Tab, _} = Oid) ->
    rlock(Tid, Store, {Tab, ?ALL}),
    wlock(Tid, Store, Oid),
    wlock(Tid, Store, {Tab, ?STICK}),
    gen_server:cast(mnesia_locker, {stick, Tab}).

stuck(Tab) ->
    gen_server:call(mnesia_locker, {stuck, Tab}, infinity).

sticky_result({Tab, Key}, read_write, _) -> ets:lookup(Tab, Key);
sticky_result({_, ?ALL}, _, Node) -> [Node];
sticky_result(_, _, _) -> ok.

% A lock of a global item on the listed nodes this node knows: a write lock returns the nodes, a read lock too.
global_lock(Tid, Store, Item, Kind, Nodes) ->
    Oid = {?GLOBAL, Item},
    Known = [N || N <- Nodes, N =:= node()],
    [granted(request({Kind, Tid, Oid}), Oid) || _ <- Known],
    Kind =:= read andalso Known =/= [] andalso ets:insert(Store, {{locks, ?GLOBAL, Item}, read}),
    Known.

% The node a table is read on; no_exists without one.
readable(Tab) ->
    case clause_mnesia_monitor:val({Tab, where_to_read}) of
        nowhere -> mnesia:abort({no_exists, Tab});
        Node -> Node
    end.

% The nodes a table is written on and its majority flag; no_exists for a table written nowhere.
writers(Tab) ->
    try ets:lookup_element(?GVAR, {Tab, where_to_wlock}, 2) of
        {[_ | _], _} = Where -> Where;
        _ -> mnesia:abort({no_exists, Tab})
    catch
        error:_ -> mnesia:abort({no_exists, Tab})
    end.

% Whether the store has no lock yet that covers the record or table.
need_lock(Store, Tab, Key, Lock) ->
    case ets:match_object(Store, {{locks, Tab, ?ALL}, Lock}) of
        [] ->
            case ets:match_object(Store, {{locks, Tab, Key}, Lock}) of
                [] -> yes;
                _ -> no
            end;
        _ ->
            no
    end.

read_result({_, ?ALL}, Node) -> [Node];
read_result({?GLOBAL, _}, Node) -> [Node];
read_result({Tab, Key}, _) -> clause_mnesia_db:lookup(Tab, Key).

request(Request) ->
    case whereis(mnesia_locker) of
        undefined -> {not_granted, {node_not_running, node()}};
        Pid -> gen_server:call(Pid, Request, infinity)
    end.

% A refused lock aborts the transaction: a #cyclic{} reason restarts it.
granted(granted, _) -> ok;
granted({not_granted, Reason}, _) -> exit({aborted, Reason}).

%% Server side.

init([]) ->
    process_flag(trap_exit, true),
    ets:new(mnesia_held_locks, [ordered_set, private, named_table]),
    ets:new(mnesia_tid_locks, [ordered_set, private, named_table]),
    {ok, #state{}}.

handle_call({Op, Tid, Oid}, From, State) when Op =:= read; Op =:= write; Op =:= read_write ->
    try_lock(Tid, Op, Oid, From, State);
handle_call({stuck, Tab}, _From, #state{sticky = Sticky} = State) ->
    {reply, lists:member(Tab, Sticky), State};
handle_call({release_tid, Tid}, _From, State) ->
    {reply, ok, release(Tid, State)};
handle_call(held_locks, _From, State) ->
    {reply, [{Oid, Lock, Tid} || {{Oid, Lock, Tid}} <- ets:tab2list(mnesia_held_locks)], State};
handle_call(lock_queue, _From, #state{queue = Queue} = State) ->
    {reply,
        [{Oid, Op, Pid, Tid, Lucky} || #queue{oid = Oid, op = Op, from = {Pid, _}, tid = Tid, lucky = Lucky} <- Queue],
        State}.

handle_cast({release_tid, Tid}, State) ->
    {noreply, release(Tid, State)};
handle_cast({stick, Tab}, #state{sticky = Sticky} = State) ->
    {noreply, State#state{sticky = [Tab | lists:delete(Tab, Sticky)]}}.

% A request is granted, queued behind a younger holder, or refused.
try_lock(Tid, Op, Oid, From, #state{queue = Queue} = State) ->
    Lock = lock_kind(Op),
    case can_lock(Tid, Lock, Oid, refused, Queue) of
        yes ->
            set_lock(Tid, Oid, Lock),
            {reply, granted, State};
        {no, Lucky} ->
            {reply, {not_granted, cyclic(Oid, Op, Lock, Lucky)}, State};
        {queue, Lucky} ->
            ets:insert(mnesia_tid_locks, {{Tid, Oid, {queued, Op}}}),
            Waiter = #queue{oid = Oid, tid = Tid, op = Op, from = From, lucky = Lucky},
            {noreply, State#state{queue = Queue ++ [Waiter]}}
    end.

lock_kind(read_write) -> write;
lock_kind(Op) -> Op.

simple_op(read_write) -> read;
simple_op(Op) -> Op.

cyclic(Oid, Op, Lock, Lucky) ->
    #cyclic{node = node(), oid = Oid, op = simple_op(Op), lock = Lock, lucky = Lucky}.

set_lock(Tid, Oid, Lock) ->
    ets:insert(mnesia_tid_locks, {{Tid, Oid, Lock}}),
    ets:insert(mnesia_held_locks, {{Oid, Lock, Tid}}).

% yes, {queue, Holder} when every conflicting holder is younger, or {no, Holder} for an older one. Waiting is the
% caller's state: a new request (refused) also checks the queue, a waiter being retried (waiting) does not.
can_lock(Tid, Lock, {Tab, Key} = Oid, Waiting, Queue) ->
    Holders = holders(Lock, Tab, Key),
    case check_holders(Tid, Holders, yes) of
        {no, _} = No -> No;
        {queue, _} = Wait -> Wait;
        yes when Waiting =:= waiting -> yes;
        yes -> check_queue(Tid, Lock, Oid, Queue)
    end.

% The transactions holding locks that conflict with the requested one.
holders(read, Tab, ?ALL) ->
    ets:select(mnesia_held_locks, [{{{{Tab, '_'}, write, '$1'}}, [], ['$1']}]);
holders(read, Tab, Key) ->
    held_by({Tab, Key}, write) ++ held_by({Tab, ?ALL}, write);
holders(write, Tab, ?ALL) ->
    ets:select(mnesia_held_locks, [{{{{Tab, '_'}, '_', '$1'}}, [], ['$1']}]);
holders(write, Tab, Key) ->
    held_by({Tab, Key}, '_') ++ held_by({Tab, ?ALL}, '_').

held_by(Oid, Lock) ->
    ets:select(mnesia_held_locks, [{{{Oid, Lock, '$1'}}, [], ['$1']}]).

% An older transaction (smaller tid) may wait for a younger holder; a younger one may not.
check_holders(Tid, [Tid | Rest], Result) ->
    check_holders(Tid, Rest, Result);
check_holders(Tid, [Holder | Rest], _) when Holder > Tid ->
    check_holders(Tid, Rest, {queue, Holder});
check_holders(_, [Holder | _], _) ->
    {no, Holder};
check_holders(_, [], Result) ->
    Result.

% A free lock still waits behind the youngest queued request of its record (for a record read) and of its table.
check_queue(Tid, read, {Tab, Key} = Oid, Queue) when Key =/= ?ALL ->
    case youngest(Oid, Queue) of
        empty -> table_queue(Tid, Tab, yes, Queue);
        Waiter when Waiter > Tid -> table_queue(Tid, Tab, {queue, Waiter}, Queue);
        Waiter -> {no, Waiter}
    end;
check_queue(Tid, _, {Tab, _}, Queue) ->
    table_queue(Tid, Tab, yes, Queue).

table_queue(Tid, Tab, Result, Queue) ->
    case youngest({Tab, ?ALL}, Queue) of
        empty -> Result;
        Tid -> Result;
        Waiter when Waiter > Tid -> {queue, Waiter};
        Waiter -> {no, Waiter}
    end.

youngest(Oid, Queue) ->
    case [T || #queue{oid = O, tid = T} <- Queue, O =:= Oid] of
        [] -> empty;
        Tids -> lists:max(Tids)
    end.

% A transaction's locks and queued requests go; the requests waiting for its records and tables are tried again.
release(Tid, State) ->
    Locks = [L || {L} <- ets:select(mnesia_tid_locks, [{{{Tid, '_', '_'}}, [], ['$_']}])],
    [ets:delete(mnesia_tid_locks, L) || L <- Locks],
    [ets:delete(mnesia_held_locks, {Oid, Lock, Tid}) || {_, Oid, Lock} <- Locks, not is_tuple(Lock)],
    Queue = [W || #queue{tid = T} = W <- State#state.queue, T =/= Tid],
    Oids = lists:usort([Oid || {_, Oid, _} <- Locks]),
    lists:foldl(fun retry/2, State#state{queue = Queue}, Oids).

% The waiters a released record or table lock may let in, youngest first.
retry({Tab, ?ALL}, #state{queue = Queue} = State) ->
    Waiters = sorted([W || #queue{oid = {T, _}} = W <- Queue, T =:= Tab]),
    retry_table(Waiters, State);
retry({Tab, _} = Oid, #state{queue = Queue} = State) ->
    Waiters = sorted([W || #queue{oid = O} = W <- Queue, O =:= {Tab, ?ALL} orelse O =:= Oid]),
    retry_record(Waiters, State).

sorted(Waiters) ->
    lists:reverse(lists:keysort(#queue.tid, Waiters)).

% A record's waiters are tried until one has to keep waiting.
retry_record([Waiter | Rest], State) ->
    case try_waiter(Waiter, State) of
        {queued, State2} -> State2;
        {_, State2} -> retry_record(Rest, State2)
    end;
retry_record([], State) ->
    State.

% A table's waiters are tried until a table request has to keep waiting; a record request that keeps waiting skips
% the other requests for its record.
retry_table([#queue{oid = {_, ?ALL}} = Waiter | Rest], State) ->
    case try_waiter(Waiter, State) of
        {queued, State2} -> State2;
        {_, State2} -> retry_table(Rest, State2)
    end;
retry_table([#queue{oid = Oid} = Waiter | Rest], State) ->
    case try_waiter(Waiter, State) of
        {queued, State2} -> retry_table([W || #queue{oid = O} = W <- Rest, O =/= Oid], State2);
        {_, State2} -> retry_table(Rest, State2)
    end;
retry_table([], State) ->
    State.

try_waiter(#queue{oid = Oid, tid = Tid, op = Op, from = From} = Waiter, #state{queue = Queue} = State) ->
    Lock = lock_kind(Op),
    case can_lock(Tid, Lock, Oid, waiting, Queue) of
        yes ->
            dequeue(Waiter),
            set_lock(Tid, Oid, Lock),
            gen_server:reply(From, granted),
            {locked, State#state{queue = lists:delete(Waiter, Queue)}};
        {queue, _} ->
            {queued, State};
        {no, Lucky} ->
            dequeue(Waiter),
            gen_server:reply(From, {not_granted, cyclic(Oid, Op, Lock, Lucky)}),
            {removed, State#state{queue = lists:delete(Waiter, Queue)}}
    end.

dequeue(#queue{oid = Oid, tid = Tid, op = Op}) ->
    ets:delete(mnesia_tid_locks, {Tid, Oid, {queued, Op}}).
