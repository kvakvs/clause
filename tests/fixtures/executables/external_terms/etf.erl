-module(etf).
-export([main/1]).

%% The external term format (plan step 41E): OTP's bytes for every term kind and option, round trips of
%% identities whose numbers differ between runtimes, and binary_to_term/2 on malformed input.
main(_) ->
    Big = 1 bsl 70,
    Terms = [
        abc,
        'quoted atom',
        list_to_atom([233]),
        list_to_atom([300]),
        0,
        255,
        256,
        -1,
        1 bsl 31,
        1 bsl 40,
        -Big,
        Big * Big * Big * Big,
        1.5,
        -0.0,
        [],
        "abc",
        [1, 2 | 3],
        [300],
        lists:seq(1, 3) ++ [256],
        {},
        {a, {b, [c]}},
        list_to_tuple(lists:seq(1, 300)),
        #{},
        #{b => 1, a => 2, 1 => x},
        <<>>,
        <<1, 2, 3>>,
        <<1, 2:3>>,
        fun lists:map/2,
        fun erlang:abs/1,
        list_to_ref("#Ref<0.1.2.3>")
    ],
    [io:format("~w~n", [term_to_binary(Term)]) || Term <- Terms],
    Long = [lists:seq(1, 70000), lists:seq(1, 65535)],
    io:format("~p~n", [
        [
            {byte_size(term_to_binary(Term)), binary_to_term(term_to_binary(Term)) =:= Term}
         || Term <- Long
        ]
    ]),
    Options = [
        [],
        [{minor_version, 0}],
        [{minor_version, 1}],
        [{minor_version, 2}],
        [deterministic]
    ],
    [io:format("~w~n", [term_to_binary({abc, 1.5}, Option)]) || Option <- Options],
    io:format("~p~n", [
        [erlang:external_size(Term) =:= byte_size(term_to_binary(Term)) || Term <- Terms]
    ]),
    io:format("~p~n", [erlang:external_size(1.5, [{minor_version, 0}])]),
    io:format("~p~n", [
        iolist_to_binary(term_to_iovec({a, <<"abc">>})) =:= term_to_binary({a, <<"abc">>})
    ]),
    io:format("~p~n", [[binary_to_term(term_to_binary(Term)) =:= Term || Term <- Terms]]),
    Ref = make_ref(),
    Identities = [self(), Ref, {self(), [Ref]}],
    io:format("~p~n", [[binary_to_term(term_to_binary(Term)) =:= Term || Term <- Identities]]),
    Decodes = [
        {<<131, 100, 0, 3, 97, 98, 99>>, []},
        {<<131, 99, "1.50000000000000000000e+00", 0, 0, 0, 0, 0>>, []},
        {<<131, 115, 3, 97, 98, 99, 1>>, []},
        {<<131, 115, 3, 97, 98, 99, 1>>, [used]},
        {<<131, 119, 22, "never_existing_atom_qq">>, [safe]},
        {<<131, 119, 2, "ok">>, [safe]},
        {<<131, 116, 0, 0, 0, 2, 97, 1, 97, 2, 97, 1, 97, 3>>, []},
        {<<131, 97>>, []},
        {<<130, 97, 1>>, []},
        {<<131, 97, 1, 0>>, []},
        {<<131, 97, 1>>, [bogus]},
        {<<131, 108, 0, 0, 0, 5, 106>>, []},
        {<<131, 110, 2, 0, 1, 2>>, []},
        {<<131, 77, 0, 0, 0, 1, 0, 255>>, []},
        {<<131, 255>>, []},
        {<<>>, []}
    ],
    [io:format("~p~n", [decode(Binary, Option)]) || {Binary, Option} <- Decodes],
    io:format("~p~n", [catch_error(fun() -> term_to_binary(abc, [bogus]) end)]),
    io:format("~p~n", [catch_error(fun() -> term_to_binary(abc, [{compressed, 10}]) end)]),
    io:format("~p~n", [catch_error(fun() -> binary_to_term(abc) end)]),
    io:format("etf: ok~n").

% binary_to_term/2 of Binary, or its error.
decode(Binary, Options) -> catch_error(fun() -> binary_to_term(Binary, Options) end).

% The value of Fun, or {error, Reason}.
catch_error(Fun) ->
    try
        Fun()
    catch
        error:Reason -> {error, Reason}
    end.
