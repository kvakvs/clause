%% A transaction's own writes in what it reads (plan 21 step MIM1B, docs/libraries/mnesia.md#transactions). The store
%% holds {Oid, Record, write}, {Oid, Oid, delete} and {Oid, Record, delete_object} operations; reads merge them into the
%% table's records as OTP does, so results keep OTP's order: written records first for set and bag tables, in key
%% order for ordered_set tables.
-module(clause_mnesia_written).
-export([read/4, match/4, indexed/5, merge/4, ordered/3, chunk/4, sort_ops/2, store_ops/2]).
-export([stored_key/6]).

-include("clause_mnesia.hrl").

%% A written key that comes after an object's key in the walk's direction, and one that comes before it.
-define(AFTER(Key, Obj, Dir),
    ((Dir =:= forward andalso Key > element(2, Obj)) orelse (Dir =:= reverse andalso Key < element(2, Obj)))
).
-define(BEFORE(Key, Obj, Dir),
    ((Dir =:= forward andalso Key < element(2, Obj)) orelse (Dir =:= reverse andalso Key > element(2, Obj)))
).

% A record read: Written holds the store's operations on its key, Objs the table's records (or a fun that locks and
% reads them, called only when the operations do not decide the result).
read([], _, Objs, _) ->
    objects(Objs);
read(Written, Tab, Objs, Kind) ->
    case clause_mnesia_monitor:val({Tab, setorbag}) of
        bag -> read_bag(Written, objects(Objs), []);
        _ when Kind =:= read; Kind =:= write -> read_set(Written, Objs);
        _ -> read_set(Written, objects(Objs))
    end.

objects(Objs) when is_function(Objs) -> Objs();
objects(Objs) -> Objs.

read_set(Written, Objs) ->
    case lists:last(Written) of
        {_, _, delete} -> [];
        {_, Val, write} -> [Val];
        {Oid, _, delete_object} -> [Val || Val <- objects(Objs), not lists:member({Oid, Val, delete_object}, Written)]
    end.

% A bag key's records: the table's records not written again, then the written ones, oldest first.
read_bag([{_, Val, write} | Rest], Objs, Added) ->
    read_bag(Rest, lists:delete(Val, Objs), [Val | Added]);
read_bag([{_, _, delete} | Rest], _, _) ->
    read_bag(Rest, [], []);
read_bag([{_, Val, delete_object} | Rest], Objs, Added) ->
    read_bag(Rest, lists:delete(Val, Objs), lists:delete(Val, Added));
read_bag([], Objs, Added) ->
    Objs ++ lists:reverse(Added).

% The records of a match: the table's matching records with the store's operations applied, then matched again.
match(Store, Pat, Tab, Objs) ->
    Ops = ets:select(Store, [
        {{{Tab, '_'}, '_', write}, [], ['$_']},
        {{{Tab, '_'}, '_', delete}, [], ['$_']},
        {{{Tab, '_'}, Pat, delete_object}, [], ['$_']}
    ]),
    Merged = merge(Ops, Objs, clause_mnesia_monitor:val({Tab, setorbag}), forward),
    ets:match_spec_run(Merged, ets:match_spec_compile([{Pat, [], ['$_']}])).

% The records of an index read: those whose position holds the value, written ones included.
indexed(Store, Pos, Tab, Value, Objs) ->
    match(Store, setelement(Pos, clause_mnesia_monitor:val({Tab, wild_pattern}), Value), Tab, Objs).

% The store's operations applied to records: a set or bag table's written records go first, an ordered_set table's in
% key order.
merge([], Objs, _, _) -> Objs;
merge(Written, Objs, ordered_set, Dir) -> ordered(sort_ops(Written, Dir), Objs, Dir);
merge([{Oid, _, delete} | Rest], Objs, Type, Dir) -> merge(Rest, without_key(Oid, Objs), Type, Dir);
merge([{_, Val, delete_object} | Rest], Objs, Type, Dir) -> merge(Rest, lists:delete(Val, Objs), Type, Dir);
merge([{_, Val, write} | Rest], Objs, bag, Dir) -> merge(Rest, [Val | lists:delete(Val, Objs)], bag, Dir);
merge([{Oid, Val, write} | Rest], Objs, set, Dir) -> merge(Rest, [Val | without_key(Oid, Objs)], set, Dir).

ordered(Sorted, Objs, Dir) ->
    ordered(Sorted, Objs, [], Dir).

