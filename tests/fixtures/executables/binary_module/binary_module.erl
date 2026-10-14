-module(binary_module).
-export([main/1]).

%% The binary module (plan step 41W): searching, splitting, replacing, parts,
%% conversions and encodings with valid, boundary and invalid arguments.
main([]) ->
    [io:format("~w~n", [outcome(F, A)]) || {F, A} <- calls()],
    io:format("~w~n", [replaces()]),
    io:format("~w~n", [compiled()]),
    io:format("binary_module: ok~n").

%% The result of binary:F(A...), or the error it raises.
outcome(F, A) ->
    try
        {F, apply(binary, F, A)}
    catch
        error:R -> {F, error, R}
    end.

calls() ->
    S = <<"abcabcab">>,
    [
        {match, [S, <<"ca">>]},
        {match, [S, <<"x">>]},
        {match, [S, [<<"bc">>, <<"b">>, <<"abc">>]]},
        {match, [S, [<<"c">>, <<"ab">>]]},
        {match, [S, <<"ab">>, [{scope, {1, 7}}]]},
        {match, [S, <<"ab">>, [{scope, {8, -4}}]]},
        {match, [S, <<"ab">>, [{scope, {9, 0}}]]},
        {match, [S, <<"ab">>, [{scope, {2, 7}}]]},
        {match, [S, <<"ab">>, [{scope, {-1, 2}}]]},
        {match, [S, <<"ab">>, [bad]]},
        {match, [S, <<>>]},
        {match, [S, []]},
        {match, [S, [<<"a">>, <<>>]]},
        {match, [S, a]},
        {match, [<<1:1>>, <<"a">>]},
        {match, [<<>>, <<"a">>]},
        {matches, [S, <<"ab">>]},
        {matches, [S, [<<"a">>, <<"abc">>]]},
        {matches, [<<"aaaa">>, <<"aa">>]},
        {matches, [S, <<"ab">>, [{scope, {2, 6}}]]},
        {matches, [S, <<"z">>]},
        {split, [S, <<"c">>]},
        {split, [S, <<"c">>, [global]]},
        {split, [<<",a,,b,,">>, <<",">>, [global]]},
        {split, [<<",a,,b,,">>, <<",">>, [global, trim]]},
        {split, [<<",a,,b,,">>, <<",">>, [global, trim_all]]},
        {split, [<<",a,,b,,">>, <<",">>, [trim_all]]},
        {split, [<<>>, <<",">>]},
        {split, [<<>>, <<",">>, [trim]]},
        {split, [<<>>, <<",">>, [trim_all]]},
        {split, [S, <<"b">>, [global, {scope, {2, 4}}]]},
        {split, [S, [<<"a">>, <<"bc">>], [global]]},
        {split, [S, <<"q">>, [global, trim_all]]},
        {split, [S, <<"b">>, [global, x]]},
        {part, [S, 1, 3]},
        {part, [S, 8, -3]},
        {part, [S, {0, 8}]},
        {part, [S, {8, 0}]},
        {part, [S, 7, 2]},
        {part, [S, -1, 1]},
        {part, [S, {1, -2}]},
        {part, [a, 0, 0]},
        {at, [S, 0]},
        {at, [S, 7]},
        {at, [S, 8]},
        {at, [S, -1]},
        {first, [S]},
        {first, [<<>>]},
        {last, [S]},
        {last, [<<>>]},
        {bin_to_list, [<<1, 2, 3>>]},
        {bin_to_list, [<<1, 2, 3>>, {1, 2}]},
        {bin_to_list, [<<1, 2, 3>>, 3, -2]},
        {bin_to_list, [<<1, 2, 3>>, 2, 2]},
        {list_to_bin, [[1, <<2>>, [3]]]},
        {list_to_bin, [[256]]},
        {list_to_bin, [a]},
        {copy, [<<"ab">>]},
        {copy, [<<"ab">>, 3]},
        {copy, [<<"ab">>, 0]},
        {copy, [<<"ab">>, -1]},
        {longest_common_prefix, [[<<"abcd">>, <<"abxy">>, <<"ab">>]]},
        {longest_common_prefix, [[<<"abc">>]]},
        {longest_common_prefix, [[<<"x">>, <<"y">>]]},
        {longest_common_prefix, [[]]},
        {longest_common_suffix, [[<<"xbc">>, <<"abc">>]]},
        {longest_common_suffix, [[<<"a">>, a]]},
        {referenced_byte_size, [S]},
        {decode_unsigned, [<<1, 0>>]},
        {decode_unsigned, [<<1, 0>>, little]},
        {decode_unsigned, [<<>>]},
        {decode_unsigned, [<<255, 255, 255, 255, 255, 255, 255, 255, 255>>]},
        {decode_unsigned, [<<1>>, middle]},
        {encode_unsigned, [256]},
        {encode_unsigned, [256, little]},
        {encode_unsigned, [0]},
        {encode_unsigned, [1 bsl 70]},
        {encode_unsigned, [-1]},
        {encode_hex, [<<1, 171, 255>>]},
        {encode_hex, [<<1, 171, 255>>, lowercase]},
        {encode_hex, [<<>>]},
        {encode_hex, [<<1>>, mixed]},
        {decode_hex, [<<"01abFF">>]},
        {decode_hex, [<<"0">>]},
        {decode_hex, [<<"0g">>]},
        {join, [[<<"a">>, <<"b">>, <<"c">>], <<", ">>]},
        {join, [[], <<",">>]},
        {join, [[<<"a">>], <<",">>]},
        {join, [[<<"a">>, b], <<",">>]}
    ].

%% replace/3,4 with binary and fun replacements.
replaces() ->
    S = <<"abcabc">>,
    [
        binary:replace(S, <<"b">>, <<"X">>),
        binary:replace(S, <<"b">>, <<"X">>, [global]),
        binary:replace(S, [<<"a">>, <<"c">>], <<"[]">>, [global, {insert_replaced, 1}]),
        binary:replace(S, <<"bc">>, <<"<>">>, [global, {insert_replaced, [0, 1, 2]}]),
        binary:replace(S, <<"b">>, fun(M) -> <<M/binary, M/binary>> end, [global]),
        binary:replace(S, <<"b">>, <<"-">>, [global, {scope, {2, 4}}]),
        binary:replace(S, <<"z">>, <<"-">>, [global]),
        outcome(replace, [S, <<"b">>, <<"X">>, [{insert_replaced, 2}]]),
        outcome(replace, [S, <<"b">>, x]),
        outcome(replace, [S, <<"b">>, <<"X">>, [bad]]),
        outcome(replace, [S, <<"b">>, fun(_) -> x end])
    ].

%% Compiled patterns work wherever patterns do.
compiled() ->
    One = binary:compile_pattern(<<"ab">>),
    Many = binary:compile_pattern([<<"b">>, <<"ca">>]),
    [
        binary:match(<<"xxab">>, One),
        binary:matches(<<"abcab">>, Many),
        binary:split(<<"1ab2">>, One),
        binary:replace(<<"abab">>, One, <<"!">>, [global]),
        outcome(compile_pattern, [<<>>]),
        outcome(compile_pattern, [[]])
    ].
