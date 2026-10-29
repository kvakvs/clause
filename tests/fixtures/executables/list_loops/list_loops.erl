-module(list_loops).
-export([main/1, id/1]).
-compile(nowarn_shadow_vars).
-compile({nowarn_deprecated_function, [{lists, zf, 2}]}).

%% At O2, lists:map/2 and its kin called with a fun written in the call (or a variable bound once to one) become
%% loops with the fun's clauses inlined, and calls of such a variable inline its clauses (list loops, inline funs).
%% Results, evaluation order and error reasons must stay those of the lists module and of calling the fun.
id(X) -> X.

show(Name, F) ->
    R =
        try F() of
            V -> {ok, V}
        catch
            C:E -> {C, E}
        end,
    io:format("~s: ~p~n", [Name, R]).

main(_) ->
    L = id([1, 2, 3, 4]),
    K = id(10),
    maps(L, K),
    filters(L, K),
    predicates(L),
    selections(L),
    prefixes(L),
    folds(L),
    bound(L, K),
    nesting(L),
    collections(),
    receiving(),
    io:format("list_loops: ok~n").

maps(L, K) ->
    show("map", fun() -> lists:map(fun(X) -> X * K end, L) end),
    show("map clauses", fun() ->
        lists:map(
            fun
                (1) -> one;
                (X) when X > 3 -> big;
                (_) -> other
            end,
            L
        )
    end),
    show("map no clause", fun() -> lists:map(fun(X) when X < 3 -> X end, L) end),
    show("map empty", fun() -> lists:map(fun(X) -> X end, id([])) end),
    show("map non-list", fun() -> lists:map(fun(X) -> X end, id(foo)) end),
    show("map improper", fun() -> lists:map(fun(X) -> X end, id([1, 2 | foo])) end),
    show("map order", fun() ->
        lists:map(
            fun(X) ->
                put(k, [X | dictionary(k)]),
                X
            end,
            L
        ),
        get(k)
    end),
    show("flatmap", fun() -> lists:flatmap(fun(X) -> [X, X * K] end, L) end),
    show("flatmap non-list result", fun() -> lists:flatmap(fun(X) -> X end, L) end),
    show("flatmap improper", fun() -> lists:flatmap(fun(X) -> [X] end, id([1 | y])) end),
    show("shadowed names", fun() ->
        X = 5,
        Mapped = lists:map(
            fun(X) ->
                Y = X + 1,
                Y
            end,
            L
        ),
        Y = X,
        {Mapped, X, Y}
    end).

filters(L, K) ->
    show("filter", fun() -> lists:filter(fun(X) -> X rem 2 =:= 0 end, L) end),
    show("filter non-boolean", fun() -> lists:filter(fun(X) -> X end, L) end),
    show("filter improper", fun() -> lists:filter(fun(_) -> true end, id([1 | bar])) end),
    show("filter non-list", fun() -> lists:filter(fun(_) -> true end, id(bar)) end),
    show("foreach", fun() ->
        lists:foreach(fun(X) -> put(f, X + K) end, L),
        get(f)
    end),
    show("foreach improper", fun() -> lists:foreach(fun(X) -> X end, id([1 | x])) end).

predicates(L) ->
    show("all", fun() -> lists:all(fun(X) -> X > 0 end, L) end),
    show("all false", fun() -> lists:all(fun(X) -> X < 2 end, L) end),
    show("all non-boolean", fun() -> lists:all(fun(X) -> X end, L) end),
    show("all non-list", fun() -> lists:all(fun(X) -> X end, id(x)) end),
    show("all improper", fun() -> lists:all(fun(_) -> true end, id([1 | x])) end),
    show("any", fun() -> lists:any(fun(X) -> X > 3 end, L) end),
    show("any none", fun() -> lists:any(fun(X) -> X > 30 end, L) end),
    show("any non-boolean", fun() -> lists:any(fun(_) -> perhaps end, L) end),
    show("any stops", fun() ->
        lists:any(
            fun(X) ->
                put(a, X),
                X =:= 2
            end,
            L
        ),
        get(a)
    end).

