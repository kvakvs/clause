-module(literals).
-export([main/1]).

% Compound literals from the module literal area (OPT0.4): strings, floats, bignums and constant tuples and lists,
% compared with equal terms built at run time, sent between processes, kept across collections, stored as
% persistent terms, encoded, updated and matched.

text() -> "literal text".
real() -> -2.5e-3.
big() -> 123456789012345678901234567890123.
negative() -> -98765432109876543210.
nested() ->
    {1, [2, 3.0], "four", {5, [], {}}, [true | false], -340282366920938463463374607431768211456}.

built() -> {1, [2, 3.0], lists:reverse("ruof"), {5, [], {}}, [true | false], -(1 bsl 128)}.

main(_) ->
    io:format("~p~n~p ~p ~p~n", [text(), real(), big(), negative()]),
    io:format("~p ~p ~p~n", [
        nested() =:= built(), nested() == built(), erlang:phash2(nested()) == erlang:phash2(built())
    ]),
    io:format("~p~n", [lists:sort([big(), real(), text(), nested(), negative(), 7])]),
    Parent = self(),
    Pid = spawn(fun() ->
        receive
            {From, Value} -> From ! {echo, Value, Value =:= nested()}
        end
    end),
    Pid ! {Parent, nested()},
    receive
        {echo, Echoed, Same} -> io:format("echo ~p ~p~n", [Echoed =:= built(), Same])
    end,
    Kept = [nested(), text(), big()],
    Garbage = [lists:seq(1, 1000) || _ <- lists:seq(1, 50)],
    erlang:garbage_collect(),
    io:format("after gc ~p ~p~n", [Kept =:= [built(), "literal text", big()], length(Garbage)]),
    persistent_term:put(literals_key, nested()),
    io:format("persistent ~p~n", [persistent_term:get(literals_key) =:= built()]),
    Encoded = term_to_binary(nested()),
    io:format("encoded ~p ~p~n", [byte_size(Encoded), binary_to_term(Encoded) =:= nested()]),
    io:format("updated ~p~n", [setelement(1, nested(), one)]),
    {1, [_, F], S, {_, [], {}}, [true | false], N} = nested(),
    io:format("matched ~p ~p ~p ~p~n", [F, S, N, [C || C <- text(), C > $l]]),
    io:format("literals: ok~n").
