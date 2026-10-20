-module(integer_text).
-export([main/1, id/1]).

%% integer_to_list/1 of a small integer and list_to_integer/1 of a short decimal list are made inline in generated
%% code (inline integer text); every other argument takes the builtin, which raises the errors. Results and error
%% reasons must stay OTP's on both paths and at the small-integer limits of every word width.
id(X) -> X.

show(Name, F) ->
    R =
        try F() of
            V -> {ok, V}
        catch
            C:E -> {C, E}
        end,
    io:format("~s: ~p~n", [Name, R]).

main(_) ->
    to_text(),
    from_text(),
    prefixes(),
    loops(),
    io:format("integer_text: ok~n").

to_text() ->
    Values = [
        0,
        7,
        9,
        10,
        99,
        100,
        -1,
        -9,
        -10,
        -100,
        12345,
        134217727,
        134217728,
        -134217728,
        -134217729,
        576460752303423487,
        576460752303423488,
        -576460752303423488,
        -576460752303423489,
        1180591620717411303424,
        -1180591620717411303424
    ],
    [
        show("integer_to_list " ++ integer_to_list(V), fun() -> integer_to_list(id(V)) end)
     || V <- Values
    ],
    show("integer_to_list float", fun() -> integer_to_list(id(1.0)) end),
    show("integer_to_list atom", fun() -> integer_to_list(id(a)) end),
    show("integer_to_list appended", fun() ->
        integer_to_list(id(-42)) ++ "-" ++ atom_to_list(item)
    end).

from_text() ->
    Texts = [
        "0",
        "7",
        "123",
        "-0",
        "-15",
        "+7",
        "007",
        "-007",
        "",
        "-",
        "+",
        "1a",
        "a1",
        "--1",
        "-+1",
        " 1",
        "1 ",
        "99999999",
        "134217727",
        "134217728",
        "-134217728",
        "-134217729",
        "12345678901234567",
        "123456789012345678",
        "576460752303423487",
        "576460752303423488",
        "-576460752303423488",
        "-576460752303423489",
        "123456789012345678901234567890",
        [16#661]
    ],
    [
        show(io_lib:format("list_to_integer ~w", [T]), fun() -> list_to_integer(id(T)) end)
     || T <- Texts
    ],
    show("list_to_integer improper", fun() -> list_to_integer(id([$1, $2 | $3])) end),
    show("list_to_integer float element", fun() -> list_to_integer(id([49.0])) end),
    show("list_to_integer atom", fun() -> list_to_integer(id(a)) end),
    show("list_to_integer binary", fun() -> list_to_integer(id(<<"1">>)) end).

%% `integer_to_list(N) ++ Right` writes N's digits onto Right; N is checked where the call stands, before Right runs.
prefixes() ->
    show("prefix", fun() -> integer_to_list(id(12)) ++ "-" ++ atom_to_list(item) end),
    show("prefix negative", fun() -> integer_to_list(id(-305)) ++ "x" end),
    show("prefix zero", fun() -> integer_to_list(id(0)) ++ [] end),
    show("prefix bignum", fun() -> integer_to_list(id(1 bsl 70)) ++ "x" end),
    show("prefix improper", fun() -> integer_to_list(id(5)) ++ id(tail) end),
    show("prefix grouped", fun() -> (integer_to_list(id(77))) ++ "y" end),
    show("prefix nested", fun() -> integer_to_list(id(1)) ++ integer_to_list(id(-2)) ++ "z" end),
    show("prefix other call", fun() -> lists:reverse(id("ab")) ++ "c" end),
    show("prefix order", fun() ->
        erase(seen),
        R =
            try
                integer_to_list(id(a)) ++ [put(seen, true)]
            catch
                error:badarg -> badarg
            end,
        {R, get(seen)}
    end),
    show("prefix strings", fun() -> length(lists:append(strings(-50000, 50000, []))) end).

strings(N, Last, Acc) when N > Last -> Acc;
strings(N, Last, Acc) ->
    strings(N + 1, Last, [integer_to_list(N * 31) ++ "-" ++ atom_to_list(item) | Acc]).

%% Enough conversions to fill heap blocks, so some take the builtin when a block has no room.
loops() ->
    show("round trip", fun() -> round_trip(-100000, 100000, 0) end),
    show("lengths", fun() -> lengths(1, 200000, 0) end).

round_trip(N, Last, Acc) when N > Last -> Acc;
round_trip(N, Last, Acc) ->
    round_trip(N + 1, Last, Acc + list_to_integer(integer_to_list(N * 7919))).

lengths(N, Last, Acc) when N > Last -> Acc;
lengths(N, Last, Acc) -> lengths(N + 1, Last, Acc + length(integer_to_list(N * N))).
