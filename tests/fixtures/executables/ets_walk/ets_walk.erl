-module(ets_walk).
-export([main/1, id/1]).

%% ETS traversal (plan 21 step LIB10D): first/next/last/prev and their *_lookup forms, slot/2, safe_fixtable/2 with
%% deletes and inserts during a walk, tab2list/1, foldl/3, foldr/3 and the i/1 table browser. Hash tables walk in an
%% unspecified order, so their walks are printed sorted; ordered_set walks in term order.
main(["browse"]) ->
    browse(),
    io:format("ets_walk: ok~n");
main(_) ->
    ordered(),
    [hashed(Type) || Type <- [set, bag, duplicate_bag]],
    slots(),
    fixation(),
    folds(),
    errors(),
    io:format("ets_walk: ok~n").

id(X) -> X.

show(Label, Values) -> io:format("~p: ~w~n", [Label, Values]).

attempt(F) ->
    try F() of
        Value -> {ok, Value}
    catch
        Class:Reason -> {Class, Reason}
    end.

keys(T, '$end_of_table') when is_reference(T); is_atom(T) -> [];
keys(T, Key) -> [Key | keys(T, ets:next(T, Key))].

reverse_keys(_T, '$end_of_table') -> [];
reverse_keys(T, Key) -> [Key | reverse_keys(T, ets:prev(T, Key))].

lookups(_T, '$end_of_table') -> [];
lookups(T, {Key, _} = Entry) -> [Entry | lookups(T, ets:next_lookup(T, Key))].

reverse_lookups(_T, '$end_of_table') -> [];
reverse_lookups(T, {Key, _} = Entry) -> [Entry | reverse_lookups(T, ets:prev_lookup(T, Key))].

ordered() ->
    T = ets:new(ordered, [ordered_set]),
    Empty = [
        ets:first(T),
        ets:last(T),
        ets:first_lookup(T),
        ets:last_lookup(T),
        ets:next(T, a),
        ets:prev(T, a)
    ],
    ets:insert(T, [{K, value} || K <- [3, 1, 2.5, b, a, {t}, "s", <<"b">>, [], 1 bsl 70]]),
    show(ordered, [
        Empty,
        keys(T, ets:first(T)),
        reverse_keys(T, ets:last(T)),
        lookups(T, ets:first_lookup(T)),
        reverse_lookups(T, ets:last_lookup(T)),
        ets:next(T, 2),
        ets:prev(T, 2),
        ets:next(T, zzz),
        ets:prev(T, -100),
        ets:next(T, 1.0),
        ets:next_lookup(T, 1.5),
        ets:prev_lookup(T, 1 bsl 80),
        ets:next(T, <<"z">>),
        ets:prev(T, 0)
    ]),
    ets:delete(T).

hashed(Type) ->
    T = ets:new(hashed, [Type]),
    Empty = [ets:first(T), ets:last(T), ets:first_lookup(T), ets:last_lookup(T)],
    ets:insert(T, [{K, N} || K <- [a, b, c, 1, 2, {x}], N <- [1, 2]]),
    Keys = keys(T, ets:first(T)),
    Reverse = reverse_keys(T, ets:last(T)),
    Lookups = lookups(T, ets:first_lookup(T)),
    show(Type, [
        Empty,
        lists:sort(Keys),
        length(Keys),
        Reverse =:= Keys,
        lists:sort([{K, lists:sort(Os)} || {K, Os} <- Lookups]),
        ets:last(T) =:= ets:first(T),
        attempt(fun() -> ets:next(T, id(no_such_key)) end),
        attempt(fun() -> ets:prev(T, id(no_such_key)) end),
        attempt(fun() -> ets:next_lookup(T, id(no_such_key)) end),
        attempt(fun() -> ets:next(T, 1.0) end),
        lists:last(Keys) =:= hd(Keys) orelse ets:next(T, lists:last(Keys))
    ]),
    ets:delete(T).

all_slots(T, N) ->
    case ets:slot(T, N) of
        '$end_of_table' ->
            {N, []};
        Objects ->
            {Count, Rest} = all_slots(T, N + 1),
            {Count, Objects ++ Rest}
    end.

slots() ->
    O = ets:new(slots, [ordered_set]),
    S = ets:new(slots, [set]),
    ets:insert(O, [{K} || K <- [c, a, b]]),
    ets:insert(S, [{K} || K <- lists:seq(1, 50)]),
    {Count, Objects} = all_slots(S, 0),
    show(slots, [
        [ets:slot(O, N) || N <- [0, 1, 2, 3]],
        attempt(fun() -> ets:slot(O, 4) end),
        attempt(fun() -> ets:slot(O, id(-1)) end),
        attempt(fun() -> ets:slot(O, id(a)) end),
        lists:sort(Objects) =:= [{K} || K <- lists:seq(1, 50)],
        Count >= 1,
        attempt(fun() -> ets:slot(S, Count + 1) end),
        ets:slot(ets:new(empty, []), 0) =:= '$end_of_table' orelse empty_slot0
    ]),
    ets:delete(O),
    ets:delete(S).

