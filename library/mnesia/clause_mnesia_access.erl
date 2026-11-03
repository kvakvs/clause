%% mnesia's default access module (plan 21 step MIM1B, docs/libraries/mnesia.md#activities): the functions mnesia:F/N
%% reaches with an activity's id and opaque state, as mnesia:F(ActivityId, Opaque, ...). A transaction ({tid, _, _})
%% locks and works on its store; a dirty activity ({async_dirty | sync_dirty, Pid}) runs the dirty functions; an ets
%% activity works on the ets tables directly. Also the record updates of dirty writes and commits.
-module(clause_mnesia_access).
-export([write/5, delete/5, delete_object/5, read/5, match_object/5, all_keys/4]).
-export([first/3, last/3, next/4, prev/4, foldl/6, foldr/6]).
-export([select/5, select/6, select_reverse/5, select_reverse/6, select_cont/3, fun_select/7, fun_select/10]).
-export([dirty_sel_init/5, index_read/6, index_match_object/6, lock/4, clear_table/4]).
-export([update/3, validate_record/2, uniq/1]).

-include("clause_mnesia.hrl").

%% A select continuation, as OTP's: the table, the activity, where and how it reads, the table's continuation, the
%% operations still to merge, the compiled spec, the table type, the spec run on the table and the direction.
-record(mnesia_select, {tab, tid, node, storage, cont, written = [], spec, type, orig, dir}).

%% Writes.

write(Tid, Ts, Tab, Val, Kind) when is_atom(Tab), Tab =/= schema, is_tuple(Val), tuple_size(Val) > 2 ->
    case element(1, Tid) of
        ets ->
            ok = clause_mnesia_db:insert(Tab, Val),
            ok;
        tid ->
            #tidstore{store = Store} = Ts,
            Oid = {Tab, element(2, Val)},
            write_lock(Tid, Store, Oid, Kind),
            case validate_record(Tab, Val) of
                bag -> ok;
                _ -> ets:delete(Store, Oid)
            end,
            ets:insert(Store, {Oid, Val, write}),
            ok;
        _ ->
            mnesia:dirty_write(Tab, Val)
    end;
write(_, _, Tab, Val, Kind) ->
    abort({bad_type, Tab, Val, Kind}).

delete(Tid, Ts, Tab, Key, Kind) when is_atom(Tab), Tab =/= schema ->
    case element(1, Tid) of
        ets ->
            ok = clause_mnesia_db:delete(Tab, Key),
            ok;
        tid ->
            #tidstore{store = Store} = Ts,
            Oid = {Tab, Key},
            write_lock(Tid, Store, Oid, Kind),
            ets:delete(Store, Oid),
            ets:insert(Store, {Oid, Oid, delete}),
            ok;
        _ ->
            mnesia:dirty_delete(Tab, Key)
    end;
delete(_, _, Tab, _, _) ->
    abort({bad_type, Tab}).

