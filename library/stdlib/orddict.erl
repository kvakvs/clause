%% Clause's orddict (plan 21 step LIB1, docs/builtins.md#collections): lists of {Key, Value} pairs sorted by key.
%% Lookups and updates without funs are runtime builtins with clause_builtin_placeholder bodies; the functions taking
%% funs are Erlang and raise what OTP's clauses raise for a shape they do not accept.
-module(orddict).
-export([
    append/3,
    append_list/3,
    erase/2,
    fetch/2,
    fetch_keys/1,
    filter/2,
    find/2,
    fold/3,
    from_list/1,
    is_empty/1,
    is_key/2,
    map/2,
    merge/3,
    new/0,
    size/1,
    store/3,
    take/2,
    to_list/1,
    update/3,
    update/4,
    update_counter/3
]).
-include("orddict.types.hrl").

append(_, _, _) -> clause_builtin_placeholder.
append_list(_, _, _) -> clause_builtin_placeholder.
erase(_, _) -> clause_builtin_placeholder.
fetch(_, _) -> clause_builtin_placeholder.
fetch_keys(_) -> clause_builtin_placeholder.
find(_, _) -> clause_builtin_placeholder.
from_list(_) -> clause_builtin_placeholder.
is_key(_, _) -> clause_builtin_placeholder.
store(_, _, _) -> clause_builtin_placeholder.
take(_, _) -> clause_builtin_placeholder.
update_counter(_, _, _) -> clause_builtin_placeholder.

%% The empty dictionary.
new() -> [].

%% The dictionary as a list: itself.
to_list(Orddict) -> Orddict.

%% The number of pairs.
size(Orddict) -> length(Orddict).

%% Whether the dictionary has no pairs.
is_empty([]) -> true;
is_empty([_ | _]) -> false.

%% Key's value replaced by Fun(Value); function_clause when Key is absent.
update(Key, Fun, Orddict) ->
    case orddict:find(Key, Orddict) of
        {ok, Value} -> orddict:store(Key, Fun(Value), Orddict);
        error -> erlang:error(function_clause, [Key, Fun, Orddict])
    end.

%% Key's value replaced by Fun(Value), or Key added with Initial.
update(Key, Fun, Initial, Orddict) ->
    case orddict:find(Key, Orddict) of
        {ok, Value} -> orddict:store(Key, Fun(Value), Orddict);
        error -> orddict:store(Key, Initial, Orddict)
    end.

%% Fun(Key, Value, Acc) over the pairs in key order.
fold(Fun, Acc, []) when is_function(Fun, 3) -> Acc;
fold(Fun, Acc, [{Key, Value} | Rest]) -> fold(Fun, Fun(Key, Value, Acc), Rest).

%% Every value replaced by Fun(Key, Value).
map(Fun, []) when is_function(Fun, 2) -> [];
map(Fun, [{Key, Value} | Rest]) -> [{Key, Fun(Key, Value)} | map(Fun, Rest)].

%% The pairs for which Pred(Key, Value) is true; anything but a boolean is a case_clause.
filter(Pred, []) when is_function(Pred, 2) ->
    [];
filter(Pred, [{Key, Value} = Pair | Rest]) ->
    case Pred(Key, Value) of
        true -> [Pair | filter(Pred, Rest)];
        false -> filter(Pred, Rest)
    end.

%% Both dictionaries merged, Fun(Key, Value1, Value2) for the keys of both.
merge(Fun, [], Orddict2) when is_function(Fun, 3) ->
    Orddict2;
merge(Fun, Orddict1, []) when is_function(Fun, 3) ->
    Orddict1;
merge(
    Fun, [{Key1, Value1} = Pair1 | Rest1] = Orddict1, [{Key2, Value2} = Pair2 | Rest2] = Orddict2
) ->
    if
        Key1 < Key2 -> [Pair1 | merge(Fun, Rest1, Orddict2)];
        Key1 > Key2 -> [Pair2 | merge(Fun, Orddict1, Rest2)];
        true -> [{Key1, Fun(Key1, Value1, Value2)} | merge(Fun, Rest1, Rest2)]
    end.
