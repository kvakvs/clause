%% Recursions over trees (steps 58Q, 58S): a recursive result that rebuilds a tree climbs to the container depth
%% limit, where one set of values could take several union shapes, so rounds kept changing until each union dropped
%% the members another member holds (58S). map/2 converges to the precise tree below (docs/semantic.md#inference).
-module(trees).
-export([main/1]).

%% expect: main(_) -> ok
main(_) ->
    Tree = insert(3, c, insert(1, a, insert(2, b, empty))),
    io:format("~p~n", [map(fun(_, V) -> {V} end, Tree)]),
    io:format("trees: ok~n").

%% expect: insert(1..3, a | b | c, empty) -> {empty, 1..3, a | b | c, empty}; (1..3, a | b | c, {_, _, _, _}) -> {_, _, _, _}
insert(K, V, empty) ->
    {empty, K, V, empty};
insert(K, V, {L, K1, V1, R}) when K < K1 ->
    {insert(K, V, L), K1, V1, R};
insert(K, V, {L, K1, V1, R}) ->
    {L, K1, V1, insert(K, V, R)}.

%% expect: map(fun((_, _) -> {_}), empty) -> empty; (fun((_, _) -> {_}), {_, _, _, _}) -> {empty | {empty | {empty | {empty | {_, _, _, _}, _, {_}, empty | {_, _, _, _}}, _, {_}, empty | {empty | {_, _, _, _}, _, {_}, empty | {_, _, _, _}}}, _, {_}, empty | {empty | {empty | {_, _, _, _}, _, {_}, empty | {_, _, _, _}}, _, {_}, empty | {empty | {_, _, _, _}, _, {_}, empty | {_, _, _, _}}}}, _, {_}, empty | {empty | {empty | {empty | {_, _, _, _}, _, {_}, empty | {_, _, _, _}}, _, {_}, empty | {empty | {_, _, _, _}, _, {_}, empty | {_, _, _, _}}}, _, {_}, empty | {empty | {empty | {_, _, _, _}, _, {_}, empty | {_, _, _, _}}, _, {_}, empty | {empty | {_, _, _, _}, _, {_}, empty | {_, _, _, _}}}}}
map(_, empty) ->
    empty;
map(F, {A, K, V, B}) ->
    {map(F, A), K, F(K, V), map(F, B)}.
