%% ASSERT keeps assert.hrl's macros on over NOASSERT (plan 21 step LIB75).
-module(assert_forced).
-export([run/0]).

-define(NOASSERT, true).
-define(ASSERT, true).
-include_lib("stdlib/include/assert.hrl").

run() ->
    try ?assertEqual(1, id(2)) of
        Value -> {value, Value}
    catch
        error:{assertEqual, Items} -> {assertEqual, Items}
    end.

id(Value) -> Value.