folds(L) ->
    show("foldl", fun() -> lists:foldl(fun(X, A) -> X + A end, 0, L) end),
    show("foldl list", fun() -> lists:foldl(fun(X, A) -> [X | A] end, [], L) end),
    show("foldl improper", fun() -> lists:foldl(fun(X, A) -> X + A end, 0, id([1 | z])) end),
    show("foldl non-list", fun() -> lists:foldl(fun(X, A) -> X + A end, 0, id(z)) end),
    show("foldl badarith", fun() -> lists:foldl(fun(X, A) -> X + A end, a, L) end),
    show("foldl no clause", fun() -> lists:foldl(fun(X, A) when X < 3 -> X + A end, 0, L) end),
    show("foldr", fun() -> lists:foldr(fun(X, A) -> [X | A] end, [], L) end),
    show("foldr order", fun() ->
        lists:foldr(
            fun(X, A) ->
                put(r, [X | dictionary(r)]),
                A
            end,
            0,
            L
        ),
        get(r)
    end),
    show("foldr improper", fun() -> lists:foldr(fun(X, A) -> X + A end, 0, id([1 | z])) end),
    show("mapfoldl", fun() -> lists:mapfoldl(fun(X, A) -> {X * 2, X + A} end, 0, L) end),
    show("mapfoldl badmatch", fun() -> lists:mapfoldl(fun(X, A) -> X + A end, 0, L) end),
    show("mapfoldl improper", fun() -> lists:mapfoldl(fun(X, A) -> {X, A} end, 0, id([1 | q])) end),
    show("mapfoldr", fun() -> lists:mapfoldr(fun(X, A) -> {X * 2, [X | A]} end, [], L) end),
    show("mapfoldr improper", fun() -> lists:mapfoldr(fun(X, A) -> {X, A} end, 0, id([1 | q])) end).

%% filtermap/2 (and zf/2, its old name), partition/2 and search/2: what the fun returns picks the elements.
selections(L) ->
    show("filtermap", fun() ->
        lists:filtermap(
            fun
                (X) when X rem 2 =:= 0 -> {true, X * 10};
                (X) -> X > 2
            end,
            L
        )
    end),
    show("filtermap non-boolean", fun() -> lists:filtermap(fun(X) -> X end, L) end),
    show("filtermap bad pair", fun() -> lists:filtermap(fun(X) -> {false, X} end, L) end),
    show("filtermap triple", fun() -> lists:filtermap(fun(X) -> {true, X, X} end, L) end),
    show("filtermap improper", fun() -> lists:filtermap(fun(_) -> true end, id([1 | w])) end),
    show("filtermap non-list", fun() -> lists:filtermap(fun(_) -> true end, id(w)) end),
    show("zf", fun() -> lists:zf(fun(X) -> X > 2 orelse {true, -X} end, L) end),
    show("zf improper", fun() -> lists:zf(fun(_) -> false end, id([1 | z])) end),
    show("partition", fun() -> lists:partition(fun(X) -> X > 2 end, L) end),
    show("partition empty", fun() -> lists:partition(fun(X) -> X > 2 end, id([])) end),
    show("partition non-boolean", fun() -> lists:partition(fun(_) -> unsure end, L) end),
    show("partition improper", fun() -> lists:partition(fun(_) -> true end, id([1 | p])) end),
    P = fun(X) -> X rem 2 =:= 1 end,
    show("partition bound", fun() -> lists:partition(P, L) end),
    show("search", fun() -> lists:search(fun(X) -> X > 2 end, L) end),
    show("search none", fun() -> lists:search(fun(X) -> X > 20 end, L) end),
    show("search empty", fun() -> lists:search(fun(X) -> X end, id([])) end),
    show("search non-boolean", fun() -> lists:search(fun(X) -> X end, L) end),
    show("search improper", fun() -> lists:search(fun(_) -> false end, id([1 | s])) end),
    show("search stops", fun() -> lists:search(fun(X) -> X > 1 end, id([1, 2 | s])) end).

