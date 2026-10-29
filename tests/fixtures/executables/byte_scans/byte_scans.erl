-module(byte_scans).
-export([main/1]).

%% Binary loops whose first matching clause consumes one byte of a class skip whole runs of that class at once
%% (byte scans). Each case checks that runs of every length around the 16-byte step, unaligned and short binaries,
%% earlier clauses on literal bytes, counters that are not small integers or end near the limit, and errors give the
%% same results as matching byte by byte.
main(_) ->
    Lengths = [0, 1, 2, 15, 16, 17, 31, 32, 33, 47, 100],
    [io:format("~p~n", [cases(N)]) || N <- Lengths],
    All = list_to_binary([X rem 256 || X <- lists:seq(1, 600)]),
    io:format("~p~n", [{digits(All, 0), quoted(All, 0), plain(All, 0), latin(All, 0, 0), hex(All, 0)}]),
    Mixed = <<"  12  abc\t\t\n\r 0x1f  \"quoted \\\" text with \\\\ escapes and more text\" tail">>,
    io:format("~p~n", [{skip_ws(Mixed), quoted(Mixed, 0), plain(Mixed, 0), latin(Mixed, 0, 0)}]),
    %% Binaries that start inside a byte, and a bitstring rest.
    <<_:4, Odd:40/binary, _:4>> = <<0:4, (list_to_binary(lists:duplicate(40, $7)))/binary, 0:4>>,
    io:format("~p~n", [{digits(Odd, 0), skip_ws(Odd)}]),
    io:format("~p~n", [bits_len(<<0:300>>, 0)]),
    <<_:3, Bits/bits>> = <<"                                          z">>,
    io:format("~p~n", [skip_ws(Bits)]),
    <<_:2/binary, Part/binary>> = <<"xx                                     q">>,
    io:format("~p~n", [skip_ws(Part)]),
    %% Counters that are floats, bignums, near the small-integer limit or not numbers at all.
    Run = <<"1234567890123456789012345678901234567890 ">>,
    io:format("~p~n", [digits(Run, 1.5)]),
    io:format("~p~n", [digits(Run, 1 bsl 70)]),
    io:format("~p~n", [[digits(Run, (1 bsl 59) - K) || K <- [1, 20, 40, 41]]]),
    io:format("~p~n", [[digits(Run, -(1 bsl 59) + K) || K <- [0, 20, 41]]]),
    io:format("~p~n", [down(list_to_binary(lists:duplicate(50, $a) ++ "."), 0, x)]),
    io:format("~p~n", [
        try digits(Run, foo) of
            Value -> Value
        catch
            Class:Reason -> {Class, Reason}
        end
    ]),
    io:format("byte_scans: ok~n").

cases(N) ->
    Ws = list_to_binary(lists:duplicate(N, $\s) ++ "x"),
    Ds = list_to_binary(lists:duplicate(N, $7) ++ " "),
    Js = list_to_binary(lists:duplicate(N, $k) ++ "\""),
    Hs = list_to_binary(lists:duplicate(N, 200)),
    {
        N,
        byte_size(skip_ws(Ws)),
        digits(Ds, 0),
        quoted(Js, 0),
        plain(Js, 0),
        latin(Hs, 0, 0),
        any_len(Hs, 0),
        hex(Ds, 0),
        empty_first(Ws, 0)
    }.

%% A guard of alternatives.
skip_ws(<<C, R/binary>>) when C =:= $\s; C =:= $\t; C =:= $\n; C =:= $\r -> skip_ws(R);
skip_ws(R) -> R.

%% A range guard and a counter.
digits(<<C, R/binary>>, N) when C >= $0, C =< $9 -> digits(R, N + 1);
digits(R, N) -> {N, byte_size(R)}.

%% Earlier clauses on literal bytes, then any byte.
quoted(<<$", R/binary>>, N) -> {quote, N, byte_size(R)};
quoted(<<$\\, _, R/binary>>, N) -> quoted(R, N + 2);
quoted(<<>>, N) -> {done, N};
quoted(<<_, R/binary>>, N) -> quoted(R, N + 1).

%% A JSON-like plain class with andalso.
plain(<<C, R/binary>>, N) when C >= 32 andalso C =/= $" andalso C =/= $\\ andalso C < 128 -> plain(R, N + 1);
plain(_, N) -> N.

%% Bytes 128 and up, and a second counter with another step.
latin(<<C:8/unsigned, R/bytes>>, N, M) when not (C < 128) -> latin(R, N + 1, M + 3);
latin(<<_, R/binary>>, N, M) -> latin(R, N, M - 1);
latin(<<>>, N, M) -> {N, M}.

%% No guard at all.
any_len(<<_, R/binary>>, N) -> any_len(R, 1 + N);
any_len(<<>>, N) -> N.

%% Three ranges, arithmetic and is_integer/1 in the guard.
hex(<<C, R/binary>>, N) when is_integer(C), C - $0 < 10, C >= $0; C >= $a, C =< $f; C >= $A, C =< $F ->
    hex(R, N - 2);
hex(R, N) ->
    {N, byte_size(R)}.

%% An earlier clause matching the empty binary only.
empty_first(<<>>, N) -> {empty, N};
empty_first(<<$\s, R/binary>>, N) -> empty_first(R, N);
empty_first(<<_, R/binary>>, N) -> empty_first(R, N + 1).

%% A bitstring rest.
bits_len(<<_:8, R/bits>>, N) -> bits_len(R, N + 1);
bits_len(R, N) -> {N, bit_size(R)}.

%% A literal byte (no scan) with a counter counting down and an unchanged parameter.
down(<<$a, R/binary>>, N, X) -> down(R, N - 2, X);
down(R, N, X) -> {R, N, X}.
