-module(atom_order).
-export([main/1]).

%% Atom ordering (plan 20 step OPT3.14): two atoms compare by spelling straight from the atom
%% table; the fixed atoms, atoms made at run time, prefixes, Unicode and mixed kinds keep OTP's order.
main(_) ->
    Made = [list_to_atom(S) || S <- ["zeta", "alpha", "ab", "abc", "", [228], [8364], "Z", "a b"]],
    Fixed = [undefined, ok, error, true, false],
    Sorted = lists:sort(Made ++ Fixed ++ [beta, 'Alpha', '_x', abd]),
    io:format("~w~n", [[atom_to_list(A) || A <- Sorted]]),
    io:format("~p~n", [
        [
            id(ab) < id(abc),
            id(abc) < id(abd),
            id('') < id(a),
            id('Z') < id(a),
            id(list_to_atom([228])) > id(z),
            id(false) < id(true),
            id(ok) >= id(error),
            id(undefined) =< id(undefined),
            id(alpha) == id(alpha),
            id(alpha) /= id(beta),
            min(id(zeta), id(alpha)),
            max(id(zeta), id(alpha)),
            id(alpha) < id(1),
            id(alpha) > id(1),
            id(alpha) < id({}),
            id(alpha) < id(make_ref())
        ]
    ]),
    io:format("~p~n", [[guarded(A, B) || {A, B} <- [{a, b}, {b, a}, {a, a}, {a, 1}]]]),
    io:format("~p~n", [lists:keysort(1, [{c, 1}, {a, 2}, {b, 3}, {a, 0}])]),
    io:format("atom_order: ok~n").

guarded(A, B) when A < B -> less;
guarded(A, B) when A > B -> greater;
guarded(_, _) -> same.

id(Value) -> Value.