%% Walks a fixed table while deleting each visited key and inserting new ones: every original key is seen once.
fixed_walk(T, '$end_of_table', Seen) ->
    {lists:reverse(Seen), T};
fixed_walk(T, Key, Seen) ->
    case Key =< 100 of
        true ->
            ets:delete(T, Key),
            ets:insert(T, {Key + 1000});
        false ->
            ok
    end,
    fixed_walk(T, ets:next(T, Key), [Key | Seen]).

fixation() ->
    T = ets:new(fixed, [set, public]),
    ets:insert(T, [{K} || K <- lists:seq(1, 100)]),
    Before = [ets:info(T, fixed), ets:info(T, safe_fixed)],
    Fix1 = ets:safe_fixtable(T, true),
    Fix2 = ets:safe_fixtable(T, true),
    Self = self(),
    {Time, [{Pid, Count}]} = ets:info(T, safe_fixed),
    {MonoTime, _} = ets:info(T, safe_fixed_monotonic_time),
    {Seen, _} = fixed_walk(T, ets:first(T), []),
    Original = [K || K <- Seen, K =< 100],
    During = ets:info(T, fixed),
    ets:safe_fixtable(T, false),
    Still = ets:info(T, fixed),
    ets:safe_fixtable(T, false),
    Other = spawn(fun() ->
        ets:safe_fixtable(T, true),
        Self ! fixed,
        receive
            stop -> ok
        end
    end),
    receive
        fixed -> ok
    end,
    ByOther = ets:info(T, safe_fixed),
    Ref = monitor(process, Other),
    Other ! stop,
    receive
        {'DOWN', Ref, process, Other, _} -> ok
    end,
    show(fixation, [
        Before,
        Fix1,
        Fix2,
        is_tuple(Time),
        Pid =:= Self,
        Count,
        is_integer(MonoTime),
        lists:sort(Original) =:= lists:seq(1, 100),
        length(Original),
        lists:usort(Original) =:= lists:sort(Original),
        During,
        Still,
        ets:info(T, fixed),
        ets:info(T, size),
        lists:sort(ets:tab2list(T)) -- [{K + 1000} || K <- lists:seq(1, 100)],
        element(2, ByOther) =:= [{Other, 1}],
        ets:info(T, fixed),
        ets:safe_fixtable(T, false),
        attempt(fun() -> ets:safe_fixtable(T, id('maybe')) end),
        attempt(fun() -> ets:safe_fixtable(id(no_such_table), true) end)
    ]),
    ets:delete(T).

folds() ->
    O = ets:new(folds, [ordered_set]),
    B = ets:new(folds, [bag]),
    ets:insert(O, [{K, K * K} || K <- lists:seq(1, 6)]),
    ets:insert(B, [{K, N} || K <- [a, b], N <- [1, 2, 3]]),
    Named = ets:new(fold_name, [named_table]),
    ets:insert(Named, {only}),
    show(folds, [
        ets:foldl(fun({K, _}, Acc) -> [K | Acc] end, [], O),
        ets:foldr(fun({K, _}, Acc) -> [K | Acc] end, [], O),
        ets:foldl(fun({_, V}, Acc) -> V + Acc end, 0, O),
        lists:sort(ets:foldl(fun(Obj, Acc) -> [Obj | Acc] end, [], B)),
        ets:foldl(fun(Obj, Acc) -> [Obj | Acc] end, [], fold_name),
        ets:foldr(fun(Obj, Acc) -> [Obj | Acc] end, [], Named),
        attempt(fun() -> ets:foldl(fun(_, Acc) -> Acc end, [], id(no_such_table)) end),
        attempt(fun() -> ets:foldl(fun(_, _) -> error(inside) end, [], O) end),
        ets:info(O, fixed),
        ets:tab2list(O),
        lists:sort(ets:tab2list(B))
    ]),
    ets:delete(O),
    ets:delete(B),
    ets:delete(Named).

errors() ->
    T = ets:new(errors, []),
    ets:delete(T),
    show(errors, [
        attempt(fun() -> ets:first(T) end),
        attempt(fun() -> ets:next(T, a) end),
        attempt(fun() -> ets:last_lookup(T) end),
        attempt(fun() -> ets:tab2list(T) end),
        attempt(fun() -> ets:slot(T, 0) end),
        attempt(fun() -> ets:first(id(42)) end),
        attempt(fun() -> ets:tab2list(id(no_such_table)) end)
    ]).

%% Browses an ordered table with the i/1 pager; the commands come from standard input.
browse() ->
    T = ets:new(browse, [ordered_set]),
    ets:insert(T, [{K, lists:duplicate(K rem 7, $x), K * 3} || K <- lists:seq(1, 45)]),
    ets:insert(T, {long, lists:seq(1, 60)}),
    ok = ets:i(T),
    ok = ets:i(T),
    io:format("size after kill: ~w~n", [ets:info(T)]).
