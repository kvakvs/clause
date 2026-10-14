-module(unicode_tables).
-export([main/1]).

%% unicode_util's tables (plan step 41O1): the lookup, case, width,
%% whitespace and identifier answers of plane 0 and sample blocks of the
%% other planes, digested per block of 4096 code points so a difference names
%% its block (every code point would take minutes in a debug runtime);
%% then cp/1 over mixed chardata, the case functions and their errors.
main([]) ->
    io:format("~w~n", [unicode_util:spec_version()]),
    io:format("~w~n", [unicode_util:pattern_whitespace()]),
    io:format("~p~n", [[block(First) || First <- blocks()]]),
    io:format("~p~n", [samples()]),
    io:format("~p~n", [chardata()]),
    io:format("~p~n", [errors()]),
    io:format("unicode_tables: ok~n").

%% Every block of plane 0, where the dense data lives, and blocks of mathematical alphanumerics, emoji, CJK, tags,
%% private use and the last code points.
blocks() ->
    lists:seq(0, 16#FFFF, 4096) ++ [16#1D000, 16#1F000, 16#20000, 16#E0000, 16#F0000, 16#10F000].

%% The MD5 of the answers of the 4096 code points from First, as hexadecimal; equal answers give equal bytes.
block(First) ->
    Digest = erlang:md5(term_to_binary([answers(CP) || CP <- lists:seq(First, First + 4095)])),
    {integer_to_list(First, 16), binary:encode_hex(Digest)}.

answers(CP) ->
    #{ccc := CCC, canon := Canon, compat := Compat, category := Category} = unicode_util:lookup(CP),
    #{upper := U, lower := L, title := T, fold := F} = unicode_util:get_case(CP),
    {CCC, Canon, Compat, Category, unicode_util:category(CP), {U, L, T, F},
        unicode_util:is_wide(CP), unicode_util:is_whitespace(CP),
        unicode_util:is_other_id_start(CP), unicode_util:is_other_id_continue(CP),
        unicode_util:is_letter_not_pattern_syntax(CP)}.

%% Answers in full for code points of every kind.
samples() ->
    [
        {CP, lists:sort(maps:to_list(unicode_util:lookup(CP))),
            lists:sort(maps:to_list(unicode_util:get_case(CP)))}
     || CP <- [
            $A,
            $z,
            16#C5,
            16#DF,
            16#1C5,
            16#3A3,
            16#1E9E,
            16#FB01,
            16#AC00,
            16#D7A3,
            16#2126,
            16#212B,
            16#0344,
            16#1F600,
            16#E000,
            16#F0000,
            16#D800,
            16#10FFFF,
            16#378,
            16#4E00,
            16#30000,
            16#FDFA,
            16#2460
        ]
    ].

chardata() ->
    [
        unicode_util:cp("abc"),
        unicode_util:cp([]),
        unicode_util:cp(<<>>),
        unicode_util:cp(<<"åb"/utf8>>),
        unicode_util:cp([<<"x">>, "y"]),
        unicode_util:cp([[], [[$a]], $b]),
        unicode_util:cp([[$a, $b, $c, $d, $e], $f]),
        unicode_util:cp([[$a, [$b, [$c, $d]]], <<"e">>]),
        unicode_util:cp([[$a | <<"bc">>], "d"]),
        unicode_util:cp([<<>>, [], <<"z">>]),
        unicode_util:cp(<<255, 1>>),
        unicode_util:cp([<<255>>, "a"]),
        unicode_util:uppercase("abc"),
        unicode_util:uppercase(<<"ßx"/utf8>>),
        unicode_util:lowercase([[16#130], "X"]),
        unicode_util:titlecase(<<"ǆa"/utf8>>),
        unicode_util:casefold("ẞ"),
        unicode_util:uppercase([]),
        unicode_util:is_wide("a"),
        unicode_util:is_wide([$a, 16#FE0F]),
        unicode_util:is_wide([$a, 16#4E00]),
        unicode_util:is_wide([]),
        unicode_util:is_whitespace([13, 10]),
        unicode_util:is_whitespace("ab"),
        unicode_util:is_whitespace(x),
        unicode_util:is_other_id_start(x),
        unicode_util:is_letter_not_pattern_syntax(x),
        unicode_util:is_letter_not_pattern_syntax($!)
    ].

errors() ->
    [
        outcome(fun() -> unicode_util:uppercase(<<255>>) end),
        outcome(fun() -> unicode_util:lookup(-1) end),
        outcome(fun() -> unicode_util:lookup(16#110000) end),
        outcome(fun() -> unicode_util:category(a) end),
        outcome(fun() -> unicode_util:get_case(1.0) end),
        outcome(fun() -> unicode_util:is_wide(x) end),
        outcome(fun() -> unicode_util:cp(x) end),
        outcome(fun() -> unicode_util:cp([x]) end)
    ].

%% The value of Fun(), or its error class and reason.
outcome(Fun) ->
    try
        Fun()
    catch
        Class:Reason -> {Class, Reason}
    end.
