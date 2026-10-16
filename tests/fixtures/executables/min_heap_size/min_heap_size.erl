-module(min_heap_size).
-export([main/1]).

%% min_heap_size: spawn_opt and process_flag settings, rounded up to a heap size and never below the runtime default.
main(_) ->
    Self = self(),
    [
        begin
            P = spawn_opt(
                fun() ->
                    receive
                        go -> Self ! {done, self()}
                    end
                end,
                [{min_heap_size, N}]
            ),
            io:format("~p -> ~p~n", [N, minimum(P)]),
            P ! go,
            receive
                {done, P} -> ok
            end
        end
     || N <- [0, 10, 233, 300, 1000, 10000]
    ],
    io:format("self ~p~n", [minimum(self())]),
    io:format("flag ~p~n", [process_flag(min_heap_size, 5000)]),
    io:format("flag ~p~n", [process_flag(min_heap_size, 100)]),
    io:format("after ~p~n", [minimum(self())]),
    io:format("~p~n", [erlang:system_info(min_heap_size)]),
    io:format("~p~n", [caught(fun() -> spawn_opt(fun() -> ok end, [{min_heap_size, -1}]) end)]),
    io:format("~p~n", [caught(fun() -> process_flag(min_heap_size, foo) end)]),
    io:format("~p~n", [grown()]),
    io:format("min_heap_size: ok~n").

minimum(Pid) ->
    {garbage_collection, Settings} = erlang:process_info(Pid, garbage_collection),
    {min_heap_size, Minimum} = lists:keyfind(min_heap_size, 1, Settings),
    Minimum.

caught(Fun) ->
    try Fun() of
        _ -> returned
    catch
        error:Reason -> {error, Reason}
    end.

%% A process with a large minimum heap builds and keeps its data like any other.
grown() ->
    Self = self(),
    spawn_opt(fun() -> Self ! {sum, lists:sum([X * 2 || X <- lists:seq(1, 100000)])} end, [
        {min_heap_size, 500000}
    ]),
    receive
        {sum, Sum} -> Sum
    end.
