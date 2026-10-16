-module(binary_loops).
-export([main/1]).

%% Functions that match a binary parameter and pass its rest back to themselves in tail calls loop on one
%% sub-binary cell. Each case checks that the caller's binaries stay intact, that a failed guard or clause sees the
%% unchanged binary, and that errors and collections inside such a loop behave as on OTP.
main(_) ->
    Big = <<<<X:32>> || X <- lists:seq(1, 50000)>>,
    io:format("~p~n", [sum(Big, 0)]),
    %% A sub-binary of the caller's binary: the caller reads it again afterwards.
    <<_:3/binary, Part/binary>> = Big,
    Words = byte_size(Part) div 4 * 4,
    <<Aligned:Words/binary, _/binary>> = Part,
    io:format("~p~n", [{sum(Aligned, 0), byte_size(Aligned), binary:part(Aligned, 0, 4)}]),
    %% A small heap binary.
    Small = <<1, 2, 3, 4, 5, 6, 7, 8>>,
    io:format("~p~n", [{sum(Small, 0), Small}]),
    io:format("~p~n", [guarded(<<"a1b22c333">>, [], [])]),
    io:format("~p~n", [ones(<<16#F0F0F0F0:32, 5:3>>, 0)]),
    io:format("~p~n", [switched(<<1, 2, 255, 9, 9>>, 0)]),
    io:format("~p~n", [kept(<<1, 2, 3>>, [])]),
    io:format("~p~n", [caught(fun() -> failing(<<1, 2, 3, 0, 4>>, 10) end)]),
    io:format("~p~n", [caught(fun() -> pairs(<<1, 2, 3>>, 0) end)]),
    io:format("~p~n", [garbage(200)]),
    io:format("binary_loops: ok~n").

%% The sum of the 32-bit words.
sum(<<X:32, Rest/binary>>, Acc) -> sum(Rest, Acc + X);
sum(<<>>, Acc) -> Acc.

%% A guard rejects the first clause after its rest was extracted; the second clause matches the same binary.
guarded(<<Letter, Rest/binary>>, Letters, Digits) when Letter >= $a, Letter =< $z ->
    guarded(Rest, [Letter | Letters], Digits);
guarded(<<Digit, Rest/binary>>, Letters, Digits) ->
    guarded(Rest, Letters, [Digit - $0 | Digits]);
guarded(<<>>, Letters, Digits) ->
    {lists:reverse(Letters), lists:reverse(Digits)}.

%% A loop over bits: the rest is not whole bytes.
ones(<<1:1, Rest/bits>>, Count) -> ones(Rest, Count + 1);
ones(<<0:1, Rest/bits>>, Count) -> ones(Rest, Count);
ones(<<>>, Count) -> Count.

%% A tail call passing another binary leaves the loop.
switched(<<255, _/binary>>, Acc) -> switched(<<100, 200>>, Acc);
switched(<<X, Rest/binary>>, Acc) -> switched(Rest, Acc + X);
switched(<<>>, Acc) -> Acc.

%% The rest is also kept, so this function makes a new cell per step.
kept(<<_, Rest/binary>>, Seen) -> kept(Rest, [Rest | Seen]);
kept(<<>>, Seen) -> Seen.

%% An error raised inside the loop names the function.
failing(<<X, Rest/binary>>, Acc) -> failing(Rest, Acc div X);
failing(<<>>, Acc) -> Acc.

%% The reason of an error and the innermost frame of this module in its stack (OTP lists erlang:div/2 above it).
caught(Fun) ->
    try
        Fun()
    catch
        error:Reason:Stack ->
            [{Module, Function, Arity, _} | _] = [
                Frame
             || {binary_loops, _, _, _} = Frame <- Stack
            ],
            {Reason, Module, Function, arity(Arity)}
    end.

%% No clause matches the rest left after one step.
pairs(<<X, Y, Rest/binary>>, Acc) -> pairs(Rest, Acc + X * Y);
pairs(<<>>, Acc) -> Acc.

arity(Arity) when is_integer(Arity) -> Arity;
arity(Arguments) -> length(Arguments).

%% Many loops over fresh binaries, so collections move the loop cell while it is in use.
garbage(0) ->
    done;
garbage(N) ->
    Binary = <<<<(X + N):16>> || X <- lists:seq(1, 300)>>,
    Total = shorts(Binary, 0),
    case Total =:= 300 * N + 45150 of
        true -> garbage(N - 1);
        false -> {wrong, N, Total}
    end.

shorts(<<X:16, Rest/binary>>, Acc) -> shorts(Rest, Acc + X);
shorts(<<>>, Acc) -> Acc.
