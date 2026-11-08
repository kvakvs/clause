%% mnesia's lock manager, registered as mnesia_locker as in OTP (plan 21 step MIM1B, docs/libraries/mnesia.md#locks):
%% read and write locks on records, whole tables and global items for transactions. A conflicting request of an older
%% transaction waits in a queue; a younger one is refused with a #cyclic{} reason and its transaction restarts.
%% Transactions call the client functions here, in their own process; the server keeps the locks and the queue.
%% Other nodes' transactions send OTP's raw requests, answered as {mnesia_locker, Node, Reply} (MIM1H5); a transaction
%% here asks the lock managers of the nodes that keep the table in the same form, and remembers each such node in its
%% store as {nodes, Node} so that its end releases the locks there (MIM1H7).
-module(clause_mnesia_locker).
-behaviour(gen_server).
-export([start_link/0, held_locks/0, lock_queue/0, release_tid/1, release_tid_sync/1]).
-export([remote_request/2, mnesia_down/1, ix_read/5, lock_nodes/1, release_remote/2, sync_release_remote/2]).
-export([rlock/3, rlock_table/3, wlock/3, wlock_table/3, wlock_no_exist/4, wlock_new/4, load_lock_table/3, rwlock/3]).
-export([sticky_wlock/3, sticky_wlock_table/3, sticky_rwlock/3, global_lock/5]).
-export([init/1, handle_call/3, handle_cast/2, handle_info/2]).

-include("clause_mnesia.hrl").

%% The key of a whole table's lock and of a global item's table, as OTP spells them.
-define(ALL, '______WHOLETABLE_____').
-define(GLOBAL, '______GLOBAL_____').
-define(STICK, '______STICK_____').

%% A queued request: the lock it waits for, the transaction, its operation, who to answer and the holder it waits on.
%% Another node's request is answered from {Pid, {remote, Answer}}, Answer saying what its grant carries (MIM1H7).
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

% A lock request to another node's lock manager, in OTP's raw form (MIM1H5): granted, or {not_granted, Reason} also
% when that node's mnesia is not running.
remote_request(Node, Request) ->
    Ref = erlang:monitor(process, {mnesia_locker, Node}),
    {mnesia_locker, Node} ! {self(), Request},
    receive
        {mnesia_locker, Node, Reply} ->
            erlang:demonitor(Ref, [flush]),
            Reply;
        {'DOWN', Ref, process, _, _} ->
            {not_granted, {node_not_running, Node}}
    end.

% The locks of the transactions of a node whose mnesia went down go (MIM1H5).
mnesia_down(Node) ->
    gen_server:cast(mnesia_locker, {mnesia_down, Node}).

%% Client side: the locks of a transaction, each asked for once and remembered in its store as {{locks, Tab, Key}, Lock}.

% A read lock on a record returns its records; on a table ok when taken, its node when held already, as OTP's do. The
% lock is taken on the node the table is read on, which sends the records with its grant when it is another (MIM1H7).
rlock(Tid, Store, {Tab, Key} = Oid) ->
    Node = readable(Tab),
    case need_lock(Store, Tab, Key, '_') of
        yes ->
            Records = lock_on(Node, Store, {read, Tid, Oid}),
            ets:insert(Store, {{locks, Tab, Key}, read}),
            case Oid of
                {_, ?ALL} -> ok;
                {?GLOBAL, _} -> ok;
                _ when Node =:= node() -> read_result(Oid, Node);
                _ -> Records
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
            [lock_on(Node, Store, {Op, Tid, Oid}) || Node <- Nodes],
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

% A new table's write lock on each of Nodes, before the table exists (MIM1H8).
wlock_new(Tid, Store, Tab, Nodes) ->
    ets:insert(Store, {{locks, Tab, ?ALL}, write}),
    [lock_on(Node, Store, {write, Tid, {Tab, ?ALL}}) || Node <- Nodes],
    ok.

