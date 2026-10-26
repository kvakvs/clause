%% Clause's rand (plan 21 step LIB6, docs/builtins.md#random-numbers): states are OTP's {AlgHandler, AlgState} with
%% OTP's handler maps. For OTP's own algorithms the work is done by the clause_rand builtins on the algorithm state;
%% a plug-in handler (any other map) runs OTP's generic algorithms here, over its funs. The word-level utilities
%% (splitmix64_next/1, exsp_next/1, exsp_jump/1, the mwc59 functions) are runtime builtins.
-module(rand).
-export([
    bc64/1,
    bytes/1,
    bytes_s/2,
    export_seed/0,
    export_seed_s/1,
    exro928_jump_2pow20/1,
    exro928_jump_2pow512/1,
    exro928_next/1,
    exro928_next_state/1,
    exro928_seed/1,
    exsp_jump/1,
    exsp_next/1,
    float2str/1,
    format_jumpconst58/1,
    jump/0,
    jump/1,
    make_float/3,
    mwc59/1,
    mwc59_float/1,
    mwc59_seed/0,
    mwc59_seed/1,
    mwc59_value/1,
    mwc59_value32/1,
    normal/0,
    normal/2,
    normal_s/1,
    normal_s/3,
    seed/1,
    seed/2,
    seed58/2,
    seed_s/1,
    seed_s/2,
    shuffle/1,
    shuffle_s/2,
    splitmix64_next/1,
    uniform/0,
    uniform/1,
    uniform_real/0,
    uniform_real_s/1,
    uniform_s/1,
    uniform_s/2
]).

-include("rand.types.hrl").

-define(SEED_KEY, rand_seed).
-define(BIT(Bits), (1 bsl (Bits))).
-define(MASK(Bits), (?BIT(Bits) - 1)).
-define(TWO_POW_MINUS53, 1.1102230246251565e-16).
-define(NORMAL_EDGE, 3.6541528853610087963519472518).

%% Seeding and the process dictionary

export_seed() ->
    case get(?SEED_KEY) of
        {#{type := Alg}, AlgState} -> {Alg, AlgState};
        _ -> undefined
    end.

export_seed_s({#{type := Alg}, AlgState}) -> {Alg, AlgState}.

seed(AlgOrState) -> seed_put(seed_s(AlgOrState)).

seed_s({Handler, _} = State) when is_map(Handler) -> State;
seed_s({Alg, AlgState}) when is_atom(Alg) -> {handler(Alg), AlgState};
seed_s(Alg) -> seed_s(Alg, default_seed()).

seed(Alg, Seed) -> seed_put(seed_s(Alg, Seed)).

seed_s(default, Seed) ->
    seed_s(exsss, Seed);
seed_s(Alg, Seed) ->
    Handler = handler(Alg),
    {Handler, clause_rand:seed(Alg, Seed)}.

%% A seed unique to this call: the process, a unique integer and the time, spread by MD5.
default_seed() ->
    <<A:43, B:43, C:42>> = erlang:md5(
        term_to_binary({self(), erlang:unique_integer(), erlang:system_time()})
    ),
    {A, B, C}.

seed_put(State) ->
    put(?SEED_KEY, State),
    State.

seed_get() ->
    case get(?SEED_KEY) of
        undefined -> seed(exsss);
        State -> State
    end.

%% OTP's handler of each algorithm; the funs run the algorithm's builtins.
handler(exs64) ->
    #{type => exs64, max => ?MASK(64), next => fun next_exs64/1};
handler(exsplus) ->
    #{type => exsplus, max => ?MASK(58), next => fun next_exsp/1, jump => fun jump_exsplus/1};
handler(exsp) ->
    #{
        type => exsp,
        bits => 58,
        weak_low_bits => 1,
        next => fun next_exsp/1,
        uniform => fun uniform_exsp/1,
        uniform_n => fun uniform_exsp/2,
        jump => fun jump_exsplus/1
    };
handler(exsss) ->
    #{
        type => exsss,
        bits => 58,
        next => fun next_exsss/1,
        uniform => fun uniform_exsss/1,
        uniform_n => fun uniform_exsss/2,
        jump => fun jump_exsplus/1
    };
handler(exs1024) ->
    #{type => exs1024, max => ?MASK(64), next => fun next_exs1024/1, jump => fun jump_exs1024/1};
handler(exs1024s) ->
    #{
        type => exs1024s,
        bits => 64,
        weak_low_bits => 3,
        next => fun next_exs1024/1,
        jump => fun jump_exs1024/1
    };
