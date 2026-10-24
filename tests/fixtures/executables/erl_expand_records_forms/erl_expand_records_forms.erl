%% erl_expand_records:module/2 over the forms of a snippet module (plan 21 step LIB34): tuple records in
%% constructions, patterns, guards, field access, updates, record_info/2 and is_record/2, native and imported native
%% records, imported functions and BIFs, under each option. The expanded functions are printed with erl_pp.
-module(erl_expand_records_forms).
-export([main/1]).

main(_) ->
    Forms = forms(source()),
    [
        begin
            io:format("options ~p:~n", [Options]),
            [
                io:format("~ts", [erl_pp:form(F)])
             || {function, _, _, _, _} = F <- erl_expand_records:module(Forms, Options)
            ]
        end
     || Options <- [[], [dialyzer], [no_strict_record_tests], [expand_inits], [strict_record_tests]]
    ],
    io:format("attributes: ~p~n", [
        [element(3, F) || F <- erl_expand_records:module(Forms, []), element(1, F) =:= attribute]
    ]),
    io:format("erl_expand_records_forms: ok~n").

forms(Text) -> forms(Text, 1, []).

forms(Text, Line, Acc) ->
    case erl_scan:tokens([], Text, Line) of
        {done, {ok, Tokens, End}, Rest} ->
            {ok, Form} = erl_parse:parse_form(Tokens),
            forms(Rest, End, [Form | Acc]);
        _ ->
            lists:reverse(Acc)
    end.

source() ->
    "-module(snippet).\n"
    "-export([build/1, match/1, guard/1, access/1, update/2, info/0, test/1, native/1, calls/1, funs/0]).\n"
    "-import(lists, [reverse/1]).\n"
    "-import_record(other, [ext]).\n"
    "-record(r, {a = 1 :: integer(), b, c = [x]}).\n"
    "-record(s, {x, y = #r{}}).\n"
    "-record #n{p = 0, q}.\n"
    "build(X) -> {#r{}, #r{b = X}, #r{_ = z}, #r{a = X, _ = w}, #s{x = #r{c = X}}}.\n"
    "match(#r{a = 1, b = B}) -> B;\n"
    "match(#s{y = #r{c = C}} = S) -> {C, S};\n"
    "match(#r{_ = '_'}) -> any.\n"
    "guard(X) when is_record(X, r) -> r;\n"
    "guard(X) when X#r.a > 1, record(X, r) -> big;\n"
    "guard(X) when is_record(X, s) andalso (X#s.x =:= 1 orelse X#s.y =:= 2) -> s;\n"
    "guard({X, Y}) when is_record(X, r), is_record(Y, s); is_record(X, s) -> pair;\n"
    "guard(X) when not (X#r.b =:= 3) -> not_three;\n"
    "guard(_) -> other.\n"
    "access(X) -> {X#r.a, (element(1, X))#r.b, #r.c, X#s.y#r.a}.\n"
    "update(X, V) -> {X#r{a = V}, X#r{b = V + 1, c = [V]}, X#r{}, (id(X))#s{x = 1}}.\n"
    "info() -> {record_info(size, r), record_info(fields, s)}.\n"
    "test(X) -> {is_record(X, r), is_record(id(X), s), erlang:is_record(X, r), is_record(X, r, 0)}.\n"
    "native(X) -> {#n{q = 1}, X#n.p, X#n{q = 2}, #ext{f = 1}, X#ext.f, is_record(X, n),\n"
    "              case X of #n{p = P} -> P; #ext{} -> e end, X#_.p, X#_{p = 1}}.\n"
    "calls(X) -> {reverse(X), length(X), id(X), [Y || Y <- X, is_record(Y, r)], [Z || Z <- X, Z#r.a > 1]}.\n"
    "funs() -> {fun reverse/1, fun length/1, fun id/1, fun module_info/0, fun(R) when R#r.a =:= 1 -> R end}.\n"
    "id(X) -> X.\n".