load_lock_table(Tid, Store, Tab) ->
    wlock(Tid, Store, {Tab, ?ALL}).

% wread: a write lock that returns the record's records, read with the lock on the node the table is read on and
% written with it on the other nodes that write it (MIM1H7).
rwlock(Tid, Store, {Tab, Key} = Oid) ->
    Node = readable(Tab),
    case need_lock(Store, Tab, Key, write) of
        yes ->
            {Nodes, _Majority} = writers(Tab),
            Records = lock_on(Node, Store, {read_write, Tid, Oid}),
            [lock_on(N, Store, {write, Tid, Oid}) || N <- Nodes, N =/= Node],
            ets:insert(Store, {{locks, Tab, Key}, write}),
            remote_records(Oid, Node, Records);
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

% A lock of a global item on the listed nodes running with this one, in their order (MIM1H7): a write lock returns the
% nodes, a read lock too.
global_lock(Tid, Store, Item, Kind, Nodes) ->
    Oid = {?GLOBAL, Item},
    Running = clause_mnesia_monitor:val({current, db_nodes}),
    Known = [N || N <- Nodes, lists:member(N, Running)],
    [lock_on(N, Store, {Kind, Tid, Oid}) || N <- Known],
    Kind =:= read andalso Known =/= [] andalso ets:insert(Store, {{locks, ?GLOBAL, Item}, read}),
    Known.

% An index read under the table's read lock: the records whose attribute at Pos is Value, read on this node, or with
% the lock on the node the table is read on, as OTP's ix_read (MIM1H7).
ix_read(Tid, Store, Tab, Value, Pos) ->
    Node = readable(Tab),
    case {Node =:= node(), need_lock(Store, Tab, ?ALL, '_')} of
        {true, _} ->
            rlock_table(Tid, Store, Tab),
            clause_mnesia_index:read(Tab, Value, Pos);
        {false, yes} ->
            Records = lock_on(Node, Store, {ix_read, Tid, Tab, Value, Pos}),
            ets:insert(Store, {{locks, Tab, ?ALL}, read}),
            Records;
        {false, no} ->
            mnesia:dirty_rpc(Tab, mnesia_index, dirty_read2, [Tab, Value, Pos])
    end.

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
read_result({Tab, Key}, Node) when Node =:= node() -> clause_mnesia_db:lookup(Tab, Key);
read_result({Tab, Key}, _) -> mnesia:dirty_rpc(Tab, mnesia_lib, db_get, [Tab, Key]).

% The records of a lock taken on Node: read here, or those its grant carried.
remote_records(Oid, Node, _) when Node =:= node() -> read_result(Oid, Node);
remote_records(_, _, Records) -> Records.

% A lock taken by Node's lock manager: this node's by a call, another's by OTP's raw request, whose grant may carry the
% records read (MIM1H7). A refusal aborts the transaction.
lock_on(Node, _Store, Request) when Node =:= node() ->
    granted(request(Request), Request);
lock_on(Node, Store, Request) ->
    ets:insert(Store, {nodes, Node}),
    case remote_request(Node, Request) of
        granted -> ok;
        {granted, Records} -> Records;
        {granted, Records, _Locked} -> Records;
        Refused -> granted(Refused, Request)
    end.

% The other nodes whose lock managers a transaction's store names.
lock_nodes(Store) ->
    lists:usort([Node || {nodes, Node} <- ets:lookup(Store, nodes)]).

% An ended transaction's locks go on the other nodes too, without waiting (MIM1H7).
release_remote(Tid, Nodes) ->
    [{mnesia_locker, Node} ! {release_tid, Tid} || Node <- Nodes],
    ok.

% A restarting transaction's locks go on the other nodes, each confirming, or gone (MIM1H7).
sync_release_remote(Tid, Nodes) ->
    [sync_release(Node, Tid) || Node <- Nodes],
    ok.

