%% Clause's random (plan 21 step LIB6, docs/builtins.md#random-numbers): the deprecated Wichmann-Hill (AS183)
%% generator. uniform_s/1 is a runtime builtin; the rest keeps the state under random_seed in the process
%% dictionary, as OTP's does.
-module(random).
-export([
    seed/0,
    seed/1,
    seed/3,
    seed0/0,
    uniform/0,
    uniform/1,
    uniform_s/1,
    uniform_s/2
]).

seed0() -> {3172, 9814, 20125}.

seed() ->
    case seed_put(seed0()) of
        undefined -> seed0();
        {_, _, _} = Previous -> Previous
    end.

seed(Int) when is_integer(Int) ->
    A2 = Int band 16#ffffff,
    seed((Int bsr 16) band 16#fffffff, A2, (Int bsr 36) bor (A2 bsr 16));
seed({A1, A2, A3}) ->
    seed(A1, A2, A3).

%% Seeds avoiding even divisors of the primes; the previous state, or undefined.
seed(A1, A2, A3) ->
    seed_put({(abs(A1) rem 30268) + 1, (abs(A2) rem 30306) + 1, (abs(A3) rem 30322) + 1}).

seed_put(Seed) -> put(random_seed, Seed).

uniform() ->
    State =
        case get(random_seed) of
            undefined -> seed0();
            Tuple -> Tuple
        end,
    {X, NewState} = random:uniform_s(State),
    put(random_seed, NewState),
    X.

uniform(N) when is_integer(N), N >= 1 -> trunc(uniform() * N) + 1.

uniform_s(_) -> clause_builtin_placeholder.

uniform_s(N, State) when is_integer(N), N >= 1 ->
    {X, NewState} = random:uniform_s(State),
    {trunc(X * N) + 1, NewState}.