% Sorted operations walked together with records in the same order.
ordered([{{_, Key}, _, _} | _] = Written, [Obj | Objs], Acc, Dir) when ?AFTER(Key, Obj, Dir) ->
    ordered(Written, Objs, [Obj | Acc], Dir);
ordered([{{_, Key}, Val, write} | Rest], [Obj | _] = Objs, Acc, Dir) when ?BEFORE(Key, Obj, Dir) ->
    ordered(Rest, [Val | Objs], Acc, Dir);
ordered([{{_, Key}, _, _} | Rest], [Obj | _] = Objs, Acc, Dir) when ?BEFORE(Key, Obj, Dir) ->
    ordered(Rest, Objs, Acc, Dir);
ordered([{_, Val, write} | Rest], [], Acc, Dir) ->
    ordered(Rest, [Val], Acc, Dir);
ordered([_ | Rest], [], Acc, Dir) ->
    ordered(Rest, [], Acc, Dir);
ordered([Op | Rest], Objs, Acc, Dir) ->
    ordered(Rest, same_key(Op, Objs), Acc, Dir);
ordered([], Objs, Acc, _) ->
    lists:reverse(Acc, Objs).

% An operation on the key of the first record.
same_key({_, Val, write}, [_ | Objs]) -> [Val | Objs];
same_key({_, _, delete}, [_ | Objs]) -> Objs;
same_key({_, Val, delete_object}, [Val | Objs]) -> Objs;
same_key({_, _, delete_object}, Objs) -> Objs.

% One chunk of a select with its operations applied: {Records, the operations left for later chunks}.
chunk(Sorted, Objs, ordered_set, Dir) -> chunk_ordered(Sorted, Objs, [], Dir);
chunk(Written, Objs, Type, _) -> chunk_hashed(Written, Objs, Type, []).

chunk_hashed([], Objs, _, Left) ->
    {Objs, lists:reverse(Left)};
chunk_hashed([{Oid, _, delete} = Op | Rest], Objs, Type, Left) ->
    chunk_applied(Rest, Objs, without_key(Oid, Objs), Op, Type, Left);
chunk_hashed([{_, Val, delete_object} = Op | Rest], Objs, Type, Left) ->
    chunk_applied(Rest, Objs, lists:delete(Val, Objs), Op, Type, Left);
chunk_hashed([{{_, Key} = Oid, Val, write} = Op | Rest], Objs, bag, Left) ->
    case lists:keymember(Key, 2, Objs) of
        true -> chunk_hashed(Rest, [Val | lists:delete(Val, Objs)], bag, [{Oid, Val, delete_object} | Left]);
        false -> chunk_hashed(Rest, Objs, bag, [Op | Left])
    end;
chunk_hashed([{Oid, Val, write} = Op | Rest], Objs, set, Left) ->
    case without_key(Oid, Objs) of
        Objs -> chunk_hashed(Rest, Objs, set, [Op | Left]);
        Others -> chunk_hashed(Rest, [Val | Others], set, Left)
    end.

% A delete that removed nothing stays for later chunks, and so does any delete of a bag table.
chunk_applied(Rest, Objs, Objs, Op, Type, Left) -> chunk_hashed(Rest, Objs, Type, [Op | Left]);
chunk_applied(Rest, _, Others, _, set, Left) -> chunk_hashed(Rest, Others, set, Left);
chunk_applied(Rest, _, Others, Op, Type, Left) -> chunk_hashed(Rest, Others, Type, [Op | Left]).

chunk_ordered([{{_, Key}, _, _} | _] = Written, [Obj | Objs], Acc, Dir) when ?AFTER(Key, Obj, Dir) ->
    chunk_ordered(Written, Objs, [Obj | Acc], Dir);
chunk_ordered([{{_, Key}, Val, write} | Rest], [Obj | _] = Objs, Acc, Dir) when ?BEFORE(Key, Obj, Dir) ->
    chunk_ordered(Rest, [Val | Objs], Acc, Dir);
chunk_ordered([{{_, Key}, _, _} | Rest], [Obj | _] = Objs, Acc, Dir) when ?BEFORE(Key, Obj, Dir) ->
    chunk_ordered(Rest, Objs, Acc, Dir);
chunk_ordered(Ops, [], Acc, _) ->
    {lists:reverse(Acc), Ops};
