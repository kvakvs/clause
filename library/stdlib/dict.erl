%% Clause's dict (plan 21 step LIB2, docs/builtins.md#collections): OTP's dict record over a linear hash table.
%% Lookups and updates without funs are runtime builtins with clause_builtin_placeholder bodies; the functions taking
%% funs are Erlang over them and the private clause_hashed builtins, visiting the entries in OTP's order.
-module(dict).
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
-include("dict.types.hrl").

append(_, _, _) -> clause_builtin_placeholder.
append_list(_, _, _) -> clause_builtin_placeholder.
erase(_, _) -> clause_builtin_placeholder.
fetch(_, _) -> clause_builtin_placeholder.
fetch_keys(_) -> clause_builtin_placeholder.
find(_, _) -> clause_builtin_placeholder.
from_list(_) -> clause_builtin_placeholder.
is_empty(_) -> clause_builtin_placeholder.
is_key(_, _) -> clause_builtin_placeholder.
new() -> clause_builtin_placeholder.
size(_) -> clause_builtin_placeholder.
store(_, _, _) -> clause_builtin_placeholder.
take(_, _) -> clause_builtin_placeholder.
to_list(_) -> clause_builtin_placeholder.
update_counter(_, _, _) -> clause_builtin_placeholder.

%% Key's value replaced by Fun(Value); badarg when Key is absent.
update(Key, Fun, Dict) ->
    case dict:find(Key, Dict) of
        {ok, Value} -> dict:store(Key, Fun(Value), Dict);
        error -> erlang:error(badarg, [Key, Fun, Dict])
    end.

%% Key's value replaced by Fun(Value), or Key stored with Initial.
update(Key, Fun, Initial, Dict) ->
    case dict:find(Key, Dict) of
        {ok, Value} -> dict:store(Key, Fun(Value), Dict);
        error when is_function(Fun, 1) -> dict:store(Key, Initial, Dict);
        error -> erlang:error(function_clause, [Key, Fun, Initial, Dict])
    end.

%% Fun(Key, Value, Acc) over the entries in OTP's fold order.
fold(Fun, Acc, Dict) ->
    Pairs = dict:to_list(Dict),
    case is_function(Fun, 3) of
        true -> lists:foldr(fun({Key, Value}, A) -> Fun(Key, Value, A) end, Acc, Pairs);
        false -> erlang:error(function_clause, [Fun, Acc, Dict])
    end.

%% Every value replaced by Fun(Key, Value), the entries visited in OTP's order.
map(Fun, Dict) when is_record(Dict, dict), is_function(Fun, 2) ->
    clause_hashed:with_values(Dict, [Fun(Key, Value) || {Key, Value} <- clause_hashed:entries(Dict)]);
map(Fun, Dict) when is_record(Dict, dict) ->
    erlang:error(function_clause, [Fun, Dict]);
map(_, Dict) ->
    erlang:error({badrecord, Dict}).

%% The entries for which Pred(Key, Value) is true; anything but a boolean is a case_clause.
filter(Pred, Dict) when is_record(Dict, dict), is_function(Pred, 2) ->
    Flags = [
        case Pred(Key, Value) of
            true -> true;
            false -> false
        end
     || {Key, Value} <- clause_hashed:entries(Dict)
    ],
    clause_hashed:keep(Dict, Flags);
filter(Pred, Dict) when is_record(Dict, dict) ->
    erlang:error(function_clause, [Pred, Dict]);
filter(_, Dict) ->
    erlang:error({badrecord, Dict}).

%% Both dictionaries merged, the smaller folded into the other, Fun(Key, Value1, Value2) for the keys of both.
merge(Fun, Dict1, Dict2) when Dict1#dict.size < Dict2#dict.size ->
    fold(
        fun(Key, Value1, D) ->
            update(Key, fun(Value2) -> Fun(Key, Value1, Value2) end, Value1, D)
        end,
        Dict2,
        Dict1
    );
merge(Fun, Dict1, Dict2) ->
    fold(
        fun(Key, Value2, D) ->
            update(Key, fun(Value1) -> Fun(Key, Value1, Value2) end, Value2, D)
        end,
        Dict1,
        Dict2
    ).
