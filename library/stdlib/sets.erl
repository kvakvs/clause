%% Clause's sets (plan 21 step LIB2, docs/builtins.md#collections): version 2 sets are maps, version 1 OTP's set
%% record over a linear hash table. The operations without funs are runtime builtins with clause_builtin_placeholder
%% bodies; the functions taking funs are Erlang over them and the private clause_hashed builtins.
-module(sets).
-export([
    add_element/2,
    del_element/2,
    filter/2,
    filtermap/2,
    fold/3,
    from_list/1,
    from_list/2,
    intersection/1,
    intersection/2,
    is_disjoint/2,
    is_element/2,
    is_empty/1,
    is_equal/2,
    is_set/1,
    is_subset/2,
    map/2,
    new/0,
    new/1,
    size/1,
    subtract/2,
    to_list/1,
    union/1,
    union/2
]).
-include("sets.types.hrl").

add_element(_, _) -> clause_builtin_placeholder.
del_element(_, _) -> clause_builtin_placeholder.
from_list(_) -> clause_builtin_placeholder.
intersection(_) -> clause_builtin_placeholder.
intersection(_, _) -> clause_builtin_placeholder.
is_disjoint(_, _) -> clause_builtin_placeholder.
is_element(_, _) -> clause_builtin_placeholder.
is_empty(_) -> clause_builtin_placeholder.
is_equal(_, _) -> clause_builtin_placeholder.
is_set(_) -> clause_builtin_placeholder.
is_subset(_, _) -> clause_builtin_placeholder.
size(_) -> clause_builtin_placeholder.
subtract(_, _) -> clause_builtin_placeholder.
to_list(_) -> clause_builtin_placeholder.
union(_) -> clause_builtin_placeholder.
union(_, _) -> clause_builtin_placeholder.

%% An empty set of version 2: a map.
new() -> #{}.

%% An empty set of the version Options name (2 by default).
new([{version, 2}]) ->
    #{};
new(Options) ->
    version(Options, fun() -> #set{empty = segment(), segs = {segment()}} end, fun() -> #{} end).

%% The set of List's elements, of the version Options name.
from_list(List, [{version, 2}]) ->
    sets:from_list(List);
from_list(List, Options) ->
    version(
        Options,
        fun() ->
            lists:foldl(
                fun(Element, Set) -> sets:add_element(Element, Set) end, new([{version, 1}]), List
            )
        end,
        fun() -> sets:from_list(List) end
    ).

%% Fun(Element, Acc) over the elements: a map's keys in order, a record's in OTP's fold order.
fold(Fun, Acc, Set) when is_map(Set), is_function(Fun, 2) -> lists:foldl(Fun, Acc, maps:keys(Set));
fold(Fun, Acc, Set) when is_record(Set, set), is_function(Fun, 2) ->
    lists:foldr(Fun, Acc, sets:to_list(Set)).

%% The elements Pred keeps; a map filter's non-boolean is a bad_filter, a record's a case_clause.
filter(Pred, Set) when is_map(Set), is_function(Pred, 1) ->
    maps:from_keys([Key || Key := _ <- Set, Pred(Key)], []);
filter(Pred, Set) when is_record(Set, set), is_function(Pred, 1) ->
    Flags = [
        case Pred(Element) of
            true -> true;
            false -> false
        end
     || Element <- clause_hashed:entries(Set)
    ],
    clause_hashed:keep(Set, Flags).

%% The set of Fun's results, of the same version.
map(Fun, Set) when is_map(Set), is_function(Fun, 1) ->
    maps:from_keys([Fun(Key) || Key := _ <- Set], []);
map(Fun, Set) when is_record(Set, set), is_function(Fun, 1) ->
    fold(fun(Element, Acc) -> sets:add_element(Fun(Element), Acc) end, new([{version, 1}]), Set).

%% The set of the elements Fun keeps (true) or replaces ({true, New}).
filtermap(Fun, Set) when is_map(Set), is_function(Fun, 1) ->
    maps:from_keys(lists:filtermap(Fun, sets:to_list(Set)), []);
filtermap(Fun, Set) when is_record(Set, set), is_function(Fun, 1) ->
    fold(
        fun(Element, Acc) ->
            case Fun(Element) of
                true -> sets:add_element(Element, Acc);
                {true, New} -> sets:add_element(New, Acc);
                false -> Acc
            end
        end,
        new([{version, 1}]),
        Set
    ).

%% The value of the first of OnOne and OnTwo that Options' version selects; a case_clause for another version.
version(Options, OnOne, OnTwo) ->
    case proplists:get_value(version, Options, 2) of
        1 -> OnOne();
        2 -> OnTwo()
    end.

%% An empty segment of 16 buckets.
segment() -> erlang:make_tuple(16, []).
