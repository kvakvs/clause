%% Counters inside containers (plan 22 CONV.0): a recursive result that counts inside a tuple and a local input that
%% counts inside a map grow by one each round or pass. Widening relaxes integer bounds at every level of tuples, maps
%% and lists, so both reach their category and settle instead of ending at term().
-module(widening).
-export([main/1]).

%% expect: main(_) -> ok
main(_) ->
    io:format("~p~n", [depth({node, {node, leaf, leaf}, leaf})]),
    io:format("~p~n", [tally([a, b, a], #{seen => 0})]),
    io:format("widening: ok~n").

%% expect: depth(leaf) -> {depth, 0}; ({node, leaf | {node, leaf, leaf}, leaf}) -> {depth, pos_integer()}
depth(leaf) ->
    {depth, 0};
depth({node, L, R}) ->
    {depth, D} = depth(L),
    {depth, E} = depth(R),
    {depth, max(D, E) + 1}.

%% expect: tally([], #{seen := non_neg_integer()}) -> #{seen := non_neg_integer()}; ([a | b, ...], #{seen := non_neg_integer()}) -> #{seen := pos_integer()}
tally([], Counts) ->
    Counts;
tally([_ | T], #{seen := N} = Counts) ->
    tally(T, Counts#{seen := N + 1}).