%% takewhile/2, dropwhile/2 and splitwith/2 stop at the first element the predicate fails for.
prefixes(L) ->
    show("takewhile", fun() -> lists:takewhile(fun(X) -> X < 3 end, L) end),
    show("takewhile all", fun() -> lists:takewhile(fun(X) -> X < 10 end, L) end),
    show("takewhile stops", fun() ->
        lists:takewhile(
            fun(X) ->
                put(t, X),
                X < 2
            end,
            L
        ),
        get(t)
    end),
    show("takewhile before tail", fun() -> lists:takewhile(fun(X) -> X < 2 end, id([1, 2 | t])) end),
    show("takewhile improper", fun() -> lists:takewhile(fun(_) -> true end, id([1 | t])) end),
    show("takewhile non-boolean", fun() -> lists:takewhile(fun(_) -> 0 end, L) end),
    show("dropwhile", fun() -> lists:dropwhile(fun(X) -> X < 3 end, L) end),
    show("dropwhile all", fun() -> lists:dropwhile(fun(X) -> X < 10 end, L) end),
    show("dropwhile rest", fun() -> lists:dropwhile(fun(X) -> X < 2 end, id([1, 2 | d])) end),
    show("dropwhile non-boolean", fun() -> lists:dropwhile(fun(X) -> X end, L) end),
    show("dropwhile improper", fun() -> lists:dropwhile(fun(_) -> true end, id([1 | d])) end),
    show("dropwhile non-list", fun() -> lists:dropwhile(fun(_) -> true end, id(d)) end),
    show("splitwith", fun() -> lists:splitwith(fun(X) -> X < 3 end, L) end),
    show("splitwith all", fun() -> lists:splitwith(fun(X) -> X < 10 end, L) end),
    show("splitwith none", fun() -> lists:splitwith(fun(X) -> X > 10 end, L) end),
    show("splitwith rest", fun() -> lists:splitwith(fun(X) -> X < 2 end, id([1, 2 | r])) end),
    show("splitwith improper", fun() -> lists:splitwith(fun(_) -> true end, id([1 | r])) end),
    show("splitwith non-boolean", fun() -> lists:splitwith(fun(_) -> [] end, L) end).

%% Variables bound once to a fun: their calls and lists calls inline it; anything else still sees the fun.
bound(L, K) ->
    F = fun(X) -> X + K end,
    show("bound map", fun() -> lists:map(F, L) end),
    show("bound call", fun() -> F(5) end),
    show("bound value", fun() -> {is_function(F, 1), (id(F))(1)} end),
    G = fun
        ({A, B}) -> A + B;
        (X) when is_integer(X) -> X
    end,
    io:format("direct: ~p~n", [
        {F(1), lists:map(F, L), lists:foldl(fun(X, A) -> F(X) + A end, 0, L), G(7)}
    ]),
    show("bound clauses", fun() -> {G({1, 2}), G(7)} end),
    show("bound no clause", fun() -> G(x) end),
    show("bound arity", fun() ->
        try
            G(1, 2)
        catch
            error:{badarity, _} -> badarity
        end
    end),
    H = fun(X) -> X * 3 end,
    show("bound in a fun", fun() -> lists:foldl(fun(X, A) -> H(X) + A end, 0, L) end),
    Twice =
        case id(K) of
            10 -> fun(X) -> 2 * X end;
            _ -> fun(X) -> X end
        end,
    show("bound twice", fun() -> Twice(4) end),
    show("literal call", fun() -> (fun(X, Y) -> X - Y end)(10, 3) end).

