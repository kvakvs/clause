-module(rand_sequences).
-export([main/1]).
-compile(nowarn_deprecated_function).

%% rand and random (plan 21 step LIB6): seeded sequences of every algorithm equal OTP's for uniform floats, ranges
%% below and past the generator's bits, dense floats, bytes, normals (a digest of many, so the ziggurat's slow paths
%% are reached), shuffles and jumps; plug-in generators run the generic algorithms; seeds, utilities and errors.
main(_) ->
    Algs = [exsss, exro928ss, exrop, exs1024s, exsp, exs64, exsplus, exs1024, dummy],
    [algorithm(Alg) || Alg <- Algs],
    seeds(),
    plugins(),
    dictionary(),
    utilities(),
    errors(),
    random_module(),
    io:format("rand_sequences: ok~n").

algorithm(Alg) ->
    S0 = rand:seed_s(Alg, 42),
    show({Alg, seed}, rand:export_seed_s(S0)),
    {Floats, S1} = repeat(fun rand:uniform_s/1, S0, 5),
    show({Alg, uniform}, Floats),
    Ranges = [
        1,
        2,
        10,
        1000000,
        (1 bsl 58) - 3,
        1 bsl 58,
        (1 bsl 58) + 1,
        1 bsl 60,
        (1 bsl 70) + 1,
        1 bsl 64
    ],
    {Ints, S2} = lists:mapfoldl(fun(N, S) -> rand:uniform_s(N, S) end, S1, Ranges ++ [pow(10, 30)]),
    show({Alg, ranges}, Ints),
    {Reals, S3} = repeat(fun rand:uniform_real_s/1, S2, 5),
    show({Alg, uniform_real}, Reals),
    {Bytes, S4} = lists:mapfoldl(fun(N, S) -> rand:bytes_s(N, S) end, S3, [0, 1, 5, 7, 8, 23, 64]),
    show({Alg, bytes}, Bytes),
    {Normals, S5} = repeat(fun rand:normal_s/1, S4, 2000),
    show({Alg, normal}, {lists:sublist(Normals, 4), digest(Normals)}),
    {Scaled, S6} = rand:normal_s(10, 4, S5),
    show({Alg, normal3}, Scaled),
    {Shuffles, S7} = lists:mapfoldl(
        fun(N, S) -> rand:shuffle_s(lists:seq(1, N), S) end, S6, [0, 1, 2, 3, 4, 5, 9, 17]
    ),
    show({Alg, shuffle}, Shuffles),
    {Long, S8} = rand:shuffle_s(lists:seq(1, 500), S7),
    show({Alg, shuffle_long}, digest(Long)),
    show({Alg, jump}, catch_error(fun() -> jumped(S8) end)),
    show({Alg, final}, digest(rand:export_seed_s(S8))).

jumped(State) ->
    J = rand:jump(State),
    {X, _} = rand:uniform_s(J),
    {digest(rand:export_seed_s(J)), X}.

seeds() ->
    Seeds = [
        0, -1, 1 bsl 80, -(1 bsl 70), [1, 2], [0, 5], [1 bsl 64, 3], {1, 2, 3}, {-5, 1 bsl 70, 0}
    ],
    show(seed_forms, [
        {Alg, [
            catch_error(fun() -> digest(rand:export_seed_s(rand:seed_s(Alg, S))) end)
         || S <- Seeds
        ]}
     || Alg <- [exsss, exsp, exrop, exs64, exs1024s, exro928ss, dummy]
    ]),
    S16 = lists:seq(1, 16),
    show(seed_lists, [
        catch_error(fun() -> rand:export_seed_s(rand:seed_s(A, S)) end)
     || {A, S} <- [
            {exro928ss, S16},
            {exs1024, [7]},
            {dummy, [99]},
            {exs64, [1 bsl 65]},
            {exs64, [(1 bsl 65) + 3]}
        ]
    ]),
    {Alg, AlgState} = rand:export_seed_s(rand:seed_s(exsss, 5)),
    Restored = rand:seed_s({Alg, AlgState}),
    show(restored, [
        element(1, rand:uniform_s(Restored)), element(1, rand:uniform_s(rand:seed_s(exsss, 5)))
    ]),
    show(
        default_alg,
        rand:export_seed_s(rand:seed_s(default, 3)) =:= rand:export_seed_s(rand:seed_s(exsss, 3))
    ),
    show(auto_seed, [is_map(element(1, rand:seed_s(A))) || A <- [exsss, default, exro928ss]]).