handler(exrop) ->
    #{
        type => exrop,
        bits => 58,
        weak_low_bits => 1,
        next => fun next_exrop/1,
        uniform => fun uniform_exrop/1,
        uniform_n => fun uniform_exrop/2,
        jump => fun jump_exrop/1
    };
handler(exro928ss) ->
    #{
        type => exro928ss,
        bits => 58,
        next => fun next_exro928ss/1,
        uniform => fun uniform_exro928ss/1,
        uniform_n => fun uniform_exro928ss/2,
        jump => fun jump_exro928ss/1
    };
handler(dummy) ->
    #{
        type => dummy,
        bits => 58,
        next => fun next_dummy/1,
        uniform => fun uniform_dummy/1,
        uniform_n => fun uniform_dummy/2
    }.

next_exs64(S) -> clause_rand:next(exs64, S).
next_exsp(S) -> clause_rand:next(exsp, S).
next_exsss(S) -> clause_rand:next(exsss, S).
next_exs1024(S) -> clause_rand:next(exs1024, S).
next_exrop(S) -> clause_rand:next(exrop, S).
next_exro928ss(S) -> clause_rand:next(exro928ss, S).
next_dummy(S) -> clause_rand:next(dummy, S).

uniform_exsp(State) -> uniform_with(exsp, State).
uniform_exsss(State) -> uniform_with(exsss, State).
uniform_exrop(State) -> uniform_with(exrop, State).
uniform_exro928ss(State) -> uniform_with(exro928ss, State).
uniform_dummy(State) -> uniform_with(dummy, State).

uniform_exsp(N, State) -> uniform_with(exsp, N, State).
uniform_exsss(N, State) -> uniform_with(exsss, N, State).
uniform_exrop(N, State) -> uniform_with(exrop, N, State).
uniform_exro928ss(N, State) -> uniform_with(exro928ss, N, State).
uniform_dummy(N, State) -> uniform_with(dummy, N, State).

jump_exsplus({Handler, S}) -> {Handler, clause_rand:jump(exsp, S)}.
jump_exs1024({Handler, S}) -> {Handler, clause_rand:jump(exs1024, S)}.
jump_exrop({Handler, S}) -> {Handler, clause_rand:jump(exrop, S)}.
jump_exro928ss({Handler, S}) -> {Handler, clause_rand:jump(exro928ss, S)}.

uniform_with(Alg, {Handler, S}) ->
    {X, S1} = clause_rand:uniform(Alg, S),
    {X, {Handler, S1}}.

uniform_with(Alg, N, {Handler, S}) ->
    {X, S1} = clause_rand:uniform(Alg, N, S),
    {X, {Handler, S1}}.

