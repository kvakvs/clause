%% Clause's ordsets (plan 21 step LIB1, docs/builtins.md#collections): sorted lists without duplicates. The walks
%% over sets are runtime builtins with clause_builtin_placeholder bodies; the rest is Erlang glue over lists.
-module(ordsets).
-export([
    add_element/2,
    del_element/2,
    filter/2,
    filtermap/2,
    fold/3,
    from_list/1,
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
    size/1,
    subtract/2,
    to_list/1,
    union/1,
    union/2
]).
-include("ordsets.types.hrl").

add_element(_, _) -> clause_builtin_placeholder.
del_element(_, _) -> clause_builtin_placeholder.
intersection(_) -> clause_builtin_placeholder.
intersection(_, _) -> clause_builtin_placeholder.
is_disjoint(_, _) -> clause_builtin_placeholder.
is_element(_, _) -> clause_builtin_placeholder.
is_set(_) -> clause_builtin_placeholder.
is_subset(_, _) -> clause_builtin_placeholder.
subtract(_, _) -> clause_builtin_placeholder.
union(_, _) -> clause_builtin_placeholder.

%% The empty set.
new() -> [].

%% The number of elements.
size(Ordset) -> length(Ordset).

%% Whether the set has no elements.
is_empty(Ordset) -> Ordset =:= [].

%% Whether two sets hold equal elements (==).
is_equal(Ordset1, Ordset2) when is_list(Ordset1), is_list(Ordset2) -> Ordset1 == Ordset2.

%% The set as a list: itself.
to_list(Ordset) -> Ordset.

%% The sorted list without duplicates.
from_list(List) -> lists:usort(List).

%% The union of a list of sets.
union(OrdsetList) -> lists:umerge(OrdsetList).

%% Fun over the elements in order.
fold(Fun, Acc, Ordset) -> lists:foldl(Fun, Acc, Ordset).

%% The elements Pred keeps.
filter(Pred, Ordset) -> lists:filter(Pred, Ordset).

%% The set of Fun's results.
map(Fun, Ordset) -> lists:usort(lists:map(Fun, Ordset)).

%% The set of the values Fun keeps or replaces.
filtermap(Fun, Ordset) -> lists:usort(lists:filtermap(Fun, Ordset)).
