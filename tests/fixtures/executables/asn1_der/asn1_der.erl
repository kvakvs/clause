%% clau ASN.1 modules compiled for DER against asn1ct: the encodings, decodings and error reasons of the
%% Kitchen types (plan 21 step LIB84).
-module(asn1_der).
-export([main/1]).
-include("Kitchen.hrl").

main(_) ->
    [roundtrip(T, V) || {T, V} <- values()],
    [decode_only(T, B) || {T, B} <- encodings()],
    io:format("asn1_der: ok~n").

roundtrip(Type, Value) ->
    case 'Kitchen':encode(Type, Value) of
        {ok, Bin} ->
            io:format("~p ~w~n  => ~w~n  <= ~w~n", [Type, Value, Bin, decode(Type, Bin)]);
        {error, {asn1, {Reason, _}}} ->
            io:format("~p ~w~n  => error ~w~n", [Type, Value, Reason])
    end.

decode_only(Type, Bin) ->
    io:format("~p ~w~n  <= ~w~n", [Type, Bin, decode(Type, Bin)]).

decode(Type, Bin) ->
    case 'Kitchen':decode(Type, Bin) of
        {ok, V} -> V;
        {error, {asn1, {R, _}}} -> {error, R}
    end.

values() ->
    [
        {'Flag', true},
        {'Flag', false},
        {'Flag', maybe_not},
        {'Count', 0},
        {'Count', 127},
        {'Count', 128},
        {'Count', -1},
        {'Count', -129},
        {'Count', 256},
        {'Count', 1 bsl 70},
        {'Count', -(1 bsl 70)},
        {'Count', foo},
        {'Level', medium},
        {'Level', 7},
        {'Level', unknown},
        {'Level', 10},
        {'Small', 300},
        {'Colour', blue},
        {'Colour', 1},
        {'Colour', pink},
        {'Shape', triangle},
        {'Nothing', 'NULL'},
        {'Nothing', anything},
        {'Bits', <<>>},
        {'Bits', <<1:1, 0:1, 1:1>>},
        {'Bits', <<255, 1:1>>},
        {'Bits', [1, 0]},
        {'Rights', [read, admin]},
        {'Rights', []},
        {'Rights', <<2#101:3>>},
        {'Rights', [{bit, 12}]},
        {'Rights', [bogus]},
        {'Bytes', <<1, 2, 3>>},
        {'Bytes', "abc"},
        {'Bytes', 42},
        {'Oid', {1, 2, 840, 113549}},
        {'Oid', [2, 999, 3]},
        {'Oid', {1}},
        {'RelOid', {8571, 3, 2}},
        {'Number', 0},
        {'Number', {1, 2, 3}},
        {'Number', {-5, 2, -2}},
        {'Number', {12500, 10, -2}},
        {'Number', 'PLUS-INFINITY'},
        {'Number', 'MINUS-INFINITY'},
        {'Ia5', "hello"},
        {'Printable', <<"bin">>},
        {'Utf8', <<"caf", 195, 169>>},
        {'Utf8', "ab"},
        {'Bmp', "BMP"},
        {'Bmp', [{0, 0, 4, 32}, 65]},
        {'Universal', [{0, 1, 2, 3}, 66]},
        {'Numeric', "123"},
        {'Visible', "vis"},
        {'Teletex', "tt"},
        {'General', "gen"},
        {'Graphic', "gr"},
        {'Videotex', "vt"},
        {'Utc', "261026120000Z"},
        {'Generalized', "20261026120000Z"},
        {'Descriptor', "desc"},
        {'Record', #'Record'{id = 1}},
        {'Record', #'Record'{id = 2, name = "two", level = high, flag = false}},
        {'Record', #'Record'{
            id = 3, level = 5, flag = true, inner = #'Record_inner'{a = 9, b = <<"b">>}
        }},
        {'Record', #'Record'{id = 4, level = medium}},
        {'Record', {'Record', 1, 2}},
        {'Record', not_a_record},
        {'Extended', #'Extended'{
            first = 1, second = true, third = 3, fourth = 'NULL', last = <<"z">>
        }},
        {'Extended', #'Extended'{first = 1, second = false, third = 3, last = <<>>}},
        {'Unordered', #'Unordered'{z = 26, a = true, m = <<"m">>}},
        {'Unordered', #'Unordered'{z = 26, a = false}},
        {'Numbers', [1, 2, 300]},
        {'Numbers', []},
        {'NumberSet', [<<"b">>, <<"a">>, <<"ab">>]},
        {'Pairs', [#'Pairs_SEQOF'{key = "k", value = 1}]},
        {'Choice', {int, 5}},
        {'Choice', {str, "s"}},
        {'Choice', {seq, #'Record'{id = 8}}},
        {'Choice', {later, true}},
        {'Choice', {nope, 1}},
        {'Choice', 7},
        {'Tagged', #'Tagged'{impl = 1, expl = 2, priv = <<"p">>, deep = true, choice = {int, 3}}},
        {'Tree', #'Tree'{
            value = 1, children = [#'Tree'{value = 2}, #'Tree'{value = 3, children = []}]
        }},
        {'Holder', #'Holder'{anything = <<5, 0>>, defined = [2, 1, 9], id = 1}},
        {'Attribute', #'Attribute'{type = {1, 2, 3}, value = 42}},
        {'Attribute', #'Attribute'{type = {1, 2, 4}, value = "name"}},
        {'Attribute', #'Attribute'{type = {1, 2, 5}, value = #'Record'{id = 5}}},
        {'Attribute', #'Attribute'{type = {1, 2, 9}, value = {asn1_OPENTYPE, <<4, 1, 0>>}}},
        {'Attribute', #'Attribute'{type = {1, 2, 9}, value = 1}},
        {'ClosedAttribute', #'ClosedAttribute'{type = {1, 2, 9}, value = 1}},
        {'MultiValued', #'MultiValued'{type = {1, 2, 3}, values = [1, 2]}},
        {'WrappedRecord', #'WrappedRecord'{version = 1, body = #'Record'{id = 6}}},
        {'WrappedList', [{'Wrapper', 1, <<"x">>}]},
        {'Instance', #'INSTANCE OF'{'type-id' = {1, 2, 3}, value = <<2, 1, 7>>}},
        {'Missing', 1}
    ].

encodings() ->
    [
        {'Count', <<2, 0>>},
        {'Count', <<2, 1>>},
        {'Count', <<3, 1, 0>>},
        {'Count', <<2, 1, 1, 9>>},
        {'Count', <<2, 9, 1, 0, 0, 0, 0, 0, 0, 0, 0>>},
        {'Count', <<48, 0>>},
        {'Count', <<2>>},
        {'Count', <<2, 5, 1>>},
        {'Count', <<2, 129, 1, 7>>},
        {'Count', <<2, 128, 0, 0>>},
        {'Count', <<34, 3, 2, 1, 1>>},
        {'Flag', <<1, 1, 5>>},
        {'Flag', <<1, 2, 0, 0>>},
        {'Small', <<2, 2, 1, 44>>},
        {'Colour', <<10, 1, 7>>},
        {'Shape', <<10, 1, 7>>},
        {'Nothing', <<5, 1, 0>>},
        {'Bits', <<3, 1, 0>>},
        {'Bits', <<3, 0>>},
        {'Bits', <<3, 2, 9, 255>>},
        {'Bits', <<35, 6, 3, 2, 4, 160, 3, 0>>},
        {'Rights', <<3, 3, 0, 129, 128>>},
        {'Bytes', <<36, 6, 4, 1, 97, 4, 1, 98>>},
        {'Bytes', <<36, 128, 4, 1, 97, 0, 0>>},
        {'Oid', <<6, 0>>},
        {'Oid', <<6, 2, 136, 55>>},
        {'Bmp', <<30, 3, 0, 65, 1>>},
        {'Ia5', <<54, 4, 22, 2, 104, 105>>},
        {'Record', <<48, 3, 2, 1, 1, 0>>},
        {'Record', <<48, 6, 2, 1, 1, 4, 1, 0>>},
        {'Record', <<48, 0>>},
        {'Record', <<48, 3, 1, 1, 1>>},
        {'Record', <<16, 3, 2, 1, 1>>},
        {'Extended', <<48, 9, 2, 1, 1, 159, 30, 0, 4, 1, 120>>},
        {'Extended', <<48, 6, 2, 1, 1, 4, 1, 120>>},
        {'Unordered', <<49, 6, 128, 1, 255, 130, 1, 9>>},
        {'Unordered', <<49, 9, 128, 1, 255, 130, 1, 9, 131, 0>>},
        {'Choice', <<131, 1, 255>>},
        {'Choice', <<132, 1, 255>>},
        {'Attribute', <<48, 9, 6, 2, 42, 9, 2, 1, 1>>},
        {'ClosedAttribute', <<48, 7, 6, 2, 42, 9, 2, 1, 1>>},
        {'Attribute', <<48, 7, 6, 2, 42, 3, 4, 1, 1>>},
        {'Holder', <<48, 8, 5, 0, 2, 1, 1, 2, 1, 1>>},
        {'Count', <<>>},
        {'Count', <<2, 5>>},
        {'Count', <<31, 255, 255, 255, 0>>},
        {'Count', <<2, 132, 0, 0, 0, 1, 1>>},
        {'Missing', <<5, 0>>},
        {'Count', [2, 1, 3]}
    ].
