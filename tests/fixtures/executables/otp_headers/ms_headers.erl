%% ms_transform.hrl names the transform (plan 21 step LIB75): ets:fun2ms/1 becomes a match specification.
-module(ms_headers).
-export([run/0]).

-include_lib("stdlib/include/ms_transform.hrl").

run() ->
    Spec = ets:fun2ms(fun({Key, Value}) when Value > 1 -> {Value, Key} end),
    Table = ets:new(ms_headers, []),
    true = ets:insert(Table, [{a, 1}, {b, 2}, {c, 3}]),
    Selected = lists:sort(ets:select(Table, Spec)),
    true = ets:delete(Table),
    {Spec, Selected}.
