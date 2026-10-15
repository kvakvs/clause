-module(bit_integers).
-export([main/1]).

% Integer segments of binary construction (OPT0.9 word path and its multiprecision fallback): every width up to 64
% in both byte orders, negative and truncated values, segments after an unaligned prefix, sizes in units, size 0,
% and widths past one word.

values() ->
    [
        0,
        1,
        -1,
        5,
        -6,
        255,
        256,
        -129,
        16#7FFFFFFF,
        -16#80000000,
        16#123456789ABCDEF,
        -16#123456789ABCDEF
    ].

widths() -> [0, 1, 3, 7, 8, 9, 12, 15, 16, 17, 24, 31, 32, 33, 40, 48, 56, 63, 64].

big(V, W) -> <<V:W>>.
little(V, W) -> <<V:W/little>>.
unaligned(V, W) -> <<1:3, V:W, 5:5/little>>.
unaligned_little(V, W) -> <<1:3, V:W/little-signed>>.
units(V, N) -> <<V:N/unit:8>>.

% A position-weighted sum of the bytes of a bitstring padded to whole bytes, and its bit size.
checksum(Bits) ->
    Padded = <<Bits/bitstring, 0:((8 - bit_size(Bits) rem 8) rem 8)>>,
    {Sum, _} = lists:foldl(
        fun(Byte, {Acc, I}) -> {Acc + Byte * I, I + 1} end, {0, 1}, binary_to_list(Padded)
    ),
    Sum * 128 + bit_size(Bits).

main(_) ->
    Cases = [{V, W} || V <- values(), W <- widths()],
    Sums = [
        {
            checksum(big(V, W)),
            checksum(little(V, W)),
            checksum(unaligned(V, W)),
            checksum(unaligned_little(V, W))
        }
     || {V, W} <- Cases
    ],
    io:format("~p cases, sum ~p~n", [
        length(Cases), lists:sum([A + B + C + D || {A, B, C, D} <- Sums])
    ]),
    io:format("~w~n", [
        [big(-6, 5), little(16#1234, 12), unaligned(-1, 9), unaligned_little(-200, 11)]
    ]),
    io:format("~w~n", [[units(16#0102030405060708, N) || N <- [0, 1, 4, 8]]]),
    io:format("~w~n", [[big(-1, 65), little(1 bsl 70 + 3, 72), big(16#123456789ABCDEF, 70)]]),
    <<A:13/little-signed, B:64, C:3>> = <<-1000:13/little, 16#FEDCBA9876543210:64, 6:3>>,
    io:format("~w~n", [{A, B, C}]),
    io:format("bit_integers: ok~n").
