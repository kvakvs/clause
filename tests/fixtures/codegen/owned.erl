-module(owned).
-export([chain/0, shared/0]).

%% Updates of a map nothing else reads change it in place (plan 20 step OPT0.5B).
chain() ->
    A = #{x => 1},
    B = A#{y => 2},
    B#{z => 3}.

%% A source map read again keeps its value: the update copies.
shared() ->
    A = #{x => 1},
    B = A#{y => 2},
    {A, B}.
