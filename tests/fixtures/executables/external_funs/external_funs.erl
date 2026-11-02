%% Local funs in the external term format (plan 30 step DIST5d, docs/distribution.md#funs): round trips of the
%% program's funs, and NEW_FUN_EXT of funs whose code the program lacks, crafted byte for byte.
-module(external_funs).
-export([main/1, twice/1]).

main(Args) ->
    round_trips(length(Args)),
    crafted(),
    foreign_calls(),
    comparisons(),
    malformed(),
    io:format("external_funs: ok~n").

twice(X) -> 2 * X.

%% The program's funs decode to themselves; their captured values come from the arguments, so no compiler folds them.
round_trips(Count) ->
    Y = Count + 7,
    Bin = integer_to_binary(Count),
    Inner = fun(X) -> X + Y end,
    Outer = fun(X) -> {Inner(X), Bin, self()} end,
    Named = fun
        Fact(0) -> 1;
        Fact(N) -> N * Fact(N - 1)
    end,
    Funs = [Inner, Outer, Named, fun twice/1],
    Decoded = [binary_to_term(term_to_binary(F)) || F <- Funs],
    io:format("equal: ~p~n", [[D =:= F || {D, F} <- lists:zip(Decoded, Funs)]]),
    [Inner2, Outer2, Named2, Twice2] = Decoded,
    io:format("calls: ~w~n", [[Inner2(1), Outer2(2) =:= Outer(2), Named2(5), Twice2(4)]]),
    io:format("shape: ~p~n", [[shape(F) || F <- Funs]]),
    io:format("sizes: ~p~n", [[erlang:external_size(F) =:= byte_size(term_to_binary(F)) || F <- Funs]]),
    Compressed = binary_to_term(term_to_binary(Outer, [compressed])),
    io:format("compressed: ~p~n", [Compressed =:= Outer]),
    io:format("env: ~p~n", [[[is_function(E) || E <- element(2, erlang:fun_info(F, env))] || F <- Decoded]]),
    Same = [
        erlang:fun_info(D, Item) =:= erlang:fun_info(F, Item)
     || {D, F} <- lists:zip(Decoded, Funs), Item <- [module, name, arity, new_uniq, new_index, uniq, index]
    ],
    io:format("fun_info: ~p~n", [lists:usort(Same)]),
    Rebuilt = craft_from(Inner),
    io:format("rebuilt: ~p~n", [[Rebuilt =:= Inner, Rebuilt(3)]]),
    Nested = binary_to_term(term_to_binary({[Outer, #{k => Inner}], Inner})),
    io:format("nested: ~p~n", [Nested =:= {[Outer, #{k => Inner}], Inner}]).

%% The leading fields of a local fun's encoding: its size, arity and free value count.
shape(Fun) ->
    <<131, 112, Size:32, Arity, _Uniq:16/binary, _Index:32, Free:32, _/binary>> = Bin = term_to_binary(Fun),
    {Size =:= byte_size(Bin) - 2, Arity, Free}.

%% A NEW_FUN_EXT naming a program fun by its fun_info items.
craft_from(Fun) ->
    {module, Module} = erlang:fun_info(Fun, module),
    {new_uniq, Uniq} = erlang:fun_info(Fun, new_uniq),
    {new_index, Index} = erlang:fun_info(Fun, new_index),
    {index, OldIndex} = erlang:fun_info(Fun, index),
    {uniq, OldUniq} = erlang:fun_info(Fun, uniq),
    {arity, Arity} = erlang:fun_info(Fun, arity),
    {env, Env} = erlang:fun_info(Fun, env),
    binary_to_term(craft(Module, Uniq, Index, OldIndex, OldUniq, Arity, Env)).

%% Funs whose code the program lacks print, report and encode as they were read.
crafted() ->
    Bytes = craft(nosuch, uniq(1), 5, 6, 123, 1, [a, 1]),
    Fun = binary_to_term(Bytes),
    io:format("foreign: ~p ~w ~s~n", [Fun, Fun, erlang:fun_to_list(Fun)]),
    Items = [module, new_index, new_uniq, index, uniq, name, arity, env, type],
    io:format("info: ~p~n", [[erlang:fun_info(Fun, Item) || Item <- Items]]),
    io:format("again: ~p~n", [term_to_binary(Fun) =:= Bytes]),
    io:format("is_function: ~p~n", [[is_function(Fun), is_function(Fun, 1), is_function(Fun, 2)]]),
    Self = self(),
    Self ! Fun,
    Received =
        receive
            F -> F
        end,
    io:format("sent: ~p~n", [Received =:= Fun]),
    Table = ets:new(funs, []),
    ets:insert(Table, {key, Fun}),
    io:format("ets: ~p~n", [ets:lookup(Table, key) =:= [{key, Fun}]]),
    Outer = craft(nosuch, uniq(2), 1, 1, 9, 0, [Fun, fun twice/1, <<"x">>]),
    io:format("outer: ~p~n", [term_to_binary(binary_to_term(Outer)) =:= Outer]),
    Zero = binary_to_term(<<131, 112, 0:32, (binary:part(Bytes, 6, byte_size(Bytes) - 6))/binary>>),
    io:format("size ignored: ~p~n", [Zero =:= Fun]).

%% Calls of funs whose code the program lacks fail as OTP's error_handler fails them.
foreign_calls() ->
    Fun = binary_to_term(craft(nosuch, uniq(1), 5, 6, 123, 1, [a, 1])),
    io:format("call: ~p~n", [failure(fun() -> Fun(3) end)]),
    io:format("badarity: ~p~n", [reason(fun() -> Fun(3, 4) end)]),
    None = binary_to_term(craft(nosuch, uniq(1), 5, 6, 1, 0, [])),
    io:format("no arguments: ~p~n", [failure(fun() -> None() end)]),
    Lists = binary_to_term(craft(lists, uniq(1), 5, 6, 123, 1, [])),
    io:format("loaded module: ~p~n", [reason(fun() -> Lists(3) end)]),
    Own = binary_to_term(craft(external_funs, uniq(3), 0, 0, 0, 1, [])),
    io:format("own module: ~p~n", [reason(fun() -> Own(3) end)]),
    io:format("apply: ~p~n", [reason(fun() -> apply(Lists, [1]) end)]).

%% Equality and order: =:= needs the same fun, == and order look at module, index, old uniq and environment.
comparisons() ->
    One = binary_to_term(craft(nosuch, uniq(1), 5, 6, 123, 1, [a, 1])),
    Same = binary_to_term(craft(nosuch, uniq(1), 5, 6, 123, 1, [a, 1])),
    Later = binary_to_term(craft(nosuch, uniq(1), 5, 7, 124, 1, [a, 1])),
    OtherUniq = binary_to_term(craft(nosuch, uniq(4), 5, 6, 123, 1, [a, 1])),
    OtherArity = binary_to_term(craft(nosuch, uniq(1), 5, 6, 123, 2, [a, 1])),
    Lower = binary_to_term(craft(nosuch, uniq(1), 4, 6, 123, 1, [a, 1])),
    OtherEnv = binary_to_term(craft(nosuch, uniq(1), 5, 6, 123, 1, [b, 1])),
    Fewer = binary_to_term(craft(nosuch, uniq(1), 5, 6, 123, 1, [a])),
    io:format("same: ~p~n", [[One =:= Same, One =:= Later, Later]]),
    io:format("other uniq: ~p~n", [[One =:= OtherUniq, One == OtherUniq, One < OtherUniq, One > OtherUniq]]),
    io:format("other arity: ~p~n", [[One =:= OtherArity, One == OtherArity]]),
    io:format("order: ~p~n", [[Lower < One, One < OtherEnv, Fewer < One, One < fun twice/1]]),
    Sorted = lists:sort([OtherEnv, Fewer, One, Lower]),
    io:format("sorted: ~p~n", [
        [{element(2, erlang:fun_info(F, new_index)), element(2, erlang:fun_info(F, env))} || F <- Sorted]
    ]),
    io:format("hash: ~p~n", [erlang:phash2(One) =:= erlang:phash2(Same)]),
    io:format("map: ~p~n", [maps:get(Same, #{One => found}, missing)]).

%% Malformed NEW_FUN_EXT: no pid as the creator, an atom as the old index, truncated input.
malformed() ->
    Head = <<131, 112, 0:32, 1, (uniq(1))/binary, 5:32, 0:32, (enc(nosuch))/binary>>,
    Pid = init_pid(),
    io:format("no pid: ~p~n", [decode(<<Head/binary, (enc(6))/binary, (enc(1))/binary, (enc(x))/binary>>)]),
    io:format("atom index: ~p~n", [decode(<<Head/binary, (enc(x))/binary, (enc(1))/binary, Pid/binary>>)]),
    Bytes = craft(nosuch, uniq(1), 5, 6, 123, 1, [a, 1]),
    io:format("truncated: ~p~n", [decode(binary:part(Bytes, 0, byte_size(Bytes) - 1))]),
    io:format("missing free: ~p~n", [decode(<<131, 112, 0:32, 1, (uniq(1))/binary, 5:32, 9:32>>)]).

%% NEW_FUN_EXT of Module's fun Index with the given uniq, old index, old uniq, arity and free values.
craft(Module, Uniq, Index, OldIndex, OldUniq, Arity, Free) ->
    Rest = <<
        Arity,
        Uniq/binary,
        Index:32,
        (length(Free)):32,
        (enc(Module))/binary,
        (enc(OldIndex))/binary,
        (enc(OldUniq))/binary,
        (init_pid())/binary,
        <<<<(enc(V))/binary>> || V <- Free>>/binary
    >>,
    <<131, 112, (byte_size(Rest) + 4):32, Rest/binary>>.

%% NEW_PID_EXT of init's pid <0.0.0>, the creator OTP writes, in a node that is not alive.
init_pid() -> <<88, (enc(node()))/binary, 0:32, 0:32, 0:32>>.

%% A 16-byte uniq starting at First.
uniq(First) -> list_to_binary(lists:seq(First, First + 15)).

%% A term's encoding without the version byte.
enc(Term) ->
    <<131, Bytes/binary>> = term_to_binary(Term),
    Bytes.

decode(Bytes) -> reason(fun() -> binary_to_term(Bytes) end).

%% The class, reason and top stack frame of a failed call.
failure(Fun) ->
    try Fun() of
        Value -> {ok, Value}
    catch
        Class:Reason:Stack -> {Class, Reason, hd(Stack)}
    end.

%% The error reason of a failed call.
reason(Fun) ->
    try Fun() of
        Value -> {ok, Value}
    catch
        error:Reason -> Reason
    end.
