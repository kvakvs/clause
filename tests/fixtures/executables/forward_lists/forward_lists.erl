-module(forward_lists).
-export([main/1]).

%% Lists that comprehensions and the lists loops build forward, writing each new cell into the tail of the last one
%% (forward-built lists, plan 20 OPT1.12). Collections in the middle of a build leave its last cell old, below the
%% high-water mark or promoted, while a large live list stays old: every result must still be complete and in order.
main(_) ->
    Live = [{X, [X]} || X <- lists:seq(1, 200000)],
    Seq = lists:seq(1, 100000),
    io:format("~p~n", [summary([X * 2 || X <- Seq])]),
    %% Cells of immediates only: each must be a new cell, never a shared literal.
    io:format("~p~n", [{length([$x || _ <- Seq]), length([a || X <- Seq, X rem 2 =:= 0])}]),
    io:format("~p~n", [{length(lists:map(fun(_) -> ok end, Seq)), iolist_to_binary([$y || _ <- lists:seq(1, 5)])}]),
    io:format("~p~n", [summary([{X, collect(X)} || X <- Seq])]),
    Kept = [[X | Seq] || X <- lists:seq(1, 50), collect(X) =:= X],
    io:format("~p~n", [summary([length(K) + hd(K) || K <- Kept])]),
    io:format("~p~n", [summary(lists:map(fun(X) -> {X, collect(X)} end, Seq))]),
    io:format("~p~n", [summary(lists:filter(fun(X) -> collect(X) rem 3 =:= 0 end, Seq))]),
    io:format("~p~n", [summary(lists:filtermap(fun(X) -> filtermapped(collect(X)) end, Seq))]),
    {Even, Odd} = lists:partition(fun(X) -> collect(X) rem 2 =:= 0 end, Seq),
    io:format("~p~n", [{summary(Even), summary(Odd)}]),
    io:format("~p~n", [summary(lists:zipwith(fun(X, Y) -> collect(X) + Y end, Seq, lists:reverse(Seq)))]),
    {Mapped, Total} = lists:mapfoldl(fun(X, Acc) -> {collect(X) * 3, Acc + X} end, 0, Seq),
    io:format("~p~n", [{summary(Mapped), Total}]),
    {Prefix, Rest} = lists:splitwith(fun(X) -> collect(X) < 70000 end, Seq),
    io:format("~p~n", [{summary(Prefix), length(Rest)}]),
    io:format("~p~n", [summary(lists:takewhile(fun(X) -> collect(X) < 90000 end, Seq))]),
    {Mapped2, _} = lists:mapfoldr(fun(X, Acc) -> {collect(X) - 1, Acc} end, 0, Seq),
    io:format("~p~n", [summary(Mapped2)]),
    io:format("~p~n", [summary(lists:flatmap(fun(X) -> [X, -collect(X)] end, lists:sublist(Seq, 20000)))]),
    Nested = [[Y || Y <- lists:seq(1, X rem 50)] || X <- Seq],
    io:format("~p~n", [{length(Nested), lists:sum([length(N) || N <- Nested])}]),
    io:format("~p~n", [maps:size(#{X => collect(X) || X <- lists:seq(1, 30000)})]),
    io:format("~p~n", [byte_size(<<<<(collect(X) rem 256)>> || X <- Seq>>)]),
    io:format("~p~n", [caught(fun() -> [10 div (X - 50000) || X <- Seq] end)]),
    io:format("~p~n", [{length(Live), lists:last(Live)}]),
    io:format("forward_lists: ok~n").

%% A full collection every 4096 elements, so builds span many collections.
collect(X) when X rem 4096 =:= 0 ->
    erlang:garbage_collect(),
    X;
collect(X) ->
    X.

filtermapped(X) when X rem 3 =:= 0 -> {true, -X};
filtermapped(X) -> X rem 3 =:= 1.

%% Length, sum of the integers, first and last elements: a lost or reordered cell changes them.
summary([]) ->
    empty;
summary(List) ->
    {length(List), lists:sum([X || X <- List, is_integer(X)]), hd(List), lists:last(List)}.

caught(Fun) ->
    try
        Fun()
    catch
        Class:Reason -> {Class, Reason}
    end.
