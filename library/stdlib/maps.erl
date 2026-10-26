%% Project-owned maps module (plan 11 step 39, maps completion): original
%% implementations whose results and error reasons follow OTP's documented
%% behavior. Compiled from source into every program that calls it.
%% Iteration follows the map's key order (see docs/differences.md).
-module(maps).
-export([find/2, fold/3, from_list/1, get/2, keys/1, put/3, to_list/1, values/1]).
%% OTP 29 exports implemented natively or not yet: catalog builtins, whose clause_builtin_placeholder body the runtime's
%% builtin replaces, and stubs raising error({not_implemented, F, A}) (docs/libraries/library.md#runtime-module-replacements).
-export([
    filter/2,
    filtermap/2,
    foreach/2,
    from_keys/2,
    get/3,
    groups_from_list/2,
    groups_from_list/3,
    intersect/2,
    intersect_with/3,
    is_iterator_valid/1,
    is_key/2,
    iterator/1,
    iterator/2,
    map/2,
    merge/2,
    merge_with/3,
    new/0,
    next/1,
    remove/2,
    size/1,
    take/2,
    update/3,
    update_with/3,
    update_with/4,
    with/2,
    without/2
]).
-include("maps.types.hrl").

%% {ok, Value} for Key, or error.
find(Key, Map) when is_map(Map) ->
    case Map of
        #{Key := Value} -> {ok, Value};
        #{} -> error
    end;
find(_, Map) ->
    erlang:error({badmap, Map}).

%% Fun(Key, Value, Acc) over every association, in key order, or in an iterator's order.
fold(Fun, Init, Map) when is_function(Fun, 3), is_map(Map) ->
    Entries = clause_maps:entries(Map),
    fold_entries(Fun, Init, Entries, 1, tuple_size(Entries));
fold(Fun, Init, Iterator) when is_function(Fun, 3) ->
    lists:foldl(fun({Key, Value}, Acc) -> Fun(Key, Value, Acc) end, Init, associations(Iterator));
fold(_, _, _) ->
    erlang:error(badarg).

%% The {Key, Value} pairs of a map or of the rest of an iterator; {badmap, MapOrIter} for anything else
%% (maps completion).
associations(Map) when is_map(Map) ->
    to_list(Map);
associations(Iterator) ->
    try
        iterated(maps:next(Iterator))
    catch
        error:badarg -> erlang:error({badmap, Iterator})
    end.

iterated({Key, Value, Next}) -> [{Key, Value} | iterated(maps:next(Next))];
iterated(none) -> [].

%% The associations of a {K1, V1, K2, V2, ...} tuple from position At, read in place (map cursors).
fold_entries(Fun, Acc, Entries, At, Size) when At < Size ->
    fold_entries(
        Fun, Fun(element(At, Entries), element(At + 1, Entries), Acc), Entries, At + 2, Size
    );
fold_entries(_, Acc, _, _, _) ->
    Acc.

%% A map of {Key, Value} pairs; a later pair replaces an earlier one with an equal key.
from_list(List) ->
    case pairs(List) of
        true -> #{Key => Value || {Key, Value} <- List};
        false -> erlang:error(badarg)
    end.

%% Whether a term is a proper list of two-element tuples.
pairs([{_, _} | Rest]) -> pairs(Rest);
pairs([]) -> true;
pairs(_) -> false.

%% The value of Key: {badkey, Key} when absent, {badmap, Map} for a non-map.
get(Key, Map) -> map_get(Key, Map).

%% The keys, in key order.
keys(Map) when is_map(Map) -> [Key || Key := _ <- Map];
keys(Map) -> erlang:error({badmap, Map}).

%% Map with Key associated to Value.
put(Key, Value, Map) -> Map#{Key => Value}.

%% The {Key, Value} pairs, in key order, or the rest of an iterator's in its order (json's ordered members).
to_list(Map) when is_map(Map) -> [{Key, Value} || Key := Value <- Map];
to_list(Iterator) -> associations(Iterator).

