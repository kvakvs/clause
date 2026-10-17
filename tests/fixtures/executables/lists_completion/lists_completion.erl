-module(lists_completion).
-export([main/1]).

%% The lists functions completed after plan 20 step OPT3.14: data functions are runtime builtins,
%% functions taking funs are Erlang; results and error reasons follow OTP 29's documentation.
main(_) ->
    show(zips, [
        lists:zip([a, b], [1, 2]),
        lists:zip([a, b], [1, 2, 3], trim),
        lists:zip([a, b, c], [1, 2], trim),
        lists:zip([a, b], [1, 2, 3], {pad, {x, 0}}),
        lists:zip([a, b, c], [1, 2], {pad, {x, 0}}),
        lists:zip([], [], fail),
        lists:zip3([a, b], [1, 2], ["x", "y"]),
        lists:zip3([a], [1, 2, 3], [17, 19], trim),
        lists:zip3([a], [1, 2, 3], [17, 19], {pad, {z, 0, 0}}),
        lists:zip3([], [1], [], {pad, {p, q, r}}),
        lists:unzip([{1, a}, {2, b}]),
        lists:unzip([]),
        lists:unzip3([{a, 1, 2}, {b, 777, 999}])
    ]),
    show(zipwith, [
        lists:zipwith(fun(X, Y) -> X + Y end, [1, 2, 3], [4, 5, 6]),
        lists:zipwith(fun(X, Y) -> {Y, X} end, [a, b], [1, 2, 3], trim),
        lists:zipwith(fun(X, Y) -> X * Y end, [1, 2, 3], [5], {pad, {0, 10}}),
        lists:zipwith3(fun(X, Y, Z) -> X + Y + Z end, [1, 2], [3, 4], [5, 6]),
        lists:zipwith3(fun(X, Y, Z) -> [X, Y, Z] end, [a], [b, c], [d], {pad, {x, y, z}})
    ]),
    show(enumerate, [
        lists:enumerate([a, b, c]),
        lists:enumerate(10, [a, b, c]),
        lists:enumerate(0, -2, [a, b, c]),
        lists:enumerate(1 bsl 70, 1, [a, b]),
        lists:enumerate([])
    ]),
    Tuples = [{a, 1}, {b, 2}, {c, 3}, {b, 4}],
    show(keys, [
        lists:keyreplace(b, 1, Tuples, {b, new}),
        lists:keyreplace(z, 1, Tuples, {z, new}),
        lists:keystore(b, 1, Tuples, {b, new}),
        lists:keystore(z, 1, Tuples, {z, new}),
        lists:keytake(b, 1, Tuples),
        lists:keytake(z, 1, Tuples),
        lists:keytake(2.0, 2, Tuples),
        lists:keyreplace(1, 2, [x, {y}, {a, 1}], {r}),
        lists:keymap(fun(X) -> X * 10 end, 2, Tuples),
        lists:keymap(fun atom_to_list/1, 1, [{a, 1}, {b, 2}])
    ]),
    show(merges, [
        lists:merge([1, 3, 5], [2, 3, 4]),
        lists:merge([1.0, 2], [1, 2.0]),
        lists:merge([[1, 4], [2, 3], [], [0, 5]]),
        lists:merge3([1, 4], [1.0, 2], [3, 4.0]),
        lists:umerge3([1, 3], [1.0, 2], [2.0, 3, 5]),
        lists:umerge(fun(A, B) -> A >= B end, [5, 3, 1], [4, 3, 2]),
        lists:umerge(fun(A, B) -> A =< B end, [1, 2], [1.0, 2.0, 3]),
        lists:keymerge(1, [{a, 1}, {c, 1}], [{a, 2}, {b, 2}]),
        lists:ukeymerge(1, [{a, 1}, {c, 1}], [{a, 2}, {b, 2}, {c, 2}]),
        lists:merge([], [b]),
        lists:keymerge(2, [x], [])
    ]),
    show(sorts, [
        lists:ukeysort(1, [{b, 1}, {a, 2}, {b, 3}, {a, 4}, {c, 5}]),
        lists:ukeysort(2, [{x, 1.0}, {y, 1}, {z, 0}]),
        lists:ukeysort(1, [single]),
        lists:usort(fun(A, B) -> abs(A) =< abs(B) end, [3, -1, 2, 1, -3, 0]),
        lists:usort(fun(A, B) -> A >= B end, [1, 5, 3, 5, 1]),
        lists:usort(fun(A, B) -> A =< B end, [])
    ]),
    show(others, [
        lists:delete(b, [a, b, c, b]),
        lists:delete(1, [1.0, 1, 2]),
        lists:delete(z, [a, b]),
        lists:flatlength([a, [b, [c, []], d], [], e]),
        lists:flatlength([]),
        lists:concat([doc, '/', file, '.', 3]),
        lists:concat([1.5, -7, "str", [], 'ä']),
        lists:flatmap(fun(X) -> [X, X] end, [a, b, c]),
        lists:flatmap(fun(_) -> [] end, [a, b]),
        lists:search(fun(X) -> X > 2 end, [1, 2, 3, 4]),
        lists:search(fun(X) -> X > 9 end, [1, 2]),
        lists:subtract([a, b, c, a], [a, c])
    ]),
    show(errors, [
        reason(fun() -> lists:zip([a], [1, 2]) end),
        reason(fun() -> lists:zip([a], [1], bad) end),
        reason(fun() -> lists:zip3([a], [1], [], fail) end),
        reason(fun() -> lists:unzip([{a, 1}, {b}]) end),
        reason(fun() -> lists:unzip3([{a, 1}]) end),
        reason(fun() -> lists:enumerate(a, [x]) end),
        reason(fun() -> lists:enumerate([x | y]) end),
        reason(fun() -> lists:keyreplace(a, 0, [], {n}) end),
        reason(fun() -> lists:keyreplace(a, 1, [], not_tuple) end),
        reason(fun() -> lists:keystore(a, 1, [{b} | c], {a}) end),
        reason(fun() -> lists:keytake(a, 1, id(improper)) end),
        reason(fun() -> lists:merge([1], [2 | x]) end),
        reason(fun() -> lists:keymerge(1, [a], [b]) end),
        reason(fun() -> lists:ukeysort(0, []) end),
        reason(fun() -> lists:delete(a, [b | c]) end),
        reason(fun() -> lists:flatlength([a | b]) end),
        reason(fun() -> lists:concat([{tuple}]) end),
        reason(fun() -> lists:subtract(id(a), []) end)
    ]),
    io:format("lists_completion: ok~n").

id(Value) -> Value.

show(Name, Values) -> io:format("~w: ~w~n", [Name, Values]).

reason(Fun) ->
    try Fun() of
        Value -> {value, Value}
    catch
        error:Reason -> Reason
    end.
