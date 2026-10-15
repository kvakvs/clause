-module(binary_match).
-export([main/1]).

% Binary patterns (OPT1.4): integer segments of whole-byte widths in both byte orders and signedness, read inline
% at aligned positions and through the service at unaligned ones; rests as sub-binaries of sub-binaries, sent to
% another process, kept across collections, sliced, compared and encoded; start and end tests.

sum(<<X:32, Rest/binary>>, Acc) -> sum(Rest, Acc + X);
sum(<<>>, Acc) -> Acc.

bits(<<X:3, Rest/bits>>, Acc) -> bits(Rest, [X | Acc]);
bits(<<>>, Acc) -> lists:reverse(Acc).

tails(<<_, Rest/binary>> = B, Acc) -> tails(Rest, [B | Acc]);
tails(<<>>, Acc) -> Acc.

fields(<<A:24, B:24/little, C:40/signed, D:48/little-signed, E:16/little, F:56>>) ->
    {A, B, C, D, E, F};
fields(_) ->
    nomatch.

shifted(<<_:3, X:16, Y:8/signed, _/bits>>) -> {X, Y}.

main(_) ->
    Big = list_to_binary([X rem 256 || X <- lists:seq(1, 4000)]),
    Small = <<1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12>>,
    <<_:5, Odd/bits>> = Small,
    <<_:16, Mid/binary>> = Big,
    <<_:8, Mid2/binary>> = Mid,
    Parent = self(),
    spawn(fun() -> Parent ! {got, Mid2, byte_size(Mid2), sum(binary:part(Mid2, 1, 3996), 0)} end),
    Got =
        receive
            {got, M, N, S} -> {M =:= Mid2, N, S}
        end,
    Tails = tails(binary:part(Big, 10, 20), []),
    erlang:garbage_collect(),
    io:format("~p~n", [{sum(Big, 0), sum(Small, 0), bits(<<200, 100, 50>>, []), bit_size(Odd), Odd}]),
    io:format("~p~n", [{byte_size(Mid), binary:first(Mid2), Got, length(Tails), lists:last(Tails)}]),
    io:format("~p~n", [
        {
            Mid2 < Mid,
            Mid2 =:= binary:part(Big, 3, 3997),
            term_to_binary(Odd),
            is_binary(Mid2),
            is_binary(Odd)
        }
    ]),
    io:format("~p~n", [
        {binary_to_term(term_to_binary(Mid2)) =:= Mid2, [
            byte_size(T)
         || T <- lists:sublist(Tails, 3)
        ]}
    ]),
    io:format("~p~n", [
        fields(
            <<1, 2, 3, 4, 5, 6, 255, 254, 253, 252, 251, 1, 2, 3, 4, 5, 200, 9, 8, 1, 2, 3, 4, 5, 6,
                7>>
        )
    ]),
    io:format("~p~n", [{fields(<<1, 2, 3>>), fields(abc), shifted(<<255, 0, 129, 7, 3>>)}]),
    io:format("binary_match: ok~n").
