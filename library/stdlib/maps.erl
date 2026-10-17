%% Project-owned subset of the maps module (plan 11 step 39): original
%% implementations whose results and error reasons follow OTP's documented
%% behavior. Compiled from source into every program that calls it.
%% Iteration follows the map's key order (see docs/differences.md).
-module(maps).
-export([find/2, fold/3, from_list/1, get/2, keys/1, put/3, to_list/1, values/1]).
%% OTP 29 exports implemented natively or not yet: catalog builtins, whose clause_builtin_placeholder body the runtime's
%% builtin replaces, and stubs raising error({not_implemented, F, A}) (docs/library.md#runtime-module-replacements).
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

%% Fun(Key, Value, Acc) over every association, in key order.
fold(Fun, Init, Map) when is_function(Fun, 3), is_map(Map) ->
    Entries = clause_maps:entries(Map),
    fold_entries(Fun, Init, Entries, 1, tuple_size(Entries));
fold(Fun, _, Map) when is_function(Fun, 3) ->
    erlang:error({badmap, Map});
fold(_, _, _) ->
    erlang:error(badarg).

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

%% The {Key, Value} pairs, in key order.
to_list(Map) when is_map(Map) -> [{Key, Value} || Key := Value <- Map];
to_list(Map) -> erlang:error({badmap, Map}).

%% The values, in key order.
values(Map) when is_map(Map) -> [Value || _ := Value <- Map];
values(Map) -> erlang:error({badmap, Map}).

filtermap(_, _) -> erlang:error({not_implemented, filtermap, 2}).
from_keys(_, _) -> clause_builtin_placeholder.
get(_, _, _) -> clause_builtin_placeholder.
groups_from_list(_, _) -> erlang:error({not_implemented, groups_from_list, 2}).
groups_from_list(_, _, _) -> erlang:error({not_implemented, groups_from_list, 3}).
intersect(_, _) -> erlang:error({not_implemented, intersect, 2}).
intersect_with(_, _, _) -> erlang:error({not_implemented, intersect_with, 3}).
is_iterator_valid(_) -> erlang:error({not_implemented, is_iterator_valid, 1}).
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
merge_with(_, _, _) -> erlang:error({not_implemented, merge_with, 3}).
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
filter(Pred, Map) when is_function(Pred, 2), is_map(Map) ->
    from_list(filter_pairs(Pred, to_list(Map)));
filter(Pred, Map) when is_function(Pred, 2) ->
    erlang:error({badmap, Map});
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
map(Fun, Map) when is_function(Fun, 2) ->
    erlang:error({badmap, Map});
map(_, _) ->
    erlang:error(badarg).

%% Fun(Key, Value) for every association, in key order, then ok (plan step 41X).
foreach(Fun, Map) when is_function(Fun, 2), is_map(Map) ->
    _ = [Fun(Key, Value) || Key := Value <- Map],
    ok;
foreach(Fun, Map) when is_function(Fun, 2) ->
    erlang:error({badmap, Map});
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