%% Funs inlined inside inlined funs, deeper than inlining goes, and control flow inside inlined bodies.
nesting(L) ->
    show("nested", fun() ->
        lists:map(fun(X) -> lists:foldl(fun(Y, A) -> X * Y + A end, 0, L) end, L)
    end),
    show("deep", fun() ->
        lists:map(
            fun(A) ->
                lists:map(
                    fun(B) ->
                        lists:map(
                            fun(C) ->
                                lists:map(
                                    fun(D) ->
                                        lists:foldl(fun(E, S) -> A + B + C + D + E + S end, 0, [
                                            1, 2
                                        ])
                                    end,
                                    [1]
                                )
                            end,
                            [1, 2]
                        )
                    end,
                    [A]
                )
            end,
            L
        )
    end),
    show("case", fun() ->
        lists:map(
            fun(X) ->
                case X rem 2 of
                    0 -> even;
                    _ -> odd
                end
            end,
            L
        )
    end),
    show("try", fun() ->
        lists:map(
            fun(X) ->
                try
                    10 div (X - 2)
                catch
                    error:badarith -> inf
                end
            end,
            L
        )
    end),
    show("fun result", fun() ->
        [Made] = lists:map(fun(X) -> fun() -> X + 1 end end, [1]),
        Made()
    end).

%% Long loops whose funs allocate, so collections move the lists and accumulators the loops hold.
collections() ->
    Seq = lists:seq(1, 20000),
    show("sum", fun() ->
        lists:foldl(fun(X, A) -> X + A end, 0, lists:map(fun(X) -> X * 3 + 1 end, Seq))
    end),
    show("tuples", fun() ->
        Pairs = lists:map(fun(X) -> {X, [X, X]} end, Seq),
        lists:foldl(fun({X, [Y, Z]}, A) -> X + Y + Z + A end, 0, Pairs)
    end),
    show("filtered", fun() -> length(lists:filter(fun(X) -> X rem 7 =:= 0 end, Seq)) end),
    show("flattened", fun() ->
        length(lists:flatmap(fun(X) -> lists:duplicate(X rem 4, X) end, Seq))
    end),
    show("right", fun() -> hd(lists:foldr(fun(X, A) -> [X * 2 | A] end, [], Seq)) end),
    show("mapfold", fun() ->
        {Doubled, Sum} = lists:mapfoldl(fun(X, A) -> {[X], X + A} end, 0, Seq),
        {length(Doubled), Sum}
    end),
    show("filtermapped", fun() ->
        Kept = lists:filtermap(fun(X) -> X rem 3 =:= 0 andalso {true, {X, [X]}} end, Seq),
        {length(Kept), lists:last(Kept)}
    end),
    show("partitioned", fun() ->
        {Small, Big} = lists:partition(fun({X, _}) -> X < 15000 end, [{X, [X]} || X <- Seq]),
        {length(Small), hd(Big)}
    end),
    show("split", fun() ->
        {Prefix, Rest} = lists:splitwith(fun({X, _}) -> X < 12345 end, [{X, [X]} || X <- Seq]),
        {length(Prefix), hd(Rest), length(lists:takewhile(fun({X, _}) -> X < 777 end, Prefix))}
    end),
    show("dropped", fun() -> hd(lists:dropwhile(fun(X) -> X < 19999 end, Seq)) end),
    show("searched", fun() -> lists:search(fun(X) -> X * X > 300000000 end, Seq) end).

%% A fun that receives inside a loop: the loop suspends and resumes with its state intact.
receiving() ->
    Self = self(),
    Pid = spawn(fun() ->
        Sum = lists:foldl(
            fun(_, A) ->
                receive
                    {add, N} -> A + N
                end
            end,
            0,
            [a, b, c]
        ),
        Self ! {sum, Sum}
    end),
    lists:foreach(fun(N) -> Pid ! {add, N} end, [1, 20, 300]),
    show("receive", fun() ->
        receive
            {sum, S} -> S
        end
    end).

dictionary(Key) ->
    case get(Key) of
        undefined -> [];
        V -> V
    end.
