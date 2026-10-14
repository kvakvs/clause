-module(unicode_calls).
-export([main/1]).

%% The unicode conversions of plan step 41Y1: chardata to binaries and lists in
%% every encoding, OTP's error and incomplete results with their rest terms,
%% byte order marks and bad arguments.
main([]) ->
    [io:format("~w~n", [outcome(F, A)]) || {F, A} <- calls()],
    io:format("unicode_calls: ok~n").

%% The result of unicode:F(A...), or the error it raises.
outcome(F, A) ->
    try
        {F, apply(unicode, F, A)}
    catch
        error:R -> {F, error, R}
    end.

calls() ->
    Euro = <<16#E2, 16#82, 16#AC>>,
    [
        {characters_to_binary, [[$a, [$b, <<"cd">>], 16#20AC | <<"e">>]]},
        {characters_to_binary, [<<"plain">>]},
        {characters_to_binary, [[$a, -1, $b]]},
        {characters_to_binary, [[$a, [$b, 16#110000, $c], $d]]},
        {characters_to_binary, [[$a, [$b, [16#D800, $c], $d], $e]]},
        {characters_to_binary, [[<<"ab">>, <<255>>, "c"]]},
        {characters_to_binary, [<<"a", 16#E2, 16#82>>]},
        {characters_to_binary, [[<<"a", 16#E2>>, <<16#82, 16#AC>>]]},
        {characters_to_binary, [[<<"a", 16#E2>>, $b]]},
        {characters_to_binary, [[<<"a", 16#C0, 16#80>>]]},
        {characters_to_binary, ["a", latin1]},
        {characters_to_binary, [[256], latin1]},
        {characters_to_binary, [<<200, 201>>, latin1]},
        {characters_to_binary, [[256], unicode, latin1]},
        {characters_to_binary, [[97, 256, 98], unicode, latin1]},
        {characters_to_binary, [<<200>>, latin1, utf8]},
        {characters_to_binary, ["ab", unicode, utf16]},
        {characters_to_binary, ["ab", unicode, {utf16, little}]},
        {characters_to_binary, [[16#1F600], unicode, utf16]},
        {characters_to_binary, [[16#1F600], unicode, {utf16, little}]},
        {characters_to_binary, ["a", unicode, utf32]},
        {characters_to_binary, ["a", unicode, {utf32, little}]},
        {characters_to_binary, [<<0, 97, 16#D8, 16#3D, 16#DE, 16#00>>, utf16, unicode]},
        {characters_to_binary, [<<97, 0, 16#3D, 16#D8>>, {utf16, little}, unicode]},
        {characters_to_binary, [<<0, 97, 0>>, utf16, unicode]},
        {characters_to_binary, [<<0, 0, 0, 97>>, utf32, unicode]},
        {characters_to_binary, [<<0, 17, 0, 0>>, utf32, unicode]},
        {characters_to_binary, [<<97, 255>>, latin1, latin1]},
        {characters_to_binary, [[<<"x">> | <<"y">>]]},
        {characters_to_binary, [Euro, unicode, latin1]},
        {characters_to_binary, [[$a, [$b, -1]]]},
        {characters_to_binary, [[[<<255>>]]]},
        {characters_to_binary, [[$a, [<<255>>, $c]]]},
        {characters_to_binary, [[$a, [[-1], $b]]]},
        {characters_to_binary, [[<<"a">>, [<<255>>]]]},
        {characters_to_binary, [[[<<"x", 16#E2>>], $b]]},
        {characters_to_binary, [[<<"x", 16#E2>>, [$b]]]},
        {characters_to_binary, [[[<<"x", 16#E2>>]]]},
        {characters_to_binary, [[<<"x", 16#E2>>, <<"y">>]]},
        {characters_to_binary, [[[97, [256], 98]], unicode, latin1]},
        {characters_to_binary, [[<<"a">>, <<16#C4, 16#80>>, $b], unicode, latin1]},
        {characters_to_binary, [[]]},
        {characters_to_binary, [a]},
        {characters_to_binary, [[a]]},
        {characters_to_binary, [[$a | b]]},
        {characters_to_binary, ["a", bad]},
        {characters_to_binary, ["a", unicode, {utf16, middle}]},
        {characters_to_list, [<<"h", 16#C3, 16#A9>>]},
        {characters_to_list, [<<200>>, latin1]},
        {characters_to_list, [[<<"a">>, 16#D800]]},
        {characters_to_list, [[$a | <<"b">>]]},
        {characters_to_list, [<<"a", 16#E2>>]},
        {characters_to_list, [[16#D800]]},
        {bin_is_7bit, [<<"abc">>]},
        {bin_is_7bit, [<<"ab", 200>>]},
        {bin_is_7bit, [abc]},
        {bom_to_encoding, [<<239, 187, 191, 1>>]},
        {bom_to_encoding, [<<1>>]},
        {bom_to_encoding, [<<255, 254, 0, 0>>]},
        {bom_to_encoding, [<<255, 254, 1>>]},
        {bom_to_encoding, [<<254, 255>>]},
        {bom_to_encoding, [<<0, 0, 254, 255>>]},
        {encoding_to_bom, [{utf16, little}]},
        {encoding_to_bom, [utf16]},
        {encoding_to_bom, [utf32]},
        {encoding_to_bom, [unicode]},
        {encoding_to_bom, [latin1]},
        {encoding_to_bom, [bad]}
    ].
