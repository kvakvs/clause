#!/usr/bin/env escript
%% Observe the host OTP's rand constants (plan 21 step LIB6, docs/builtins.md#random-numbers) through its public
%% API, for tools/rand/generate.py. Writes JSON:
%%   ziggurat: [K, W] per strip of the normal distribution's ziggurat. normal_s/1 on a plug-in generator whose next
%%     word is chosen takes the strip from the word's low byte; below the strip's bound K it returns Word * W, above
%%     it reaches the slow path, which calls the generator's uniform. W is read from fast-path products, K as the
%%     smallest word of the strip reaching the slow path (words of a strip differ by 256, all the bound decides).
%%   jumps: [Before, After] states of jump/1 per algorithm, and of exro928_jump_2pow20/1 (exro928_2pow20), from
%%     which the jump polynomials are solved.
%% Run explicitly:
%%   escript tools/rand/observe.escript OBSERVED
-mode(compile).

main([Output]) ->
    Strips = [strip(Index) || Index <- lists:seq(0, 255)],
    Jumps =
        [{Alg, [jumped(Alg, Seed) || Seed <- [1, 2]]} || Alg <- [exsplus, exs1024, exrop, exro928ss]] ++
            [{exro928_2pow20, [jumped_2pow20(Seed) || Seed <- [1, 2]]}],
    Json = [
        "{\"ziggurat\": ",
        list([list([integer_to_list(K), float_text(W)]) || {K, W} <- Strips]),
        ", \"jumps\": {",
        lists:join(", ", [["\"", atom_to_list(Alg), "\": ", list(Pairs)] || {Alg, Pairs} <- Jumps]),
        "}}\n"
    ],
    ok = file:write_file(Output, Json),
    io:format("~s: ~b strips, ~b jumps~n", [Output, length(Strips), length(Jumps)]);
main(_) ->
    io:format(standard_error, "usage: observe.escript OBSERVED~n", []),
    halt(2).

list(Items) -> ["[", lists:join(", ", Items), "]"].

float_text(Float) -> io_lib:format("~w", [Float]).

%% A state before and after jump/1, as lists of integers.
jumped(Alg, Seed) ->
    {_, Before} = State = rand:seed_s(Alg, Seed),
    {_, After} = rand:jump(State),
    list([words(Before), words(After)]).

%% An exro928ss state before and after rand:exro928_jump_2pow20/1, the shorter jump crypto's crypto_aes uses.
jumped_2pow20(Seed) ->
    {_, Before} = rand:seed_s(exro928ss, Seed),
    list([words(Before), words(rand:exro928_jump_2pow20(Before))]).

words([S0 | S1]) when is_integer(S1) -> integers([S0, S1]);
words({L, RL}) -> list([integers(L), integers(RL)]).

integers(Values) -> list([integer_to_list(V) || V <- Values]).

strip(Index) -> {bound(Index), width(Index)}.

%% normal_s/1 on a 52-bit plug-in generator returning Word (sign bit clear), whose uniform returns U.
normal(Word, U) ->
    Handler = #{
        type => probe,
        bits => 52,
        next => fun(S) -> {Word bsl 1, S} end,
        uniform => fun({H, S}) -> {U, {H, S}} end
    },
    {X, _} = rand:normal_s({Handler, 0}),
    X.

%% Whether Word takes the fast path: the slow path's uniform yields a non-number, so arithmetic on it fails.
fast(Word) ->
    try normal(Word, slow) of
        _ -> true
    catch
        error:_ -> false
    end.

%% The strip's bound: the smallest word Index + 256 * J taking the slow path (strip 1 never takes the fast path).
bound(1) -> 0;
bound(Index) -> Index + 256 * first_slow(Index, 0, 1 bsl 43).

first_slow(_Index, Low, Low) ->
    Low;
first_slow(Index, Low, High) ->
    Middle = (Low + High) div 2,
    case fast(Index + 256 * Middle) of
        true -> first_slow(Index, Middle + 1, High);
        false -> first_slow(Index, Low, Middle)
    end.

%% The strip's width W: the one double whose products with sample words are the observed results. Strip 1 is
%% read on the slow path, where a uniform of 0.0 accepts the small values.
width(Index) ->
    Words = [Index + 256 * J || J <- [1, 2, 3, 5, 7, 11, 1001, 65537, 1 bsl 30]],
    Uniform =
        case Index of
            1 -> 0.0;
            _ -> slow
        end,
    Samples = [{Word, normal(Word, Uniform)} || Word <- Words],
    [{Word0, X0} | _] = Samples,
    Guess = X0 / Word0,
    case [W || W <- neighbours(Guess, 64), lists:all(fun({Word, X}) -> Word * W =:= X end, Samples)] of
        [W] -> W;
        Other -> error({ambiguous_width, Index, Other})
    end.

%% The doubles within Steps units in the last place of a positive double.
neighbours(Float, Steps) ->
    <<Bits:64>> = <<Float/float>>,
    [F || N <- lists:seq(Bits - Steps, Bits + Steps), <<F/float>> <- [<<N:64>>]].
