%% Data generation benchmarks (plan step 69A): lists, tuples, maps, binaries and big integers built in bulk.
-module(bench_datagen).
-export([main/1]).

main(_Args) ->
    bench_util:run("datagen.list_of_tuples", 260, fun() -> tuples(200000, []) end),
    bench_util:run("datagen.list_to_tuple", 1700, fun() -> to_tuples(100, []) end),
    bench_util:run("datagen.map_put", 4900, fun() -> map_put(2000, #{}) end),
    bench_util:run("datagen.binary_append", 23000, fun() -> binary_append(10000, <<>>) end),
    bench_util:run("datagen.bigint_factorial", 330, fun() -> factorial(3000, 1) end),
    bench_util:run("datagen.string_build", 1000, fun() -> strings(20000, []) end),
    bench_util:done("bench_datagen").

tuples(0, Acc) -> Acc;
tuples(N, Acc) -> tuples(N - 1, [{N, N * N, [N]} | Acc]).

to_tuples(0, Acc) ->
    Acc;
to_tuples(N, Acc) ->
    to_tuples(N - 1, [list_to_tuple(lists:seq(1, 1000)) | Acc]).

map_put(0, Map) -> Map;
map_put(N, Map) -> map_put(N - 1, maps:put(N, N + 1, Map)).

binary_append(0, Bin) -> Bin;
binary_append(N, Bin) -> binary_append(N - 1, <<Bin/binary, N:32>>).

factorial(0, Acc) -> Acc;
factorial(N, Acc) -> factorial(N - 1, Acc * N).

strings(0, Acc) -> Acc;
strings(N, Acc) -> strings(N - 1, [integer_to_list(N) ++ "-" ++ atom_to_list(item) | Acc]).
