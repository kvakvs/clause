-module(binary_append).
-export([main/1]).

% Appends to an accumulator binary (OPT0.6): long loops, appends from an older view of the same accumulator, views
% sent to another process and appended there, collections between appends, bitstring accumulators, slices and
% matching of the results, and grown buffers reused after a collection (OPT3.6).

grow(0, Acc) -> Acc;
grow(N, Acc) -> grow(N - 1, <<Acc/binary, N:8>>).

bits(0, Acc) -> Acc;
bits(N, Acc) -> bits(N - 1, <<Acc/bits, N:3>>).

sum(<<X, Rest/binary>>, Acc) -> sum(Rest, Acc + X);
sum(<<>>, Acc) -> Acc.

fill(0, _Byte, Acc) -> Acc;
fill(N, Byte, Acc) -> fill(N - 1, Byte, <<Acc/binary, Byte>>).

% Freed buffers of an earlier loop hold 255s; a later loop growing into them sees only its own bytes.
reuse() ->
    _ = fill(5000, 255, <<>>),
    erlang:garbage_collect(),
    Zeros = fill(5000, 0, <<>>),
    Odd = <<Zeros/binary, 1:3>>,
    {sum(Zeros, 0), Odd =:= <<0:40000, 1:3>>, bit_size(Odd)}.

main(_) ->
    Long = grow(20000, <<>>),
    io:format("long ~p ~p~n", [byte_size(Long), sum(Long, 0)]),
    Base = grow(100, <<0:800>>),
    X = <<Base/binary, 1>>,
    Y = <<Base/binary, 2>>,
    Z = <<X/binary, 3, 4>>,
    W = <<X/binary, 5>>,
    io:format("views ~p ~p ~p ~p ~p~n", [
        binary:last(Base),
        binary:last(X),
        binary:last(Y),
        binary:part(Z, 200, 3),
        binary:part(W, 200, 2)
    ]),
    Parent = self(),
    spawn(fun() -> Parent ! {back, <<Base/binary, 7>>, Base} end),
    Mine = <<Base/binary, 8>>,
    receive
        {back, Theirs, Same} ->
            io:format("sent ~p ~p ~p ~p~n", [
                binary:last(Theirs), binary:last(Mine), Same =:= Base, byte_size(Theirs)
            ])
    end,
    First = grow(500, <<>>),
    erlang:garbage_collect(),
    Second = grow(500, First),
    erlang:garbage_collect(),
    io:format("collected ~p ~p ~p~n", [byte_size(Second), sum(Second, 0), binary:part(First, 0, 3)]),
    Bits = bits(1000, <<1:5>>),
    io:format("bits ~p ~p~n", [bit_size(Bits), sum(<<Bits/bits, 0:(8 - bit_size(Bits) rem 8)>>, 0)]),
    Slice = binary:part(Long, 100, 300),
    Grown = <<Slice/binary, "tail">>,
    io:format("slice ~p ~p~n", [byte_size(Grown), binary:part(Grown, 296, 8)]),
    <<Head:16, _:19997/binary, Last>> = Long,
    io:format("match ~p ~p~n", [Head, Last]),
    io:format("reuse ~p~n", [reuse()]),
    io:format("binary_append: ok~n").
