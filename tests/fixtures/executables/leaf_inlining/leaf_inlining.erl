-module(leaf_inlining).
-export([main/1, id/1, exported_leaf/1, twice/1, picky/1]).
-record(point, {x = 0, y = 0}).

%% At O2, calls of small functions of the same module that call no Erlang code lower the callee's clauses in place
%% (leaf inlining). Results, evaluation order, error reasons and the function_clause of no matching clause must stay
%% those of calling the function.
id(X) -> X.

%% Errors print without stack traces: an inlined leaf has no frame of its own (docs/differences.md).
show(Name, F) ->
    R =
        try F() of
            V -> {ok, V}
        catch
            C:E -> {C, E}
        end,
    io:format("~s: ~p~n", [Name, R]).

%% No clause matching calls the function, so its frame is on top as in OTP.
show_top(Name, F) ->
    R =
        try F() of
            V -> {ok, V}
        catch
            C:E:S -> {C, E, top(S)}
        end,
    io:format("~s: ~p~n", [Name, R]).

%% The function on top of a stack trace, without arguments or location.
top([{M, F, A, _} | _]) when is_list(A) -> {M, F, length(A)};
top([{M, F, A, _} | _]) -> {M, F, A};
top(_) -> none.

main(_) ->
    L = id([1, 2, 3, 4]),
    clauses(L),
    errors(),
    values(L),
    contexts(L),
    nesting(L),
    fusion(L),
    sinking(),
    io:format("leaf_inlining: ok~n").

diff(A, B) when A > B -> A - B;
diff(A, B) -> B - A.

clamp(X, Lo, _) when X < Lo -> Lo;
clamp(X, _, Hi) when X > Hi -> Hi;
clamp(X, _, _) -> X.

small(0) -> zero;
small(N) when is_integer(N), N < 10 -> digit;
small(N) when is_integer(N) -> number.

exported_leaf(X) -> {X, X}.

clauses(L) ->
    show("guards", fun() -> [diff(X, 2) || X <- L] end),
    show("constant arguments", fun() -> [clamp(X, 2, 3) || X <- L] end),
    show("selected", fun() -> {small(id(0)), small(id(7)), small(id(70))} end),
    show_top("no clause", fun() -> small(id(seven)) end),
    show_top("no clause twice", fun() -> {small(id(1)), small(id(1.5))} end),
    show("exported", fun() -> exported_leaf(id(3)) end).

divide(A, B) -> A div B.

first(T) -> element(1, T).

matched({ok, V}) -> V.

bound(X) ->
    {A, B} = X,
    A + B.

errors() ->
    show("badarith", fun() -> divide(id(1), id(0)) end),
    show("badarg", fun() -> first(id(notuple)) end),
    show("badmatch", fun() -> bound(id({1, 2, 3})) end),
    show("caught", fun() ->
        try
            divide(id(1), id(0))
        catch
            error:badarith -> caught
        end
    end),
    show_top("leaf no clause", fun() -> matched(id(error)) end).

half(X) -> X / 2.

point(X) -> #point{x = X, y = X * 2}.