%% The values, in key order.
values(Map) when is_map(Map) -> [Value || _ := Value <- Map];
values(Map) -> erlang:error({badmap, Map}).

from_keys(_, _) -> clause_builtin_placeholder.
get(_, _, _) -> clause_builtin_placeholder.
intersect(_, _) -> clause_builtin_placeholder.
is_iterator_valid(_) -> clause_builtin_placeholder.
is_key(_, _) -> clause_builtin_placeholder.
%% Iterators (plan step 41N): [Position | Map] walks the map in its exact key order, [Keys | Map] a key list.
iterator(Map) when is_map(Map) -> [0 | Map];
iterator(Map) -> erlang:error({badmap, Map}, [Map]).

iterator(Map, undefined) when is_map(Map) -> [0 | Map];
iterator(Map, ordered) when is_map(Map) -> [maps:keys(Map) | Map];
iterator(Map, reversed) when is_map(Map) -> [lists:reverse(maps:keys(Map)) | Map];
iterator(Map, Order) when is_map(Map), is_function(Order, 2) ->
    [lists:sort(Order, maps:keys(Map)) | Map];
iterator(Map, Order) ->
    erlang:error(badarg, [Map, Order]).
merge(_, _) -> clause_builtin_placeholder.
new() -> clause_builtin_placeholder.
next({Key, Value, Iterator}) ->
    {Key, Value, Iterator};
next([Path | Map] = Iterator) when (is_integer(Path) orelse is_list(Path)), is_map(Map) ->
    try
        erts_internal:map_next(Path, Map, iterator)
    catch
        error:badarg -> erlang:error(badarg, [Iterator])
    end;
next(none) ->
    none;
next(Iterator) ->
    erlang:error(badarg, [Iterator]).
remove(_, _) -> clause_builtin_placeholder.
size(_) -> clause_builtin_placeholder.
take(_, _) -> clause_builtin_placeholder.
update(_, _, _) -> clause_builtin_placeholder.
with(_, _) -> clause_builtin_placeholder.
without(_, _) -> clause_builtin_placeholder.

%% The associations for which Pred(Key, Value) is true; anything but a boolean is a case_clause (plan step 41X).
filter(Pred, MapOrIter) when is_function(Pred, 2) ->
    from_list(filter_pairs(Pred, associations(MapOrIter)));
filter(_, _) ->
    erlang:error(badarg).

filter_pairs(Pred, [{Key, Value} | Rest]) ->
    case Pred(Key, Value) of
        true -> [{Key, Value} | filter_pairs(Pred, Rest)];
        false -> filter_pairs(Pred, Rest)
    end;
filter_pairs(_, []) ->
    [].

%% Every value replaced by Fun(Key, Value) (plan step 41X).
map(Fun, Map) when is_function(Fun, 2), is_map(Map) ->
    #{Key => Fun(Key, Value) || Key := Value <- Map};
map(Fun, Iterator) when is_function(Fun, 2) ->
    from_list([{Key, Fun(Key, Value)} || {Key, Value} <- associations(Iterator)]);
map(_, _) ->
    erlang:error(badarg).

%% Fun(Key, Value) for every association, in key order, then ok (plan step 41X).
foreach(Fun, Map) when is_function(Fun, 2), is_map(Map) ->
    _ = [Fun(Key, Value) || Key := Value <- Map],
    ok;
foreach(Fun, Iterator) when is_function(Fun, 2) ->
    _ = [Fun(Key, Value) || {Key, Value} <- associations(Iterator)],
    ok;
foreach(_, _) ->
    erlang:error(badarg).

%% Map with the value of the existing Key replaced by Fun(Value); {badkey, Key} when absent (plan step 41X).
update_with(Key, Fun, Map) when is_function(Fun, 1), is_map(Map) ->
    case Map of
        #{Key := Value} -> Map#{Key := Fun(Value)};
        #{} -> erlang:error({badkey, Key})
    end;
