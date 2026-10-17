%% Clause's proplists (plan 21 step LIB1, docs/builtins.md#collections): lists of atoms and tuples keyed by their
%% first element. The walks over lists are runtime builtins with clause_builtin_placeholder bodies; the rest is Erlang.
-module(proplists).
-export([
    append_values/2,
    compact/1,
    delete/2,
    expand/2,
    from_map/1,
    get_all_values/2,
    get_bool/2,
    get_keys/1,
    get_value/2,
    get_value/3,
    is_defined/2,
    lookup/2,
    lookup_all/2,
    normalize/2,
    property/1,
    property/2,
    split/2,
    substitute_aliases/2,
    substitute_negations/2,
    to_map/1,
    to_map/2,
    unfold/1
]).
-include("proplists.types.hrl").

append_values(_, _) -> clause_builtin_placeholder.
compact(_) -> clause_builtin_placeholder.
delete(_, _) -> clause_builtin_placeholder.
expand(_, _) -> clause_builtin_placeholder.
get_all_values(_, _) -> clause_builtin_placeholder.
get_bool(_, _) -> clause_builtin_placeholder.
get_keys(_) -> clause_builtin_placeholder.
get_value(_, _, _) -> clause_builtin_placeholder.
is_defined(_, _) -> clause_builtin_placeholder.
lookup(_, _) -> clause_builtin_placeholder.
lookup_all(_, _) -> clause_builtin_placeholder.
split(_, _) -> clause_builtin_placeholder.
substitute_aliases(_, _) -> clause_builtin_placeholder.
substitute_negations(_, _) -> clause_builtin_placeholder.
to_map(_) -> clause_builtin_placeholder.
unfold(_) -> clause_builtin_placeholder.

%% A property in its short form: {Key, true} with an atom Key is Key.
property({Key, true}) when is_atom(Key) -> Key;
property(Property) -> Property.

%% The property of Key and Value in its short form.
property(Key, true) when is_atom(Key) -> Key;
property(Key, Value) -> {Key, Value}.

%% The value of Key, or undefined.
get_value(Key, List) -> proplists:get_value(Key, List, undefined).

%% The list after the stages, compacted.
normalize(List, Stages) -> proplists:compact(stages(List, Stages)).

%% The map of the list after the stages.
to_map(List, Stages) -> proplists:to_map(stages(List, Stages)).

%% The associations of a map as {Key, Value} pairs.
from_map(Map) -> maps:to_list(Map).

%% The list after each {aliases, _}, {expand, _} and {negations, _} stage in order.
stages(List, [{aliases, Aliases} | Rest]) ->
    stages(proplists:substitute_aliases(Aliases, List), Rest);
stages(List, [{expand, Expansions} | Rest]) ->
    stages(proplists:expand(Expansions, List), Rest);
stages(List, [{negations, Negations} | Rest]) ->
    stages(proplists:substitute_negations(Negations, List), Rest);
stages(List, []) ->
    List.