move(#point{x = X} = P, D) -> P#point{x = X + D}.

tag(K, V) -> #{K => V, size => 1}.

pick(#{size := S}) -> S.

parity(N) ->
    case N rem 2 of
        0 -> Kind = even;
        _ -> Kind = odd
    end,
    {N, Kind}.

values(L) ->
    show("floats", fun() -> [half(X) + 0.5 || X <- L] end),
    show("records", fun() -> move(point(id(3)), 4) end),
    show("maps", fun() -> {lists:sort(maps:to_list(tag(a, 1))), pick(tag(b, 2))} end),
    show("case bindings", fun() -> [parity(X) || X <- L] end).

contexts(L) ->
    show("in a fun", fun() -> lists:map(fun(X) -> diff(X, 3) end, L) end),
    show("in a fold", fun() -> lists:foldl(fun(X, A) -> clamp(X, 2, 3) + A end, 0, L) end),
    show("nested calls", fun() -> diff(diff(id(10), 3), clamp(id(9), 0, 5)) end),
    show("order", fun() ->
        put(log, []),
        R = diff(log(1), log(2)),
        {R, get(log)}
    end),
    show("tail", fun() -> tail(id(5)) end),
    show("loop", fun() -> total(lists:seq(1, 1000), 0) end).

log(X) ->
    put(log, [X | get(log)]),
    X.

tail(X) -> diff(X, 1).

total([], Acc) -> Acc;
total([X | T], Acc) -> total(T, Acc + diff(X, 500)).

%% Leaves may call leaves, three deep (nested leaves); the fourth level stays a call.
scale(X) -> clamp(diff(X, 5), 0, 3) * 2.

level1(X) -> level2(X) + 1.
level2(X) -> level3(X) + 1.
level3(X) -> level4(X) + 1.
level4(X) -> X * 10.

%% Errors raised in a nested leaf, and no clause of one matching.
ratio(A, B) -> divide(A, B) + 1.
sized(N) -> small(N).

%% Recursive functions are no leaves.
ping(0) -> done;
ping(N) -> pong(N - 1).
pong(0) -> done;
pong(N) -> ping(N - 1).

%% A leaf larger than the old 24-expression limit, inlined while the caller's budget pays for it.
mix(A, B) ->
    C = A * 3 + B,
    D = C band 255,
    E = (D bsl 2) bxor A,
    F = E + (B rem 17),
    G = F - (A div 3),
    H = G band 65535,
    H + C - D.

twice(X) -> X * 2.

picky(X) when is_integer(X) -> X.

nesting(L) ->
    show("nested", fun() -> [scale(X) || X <- L ++ [9, 20]] end),
    show("four levels", fun() -> level1(id(2)) end),
    show("nested badarith", fun() -> ratio(id(1), id(0)) end),
    show_top("nested no clause", fun() -> sized(id(seven)) end),
    show("recursive", fun() -> {ping(id(3)), ping(id(4))} end),
    show("large leaf", fun() -> [mix(X, X + 1) || X <- L] end),
    show("budget", fun() ->
        {mix(1, 2), mix(3, 4), mix(5, 6), mix(7, 8), mix(9, 10), mix(11, 12)}
    end),
    show("remote self call", fun() -> [?MODULE:twice(X) || X <- L] end),
    show_top("remote no clause", fun() -> ?MODULE:picky(id(seven)) end).

%% Tuples and cons cells built and matched in one function are read from the words they were built of
%% (construct/deconstruct fusion); results, sharing and error payloads stay those of building them.
divmod(X, Y) -> {X div Y, X rem Y}.

lookup(X) when X band 3 =:= 0 -> error;
lookup(X) -> {ok, X}.

classify(X) ->
    case X rem 3 of
        0 -> {zero, X};
        1 -> {one, X + 1};
        _ -> other
    end.

push(H, T) -> [H | T].

split([H | T]) -> {H, T};
split([]) -> empty.

nest(X) -> {{inner, X}, X + 1}.

pair(A, B) -> {A, B}.

fusion(L) ->
    show("divmod", fun() ->
        [
            begin
                {Q, R} = divmod(X, 3),
                Q * 10 + R
            end
         || X <- L ++ [7]
        ]
    end),
    show("ok or error", fun() ->
        [
            case lookup(X) of
                {ok, V} -> V;
                error -> none
            end
         || X <- L
        ]
    end),
    show("three shapes", fun() ->
        [
            case classify(X) of
                {zero, V} -> {z, V};
                {one, V} -> {o, V};
                other -> o
            end
         || X <- L
        ]
    end),
    show("cons", fun() ->
        [
            begin
                [H | T] = push(X, L),
                H + length(T)
            end
         || X <- L
        ]
    end),
    show("cons or empty", fun() ->
        [
            case split(Xs) of
                {H, _} -> H;
                empty -> nothing
            end
         || Xs <- [L, []]
        ]
    end),
    show("nested tuples", fun() ->
        [
            begin
                {{inner, P}, Q} = nest(X),
                P + Q
            end
         || X <- L
        ]
    end),
    show("same function", fun() ->
        X = id(4),
        {A, B} = {X, X + 1},
        A * B
    end),
    show("kept and matched", fun() ->
        T = divmod(id(17), 5),
        {Q, _} = T,
        {Q, T}
    end),
    show("built argument", fun() -> bound(pair(id(3), id(4))) end),
    show("mapfoldl", fun() -> lists:mapfoldl(fun(X, A) -> {X * 2, A + X} end, 0, L) end),
    show("filtermap", fun() ->
        lists:filtermap(
            fun(X) ->
                case X rem 2 of
                    0 -> {true, X * 3};
                    _ -> false
                end
            end,
            L
        )
    end).

%% A failing match raises with the term it was given, built on the error path only (allocation sinking).
sinking() ->
    show("case_clause tuple", fun() ->
        case classify(id(4)) of
            {zero, V} -> V
        end
    end),
    show("case_clause atom", fun() ->
        case classify(id(5)) of
            {zero, V} -> V
        end
    end),
    show("badmatch built here", fun() ->
        {x, Y} = divmod(id(9), 2),
        Y
    end),
    show("badmatch cons", fun() ->
        [a | T] = push(id(1), [2]),
        T
    end),
    show("badmatch nested", fun() ->
        {{outer, P}, _} = nest(id(6)),
        P
    end).