update_with(_, Fun, Map) when is_function(Fun, 1) ->
    erlang:error({badmap, Map});
update_with(_, _, _) ->
    erlang:error(badarg).

%% Map with the value of Key replaced by Fun(Value), or Init when Key is absent (plan step 41X).
update_with(Key, Fun, Init, Map) when is_function(Fun, 1), is_map(Map) ->
    case Map of
        #{Key := Value} -> Map#{Key := Fun(Value)};
        #{} -> Map#{Key => Init}
    end;
update_with(_, Fun, _, Map) when is_function(Fun, 1) ->
    erlang:error({badmap, Map});
update_with(_, _, _, _) ->
    erlang:error(badarg).

%% The associations Fun(Key, Value) keeps: true keeps the value, {true, NewValue} replaces it, false drops the
%% association; anything else is a case_clause (maps completion).
filtermap(Fun, MapOrIter) when is_function(Fun, 2) ->
    from_list([Kept || {Key, Value} <- associations(MapOrIter), Kept <- kept(Fun, Key, Value)]);
filtermap(_, _) ->
    erlang:error(badarg).

kept(Fun, Key, Value) ->
    case Fun(Key, Value) of
        true -> [{Key, Value}];
        {true, NewValue} -> [{Key, NewValue}];
        false -> []
    end.

%% The map of KeyFun(Elem) to the elements with that key, in list order; the funs run last element first, as in OTP,
%% and the runtime groups the pairs (maps completion).
groups_from_list(KeyFun, List) when is_function(KeyFun, 1), length(List) >= 0 ->
    clause_maps:groups([{KeyFun(Elem), Elem} || Elem <- lists:reverse(List)]);
groups_from_list(KeyFun, List) ->
    erlang:error(badarg, [KeyFun, List]).

%% The same with each element replaced by ValueFun(Elem), called after KeyFun(Elem) (maps completion).
groups_from_list(KeyFun, ValueFun, List) when
    is_function(KeyFun, 1), is_function(ValueFun, 1), length(List) >= 0
->
    clause_maps:groups([keyed(KeyFun, ValueFun, Elem) || Elem <- lists:reverse(List)]);
groups_from_list(KeyFun, ValueFun, List) ->
    erlang:error(badarg, [KeyFun, ValueFun, List]).

keyed(KeyFun, ValueFun, Elem) ->
    Key = KeyFun(Elem),
    {Key, ValueFun(Elem)}.

%% The keys of both maps, each with Combiner(Key, Value1, Value2); badarg for a bad Combiner before {badmap, Map}
%% (maps completion).
intersect_with(Combiner, Map1, Map2) when is_function(Combiner, 3), is_map(Map1), is_map(Map2) ->
    #{
        Key => Combiner(Key, Value1, map_get(Key, Map2))
     || Key := Value1 <- maps:intersect(Map2, Map1)
    };
intersect_with(Combiner, Map1, Map2) ->
    erlang:error(combine_error(Combiner, Map1, Map2)).

%% Both maps merged, the keys of both with Combiner(Key, Value1, Value2) (maps completion).
merge_with(Combiner, Map1, Map2) when is_function(Combiner, 3), is_map(Map1), is_map(Map2) ->
    maps:merge(maps:merge(Map1, Map2), intersect_with(Combiner, Map1, Map2));
merge_with(Combiner, Map1, Map2) ->
    erlang:error(combine_error(Combiner, Map1, Map2)).

%% The reason of intersect_with/3 and merge_with/3 for bad arguments.
combine_error(Combiner, _, _) when not is_function(Combiner, 3) -> badarg;
combine_error(_, Map1, _) when not is_map(Map1) -> {badmap, Map1};
combine_error(_, _, Map2) -> {badmap, Map2}.