% clear_table/1 in a transaction: the table's write lock and a clear of the records matching Pattern at the commit
% (MIM1C).
clear_table(Tid, #tidstore{store = Store}, Tab, Pattern) when element(1, Tid) =:= tid ->
    clause_mnesia_locker:wlock_table(Tid, Store, Tab),
    ets:insert(Store, {{Tab, '_'}, Pattern, clear_table}),
    ok.

delete_object(Tid, Ts, Tab, Val, Kind) when is_atom(Tab), Tab =/= schema, is_tuple(Val), tuple_size(Val) > 2 ->
    mnesia:has_var(Val) andalso abort({bad_type, Tab, Val}),
    case element(1, Tid) of
        ets ->
            ok = clause_mnesia_db:match_delete(Tab, Val),
            ok;
        tid ->
            #tidstore{store = Store} = Ts,
            Oid = {Tab, element(2, Val)},
            write_lock(Tid, Store, Oid, Kind),
            object_deleted(Store, Oid, Val, clause_mnesia_monitor:val({Tab, setorbag}));
        _ ->
            mnesia:dirty_delete_object(Tab, Val)
    end;
delete_object(_, _, Tab, _, _) ->
    abort({bad_type, Tab}).

% A record deleted in the store: a set table's record written earlier in the transaction becomes a delete of its key.
object_deleted(Store, Oid, Val, bag) ->
    deleted_object(Store, Oid, Val);
object_deleted(Store, Oid, Val, _) ->
    case [Op || {_, _, Kind} = Op <- ets:lookup(Store, Oid), Kind =:= write orelse Kind =:= delete] of
        [] ->
            deleted_object(Store, Oid, Val);
        Ops ->
            case lists:member({Oid, Val, write}, Ops) of
                true ->
                    ets:delete(Store, Oid),
                    ets:insert(Store, {Oid, Oid, delete}),
                    ok;
                false ->
                    ok
            end
    end.

deleted_object(Store, Oid, Val) ->
    ets:match_delete(Store, {Oid, Val, '_'}),
    ets:insert(Store, {Oid, Val, delete_object}),
    ok.

write_lock(Tid, Store, Oid, write) -> clause_mnesia_locker:wlock(Tid, Store, Oid);
write_lock(Tid, Store, Oid, sticky_write) -> clause_mnesia_locker:sticky_wlock(Tid, Store, Oid);
write_lock(_, _, {Tab, _}, Kind) -> abort({bad_type, Tab, Kind}).

%% Reads.

read(Tid, Ts, Tab, Key, Kind) when is_atom(Tab), Tab =/= schema ->
    case element(1, Tid) of
        ets ->
            clause_mnesia_db:lookup(Tab, Key);
        tid ->
            #tidstore{store = Store} = Ts,
            Oid = {Tab, Key},
            Objs = fun() -> read_lock(Tid, Store, Oid, Kind) end,
            clause_mnesia_written:read(ets:lookup(Store, Oid), Tab, Objs, Kind);
        _ ->
            mnesia:dirty_read(Tab, Key)
    end;
read(_, _, Tab, _, _) ->
    abort({bad_type, Tab}).

read_lock(Tid, Store, Oid, read) -> clause_mnesia_locker:rlock(Tid, Store, Oid);
read_lock(Tid, Store, Oid, write) -> clause_mnesia_locker:rwlock(Tid, Store, Oid);
read_lock(Tid, Store, Oid, sticky_write) -> clause_mnesia_locker:sticky_rwlock(Tid, Store, Oid);
read_lock(_, _, {Tab, _}, Kind) -> abort({bad_type, Tab, Kind}).

match_object(Tid, Ts, Tab, Pat, Kind) when is_atom(Tab), Tab =/= schema, is_tuple(Pat), tuple_size(Pat) > 2 ->
    case element(1, Tid) of
        ets ->
            clause_mnesia_db:match_object(Tab, Pat);
        tid ->
            key_lock(Tid, Ts, Tab, element(2, Pat), Kind),
            clause_mnesia_written:match(Ts#tidstore.store, Pat, Tab, mnesia:dirty_match_object(Tab, Pat));
        _ ->
            mnesia:dirty_match_object(Tab, Pat)
    end;
match_object(_, _, Tab, Pat, _) ->
    abort({bad_type, Tab, Pat}).

% A record's lock for a bound key, the table's otherwise.
key_lock(Tid, Ts, Tab, Key, Kind) ->
    case mnesia:has_var(Key) of
        false -> lock_record(Tid, Ts, Tab, Key, Kind);
        true -> lock_table(Tid, Ts, Tab, Kind)
    end.

all_keys(Tid, Ts, Tab, Kind) when is_atom(Tab), Tab =/= schema ->
    Pat = setelement(2, clause_mnesia_monitor:val({Tab, wild_pattern}), '$1'),
    Keys = select(Tid, Ts, Tab, [{Pat, [], ['$1']}], Kind),
    case clause_mnesia_monitor:val({Tab, setorbag}) of
        bag -> uniq(Keys);
        _ -> Keys
    end;
all_keys(_, _, Tab, _) ->
    abort({bad_type, Tab}).

% A bag table's keys once each, as OTP's lists them: descending.
uniq(Keys) ->
    lists:reverse(lists:usort(Keys)).

%% Walks over keys.

first(Tid, Ts, Tab) when is_atom(Tab), Tab =/= schema ->
    walk_start(Tid, Ts, Tab, next, fun clause_mnesia_db:first/1, fun mnesia:dirty_first/1);
first(_, _, Tab) ->
    abort({bad_type, Tab}).

last(Tid, Ts, Tab) when is_atom(Tab), Tab =/= schema ->
    walk_start(Tid, Ts, Tab, prev, fun clause_mnesia_db:last/1, fun mnesia:dirty_last/1);
last(_, _, Tab) ->
    abort({bad_type, Tab}).

next(Tid, Ts, Tab, Key) when is_atom(Tab), Tab =/= schema ->
    walk_step(Tid, Ts, Tab, Key, next, fun clause_mnesia_db:next/2, fun mnesia:dirty_next/2);
next(_, _, Tab, _) ->
    abort({bad_type, Tab}).

prev(Tid, Ts, Tab, Key) when is_atom(Tab), Tab =/= schema ->
    walk_step(Tid, Ts, Tab, Key, prev, fun clause_mnesia_db:prev/2, fun mnesia:dirty_prev/2);
prev(_, _, Tab, _) ->
    abort({bad_type, Tab}).

walk_start(Tid, Ts, Tab, Op, Ets, Dirty) ->
    case element(1, Tid) of
        ets ->
            Ets(Tab);
        tid ->
            lock_table(Tid, Ts, Tab, read),
            Key = Dirty(Tab),
            clause_mnesia_written:stored_key(Tab, Key, '$end_of_table', Ts#tidstore.store, Op, setorbag(Tab));
        _ ->
            Dirty(Tab)
    end.

walk_step(Tid, Ts, Tab, Key, Op, Ets, Dirty) ->
    case element(1, Tid) of
        ets ->
            Ets(Tab, Key);
        tid ->
            lock_table(Tid, Ts, Tab, read),
            New = caught(fun() -> Dirty(Tab, Key) end),
            clause_mnesia_written:stored_key(Tab, New, Key, Ts#tidstore.store, Op, setorbag(Tab));
        _ ->
            Dirty(Tab, Key)
    end.

% A fold over the table's records and the keys the transaction wrote, in key order for an ordered_set table.
foldl(Tid, Ts, Fun, Acc, Tab, Kind) ->
    {Type, Written} = start_fold(Tid, Ts, Tab, Kind),
    end_fold(caught(fun() -> fold(Tid, Ts, Tab, mnesia:dirty_first(Tab), Fun, Acc, Type, Written, next) end)).

foldr(Tid, Ts, Fun, Acc, Tab, Kind) ->
    {Type, Written} = start_fold(Tid, Ts, Tab, Kind),
    Stored =
        case Type of
            ordered_set -> lists:reverse(Written);
            _ -> Written
        end,
    end_fold(caught(fun() -> fold(Tid, Ts, Tab, mnesia:dirty_last(Tab), Fun, Acc, Type, Stored, prev) end)).

start_fold(Tid, Ts, Tab, Kind) ->
    lock(Tid, Ts, {table, Tab}, Kind),
    {setorbag(Tab), written_keys(Ts, Tab)}.

written_keys(#tidstore{store = Store}, Tab) -> lists:sort(lists:append(ets:match(Store, {{Tab, '$1'}, '_', write})));
written_keys(_, _) -> [].

end_fold({'EXIT', {aborted, Reason}}) -> abort(Reason);
end_fold({'EXIT', Reason}) -> abort(Reason);
end_fold(Result) -> Result.

% The table's keys in Step order, with the written keys not in the table folded in: an ordered_set table's in their
% place, the others' at the end.
fold(Tid, Ts, Tab, '$end_of_table', Fun, Acc, _, Written, _) ->
    lists:foldl(fun(Key, Acc0) -> fold_key(Tid, Ts, Tab, Key, Fun, Acc0) end, Acc, Written);
fold(Tid, Ts, Tab, Key, Fun, Acc, ordered_set, [W | Written], Step) when W == Key ->
    fold_next(Tab, dirty_step(Step, Tab, Key), Fun, fold_key(Tid, Ts, Tab, Key, Fun, Acc), ordered_set, Written, Step);
fold(Tid, Ts, Tab, Key, Fun, Acc, ordered_set, [W | Written], Step) when
    Step =:= next, W < Key; Step =:= prev, W > Key
->
    fold_next(Tab, Key, Fun, fold_key(Tid, Ts, Tab, W, Fun, Acc), ordered_set, Written, Step);
fold(Tid, Ts, Tab, Key, Fun, Acc, Type, Written, Step) ->
    Left =
        case Type of
            ordered_set -> Written;
            _ -> ordsets:del_element(Key, Written)
        end,
    fold_next(Tab, dirty_step(Step, Tab, Key), Fun, fold_key(Tid, Ts, Tab, Key, Fun, Acc), Type, Left, Step).

% The next fold step runs with the activity's current id and state, which an activity inside the fun may change.
fold_next(Tab, Next, Fun, Acc, Type, Written, Step) ->
    {_, Tid, Ts} = get(mnesia_activity_state),
    fold(Tid, Ts, Tab, Next, Fun, Acc, Type, Written, Step).

fold_key(Tid, Ts, Tab, Key, Fun, Acc) ->
    lists:foldl(Fun, Acc, read(Tid, Ts, Tab, Key, read)).

dirty_step(next, Tab, Key) -> mnesia:dirty_next(Tab, Key);
dirty_step(prev, Tab, Key) -> mnesia:dirty_prev(Tab, Key).

%% Selects.

select(Tid, Ts, Tab, Spec, Kind) ->
    fun_select(Tid, Ts, Tab, Spec, Kind, Tab, fun(S) -> mnesia:dirty_select(Tab, S) end).

select_reverse(Tid, Ts, Tab, Spec, Kind) ->
    reverse_select(Tid, Ts, Tab, Spec, Kind, Tab, fun(S) -> mnesia:dirty_select_reverse(Tab, S) end).

% A whole select: the table's records with the store's operations on TabPat's tables applied.
fun_select(Tid, Ts, Tab, Spec, Kind, TabPat, Select) ->
    whole_select(Tid, Ts, Tab, Spec, Kind, TabPat, Select, forward).

reverse_select(Tid, Ts, Tab, Spec, Kind, TabPat, Select) ->
    whole_select(Tid, Ts, Tab, Spec, Kind, TabPat, Select, reverse).

whole_select(Tid, Ts, Tab, Spec, Kind, TabPat, Select, Dir) ->
    case element(1, Tid) of
        ets ->
            ets_select(Tab, Spec, Dir);
        tid ->
            select_lock(Tid, Ts, Kind, Spec, Tab),
            case clause_mnesia_written:store_ops(Ts#tidstore.store, TabPat) of
                [] ->
                    Select(Spec);
                Written ->
                    Records = Select(record_spec(Spec)),
                    Merged = clause_mnesia_written:merge(Written, Records, setorbag(Tab), Dir),
                    ets:match_spec_run(Merged, ets:match_spec_compile(Spec))
            end;
        _ ->
            Select(Spec)
    end.

ets_select(Tab, Spec, Dir) -> clause_mnesia_db:select(Tab, Spec, Dir).

% A one-clause spec with a bound key locks its record, any other spec the table.
select_lock(Tid, Ts, Kind, [{Head, _, _}], Tab) when is_tuple(Head), tuple_size(Head) > 2 ->
    key_lock(Tid, Ts, Tab, element(2, Head), Kind);
select_lock(Tid, Ts, Kind, _, Tab) ->
    lock_table(Tid, Ts, Tab, Kind).

% The spec with each clause returning the whole record.
record_spec(Spec) ->
    [{Head, Guards, ['$_']} || {Head, Guards, _} <- Spec].

% select/4 and select_reverse/4: the first chunk and a continuation.
select(Tid, Ts, Tab, Spec, N, Kind) ->
    chunked(Tid, Ts, Tab, Spec, N, Kind, forward).

select_reverse(Tid, Ts, Tab, Spec, N, Kind) ->
    chunked(Tid, Ts, Tab, Spec, N, Kind, reverse).

chunked(Tid, Ts, Tab, Spec, N, Kind, Dir) ->
    Node = clause_mnesia_monitor:val({Tab, where_to_read}),
    Storage = storage_at(Node, Tab),
    Init = fun(S) -> sel_init(Node, Tab, S, N, Storage, Dir) end,
    fun_select(Tid, Ts, Tab, Spec, Kind, Tab, Init, N, Node, Storage, Dir).

fun_select(Tid, Ts, Tab, Spec, Kind, TabPat, Init, N, Node, Storage) ->
    fun_select(Tid, Ts, Tab, Spec, Kind, TabPat, Init, N, Node, Storage, forward).

fun_select(Tid, Ts, Tab, Spec, Kind, TabPat, Init, N, Node, Storage, Dir) ->
    State = #mnesia_select{tid = Tid, node = Node, storage = Storage, tab = Tab, orig = Spec, dir = Dir},
    case element(1, Tid) of
        ets ->
            select_state(table_chunk(Tab, Spec, N, Dir), State);
        tid ->
            select_lock(Tid, Ts, Kind, Spec, Tab),
            case clause_mnesia_written:store_ops(Ts#tidstore.store, TabPat) of
                [] ->
                    select_state(Init(Spec), State);
                Written0 ->
                    Type = setorbag(Tab),
                    Written =
                        case Type of
                            ordered_set -> clause_mnesia_written:sort_ops(Written0, Dir);
                            _ -> Written0
                        end,
                    Fixed = record_spec(Spec),
                    Merging = State#mnesia_select{written = Written, spec = ets:match_spec_compile(Spec), type = Type},
                    trans_select(Init(Fixed), Merging#mnesia_select{orig = Fixed})
            end;
        _ ->
            select_state(Init(Spec), State)
    end.

dirty_sel_init(Node, Tab, Spec, N, Storage) ->
    sel_init(Node, Tab, Spec, N, Storage, forward).

% The first chunk read where the table is.
sel_init(Node, Tab, Spec, N, _Storage, Dir) ->
    dirty_rpc(Tab, Node, [Tab, Spec, N], fun() -> table_chunk(Tab, Spec, N, Dir) end).

table_chunk(Tab, Spec, N, Dir) -> clause_mnesia_db:select(Tab, Spec, N, Dir).

dirty_rpc(_, nowhere, Args, _) ->
    abort({no_exists, Args});
dirty_rpc(_, _, Args, Read) ->
    try
        Read()
    catch
        throw:Result -> Result;
        _:_ -> abort({badarg, Args})
    end.

% The storage of a table's replica on a node; unknown without one.
storage_at(Node, Tab) ->
    Copies = [{S, clause_mnesia_monitor:val({Tab, S})} || S <- [disc_copies, ram_copies, disc_only_copies]],
    case [S || {S, Nodes} <- Copies, lists:member(Node, Nodes)] of
        [Storage | _] -> Storage;
        [] -> unknown
    end.

% select/1: the next chunk of a continuation, in the activity that made it.
select_cont(_, _, '$end_of_table') ->
    '$end_of_table';
select_cont(Tid, _, #mnesia_select{tid = Tid, cont = Cont} = State) when element(1, Tid) =:= ets ->
    case Cont of
        '$end_of_table' -> '$end_of_table';
        _ -> select_state(clause_mnesia_db:select(Cont), State)
    end;
select_cont(Tid, _, #mnesia_select{tid = Tid, written = []} = State) ->
    select_state(next_chunk(State), State);
select_cont(Tid, _, #mnesia_select{tid = Tid} = State) ->
    trans_select(next_chunk(State), State);
select_cont(Tid, _, #mnesia_select{}) when element(1, Tid) =:= tid ->
    abort(wrong_transaction);
select_cont(Tid, Ts, #mnesia_select{} = State) ->
    select_cont(Tid, Ts, State#mnesia_select{tid = Tid, written = [], spec = undefined, type = undefined});
select_cont(_, _, Cont) ->
    abort({badarg, Cont}).

next_chunk(#mnesia_select{cont = '$end_of_table'}) ->
    '$end_of_table';
next_chunk(#mnesia_select{node = Node, tab = Tab, storage = Storage, cont = Cont, orig = Spec}) ->
    dirty_rpc(Tab, Node, [Storage, Cont, Spec], fun() -> clause_mnesia_db:select(Cont) end).

% A chunk with the store's operations merged; the last chunk takes the operations left.
trans_select('$end_of_table', #mnesia_select{written = Written, spec = Compiled, type = ordered_set, dir = Dir}) ->
    {ets:match_spec_run(clause_mnesia_written:ordered(Written, [], Dir), Compiled), '$end_of_table'};
trans_select('$end_of_table', #mnesia_select{written = Written, spec = Compiled, type = Type, dir = Dir}) ->
    {ets:match_spec_run(clause_mnesia_written:merge(Written, [], Type, Dir), Compiled), '$end_of_table'};
trans_select({Records, Cont}, #mnesia_select{written = Written, spec = Compiled, type = Type, dir = Dir} = State) ->
    {Merged, Left} = clause_mnesia_written:chunk(Written, Records, Type, Dir),
    select_state({ets:match_spec_run(Merged, Compiled), Cont}, State#mnesia_select{written = Left}).

select_state({Matches, Cont}, State) -> {Matches, State#mnesia_select{cont = Cont}};
select_state('$end_of_table', _) -> '$end_of_table'.

%% Indexes.

index_read(Tid, Ts, Tab, Key, Attr, Kind) when is_atom(Tab), Tab =/= schema ->
    case element(1, Tid) of
        tid ->
            Pos = position(Tab, Attr),
            Kind =:= read orelse abort({bad_type, Tab, Kind}),
            mnesia:has_var(Key) andalso abort({bad_type, Tab, Attr, Key}),
            #tidstore{store = Store} = Ts,
            Records = index_records(Tid, Store, Tab, Key, Pos),
            clause_mnesia_written:indexed(Store, Pos, Tab, Key, Records);
        _ ->
            mnesia:dirty_index_read(Tab, Key, Attr)
    end;
index_read(_, _, Tab, _, _, _) ->
    abort({bad_type, Tab}).

% The records an index finds, under a read lock of the table.
index_records(Tid, Store, Tab, Key, Pos) ->
    clause_mnesia_monitor:val({Tab, where_to_read}) =:= nowhere andalso abort({no_exists, Tab}),
    #index{pos_list = Positions} = clause_mnesia_monitor:val({Tab, index_info}),
    lists:keymember(Pos, 1, [P || {P, _} <- Positions]) orelse abort({no_exists, Tab, {index, [Pos]}}),
    clause_mnesia_locker:rlock_table(Tid, Store, Tab),
    clause_mnesia_index:read(Tab, Key, Pos).

index_match_object(Tid, Ts, Tab, Pat, Attr, Kind) when
    is_atom(Tab), Tab =/= schema, is_tuple(Pat), tuple_size(Pat) > 2
->
    case element(1, Tid) of
        tid ->
            Records =
                case position(Tab, Attr) of
                    {_} ->
                        index_locked(Tid, Ts, Tab, Kind, fun() -> mnesia:dirty_match_object(Tab, Pat) end);
                    Pos when Pos =< tuple_size(Pat) ->
                        index_locked(Tid, Ts, Tab, Kind, fun() -> mnesia:dirty_index_match_object(Tab, Pat, Attr) end);
                    BadPos ->
                        abort({bad_type, Tab, BadPos})
                end,
            clause_mnesia_written:match(Ts#tidstore.store, Pat, Tab, Records);
        _ ->
            mnesia:dirty_index_match_object(Tab, Pat, Attr)
    end;
index_match_object(_, _, Tab, Pat, _, _) ->
    abort({bad_type, Tab, Pat}).

index_locked(Tid, #tidstore{store = Store}, Tab, read, Read) ->
    clause_mnesia_locker:rlock_table(Tid, Store, Tab),
    Read();
index_locked(_, _, Tab, Kind, _) ->
    abort({bad_type, Tab, Kind}).

% An attribute's position in a table's records; positions stay as they are.
position(_, Pos) when is_integer(Pos) -> Pos;
position(Tab, Attr) -> clause_mnesia_schema:attribute_position(Attr, clause_mnesia_monitor:val({Tab, attributes})).

%% Locks.

% lock/2 in a transaction; other activities take no locks.
lock(Tid, Ts, Item, Kind) ->
    case element(1, Tid) of
        tid -> lock_item(Tid, Ts, Item, Kind);
        _ -> []
    end.

lock_item(Tid, Ts, {record, Tab, Key}, Kind) -> lock_record(Tid, Ts, Tab, Key, Kind);
lock_item(Tid, Ts, {table, Tab}, Kind) -> lock_table(Tid, Ts, Tab, Kind);
lock_item(Tid, Ts, {global, Item, Nodes}, Kind) -> global_lock(Tid, Ts, Item, Kind, Nodes);
lock_item(_, _, Item, _) -> abort({bad_type, Item}).

lock_record(Tid, #tidstore{store = Store}, Tab, Key, Kind) when is_atom(Tab) ->
    Oid = {Tab, Key},
    case Kind of
        read -> clause_mnesia_locker:rlock(Tid, Store, Oid);
        write -> clause_mnesia_locker:wlock(Tid, Store, Oid);
        sticky_write -> clause_mnesia_locker:sticky_wlock(Tid, Store, Oid);
        none -> [];
        _ -> abort({bad_type, Tab, Kind})
    end;
lock_record(_, _, Tab, _, _) ->
    abort({bad_type, Tab}).

lock_table(Tid, #tidstore{store = Store}, Tab, Kind) when is_atom(Tab) ->
    case Kind of
        read -> clause_mnesia_locker:rlock_table(Tid, Store, Tab);
        write -> clause_mnesia_locker:wlock_table(Tid, Store, Tab);
        load -> clause_mnesia_locker:load_lock_table(Tid, Store, Tab);
        sticky_write -> clause_mnesia_locker:sticky_wlock_table(Tid, Store, Tab);
        none -> [];
        _ -> abort({bad_type, Tab, Kind})
    end;
lock_table(_, _, Tab, _) ->
    abort({bad_type, Tab}).

global_lock(Tid, #tidstore{store = Store}, Item, Kind, Nodes) when is_list(Nodes) ->
    Kind =:= read orelse Kind =:= write orelse abort({bad_type, Kind}),
    clause_mnesia_locker:global_lock(Tid, Store, Item, Kind, Nodes);
global_lock(_, _, _, _, Nodes) ->
    abort({bad_type, Nodes}).

%% Record updates of dirty writes and commits.

% A record of the table's record name and arity; its table type.
validate_record(Tab, Val) ->
    case gvar({Tab, record_validation}, {no_exists, Tab}) of
        {RecordName, Arity, Type} when tuple_size(Val) =:= Arity, element(1, Val) =:= RecordName -> Type;
        _ -> abort({bad_type, Val})
    end.

% An update of this node's replica and its indexes.
update(Tab, Key, {write, Val}) ->
    clause_mnesia_index:written(Tab, Key, Val, undefined),
    ok = clause_mnesia_db:insert(Tab, Val),
    ok;
update(Tab, Key, delete) ->
    clause_mnesia_index:deleted(Tab, Key),
    ok = clause_mnesia_db:delete(Tab, Key),
    ok;
update(Tab, Key, {delete_object, Val}) ->
    clause_mnesia_index:object_deleted(Tab, Key, Val),
    ok = clause_mnesia_db:match_delete(Tab, Val),
    ok;
update(Tab, _, {clear_table, Pattern}) ->
    Records = clause_mnesia_db:match_object(Tab, Pattern),
    ok = clause_mnesia_db:match_delete(Tab, Pattern),
    clause_mnesia_index:cleared(Tab, Records);
update(Tab, Key, {update_counter, RecordName, Incr}) ->
    {New, Old} = counted(Tab, Key, RecordName, Incr),
    clause_mnesia_index:written(Tab, Key, New, Old),
    element(3, New).

% The counter's new record and the one it replaced; a counter that is missing, not an integer or would go below zero
% becomes the increment, or zero.
counted(Tab, Key, RecordName, Incr) ->
    try clause_mnesia_db:update_counter(Tab, Key, Incr) of
        Value when is_integer(Value), Value >= 0 -> {{RecordName, Key, Value}, [{RecordName, Key, Value - Incr}]};
        _ -> reset(Tab, {RecordName, Key, max(Incr, 0)})
    catch
        error:_ -> reset(Tab, {RecordName, Key, max(Incr, 0)})
    end.

reset(Tab, Record) ->
    ok = clause_mnesia_db:insert(Tab, Record),
    {Record, []}.

%% Helpers.

setorbag(Tab) ->
    clause_mnesia_monitor:val({Tab, setorbag}).

gvar(Name, Error) ->
    try
        ets:lookup_element(?GVAR, Name, 2)
    catch
        error:_ -> abort(Error)
    end.

% F's result, or {'EXIT', Reason} for any exception, as OTP's ?CATCH gives.
caught(F) ->
    try
        F()
    catch
        _:Reason -> {'EXIT', Reason}
    end.

abort(Reason) ->
    mnesia:abort(Reason).
