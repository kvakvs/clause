%% Clause's gb_sets (plan 21 step LIB3, docs/builtins.md#collections): OTP's {Size, Tree} general balanced trees of
%% elements. The tree and set operations are runtime builtins with clause_builtin_placeholder bodies; the functions
%% taking funs and the constants are Erlang.
-module(gb_sets).
-export([
    add/2,
    add_element/2,
    balance/1,
    del_element/2,
    delete/2,
    delete_any/2,
    difference/2,
    empty/0,
    filter/2,
    filtermap/2,
    fold/3,
    from_list/1,
    from_ordset/1,
    insert/2,
    intersection/1,
    intersection/2,
    is_disjoint/2,
    is_element/2,
    is_empty/1,
    is_equal/2,
    is_member/2,
    is_set/1,
    is_subset/2,
    iterator/1,
    iterator/2,
    iterator_from/2,
    iterator_from/3,
    larger/2,
    largest/1,
    map/2,
    new/0,
    next/1,
    singleton/1,
    size/1,
    smaller/2,
    smallest/1,
    subtract/2,
    take_largest/1,
    take_smallest/1,
    to_list/1,
    union/1,
    union/2
]).
-include("gb_sets.types.hrl").

add(_, _) -> clause_builtin_placeholder.
add_element(_, _) -> clause_builtin_placeholder.
balance(_) -> clause_builtin_placeholder.
del_element(_, _) -> clause_builtin_placeholder.
delete(_, _) -> clause_builtin_placeholder.
delete_any(_, _) -> clause_builtin_placeholder.
difference(_, _) -> clause_builtin_placeholder.
from_list(_) -> clause_builtin_placeholder.
from_ordset(_) -> clause_builtin_placeholder.
insert(_, _) -> clause_builtin_placeholder.
intersection(_) -> clause_builtin_placeholder.
intersection(_, _) -> clause_builtin_placeholder.
is_disjoint(_, _) -> clause_builtin_placeholder.
is_element(_, _) -> clause_builtin_placeholder.
is_equal(_, _) -> clause_builtin_placeholder.
is_member(_, _) -> clause_builtin_placeholder.
is_subset(_, _) -> clause_builtin_placeholder.
iterator(_) -> clause_builtin_placeholder.
iterator(_, _) -> clause_builtin_placeholder.
iterator_from(_, _) -> clause_builtin_placeholder.
iterator_from(_, _, _) -> clause_builtin_placeholder.
larger(_, _) -> clause_builtin_placeholder.
largest(_) -> clause_builtin_placeholder.
next(_) -> clause_builtin_placeholder.
smaller(_, _) -> clause_builtin_placeholder.
smallest(_) -> clause_builtin_placeholder.
subtract(_, _) -> clause_builtin_placeholder.
take_largest(_) -> clause_builtin_placeholder.
take_smallest(_) -> clause_builtin_placeholder.
to_list(_) -> clause_builtin_placeholder.
union(_) -> clause_builtin_placeholder.
union(_, _) -> clause_builtin_placeholder.

%% The empty set.
empty() -> {0, nil}.
new() -> {0, nil}.

%% Whether the set is the empty set.
is_empty({0, nil}) -> true;
is_empty(_) -> false.

%% The number of elements.
size({Size, _}) -> Size.

%% The set of one element.
singleton(Element) -> {1, {Element, nil, nil}}.

%% Whether a term looks like a set: the empty set or a size with a node.
is_set({0, nil}) -> true;
is_set({Size, {_, _, _}}) when is_integer(Size), Size >= 0 -> true;
is_set(_) -> false.

%% The elements Pred keeps; a non-boolean is a bad_filter.
filter(Pred, Set) when is_function(Pred, 1) ->
    gb_sets:from_ordset([E || E <- gb_sets:to_list(Set), Pred(E)]).

%% Fun(Element, Acc) over the elements in order.
fold(Fun, Acc, {_, Tree}) when is_function(Fun, 2) -> fold_node(Fun, Acc, Tree).

fold_node(Fun, Acc, {Element, Smaller, Bigger}) ->
    fold_node(Fun, Fun(Element, fold_node(Fun, Acc, Smaller)), Bigger);
fold_node(_, Acc, _) ->
    Acc.

%% The set of Fun's results, visiting the nodes as OTP's map does.
map(Fun, {_, Tree}) when is_function(Fun, 1) -> gb_sets:from_list(map_node(Tree, Fun, [])).

map_node({Element, Smaller, Bigger}, Fun, Acc) ->
    map_node(Smaller, Fun, [Fun(Element) | map_node(Bigger, Fun, Acc)]);
map_node(nil, _, Acc) ->
    Acc.

%% The set of the elements Fun keeps (true) or replaces ({true, New}), visiting the nodes as OTP does.
filtermap(Fun, {_, Tree}) when is_function(Fun, 1) ->
    gb_sets:from_list(filtermap_node(Tree, Fun, [])).

filtermap_node({Element, Smaller, Bigger}, Fun, Acc) ->
    case Fun(Element) of
        true -> filtermap_node(Smaller, Fun, [Element | filtermap_node(Bigger, Fun, Acc)]);
        {true, New} -> filtermap_node(Smaller, Fun, [New | filtermap_node(Bigger, Fun, Acc)]);
        false -> filtermap_node(Smaller, Fun, filtermap_node(Bigger, Fun, Acc))
    end;
filtermap_node(nil, _, Acc) ->
    Acc.
