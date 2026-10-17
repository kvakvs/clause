%% Clause's gb_trees (plan 21 step LIB3, docs/builtins.md#collections): OTP's {Size, Tree} general balanced trees.
%% The tree operations are runtime builtins with clause_builtin_placeholder bodies; the rest is Erlang.
-module(gb_trees).
-export([
    balance/1,
    delete/2,
    delete_any/2,
    empty/0,
    enter/3,
    from_list/1,
    from_orddict/1,
    get/2,
    insert/3,
    is_defined/2,
    is_empty/1,
    iterator/1,
    iterator/2,
    iterator_from/2,
    iterator_from/3,
    keys/1,
    larger/2,
    largest/1,
    lookup/2,
    map/2,
    next/1,
    size/1,
    smaller/2,
    smallest/1,
    take/2,
    take_any/2,
    take_largest/1,
    take_smallest/1,
    to_list/1,
    update/3,
    values/1
]).
-include("gb_trees.types.hrl").

balance(_) -> clause_builtin_placeholder.
delete(_, _) -> clause_builtin_placeholder.
delete_any(_, _) -> clause_builtin_placeholder.
enter(_, _, _) -> clause_builtin_placeholder.
from_list(_) -> clause_builtin_placeholder.
from_orddict(_) -> clause_builtin_placeholder.
get(_, _) -> clause_builtin_placeholder.
insert(_, _, _) -> clause_builtin_placeholder.
is_defined(_, _) -> clause_builtin_placeholder.
iterator(_) -> clause_builtin_placeholder.
iterator(_, _) -> clause_builtin_placeholder.
iterator_from(_, _) -> clause_builtin_placeholder.
iterator_from(_, _, _) -> clause_builtin_placeholder.
keys(_) -> clause_builtin_placeholder.
larger(_, _) -> clause_builtin_placeholder.
largest(_) -> clause_builtin_placeholder.
lookup(_, _) -> clause_builtin_placeholder.
next(_) -> clause_builtin_placeholder.
smaller(_, _) -> clause_builtin_placeholder.
smallest(_) -> clause_builtin_placeholder.
take(_, _) -> clause_builtin_placeholder.
take_any(_, _) -> clause_builtin_placeholder.
take_largest(_) -> clause_builtin_placeholder.
take_smallest(_) -> clause_builtin_placeholder.
to_list(_) -> clause_builtin_placeholder.
update(_, _, _) -> clause_builtin_placeholder.
values(_) -> clause_builtin_placeholder.

%% The empty tree.
empty() -> {0, nil}.

%% Whether the tree is the empty tree.
is_empty({0, nil}) -> true;
is_empty(_) -> false.

%% The number of entries.
size({Size, _}) when is_integer(Size), Size >= 0 -> Size.

%% Every value replaced by Fun(Key, Value), visiting the nodes as OTP's map does.
map(Fun, {Size, Tree}) when is_function(Fun, 2) -> {Size, map_node(Fun, Tree)}.

map_node(_, nil) ->
    nil;
map_node(Fun, {Key, Value, Smaller, Bigger}) ->
    {Key, Fun(Key, Value), map_node(Fun, Smaller), map_node(Fun, Bigger)}.