sync_release(Node, Tid) ->
    Ref = erlang:monitor(process, {mnesia_locker, Node}),
    {mnesia_locker, Node} ! {self(), {sync_release_tid, Tid}},
    receive
        {mnesia_locker, Node, {tid_released, Tid}} -> erlang:demonitor(Ref, [flush]);
        {'DOWN', Ref, process, _, _} -> false
    end.

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
    {noreply, State#state{sticky = [Tab | lists:delete(Tab, Sticky)]}};
handle_cast({mnesia_down, Node}, State) ->
    Tids = lists:usort([Tid || {{#tid{pid = Pid} = Tid, _, _}} <- ets:tab2list(mnesia_tid_locks), node(Pid) =:= Node]),
    {noreply, lists:foldl(fun release/2, State, Tids)}.

% Another node's transaction asks for a lock, or an index read under its table's read lock, or ends, with OTP's raw
% messages (MIM1H5, MIM1H7).
handle_info({Pid, {Op, Tid, Oid}}, State) when
    is_pid(Pid), (Op =:= read orelse Op =:= write orelse Op =:= read_write)
->
    remote_lock(Tid, Op, Oid, {Pid, {remote, answer(Op, Oid)}}, State);
handle_info({Pid, {ix_read, Tid, Tab, Value, Pos}}, State) when is_pid(Pid) ->
    remote_lock(Tid, read, {Tab, ?ALL}, {Pid, {remote, {index, Value, Pos}}}, State);
handle_info({Pid, {sync_release_tid, Tid}}, State) when is_pid(Pid) ->
    State2 = release(Tid, State),
    Pid ! {mnesia_locker, node(), {tid_released, Tid}},
    {noreply, State2};
handle_info({release_tid, Tid}, State) ->
    {noreply, release(Tid, State)};
handle_info(_, State) ->
    {noreply, State}.

remote_lock(Tid, Op, Oid, From, State) ->
    case try_lock(Tid, Op, Oid, From, State) of
        {reply, Reply, State2} ->
            reply(From, Oid, Reply),
            {noreply, State2};
        NoReply ->
            NoReply
    end.

% What another node's grant carries, as OTP's (MIM1H7): a record's records for a read, ok for a table or global read,
% nothing for a write.
answer(Op, {Tab, Key}) when Op =/= write, Key =/= ?ALL, Tab =/= ?GLOBAL -> records;
answer(read, _) -> ok;
answer(_, _) -> plain.

% An answer to a local transaction's call, or to another node's raw request (MIM1H5): its grant in OTP's form.
reply({Pid, {remote, Answer}}, Oid, granted) -> Pid ! {mnesia_locker, node(), grant(Answer, Oid)};
reply({Pid, {remote, _}}, _, Reply) -> Pid ! {mnesia_locker, node(), Reply};
reply(From, _, Reply) -> gen_server:reply(From, Reply).

grant(records, {Tab, Key}) ->
    {granted, safe_read(fun() -> clause_mnesia_db:lookup(Tab, Key) end)};
grant(ok, _) ->
    {granted, ok};
grant(plain, _) ->
    granted;
grant({index, Value, Pos}, {Tab, _}) ->
    {granted, safe_read(fun() -> clause_mnesia_index:read(Tab, Value, Pos) end), [?ALL]}.

% A read for another node's grant; none when this node no longer holds the table, so that the lock manager runs on.
safe_read(Read) ->
    try
        Read()
    catch
        _:_ -> []
    end.

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
            reply(From, Oid, granted),
            {locked, State#state{queue = lists:delete(Waiter, Queue)}};
        {queue, _} ->
            {queued, State};
        {no, Lucky} ->
            dequeue(Waiter),
            reply(From, Oid, {not_granted, cyclic(Oid, Op, Lock, Lucky)}),
            {removed, State#state{queue = lists:delete(Waiter, Queue)}}
    end.

dequeue(#queue{oid = Oid, tid = Tid, op = Op}) ->
    ets:delete(mnesia_tid_locks, {Tid, Oid, {queued, Op}}).