%% Plug-in handlers: the generic algorithms over a handler's funs equal the built-in ones they mirror.
plugins() ->
    Exsp = rand:seed_s(exsp, 11),
    {_, State} = Exsp,
    Plugin = {#{type => mine, bits => 58, weak_low_bits => 1, next => fun rand:exsp_next/1}, State},
    Old = {#{type => old, max => (1 bsl 58) - 1, next => fun rand:exsp_next/1}, State},
    Builtin = {element(1, rand:seed_s(exsplus, 1)), State},
    show(plugin, [
        {
            values(Plugin) =:= values(Exsp),
            values(Old) =:= values(Builtin)
        }
    ]),
    show(plugin_values, [values(Plugin), values(Old)]),
    Custom = {
        #{
            type => custom,
            bits => 58,
            next => fun rand:exsp_next/1,
            uniform => fun({H, S}) -> {0.25, {H, S}} end
        },
        State
    },
    show(plugin_custom, [element(1, rand:uniform_s(Custom)), element(1, rand:normal_s(Custom))]),
    show(plugin_jump, catch_error(fun() -> rand:jump(Plugin) end)).

values(State0) ->
    {F, S1} = rand:uniform_s(State0),
    {N, S2} = rand:uniform_s(1000, S1),
    {B, S3} = rand:uniform_s((1 bsl 100) + 7, S2),
    {P, S4} = rand:uniform_s(1 bsl 90, S3),
    {R, S5} = rand:uniform_real_s(S4),
    {Bytes, S6} = rand:bytes_s(19, S5),
    {Normals, S7} = repeat(fun rand:normal_s/1, S6, 500),
    {Shuffled, {_, Final}} = rand:shuffle_s(lists:seq(1, 40), S7),
    {F, N, B, P, R, Bytes, digest(Normals), Shuffled, Final}.

dictionary() ->
    _ = rand:seed(exsss, 7),
    A = rand:uniform(),
    B = rand:uniform(10),
    C = rand:uniform_real(),
    D = rand:bytes(4),
    E = rand:normal(),
    F = rand:normal(1.0, 4.0),
    G = rand:shuffle([a, b, c, d, e]),
    _ = rand:jump(),
    H = rand:uniform(),
    show(dictionary, [A, B, C, D, E, F, G, H, rand:export_seed()]),
    _ = rand:seed(rand:export_seed()),
    show(reseeded, rand:uniform()),
    _ = rand:seed(exs64, 3),
    show(no_jump, catch_error(fun() -> rand:jump() end)),
    erase(rand_seed),
    show(fresh, [is_float(rand:uniform()), element(1, rand:export_seed())]),
    erase(rand_seed),
    show(fresh_export, rand:export_seed()).

utilities() ->
    show(splitmix, [rand:splitmix64_next(X) || X <- [0, 1, -1, 1 bsl 70, 16#9e3779b97f4a7c15]]),
    show(exsp_next, [rand:exsp_next([1 | 2]), rand:exsp_next([(1 bsl 58) - 1 | 12345])]),
    show(exsp_jump, [rand:exsp_jump([1 | 2]), rand:exsp_jump(element(2, rand:seed_s(exsp, 9)))]),
    Seed = rand:mwc59_seed(42),
    Chain = lists:foldl(
        fun(_, [X | _] = Acc) -> [rand:mwc59(X) | Acc] end, [Seed], lists:seq(1, 6)
    ),
    show(mwc59, [
        Seed,
        rand:mwc59_seed(0),
        rand:mwc59_seed((1 bsl 58) - 1),
        lists:reverse(Chain),
        [rand:mwc59_value32(X) || X <- Chain],
        [rand:mwc59_value(X) || X <- Chain],
        [rand:mwc59_float(X) || X <- Chain],
        is_integer(rand:mwc59_seed())
    ]).

errors() ->
    S = rand:seed_s(exsss, 1),
    show(errors, [
        catch_error(fun() -> rand:uniform_s(0, S) end),
        catch_error(fun() -> rand:uniform_s(id(1.5), S) end),
        catch_error(fun() -> rand:seed_s(exsss, []) end),
        catch_error(fun() -> rand:seed_s(exsss, [0, 0]) end),
        catch_error(fun() -> rand:seed_s(exsss, [1, 2, 3]) end),
        catch_error(fun() -> rand:seed_s(exsss, [a]) end),
        catch_error(fun() -> rand:seed_s(exsss, [0, a]) end),
        catch_error(fun() -> rand:seed_s(exsss, foo) end),
        catch_error(fun() -> rand:seed_s(nope, 1) end),
        catch_error(fun() -> rand:seed_s(exsss, {a, b, c}) end),
        catch_error(fun() -> rand:seed_s(exsss, {1, 2}) end),
        catch_error(fun() -> rand:seed_s(dummy, []) end),
        catch_error(fun() -> rand:seed_s(dummy, [1, 2]) end),
        catch_error(fun() -> rand:seed_s(dummy, [x]) end),
        catch_error(fun() -> rand:seed_s(exro928ss, lists:seq(1, 17)) end),
        catch_error(fun() -> rand:jump(rand:seed_s(dummy, 1)) end),
        catch_error(fun() -> rand:bytes_s(-1, S) end),
        catch_error(fun() -> rand:normal_s(0.0, -1.0, S) end),
        catch_error(fun() -> rand:shuffle_s(id(x), S) end),
        catch_error(fun() -> rand:shuffle_s([1, 2, 3, 4 | x], S) end),
        catch_error(fun() -> rand:mwc59(0) end),
        catch_error(fun() -> rand:mwc59_seed(-1) end),
        catch_error(fun() -> rand:mwc59_value(1 bsl 59) end),
        catch_error(fun() -> rand:splitmix64_next(id(a)) end),
        catch_error(fun() -> rand:uniform_s(id(nostate)) end),
        catch_error(fun() -> rand:export_seed_s(id(x)) end)
    ]).

random_module() ->
    S0 = random:seed0(),
    {Floats, S1} = repeat(fun random:uniform_s/1, S0, 5),
    {N, S2} = random:uniform_s(100, S1),
    show(random_s, [S0, Floats, N, S2]),
    erase(random_seed),
    First = random:seed(1, 2, 3),
    Second = random:seed({4, -5, 1 bsl 70}),
    Third = random:seed(123456789),
    A = random:uniform(),
    B = random:uniform(1000),
    Previous = random:seed(),
    C = random:uniform(),
    erase(random_seed),
    D = random:uniform(),
    show(random, [First, Second, Third, A, B, Previous, C, D, get(random_seed)]),
    show(random_errors, [
        catch_error(fun() -> random:uniform(0) end),
        catch_error(fun() -> random:uniform_s(id(0), S0) end),
        catch_error(fun() -> random:seed(id(x)) end)
    ]).

repeat(Fun, State, Count) ->
    lists:mapfoldl(fun(_, S) -> Fun(S) end, State, lists:seq(1, Count)).

pow(Base, 1) -> Base;
pow(Base, N) -> Base * pow(Base, N - 1).

digest(Term) -> binary:encode_hex(erlang:md5(term_to_binary(Term))).

id(Value) -> Value.

catch_error(Fun) ->
    try
        Fun()
    catch
        error:Reason -> {error, Reason}
    end.

show(Name, Value) -> io:format("~w: ~w~n", [Name, Value]).
