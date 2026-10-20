%% Data processing benchmarks (plan step 69A): sorting, folds, map updates, binary matching, conversions, small maps.
-module(bench_dataproc).
-export([main/1]).

main(_Args) ->
    Random = bench_util:random_list(20000, 42),
    Seq = lists:seq(1, 20000),
    Map = maps:from_list([{K, 0} || K <- lists:seq(1, 500)]),
    Bin = <<<<X:32>> || X <- Seq>>,
    Term = {Seq, Map, <<"payload">>, [{K, integer_to_list(K)} || K <- lists:seq(1, 1000)]},
    bench_util:run("dataproc.sort", 600, fun() -> lists:sort(Random) end),
    bench_util:run("dataproc.map_foldl", 6100, fun() -> map_foldl(Seq) end),
    bench_util:run("dataproc.map_update", 12000, fun() -> map_update(Map, 2) end),
    bench_util:run("dataproc.binary_match", 49000, fun() -> sum_binary(Bin, 0) end),
    bench_util:run("dataproc.int_to_list", 2000, fun() -> int_round_trip(Seq, 0) end),
    bench_util:run("dataproc.term_to_binary", 5100, fun() ->
        binary_to_term(term_to_binary(Term))
    end),
    bench_util:run("dataproc.comprehension", 15000, fun() -> [X * 2 || X <- Seq, X rem 3 =:= 0] end),
    State = #{count => 0, total => 0, last => 0, limit => 20000, name => <<"state">>, flags => []},
    bench_util:run("dataproc.small_map", 4600, fun() -> small_map(State, 20000) end),
    bench_util:done("bench_dataproc").

map_foldl(List) ->
    lists:foldl(fun(X, Acc) -> X + Acc end, 0, lists:map(fun(X) -> X * 3 + 1 end, List)).

%% Increment every value of Map Rounds times, then sum the values.
map_update(Map, 0) ->
    maps:fold(fun(_K, V, Acc) -> V + Acc end, 0, Map);
map_update(Map, Rounds) ->
    map_update(maps:fold(fun(K, V, Acc) -> Acc#{K := V + 1} end, Map, Map), Rounds - 1).

%% A state-record loop over a small map: pattern reads, map_get and an exact update per step.
small_map(State, 0) ->
    State;
small_map(#{count := C, total := T} = State, N) ->
    small_map(State#{count := C + 1, total := T + N + map_get(limit, State), last := N}, N - 1).

sum_binary(<<X:32, Rest/binary>>, Acc) -> sum_binary(Rest, Acc + X);
sum_binary(<<>>, Acc) -> Acc.

int_round_trip([], Acc) -> Acc;
int_round_trip([X | Rest], Acc) -> int_round_trip(Rest, Acc + list_to_integer(integer_to_list(X))).
