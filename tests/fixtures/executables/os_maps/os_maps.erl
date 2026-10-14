-module(os_maps).
-export([main/1]).

%% The os and maps gaps of plan step 41X: os time, process and environment
%% functions (time values checked for shape only), and the maps functions with
%% OTP's errors.
main([]) ->
    io:format("~kw~n", [os_checks()]),
    [io:format("~kw~n", [outcome(F, A)]) || {F, A} <- map_calls()],
    io:format("~kw~n", [map_funs()]),
    io:format("os_maps: ok~n").

%% The result of maps:F(A...), or the error it raises.
outcome(F, A) ->
    try
        {F, apply(maps, F, A)}
    catch
        error:R -> {F, error, R}
    end.

os_checks() ->
    {Mega, Sec, Micro} = os:timestamp(),
    Seconds = os:system_time(second),
    Pid = os:getpid(),
    true = os:putenv("OS_MAPS_TEST", "value"),
    Value = os:getenv("OS_MAPS_TEST"),
    Default = os:getenv("OS_MAPS_UNSET", "default"),
    Present = os:getenv("OS_MAPS_TEST", "default"),
    InEnv = lists:member("OS_MAPS_TEST=value", os:getenv()),
    InPairs = lists:member({"OS_MAPS_TEST", "value"}, os:env()),
    true = os:unsetenv("OS_MAPS_TEST"),
    Gone = os:getenv("OS_MAPS_TEST"),
    {Major, Minor, Release} = os:version(),
    [
        is_integer(Mega) andalso is_integer(Sec) andalso is_integer(Micro) andalso Micro < 1000000,
        abs(Mega * 1000000 + Sec - Seconds) =< 1,
        is_integer(os:system_time()),
        is_integer(os:perf_counter()),
        os:perf_counter(1000) =< os:perf_counter(1000),
        is_list(Pid) andalso list_to_integer(Pid) > 0,
        Value,
        Default,
        Present,
        InEnv,
        InPairs,
        Gone,
        is_integer(Major) andalso is_integer(Minor) andalso is_integer(Release),
        try_error(fun() -> os:putenv(a, "b") end),
        try_error(fun() -> os:getenv(a) end),
        try_error(fun() -> os:unsetenv(1) end),
        try_error(fun() -> os:system_time(bad_unit) end)
    ].

try_error(Fun) ->
    try
        Fun()
    catch
        Class:Reason -> {Class, Reason}
    end.

map_calls() ->
    M = #{a => 1, b => 2},
    [
        {get, [a, M, d]},
        {get, [z, M, d]},
        {get, [a, x, d]},
        {is_key, [a, M]},
        {is_key, [z, M]},
        {is_key, [a, x]},
        {remove, [a, M]},
        {remove, [z, M]},
        {remove, [a, x]},
        {new, []},
        {size, [M]},
        {size, [x]},
        {merge, [M, #{b => 3, c => 4}]},
        {merge, [M, x]},
        {merge, [x, M]},
        {take, [a, M]},
        {take, [z, M]},
        {take, [a, x]},
        {update, [a, 9, M]},
        {update, [z, 9, M]},
        {update, [a, 9, x]},
        {with, [[a, z], M]},
        {with, [x, M]},
        {with, [[a], x]},
        {without, [[a, z], M]},
        {without, [[a], x]},
        {from_keys, [[a, b, a], 0]},
        {from_keys, [x, 0]},
        {from_keys, [[a | b], 0]},
        {get, [1.0, #{1 => one}, none]}
    ].

%% The fun-taking maps functions.
map_funs() ->
    M = #{a => 1, b => 2, c => 3},
    [
        maps:filter(fun(_, V) -> V rem 2 =:= 1 end, M),
        maps:map(fun(K, V) -> {K, V * 10} end, M),
        maps:foreach(fun(K, V) -> self() ! {K, V} end, M),
        lists:sort([
            receive
                X -> X
            end
         || _ <- [1, 2, 3]
        ]),
        maps:update_with(a, fun(V) -> V + 100 end, M),
        maps:update_with(z, fun(V) -> V + 100 end, 0, M),
        try_error(fun() -> maps:update_with(z, fun(V) -> V end, M) end),
        try_error(fun() -> maps:filter(fun(_, _) -> true end, x) end),
        try_error(fun() -> maps:map(x, M) end),
        try_error(fun() -> maps:foreach(fun(_, _) -> ok end, x) end),
        try_error(fun() -> maps:filter(fun(_, _) -> x end, M) end)
    ].
