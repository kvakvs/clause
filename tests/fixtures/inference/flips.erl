%% Step 58Q: summaries of tree-shaped results can flip between two facts from round to round, because joins are not
%% monotone at the container depth limit. A member that returns to one of its last summaries gives up alone
%% (`term()`, shown as `_`) so its recursive group still converges below its round cap; `expect:` is what a monotone
%% join would give (docs/semantic.md#inference).
-module(flips).
-export([main/1]).

%% expect: main(_) -> ok
main(_) ->
    Tree = insert(3, c, insert(1, a, insert(2, b, empty))),
    io:format("~p~n", [map(fun(_, V) -> {V} end, Tree)]),
    io:format("flips: ok~n").

%% expect: insert(1..3, a | b | c, empty) -> {empty, 1..3, a | b | c, empty}; (1..3, a | b | c, {_, _, _, _}) -> {_, _, _, _}
insert(K, V, empty) ->
    {empty, K, V, empty};
insert(K, V, {L, K1, V1, R}) when K < K1 ->
    {insert(K, V, L), K1, V1, R};
insert(K, V, {L, K1, V1, R}) ->
    {L, K1, V1, insert(K, V, R)}.

%% expect: map(fun((_, _) -> {_}), empty | {_, _, _, _} | {_, _, _, _, _, _, _}) -> empty | {_, _, _, _} | {_, _, _, _, _, _, _}
%% today: map(fun((_, _) -> {_}), empty | {_, _, _, _} | {_, _, _, _, _, _, _}) -> _
map(_, empty) ->
    empty;
map(F, {A, K, V, B}) ->
    {map(F, A), K, F(K, V), map(F, B)};
map(F, {A, K1, V1, B, K2, V2, C}) ->
    {map(F, A), K1, F(K1, V1), map(F, B), K2, F(K2, V2), map(F, C)}.
