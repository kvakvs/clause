-module(binary_build).
-export([main/1]).

% Binary construction: whole-byte integer segments of constant widths written inline (both byte orders, native,
% negative and oversized values, up to the 64-byte inline limit), with the bits service building bignum values,
% other segments, larger binaries and appends.

fields(A, B, C, D) ->
    <<A:8, B:16, C:24/little, D:32/signed, A:40/little-signed, B:48, C:56/little, D:64,
        A:16/native>>.

wide(X) -> <<X:64, X:64, X:64, X:64, X:64, X:64, X:64, X:64>>.

wider(X) -> <<X:64, X:64, X:64, X:64, X:64, X:64, X:64, X:64, X:8>>.

units(X) -> <<X:2/unit:8, X:1/unit:32>>.

sized(X, N) -> <<X:N>>.

mixed(X, B) -> <<X:8, B/binary, X:3, "ab">>.

append(0, Bin) -> Bin;
append(N, Bin) -> append(N - 1, <<Bin/binary, N:32>>).

main(_) ->
    io:format("~p~n", [fields(1, 2, 3, 4)]),
    io:format("~p~n", [fields(-1, 70000, -2, 16#123456789)]),
    io:format("~p~n", [fields(255, -32768, 16#ABCDEF12, -4)]),
    io:format("~p~n", [fields(1 bsl 70, 2, 3, 4)]),
    io:format("~p~n", [
        {byte_size(wide(7)), wide(-1) =:= binary:copy(<<255>>, 64), wide(16#0102030405060708)}
    ]),
    io:format("~p~n", [
        {byte_size(wider(7)), binary:last(wider(7)), binary:first(wider(16#0102030405060708))}
    ]),
    io:format("~p~n", [{units(258), sized(5, 16), sized(5, 3), mixed(7, <<1, 2>>)}]),
    Built = append(1000, <<>>),
    <<First:32, _/binary>> = Built,
    io:format("~p~n", [{byte_size(Built), First, binary:last(Built), erlang:md5(Built)}]),
    Parent = self(),
    spawn(fun() -> Parent ! {built, append(50, wide(1))} end),
    receive
        {built, Other} -> io:format("~p~n", [{byte_size(Other), binary:part(Other, 64, 8)}])
    end,
    % Appends to an older view, to a slice ending where the buffer's bits in use do, and to a copy received back
    % from another process all leave every earlier view unchanged.
    Base = append(30, <<>>),
    Longer = <<Base/binary, 1:8>>,
    Fork = <<Base/binary, 2:8>>,
    Tail = binary:part(Longer, 4, byte_size(Longer) - 4),
    Extended = <<Tail/binary, 3:16>>,
    Self = self(),
    spawn(fun() -> Self ! {echo, Longer} end),
    Echoed =
        receive
            {echo, Copy} -> <<Copy/binary, 4:8>>
        end,
    Again = <<Longer/binary, 5:8>>,
    io:format("~p~n", [
        {
            byte_size(Base),
            binary:last(Longer),
            binary:last(Fork),
            byte_size(Extended),
            binary:last(Extended),
            binary:last(Echoed),
            binary:last(Again),
            binary:last(Longer),
            erlang:md5(<<Base/binary, Again/binary>>)
        }
    ]),
    io:format("binary_build: ok~n").
