%% Project-owned subset of the maps module (plan 11 step 39): original
%% implementations whose results and error reasons follow OTP's documented
%% behavior. Compiled from source into every program that calls it.
%% Iteration follows the map's key order (see docs/differences.md).
-module(maps).
-export([find/2, fold/3, from_list/1, get/2, keys/1, put/3, to_list/1, values/1]).
%% OTP 29 exports Clause does not implement yet: stubs raising error({not_implemented, F, A}),
%% or catalog builtins the runtime puts in their place (docs/library.md#runtime-module-replacements).
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
    fold_pairs(Fun, Init, to_list(Map));
fold(Fun, _, Map) when is_function(Fun, 3) ->
    erlang:error({badmap, Map});
fold(_, _, _) ->
    erlang:error(badarg).

fold_pairs(Fun, Acc, [{Key, Value} | Rest]) -> fold_pairs(Fun, Fun(Key, Value, Acc), Rest);
fold_pairs(_, Acc, []) -> Acc.

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

filter(_, _) -> erlang:error({not_implemented, filter, 2}).
filtermap(_, _) -> erlang:error({not_implemented, filtermap, 2}).
foreach(_, _) -> erlang:error({not_implemented, foreach, 2}).
from_keys(_, _) -> erlang:error({not_implemented, from_keys, 2}).
get(_, _, _) -> erlang:error({not_implemented, get, 3}).
groups_from_list(_, _) -> erlang:error({not_implemented, groups_from_list, 2}).
groups_from_list(_, _, _) -> erlang:error({not_implemented, groups_from_list, 3}).
intersect(_, _) -> erlang:error({not_implemented, intersect, 2}).
intersect_with(_, _, _) -> erlang:error({not_implemented, intersect_with, 3}).
is_iterator_valid(_) -> erlang:error({not_implemented, is_iterator_valid, 1}).
is_key(_, _) -> erlang:error({not_implemented, is_key, 2}).
iterator(_) -> erlang:error({not_implemented, iterator, 1}).
iterator(_, _) -> erlang:error({not_implemented, iterator, 2}).
map(_, _) -> erlang:error({not_implemented, map, 2}).
merge(_, _) -> erlang:error({not_implemented, merge, 2}).
merge_with(_, _, _) -> erlang:error({not_implemented, merge_with, 3}).
new() -> erlang:error({not_implemented, new, 0}).
next(_) -> erlang:error({not_implemented, next, 1}).
remove(_, _) -> erlang:error({not_implemented, remove, 2}).
size(_) -> erlang:error({not_implemented, size, 1}).
take(_, _) -> erlang:error({not_implemented, take, 2}).
update(_, _, _) -> erlang:error({not_implemented, update, 3}).
update_with(_, _, _) -> erlang:error({not_implemented, update_with, 3}).
update_with(_, _, _, _) -> erlang:error({not_implemented, update_with, 4}).
with(_, _) -> erlang:error({not_implemented, with, 2}).
without(_, _) -> erlang:error({not_implemented, without, 2}).
