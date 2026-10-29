-module(leaf_inlining).
-export([main/1, id/1, exported_leaf/1]).
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
