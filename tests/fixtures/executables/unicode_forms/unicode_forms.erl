-module(unicode_forms).
-export([main/1]).

%% unicode (plan 21 step LIB4B): the normalization forms over lists, binaries and deep chardata, invalid UTF-8
%% and bad input; category/1, is_whitespace/1, is_id_start/1 and is_id_continue/1 over a sweep of code points (every
%% one below U+3400, every 17th above), as digests; the internal characters_to_list_int/2 and
%% characters_to_binary_int/2.
main(_) ->
    Forms = [nfc, nfd, nfkc, nfkd],
    [
        show({Form, Shape}, [reason(fun() -> normalize(Form, Shape, T) end) || T <- texts()])
     || Form <- Forms,
        Shape <- [list, binary]
    ],
    [show({digest, Form}, digest(Form)) || Form <- Forms],
    show(category, [reason(fun() -> unicode:category(C) end) || C <- samples()]),
    show(category_counts, category_counts()),
    [
        show({property, Name}, property(Name))
     || Name <- [is_whitespace, is_id_start, is_id_continue]
    ],
    show(property_errors, [
        reason(fun() -> unicode:Name(Bad) end)
     || Name <- [category, is_whitespace, is_id_start, is_id_continue],
        Bad <- [-1, 16#110000, a, "a", 1.0]
    ]),
    show(int, [
        reason(fun() -> unicode:characters_to_list_int("abc", unicode) end),
        reason(fun() -> unicode:characters_to_list_int(<<"ab", 200>>, latin1) end),
        reason(fun() -> unicode:characters_to_list_int(<<"ab", 200>>, utf8) end),
        reason(fun() -> unicode:characters_to_list_int([a], utf8) end),
        reason(fun() -> unicode:characters_to_binary_int([16#E5, <<"x">>], latin1) end),
        reason(fun() -> unicode:characters_to_binary_int("a\x{3042}", unicode) end),
        reason(fun() -> unicode:characters_to_binary_int([16#110000], unicode) end),
        reason(fun() -> unicode:characters_to_binary_int(<<"a", 255>>, utf8) end)
    ]),
    io:format("unicode_forms: ok~n").

normalize(nfc, list, T) -> unicode:characters_to_nfc_list(T);
normalize(nfc, binary, T) -> unicode:characters_to_nfc_binary(T);
normalize(nfd, list, T) -> unicode:characters_to_nfd_list(T);
normalize(nfd, binary, T) -> unicode:characters_to_nfd_binary(T);
normalize(nfkc, list, T) -> unicode:characters_to_nfkc_list(T);
normalize(nfkc, binary, T) -> unicode:characters_to_nfkc_binary(T);
normalize(nfkd, list, T) -> unicode:characters_to_nfkd_list(T);
normalize(nfkd, binary, T) -> unicode:characters_to_nfkd_binary(T).

texts() ->
    [
        "",
        <<>>,
        "abc..\x{E5}\x{E4}\x{F6}",
        [<<"abc..a">>, [778], $a, [776], $o, [776]],
        ["abc..\x{E5}\x{E4}\x{F6}", [65299, 65298]],
        "\x{FB01}\x{2460}\x{FF21}\x{1E9B}\x{323}",
        <<"e", 16#CC, 16#81, "\x{1E0B}\x{323}"/utf8>>,
        "\x{AC00}\x{1100}\x{1161}\x{11A8}\x{D7A3}",
        [<<"\x{1100}"/utf8>>, [16#1161], <<"\x{11A8}x"/utf8>>],
        "a\x{315}\x{300}\x{5AE}\x{301}b",
        "\x{212B}\x{2126}\x{1E9B}",
        <<"ok", 255, "rest">>,
        ["ab", <<"c", 16#E2, 16#82>>],
        [$a, <<16#C3>>, <<16#A5>>],
        [$a | <<"bc">>],
        [[[]], <<>>, [[<<"x">>]]],
        "\x{1F468}\x{200D}\x{1F469}\x{1F1F8}\x{1F1EA}",
        "\r\n\x{301}",
        "\x{0928}\x{094D}\x{0924}",
        abc,
        [a],
        [16#110000],
        [-1],
        [$a | b]
    ].

samples() ->
    [
        0,
        9,
        32,
        $a,
        $A,
        $0,
        $_,
        $-,
        $[,
        16#A0,
        16#E5,
        16#300,
        16#3A3,
        16#5D0,
        16#640,
        16#E01,
        16#2028,
        16#20AC,
        16#2160,
        16#3000,
        16#3042,
        16#AC00,
        16#D800,
        16#E000,
        16#FFFD,
        16#FFFF,
        16#1F600,
        16#E0001,
        16#10FFFF
    ].

%% The sweep without surrogates: every code point below U+3400, every 17th above.
code_points() ->
    lists:seq(0, 16#33FF) ++ lists:seq(16#3400, 16#D7FF, 17) ++ lists:seq(16#E000, 16#10FFFF, 17).

%% The sweep with surrogates.
all_points() -> lists:seq(0, 16#33FF) ++ lists:seq(16#3400, 16#10FFFF, 17).

digest(Form) ->
    lists:foldl(
        fun(C, Acc) ->
            D = normalize(Form, list, [C]),
            (Acc * 31 + lists:sum(D) * 7 + length(D)) rem 1000000007
        end,
        0,
        code_points()
    ).

category_counts() ->
    Counts = lists:foldl(
        fun(C, Map) -> maps:update_with(unicode:category(C), fun(N) -> N + 1 end, 1, Map) end,
        #{},
        all_points()
    ),
    lists:sort(maps:to_list(Counts)).

property(Name) ->
    True = [C || C <- all_points(), unicode:Name(C)],
    {
        length(True),
        lists:sum(True),
        lists:sublist(True, 40),
        lists:sublist(lists:reverse(True), 10)
    }.

show(Name, Value) -> io:format("~w: ~w~n", [Name, Value]).

reason(Fun) ->
    try Fun() of
        Value -> {value, Value}
    catch
        error:Reason -> {error, Reason};
        throw:Thrown -> {thrown, Thrown}
    end.
