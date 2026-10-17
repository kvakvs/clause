-module(small_maps).
-export([main/1]).

%% Small-map lookups (plan 20 step OPT3.12): generated code reads and copies classic map cells for
%% atom and small-integer keys; trees, other keys and errors take the map service.
main(_) ->
    State = #{count => 0, total => 0, last => none, 7 => seven, name => <<"s">>},
    Literal = #{a => 1, b => 2},
    io:format("~p~n", [lookups(State, Literal)]),
    io:format("~p~n", [clauses([State, Literal, #{}, #{b => x}, tree(), not_a_map])]),
    io:format("~p~n", [guards([State, Literal, #{}, tree(), [], 3])]),
    io:format("~p~n", [updates(State, Literal)]),
    io:format("~p~n", [errors(State)]),
    io:format("~p~n", [sorted(loop(State, 1000))]),
    io:format("small_maps: ok~n").

lookups(State, Literal) ->
    #{count := Count, 7 := Seven} = State,
    {
        Count,
        Seven,
        map_get(name, State),
        maps:get(total, State),
        map_get(b, Literal),
        is_map_key(last, State),
        is_map_key(missing, State),
        is_map_key(1.0, #{1 => one}),
        is_map_key({k}, #{{k} => tuple}),
        map_get({k}, #{{k} => tuple}),
        map_get(1, #{1 => int, 1.0 => float}),
        map_get(1.0, #{1 => int, 1.0 => float}),
        map_size(State),
        map_size(#{}),
        map_size(tree()),
        map_get(40, tree())
    }.

%% Clause heads: a missing key falls through to the next clause, a non-map to the last.
clauses(Maps) -> [head(M) || M <- Maps].

head(#{count := C, last := L}) -> {state, C, L};
head(#{a := A}) -> {literal, A};
head(#{40 := V}) -> {tree, V};
head(#{}) -> empty_or_other;
head(_) -> not_a_map.

guards(Values) -> [guard(V) || V <- Values].

guard(M) when is_map_key(count, M), map_get(count, M) =:= 0 -> counted;
guard(M) when map_size(M) =:= 0 -> empty;
guard(M) when is_map(M), map_size(M) > 32 -> tree;
guard(M) when is_map(M) -> small;
guard(_) -> other.

updates(State, Literal) ->
    Exact = State#{count := 1, last := first},
    Again = Exact#{id(count) := 2, count := 3},
    Put = State#{total => 10},
    Added = State#{new => added},
    FromLiteral = Literal#{a := 10},
    Mixed = State#{{tuple} => key, 7 := sept},
    Tree = (tree())#{40 := forty},
    [
        sorted(M)
     || M <- [Exact, Again, Put, Added, FromLiteral, Literal, Mixed, State]
    ] ++ [map_get(40, Tree)].

errors(State) ->
    [
        reason(fun() -> map_get(missing, State) end),
        reason(fun() -> map_get(count, id(not_a_map)) end),
        reason(fun() -> maps:get(missing, State) end),
        reason(fun() -> map_size(id([])) end),
        reason(fun() -> State#{missing := 1} end),
        reason(fun() -> (id({}))#{count := 1} end)
    ].

reason(Fun) ->
    try Fun() of
        Value -> {value, Value}
    catch
        error:Reason -> Reason
    end.

id(Value) -> Value.

sorted(Map) -> lists:sort(maps:to_list(Map)).

%% A state-record loop: reads and updates of the same map on every step.
loop(State, 0) ->
    State;
loop(#{count := C, total := T} = State, N) ->
    loop(State#{count := C + 1, total := T + N, last := N}, N - 1).

tree() -> maps:from_list([{K, K * K} || K <- lists:seq(1, 40)]).
