-module(unicode_clusters).
-export([main/1]).

%% unicode_util:gc/1 and normalization (plan step 41O2): every grapheme
%% cluster rule on hand-picked strings, as lists, binaries and mixed
%% chardata with the exact rest after the first cluster; then a seeded sweep
%% of random strings over code points of every break class, digested.
main([]) ->
    io:format("~w~n", [[first(S) || S <- strings()]]),
    io:format("~w~n", [[all(S) || S <- strings()]]),
    io:format("~w~n", [[normalized(S) || S <- normalization()]]),
    io:format("~w~n", [shapes()]),
    io:format("~w~n", [errors()]),
    io:format("~s~n", [sweep(2000)]),
    io:format("unicode_clusters: ok~n").

strings() ->
    [
        "",
        "a",
        "ab",
        "\r\n",
        "\r\nx",
        "\r",
        "\n\r",
        [$a, 16#301, 16#302, $b],
        [16#E9, 16#301],
        [16#600, $a],
        [16#600, $\n],
        [16#600],
        [16#1100, 16#1161, 16#11A8, $a],
        [16#1100, 16#1100, 16#AC00, 16#11A8],
        [16#AC00, 16#1161, 16#11A8],
        [16#AC01, 16#11A8, 16#11A8],
        [16#1161, 16#1161, 16#11A8],
        [16#11A8, 16#11A8],
        [16#1F1E6, 16#1F1E8, 16#1F1E6],
        [16#1F1E6, 16#301],
        [16#1F468, 16#200D, 16#1F469, 16#200D, 16#1F467],
        [16#1F44D, 16#1F3FD, $x],
        [16#A9, 16#FE0F, $a],
        [16#2764, 16#200D, $a],
        [16#915, 16#94D, 16#937, 16#93F],
        [16#915, 16#94D, 16#200D, 16#937],
        [16#915, 16#93C, 16#94D, 16#937, $a],
        [16#915, 16#94D, $a],
        [16#AD, 16#301],
        [16#7F, 16#301],
        [$a, 16#200D, 16#1F600],
        [16#ABE0, 16#ABEC],
        [16#D7B0, 16#D7CB],
        [16#E0001, 16#E0020],
        [16#0E33, 16#0E01],
        [16#0D4E, 16#0D15]
    ].

%% The first cluster of a string as a list, a binary and nested chardata.
first(S) ->
    Binary = unicode:characters_to_binary(S),
    {
        unicode_util:gc(S),
        unicode_util:gc(Binary),
        unicode_util:gc([S]),
        unicode_util:gc([Binary, "z"])
    }.

%% Every cluster of a string, as a list and as a binary.
all(S) ->
    {clusters(S), clusters(unicode:characters_to_binary(S))}.

clusters(S) ->
    case unicode_util:gc(S) of
        [GC | Rest] -> [GC | clusters(Rest)];
        [] -> [];
        {error, _} = Error -> [Error]
    end.

normalization() ->
    [
        [16#E9],
        [$e, 16#301],
        [$a, 16#323, 16#302],
        [$a, 16#302, 16#323],
        [16#1E09],
        [16#212B],
        [16#AC00],
        [16#AC01],
        [16#1100, 16#1161],
        [16#1100, 16#1161, 16#11A8],
        [16#FB01],
        [16#2460],
        [16#BD],
        [16#1E9B, 16#323],
        [16#0344],
        [16#FDFA],
        [$A, 16#30A, 16#301],
        [16#3D3],
        [16#1D160],
        [16#F900],
        [16#0F73],
        "plain ascii",
        [16#FF21, 16#FF22]
    ].

%% Every normal form of a string, cluster by cluster.
normalized(S) ->
    [walk(Form, S) || Form <- [nfd, nfc, nfkd, nfkc]].

walk(Form, S) ->
    case unicode_util:Form(S) of
        [GC | Rest] -> [GC | walk(Form, Rest)];
        [] -> [];
        {error, _} = Error -> [Error]
    end.

shapes() ->
    [
        unicode_util:gc([<<"ab">>]),
        unicode_util:gc([$a, <<"b">>]),
        unicode_util:gc([$a | <<"b">>]),
        unicode_util:gc([$a, <<16#301/utf8>>, $b]),
        unicode_util:gc([[$a], [16#301], "b"]),
        unicode_util:gc(<<"a", 16#301/utf8, "b">>),
        unicode_util:gc([$a, 16#301 | <<"b">>]),
        unicode_util:gc([<<16#1F1E6/utf8>>, <<16#1F1E8/utf8, "x">>]),
        unicode_util:nfd(<<16#E9/utf8, "x">>),
        unicode_util:nfc([$e, <<16#301/utf8>>]),
        unicode_util:nfkd([[16#FB01] | <<"x">>]),
        unicode_util:nfkc(<<16#2460/utf8>>)
    ].

errors() ->
    [
        unicode_util:gc(<<255>>),
        unicode_util:gc([$a, <<255>>]),
        unicode_util:gc([$a, 16#301, <<255>>]),
        unicode_util:gc([16#1F600, <<255>>]),
        unicode_util:gc([16#600, <<255>>]),
        unicode_util:nfd(<<255>>),
        unicode_util:nfc([<<255>>])
    ].

%% Digests of the clusters and normal forms of Count random strings from a pool of code points of every class.
sweep(Count) ->
    Pool = list_to_tuple(pool()),
    Strings = strings(Count, 16#2545F491, Pool, []),
    Results = [{clusters(S), normalized(S)} || S <- Strings],
    binary:encode_hex(erlang:md5(term_to_binary(Results))).

strings(0, _, _, Acc) ->
    Acc;
strings(Count, Seed0, Pool, Acc) ->
    {Length, Seed1} = next(Seed0, 6),
    {S, Seed2} = string(Length + 1, Seed1, Pool, []),
    strings(Count - 1, Seed2, Pool, [S | Acc]).

string(0, Seed, _, Acc) ->
    {Acc, Seed};
string(Length, Seed0, Pool, Acc) ->
    {Index, Seed1} = next(Seed0, tuple_size(Pool)),
    string(Length - 1, Seed1, Pool, [element(Index + 1, Pool) | Acc]).

%% A linear congruential generator: a number below Range and the next seed.
next(Seed, Range) ->
    Next = (Seed * 1103515245 + 12345) band 16#7FFFFFFF,
    {(Next bsr 8) rem Range, Next}.

pool() ->
    [
        $a,
        $Z,
        $\r,
        $\n,
        0,
        16#7F,
        16#AD,
        16#A9,
        16#E9,
        16#301,
        16#323,
        16#302,
        16#600,
        16#903,
        16#915,
        16#93C,
        16#94D,
        16#937,
        16#200D,
        16#200C,
        16#1100,
        16#1161,
        16#11A8,
        16#AC00,
        16#AC01,
        16#D7B0,
        16#1F1E6,
        16#1F1E8,
        16#1F468,
        16#1F3FD,
        16#FE0F,
        16#FB01,
        16#212B,
        16#1E9B,
        16#0344,
        16#2460,
        16#FF21,
        16#4E00,
        16#E0020,
        16#0E33
    ].
