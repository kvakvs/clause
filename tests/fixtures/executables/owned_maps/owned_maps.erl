-module(owned_maps).
-export([main/1]).

%% Updates of a large map that nothing else reads change it in place (plan 20 step OPT0.5B); maps read again,
%% captured by a fun or updated in loops keep their values.
main(_) ->
    A = #{K => K || K <- lists:seq(1, 100)},
    B = A#{1 := one, 200 => new},
    C = B#{2 := two},
    D = C#{3 := three, 300 => more},
    {one, two, three, 102} = {maps:get(1, D), maps:get(2, D), maps:get(3, D), map_size(D)},
    Shared = #{K => K || K <- lists:seq(1, 100)},
    Updated = Shared#{1 := uno},
    {1, uno} = {maps:get(1, Shared), maps:get(1, Updated)},
    Captured = #{K => K || K <- lists:seq(1, 100)},
    Read = fun() -> maps:get(5, Captured) end,
    Changed = Captured#{5 := five},
    {5, five} = {Read(), maps:get(5, Changed)},
    Failing = #{K => K || K <- lists:seq(1, 100)},
    Result =
        try Failing#{missing := 1} of
            _ -> updated
        catch
            error:{badkey, missing} -> badkey
        end,
    erlang:garbage_collect(),
    Filled = fill(500, #{}),
    Sum = lists:foldl(fun(V, Acc) -> V + Acc end, 0, maps:values(Filled)),
    io:format("~p~n", [{map_size(D), maps:get(300, D), Result, Sum}]),
    io:format("owned_maps: ok~n").

%% Each step updates a fresh map once and passes it on.
fill(0, Map) ->
    Map;
fill(N, Map) ->
    Next = Map#{N => N},
    Last = Next#{last => N},
    fill(N - 1, Last).