chunk_ordered([Op | Rest], Objs, Acc, Dir) ->
    chunk_ordered(Rest, same_key(Op, Objs), Acc, Dir);
chunk_ordered([], Objs, Acc, _) ->
    {lists:reverse(Acc, Objs), []}.

% Operations in key order, stable, ascending or descending.
sort_ops(Ops, forward) -> lists:keysort(1, Ops);
sort_ops(Ops, reverse) -> lists:sort(fun(A, B) -> element(1, A) > element(1, B) end, Ops).

% The store's operations on a table's records.
store_ops(Store, TabPat) ->
    ets:match_object(Store, {{TabPat, '_'}, '_', '_'}).

without_key({_, Key}, Objs) ->
    [Obj || Obj <- Objs, element(2, Obj) /= Key].

% first/last/next/prev in a transaction: Key is the table's next key after Prev (or {'EXIT', Reason} when the table
% has no Prev); the store's written and deleted keys change it.
stored_key(Tab, '$end_of_table', Prev, Store, Op, Type) ->
    case store_keys(Store, Tab, Op, Type, []) of
        [] -> '$end_of_table';
        Keys when Type =:= ordered_set -> ordered_key(Prev, Keys, Op);
        Keys -> next_stored_key(Prev, Keys, Tab)
    end;
stored_key(Tab, {'EXIT', {aborted, {badarg, [Tab, Key]} = Reason}}, Key, Store, Op, Type) ->
    case last_op(Store, Tab, Key) of
        Last when Last =:= none; Last =:= delete ->
            mnesia:abort(Reason);
        _ ->
            case store_keys(Store, Tab, Op, Type, []) of
                [] -> '$end_of_table';
                Keys -> next_stored_key(Key, Keys, Tab)
            end
    end;
stored_key(_, {'EXIT', {aborted, Reason}}, _, _, _, _) ->
    mnesia:abort(Reason);
stored_key(Tab, Key, Prev, Store, Op, Type) ->
    case last_op(Store, Tab, Key) of
        delete -> mnesia:Op(Tab, Key);
        _ when Type =:= ordered_set -> ordered_key(Prev, store_keys(Store, Tab, Op, ordered_set, [Key]), Op);
        _ -> Key
    end.

last_op(Store, Tab, Key) ->
    case ets:match(Store, {{Tab, Key}, '_', '$1'}) of
        [] -> none;
        Ops -> hd(lists:last(Ops))
    end.

% The first sorted key past Prev.
ordered_key('$end_of_table', [First | _], _) -> First;
ordered_key(Prev, [First | _], next) when Prev < First -> First;
ordered_key(Prev, [First | _], prev) when Prev > First -> First;
ordered_key(Prev, [_ | Rest], Op) -> ordered_key(Prev, Rest, Op);
ordered_key(_, [], _) -> '$end_of_table'.

% A set or bag table's next written key after Key, skipping keys the table has, which the walk returns itself.
next_stored_key(Key, Keys, Tab) ->
    Next =
        case Key of
            '$end_of_table' -> hd(Keys);
            _ -> key_after(Key, Keys)
        end,
    case Next =:= '$end_of_table' orelse mnesia:dirty_read(Tab, Next) =:= [] of
        true -> Next;
        false -> next_stored_key(Next, Keys, Tab)
    end.

key_after(Key, Keys) ->
    case lists:dropwhile(fun(K) -> K /= Key end, Keys) of
        [] -> hd(Keys);
        [Key] -> '$end_of_table';
        [Key, Next | _] -> Next
    end.

% The keys the store writes, in the walk's order; Initial keys are kept unless deleted.
store_keys(Store, Tab, Op, Type, Initial) ->
    Keys = written_keys(ets:match(Store, {{Tab, '$1'}, '_', '$2'}), Initial),
    if
        Type =:= ordered_set, Op =:= prev -> lists:reverse(lists:sort(Keys));
        Type =:= ordered_set -> lists:sort(Keys);
        Op =:= next -> lists:reverse(Keys);
        true -> Keys
    end.

written_keys([[Key, write] | Rest], [Key | _] = Acc) -> written_keys(Rest, Acc);
written_keys([[Key, write] | Rest], Acc) -> written_keys(Rest, [Key | Acc]);
written_keys([[Key, delete] | Rest], [Key | Acc]) -> written_keys(Rest, Acc);
written_keys([_ | Rest], Acc) -> written_keys(Rest, Acc);
written_keys([], Acc) -> Acc.