%% The algorithm of a handler that is OTP's own, unchanged; none for a plug-in.
builtin(#{type := Alg} = Handler) ->
    case lists:member(Alg, [exs64, exsplus, exsp, exsss, exs1024, exs1024s, exrop, exro928ss, dummy]) of
        true -> same_handler(Alg, Handler);
        false -> none
    end;
builtin(_) ->
    none.

same_handler(Alg, Handler) ->
    case handler(Alg) of
        Handler -> Alg;
        _ -> none
    end.

%% Values

uniform() ->
    {X, State} = uniform_s(seed_get()),
    _ = seed_put(State),
    X.

uniform(N) ->
    {X, State} = uniform_s(N, seed_get()),
    _ = seed_put(State),
    X.

uniform_s({Handler, S} = State) ->
    case builtin(Handler) of
        none -> plugin_uniform(State);
        Alg -> uniform_with(Alg, {Handler, S})
    end.

uniform_s(N, {Handler, S} = State) when is_integer(N), 1 =< N ->
    case builtin(Handler) of
        none -> plugin_uniform(N, State);
        Alg -> uniform_with(Alg, N, {Handler, S})
    end.

uniform_real() ->
    {X, State} = uniform_real_s(seed_get()),
    _ = seed_put(State),
    X.

uniform_real_s({Handler, S} = State) ->
    case builtin(Handler) of
        none ->
            plugin_real(State);
        Alg ->
            {X, S1} = clause_rand:uniform_real(Alg, S),
            {X, {Handler, S1}}
    end.

bytes(N) ->
    {Bytes, State} = bytes_s(N, seed_get()),
    _ = seed_put(State),
    Bytes.

bytes_s(N, {Handler, S} = State) when is_integer(N), 0 =< N ->
    case builtin(Handler) of
        none ->
            plugin_bytes(N, State);
        Alg ->
            {Bytes, S1} = clause_rand:bytes(Alg, N, S),
            {Bytes, {Handler, S1}}
    end.

jump(State = {#{jump := Jump}, _}) -> Jump(State);
jump({#{}, _}) -> erlang:error(not_implemented).

jump() -> seed_put(jump(seed_get())).

normal() ->
    {X, State} = normal_s(seed_get()),
    _ = seed_put(State),
    X.

normal(Mean, Variance) ->
    Deviation = math:sqrt(Variance),
    Mean + Deviation * normal().

normal_s({Handler, S} = State) ->
    case builtin(Handler) of
        none ->
            plugin_normal(State);
        Alg ->
            {X, S1} = clause_rand:normal(Alg, S),
            {X, {Handler, S1}}
    end.

normal_s(Mean, Variance, State) when 0 =< Variance ->
    {X, NewState} = normal_s(State),
    {Mean + (math:sqrt(Variance) * X), NewState}.

shuffle(List) ->
    {Shuffled, State} = shuffle_s(List, seed_get()),
    _ = seed_put(State),
    Shuffled.

shuffle_s(List, {Handler, S} = State) when is_list(List) ->
    case builtin(Handler) of
        none ->
            plugin_shuffle(List, State);
        Alg ->
            {Shuffled, S1} = clause_rand:shuffle(Alg, List, S),
            {Shuffled, {Handler, S1}}
    end.

mwc59_seed() ->
    {A1, A2, A3} = default_seed(),
    ((rand:mwc59_seed(A1) - 1) bxor (rand:mwc59_seed(A2) - 1) bxor (rand:mwc59_seed(A3) - 1)) + 1.

exro928_jump_2pow20(_) -> clause_builtin_placeholder.
exro928_jump_2pow512(_) -> clause_builtin_placeholder.
exro928_next(_) -> clause_builtin_placeholder.
exro928_next_state(_) -> clause_builtin_placeholder.
exro928_seed(_) -> clause_builtin_placeholder.
exsp_jump(_) -> clause_builtin_placeholder.
exsp_next(_) -> clause_builtin_placeholder.
mwc59(_) -> clause_builtin_placeholder.
mwc59_float(_) -> clause_builtin_placeholder.
mwc59_seed(_) -> clause_builtin_placeholder.
mwc59_value(_) -> clause_builtin_placeholder.
mwc59_value32(_) -> clause_builtin_placeholder.
seed58(_, _) -> clause_builtin_placeholder.
splitmix64_next(_) -> clause_builtin_placeholder.

bc64(_) -> erlang:error({not_implemented, bc64, 1}).
float2str(_) -> erlang:error({not_implemented, float2str, 1}).
format_jumpconst58(_) -> erlang:error({not_implemented, format_jumpconst58, 1}).
make_float(_, _, _) -> erlang:error({not_implemented, make_float, 3}).

%% Plug-in generators: OTP's generic algorithms over the handler's funs, `bits` or the deprecated `max`.

plugin_uniform(State = {#{uniform := Uniform}, _}) ->
    Uniform(State);
plugin_uniform({#{bits := Bits, next := Next} = Handler, S0}) ->
    {V, S1} = Next(S0),
    {(V bsr (Bits - 53)) * ?TWO_POW_MINUS53, {Handler, S1}};
plugin_uniform({#{max := Max, next := Next} = Handler, S0}) ->
    {V, S1} = Next(S0),
    {V / (Max + 1), {Handler, S1}}.

plugin_uniform(N, State = {#{uniform_n := UniformN}, _}) ->
    UniformN(N, State);
plugin_uniform(N, {#{bits := Bits, next := Next} = Handler, S0}) ->
    {V, S1} = Next(S0),
    Room = ?BIT(Bits) - N,
    if
        Room < 0 -> wide_range(N, Handler, S1, V);
        V < N -> {V + 1, {Handler, S1}};
        V - V rem N =< Room -> {V rem N + 1, {Handler, S1}};
        true -> uniform_s(N, {Handler, S1})
    end;
plugin_uniform(N, {#{max := Max, next := Next} = Handler, S0}) ->
    {V, S1} = Next(S0),
    if
        N =< Max -> {(V rem N) + 1, {Handler, S1}};
        true -> {trunc(V / (Max + 1) * N) + 1, {Handler, S1}}
    end.

%% A range past the generator's bits: a power of two from enough bits, else two bits more than the range and a
%% fresh draw from the truncated top.
wide_range(Range, #{next := Next, bits := Bits} = Handler, S, V) ->
    Weak = maps:get(weak_low_bits, Handler, 0),
    Widen = {Next, bnot ?MASK(Weak), Bits - Weak},
    case Range band (Range - 1) of
        0 ->
            {V1, S1, _} = widen(Range bsr Bits, Widen, S, V, Bits),
            {(V1 band (Range - 1)) + 1, {Handler, S1}};
        _ ->
            {V1, S1, Count} = widen(Range bsr (Bits - 2), Widen, S, V, Bits),
            Index = V1 rem Range,
            case V1 - Index =< ?BIT(Count) - Range of
                true ->
                    {Index + 1, {Handler, S1}};
                false ->
                    {V2, S2} = Next(S1),
                    wide_range(Range, Handler, S2, V2)
            end
    end.

widen(Range, _Widen, S, V, Count) when Range =< 1 ->
    {V, S, Count};
widen(Range, {Next, Mask, Shift} = Widen, S, V, Count) ->
    {V1, S1} = Next(S),
    widen(Range bsr Shift, Widen, S1, ((V band Mask) bsl Shift) bor V1, Count + Shift).

%% uniform_real_s/1: a float of 53 significant bits however small, drawing more words while the top ones are zero.
plugin_real({Handler, S0}) ->
    {Bits, Draw} = dense_words(Handler),
    {V1, S1} = Draw(S0),
    case V1 bsr (Bits - 56) of
        M1 when M1 >= ?BIT(52) ->
            {scaled(M1, -56), {Handler, S1}};
        M1 ->
            {V2, S2} = Draw(S1),
            dense_more(Handler, Draw, Bits, M1, -56, S2, V2)
    end.

dense_words(#{bits := Bits, next := Next}) ->
    {Bits, Next};
dense_words(#{max := _, next := Next}) ->
    {56, fun(S) ->
        {V, S1} = Next(S),
        {V band ?MASK(56), S1}
    end}.

%% The top 53 of 53 to 56 bits, each worth 2^Exponent.
scaled(M, Exponent) ->
    Wasted = bit_count(M) - 53,
    (M bsr Wasted) * math:pow(2.0, Exponent + Wasted).

bit_count(0) -> 0;
bit_count(M) -> 1 + bit_count(M bsr 1).

dense_more(Handler, _Draw, Bits, M0, BitNo, S, V1) when M0 =/= 0; BitNo =:= -1064 ->
    Missing = 53 - bit_count(M0),
    {
        ((M0 bsl Missing) bor (V1 bsr (Bits - Missing))) * math:pow(2.0, BitNo - Missing),
        {Handler, S}
    };
dense_more(Handler, Draw, Bits, 0, BitNo, S, V1) ->
    case V1 bsr (Bits - 56) of
        M1 when M1 >= ?BIT(52) ->
            {scaled(M1, BitNo - 56), {Handler, S}};
        M1 when BitNo =:= -1008, M1 < ?BIT(42) ->
            plugin_real({Handler, S});
        M1 ->
            {V2, S2} = Draw(S),
            dense_more(Handler, Draw, Bits, M1, BitNo - 56, S2, V2)
    end.

%% bytes_s/2: whole bytes from the top of each word without its weak bits; the last word gives the rest.
plugin_bytes(N, State = {#{bytes := Bytes}, _}) ->
    Bytes(N, State);
plugin_bytes(N, {#{bits := Bits, next := Next} = Handler, S}) ->
    random_bytes(N, Handler, Next, S, Bits, maps:get(weak_low_bits, Handler, 0), <<>>);
plugin_bytes(N, {#{max := Max, next := Next} = Handler, S}) when ?MASK(58) =< Max ->
    random_bytes(N, Handler, Next, S, 58, 2, <<>>).

random_bytes(N, Handler, Next, S0, Bits, Weak, Acc) ->
    GoodBits = ((Bits - Weak) bsr 3) bsl 3,
    {V, S1} = Next(S0),
    case GoodBits bsr 3 of
        GoodBytes when GoodBytes < N ->
            random_bytes(
                N - GoodBytes,
                Handler,
                Next,
                S1,
                Bits,
                Weak,
                <<Acc/binary, (V bsr (Bits - GoodBits)):GoodBits>>
            );
        _ ->
            Last = N bsl 3,
            {<<Acc/binary, (V bsr (GoodBits - Last)):Last>>, {Handler, S1}}
    end.

%% normal_s/1: the ziggurat on the word's sign, 51 bits and strip; the slow path for the tail and the strip edges.
plugin_normal(State0) ->
    {Negative, R, State} = word52(State0),
    Index = R band 255,
    {Bound, Width, _} = clause_rand:ziggurat(Index),
    X = R * Width,
    Signed =
        case Negative of
            true -> -X;
            false -> X
        end,
    if
        R < Bound -> {Signed, State};
        Index =:= 0 -> normal_tail(Negative, State);
        true -> normal_edge(Index, Signed, State)
    end.

word52({#{bits := Bits, next := Next} = Handler, S0}) ->
    {Word, S1} = Next(S0),
    {?BIT(Bits - 52) band Word =/= 0, Word bsr (Bits - 51), {Handler, S1}};
word52({#{next := Next} = Handler, S0}) ->
    {Word, S1} = Next(S0),
    {?BIT(51) band Word =/= 0, Word band ?MASK(51), {Handler, S1}}.

normal_tail(Negative, State0) ->
    {U0, State1} = uniform_s(State0),
    X = -1 / ?NORMAL_EDGE * math:log(U0),
    {U1, State2} = uniform_s(State1),
    Y = -math:log(U1),
    if
        Y + Y =< X * X -> normal_tail(Negative, State2);
        Negative -> {-?NORMAL_EDGE - X, State2};
        true -> {?NORMAL_EDGE + X, State2}
    end.

normal_edge(Index, X, State0) ->
    {_, _, Outer} = clause_rand:ziggurat(Index),
    {_, _, Inner} = clause_rand:ziggurat(Index - 1),
    {U0, State1} = uniform_s(State0),
    case (Inner - Outer) * U0 + Outer < math:exp(-0.5 * X * X) of
        true -> {X, State1};
        false -> normal_s(State1)
    end.

%% shuffle_s/2: four-way random splits, two elements by one bit, three by a rejection-sampled permutation, over a
%% cache of the words' bits under a marker bit: {Cache, State, Next, Shift, Mask}.
plugin_shuffle(List, {Handler, S}) ->
    {Shift, Mask} = bit_source(Handler),
    {Shuffled, {_, S1, _, _, _}} = shuffle_onto(
        List, [], {1, S, maps:get(next, Handler), Shift, Mask}
    ),
    {Shuffled, {Handler, S1}}.

bit_source(#{bits := Bits} = Handler) ->
    Shift = maps:get(weak_low_bits, Handler, 0),
    {Shift, ?MASK(58) band (?MASK(Bits) bsr Shift)};
bit_source(#{max := Max}) ->
    {2, ?MASK(58) band (Max bsr 2)}.

%% Width random bits and the bit source after them; a fresh word when fewer remain.
take(Width, {Cache, S, Next, Shift, Mask}) when Cache >= ?BIT(Width) ->
    {Cache band ?MASK(Width), {Cache bsr Width, S, Next, Shift, Mask}};
take(Width, {_, S, Next, Shift, Mask}) ->
    {V, S1} = Next(S),
    take(Width, {((V bsr Shift) band Mask) bor (Mask + 1), S1, Next, Shift, Mask}).

%% The shuffled list put in front of Acc, and the bit source after it.
shuffle_onto([], Acc, Source) ->
    {Acc, Source};
shuffle_onto([X], Acc, Source) ->
    {[X | Acc], Source};
shuffle_onto([X, Y], Acc, Source) ->
    case take(1, Source) of
        {0, Source1} -> {[Y, X | Acc], Source1};
        {1, Source1} -> {[X, Y | Acc], Source1}
    end;
shuffle_onto([X, Y, Z], Acc, Source) ->
    case take(3, Source) of
        {0, Source1} -> {[Z, Y, X | Acc], Source1};
        {1, Source1} -> {[Y, Z, X | Acc], Source1};
        {2, Source1} -> {[Z, X, Y | Acc], Source1};
        {3, Source1} -> {[X, Z, Y | Acc], Source1};
        {4, Source1} -> {[Y, X, Z | Acc], Source1};
        {5, Source1} -> {[X, Y, Z | Acc], Source1};
        {_, Source1} -> shuffle_onto([X, Y, Z], Acc, Source1)
    end;
shuffle_onto([_, _, _, _ | _] = List, Acc, Source) ->
    split(List, Acc, Source, [], [], [], []).

split([], Acc0, Source0, Zero, One, Two, Three) ->
    {Acc1, Source1} = shuffle_onto(Zero, Acc0, Source0),
    {Acc2, Source2} = shuffle_onto(One, Acc1, Source1),
    {Acc3, Source3} = shuffle_onto(Two, Acc2, Source2),
    shuffle_onto(Three, Acc3, Source3);
split([X | Rest], Acc, Source0, Zero, One, Two, Three) ->
    case take(2, Source0) of
        {0, Source} -> split(Rest, Acc, Source, [X | Zero], One, Two, Three);
        {1, Source} -> split(Rest, Acc, Source, Zero, [X | One], Two, Three);
        {2, Source} -> split(Rest, Acc, Source, Zero, One, [X | Two], Three);
        {3, Source} -> split(Rest, Acc, Source, Zero, One, Two, [X | Three])
    end.
