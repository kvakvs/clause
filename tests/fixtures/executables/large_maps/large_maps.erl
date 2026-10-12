-module(large_maps).
-export([main/1]).

%% Maps above 32 entries live off the heap as trees (plan 20 step OPT0.5A): building, updating,
%% persistence of old versions, collections, messages, external format, ordering and generators.
main(_) ->
    M0 = build(2000, #{}),
    2000 = map_size(M0),
    M1 = M0#{5 := five, new => value},
    %% The source map is unchanged by the update.
    5 = maps:get(5, M0),
    false = is_map_key(new, M0),
    five = maps:get(5, M1),
    2001 = map_size(M1),
    badkey =
        try M0#{missing := 1} of
            _ -> updated
        catch
            error:{badkey, missing} -> badkey
        end,
    %% Nested large maps survive collections that move their keys and values.
    Nested = #{K => M0#{K => {nested, K}} || K <- lists:seq(1, 40)},
    erlang:garbage_collect(),
    {nested, 7} = maps:get(7, maps:get(7, Nested)),
    Sum = lists:foldl(fun(V, Acc) -> V + Acc end, 0, maps:values(M0)),
    %% Messages copy the whole map, trees included.
    Self = self(),
    Pid = spawn(fun() ->
        receive
            {M, From} -> From ! {echo, M}
        end
    end),
    Pid ! {{M1, Nested}, Self},
    Echo =
        receive
            {echo, Value} -> Value
        end,
    true = Echo =:= {M1, Nested},
    M1 = binary_to_term(term_to_binary(M1)),
    %% Equality and order depend on contents only.
    M0 = maps:from_list(lists:reverse(maps:to_list(M0))),
    true = M0 < M1,
    Keys = [K || K := _ <- M0],
    Keys0 = lists:seq(1, 2000),
    Keys0 = lists:sort(Keys),
    Tuples = maps:from_list([{{key, I, <<I:16>>}, I} || I <- lists:seq(1, 500)]),
    250 = maps:get({key, 250, <<250:16>>}, Tuples),
    error = maps:find({key, 501, <<501:16>>}, Tuples),
    Doubled = #{K => K * 2 || K <- lists:seq(1, 1000)},
    io:format("~p~n", [{map_size(M1), Sum, maps:get(1000, Doubled), map_size(Tuples)}]),
    io:format("large_maps: ok~n").

%% Insert one key at a time, so every update after the 32nd works on a tree.
build(0, Map) -> Map;
build(N, Map) -> build(N - 1, Map#{N => N}).
