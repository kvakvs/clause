-module(compiler_tables).
-export([main/1]).

%% erl_internal and erl_anno (plan 21 step LIB8): the BIF, guard, type test and operator tables, predefined
%% functions, and annotations with their locations, texts and flags, including the badarg and clause errors.
main(_) ->
    bifs(),
    operators(),
    predefined(),
    annotations(),
    anno_updates(),
    anno_errors(),
    io:format("compiler_tables: ok~n").

%% Every erlang export, one name that is not one (bitsize/1), and names of other modules.
bifs() ->
    Names = lists:sort(erlang:module_info(exports)) ++ [{bitsize, 1}, {no_such_bif, 0}, {size, 2}],
    show(bif, [F || {F, A} <- Names, erl_internal:bif(F, A)]),
    show(guard_bif, [{F, A} || {F, A} <- Names, erl_internal:guard_bif(F, A)]),
    show(type_test, [
        {F, A}
     || {F, A} <- Names ++ [{integer, 1}, {record, 2}, {function, 1}, {port, 1}],
        erl_internal:type_test(F, A)
    ]),
    show(bif3, [
        erl_internal:bif(erlang, abs, 1),
        erl_internal:bif(erlang, nope, 1),
        erl_internal:bif(lists, reverse, 1),
        erl_internal:bif(erlang, bitsize, 1)
    ]),
    show(table_errors, [
        catching(fun() -> erl_internal:bif(id("abs"), 1) end),
        catching(fun() -> erl_internal:bif(abs, id(one)) end),
        catching(fun() -> erl_internal:bif(abs, id(1.0)) end),
        catching(fun() -> erl_internal:guard_bif(id("abs"), 1) end),
        catching(fun() -> erl_internal:type_test(id(1), 1) end),
        catching(fun() -> erl_internal:bif(id(1), abs, 1) end),
        catching(fun() -> erl_internal:bif(erlang, id(1), 1) end),
        catching(fun() -> erl_internal:bif(lists, abs, id(x)) end),
        erl_internal:bif(abs, id(-1)),
        erl_internal:guard_bif(abs, id(-1))
    ]).

operators() ->
    Ops = [
        '+',
        '-',
        '*',
        '/',
        'bnot',
        'div',
        'rem',
        'band',
        'bor',
        'bxor',
        'bsl',
        'bsr',
        'not',
        'and',
        'or',
        'xor',
        '==',
        '/=',
        '=<',
        '<',
        '>=',
        '>',
        '=:=',
        '=/=',
        '++',
        '--',
        '!',
        'andalso',
        '=',
        '<=',
        '|'
    ],
    Arities = [0, 1, 2, 3],
    Pairs = [{Op, A} || Op <- Ops, A <- Arities],
    show(arith_op, [P || {Op, A} = P <- Pairs, erl_internal:arith_op(Op, A)]),
    show(bool_op, [P || {Op, A} = P <- Pairs, erl_internal:bool_op(Op, A)]),
    show(comp_op, [P || {Op, A} = P <- Pairs, erl_internal:comp_op(Op, A)]),
    show(list_op, [P || {Op, A} = P <- Pairs, erl_internal:list_op(Op, A)]),
    show(send_op, [P || {Op, A} = P <- Pairs, erl_internal:send_op(Op, A)]),
    show(op_type, [{P, catching(fun() -> erl_internal:op_type(Op, A) end)} || {Op, A} = P <- Pairs]),
    show(op_errors, [
        catching(fun() -> erl_internal:arith_op(id("+"), 2) end),
        catching(fun() -> erl_internal:bool_op('not', id(x)) end),
        catching(fun() -> erl_internal:comp_op(id(1), 2) end),
        catching(fun() -> erl_internal:list_op('++', id(2.0)) end),
        catching(fun() -> erl_internal:send_op(id({}), 2) end),
        catching(fun() -> erl_internal:op_type(id("+"), 2) end)
    ]).

predefined() ->
    Module = [{attribute, 1, module, m}, {function, 2, f, 0, []}],
    Behaviour = [
        {attribute, 1, module, b},
        {attribute, 2, callback, {{init, 1}, []}},
        {attribute, 3, callback, {{handle, 2}, []}},
        {attribute, 4, callback, {bad, []}},
        {attribute, 5, optional_callbacks, [{handle, 2}]},
        {attribute, 6, optional_callbacks, not_a_list},
        {attribute, 7, optional_callbacks, [{x, -1}]},
        {attribute, 8, optional_callbacks, [{y, 0}, {z, 1}]},
        {eof, 9}
    ],
    show(predefined, [
        erl_internal:add_predefined_functions(Module),
        erl_internal:add_predefined_functions(Behaviour),
        erl_internal:add_predefined_functions([
            {attribute, 1, module, m}, {attribute, 2, callback, {{f, 0}, []}}
        ]),
        erl_internal:add_predefined_functions([
            {attribute, 1, module, m}, not_a_form, {attribute, 1}
        ]),
        catching(fun() -> erl_internal:add_predefined_functions([{function, 1, f, 0, []}]) end),
        catching(fun() ->
            erl_internal:add_predefined_functions([{attribute, 1, module, m} | tail])
        end),
        catching(fun() -> erl_internal:add_predefined_functions(id(not_a_list)) end),
        catching(fun() -> erl_internal:add_predefined_functions([{attribute, 1, callback, x}]) end)
    ]).

annotations() ->
    Annos = [
        0,
        7,
        {3, 4},
        {0, 1},
        [{location, 5}],
        [{location, {5, 2}}, {text, "ab\ncd"}],
        [{location, 5}, {text, "x\n\ny"}],
        [{location, {2, 3}}, {text, "tail"}],
        [{location, 1}, {end_location, {9, 9}}, {text, "ignored"}],
        [{location, 1}, {file, "f.erl"}, {generated, true}, {record, true}],
        [{generated, true}],
        [{file, <<"bin.erl">>}, {location, {4, 1}}],
        [{location, 1}, {other, x}],
        [],
        -1,
        {1, 0},
        {a, 1},
        atom
    ],
    show(annotations, [
        [
            catching(fun() -> F(A) end)
         || F <- [
                fun erl_anno:is_anno/1,
                fun erl_anno:column/1,
                fun erl_anno:end_location/1,
                fun erl_anno:file/1,
                fun erl_anno:generated/1,
                fun erl_anno:line/1,
                fun erl_anno:location/1,
                fun erl_anno:text/1,
                fun erl_anno:to_term/1
            ]
        ]
     || A <- Annos
    ]),
    show(new, [
        catching(fun() -> erl_anno:new(L) end)
     || L <- [0, 12, {3, 1}, {3, 9}, -1, {3, 0}, {-1, 1}, x, [{location, 1}], 1.0]
    ]),
    show(from_term, [
        erl_anno:from_term(5),
        erl_anno:from_term(-5),
        erl_anno:from_term({2, 3}),
        erl_anno:from_term([{location, 1}]),
        erl_anno:from_term(anything),
        catching(fun() -> erl_anno:from_term(id({-1, 2})) end)
    ]).

anno_updates() ->
    show(updates, [
        erl_anno:set_file("a.erl", 3),
        erl_anno:set_file(<<"a.erl">>, {3, 4}),
        erl_anno:set_file("b.erl", [{location, 1}, {file, "a.erl"}]),
        erl_anno:set_generated(true, 3),
        erl_anno:set_generated(false, 3),
        erl_anno:set_generated(false, [{location, 3}, {generated, true}]),
        erl_anno:set_generated(false, [{generated, true}, {location, {3, 1}}, {file, "x"}]),
        erl_anno:set_record(true, {1, 1}),
        erl_anno:set_record(false, [{location, 1}, {record, true}]),
        erl_anno:set_text("abc", 1),
        erl_anno:set_text("new", [{location, 1}, {text, "old"}]),
        erl_anno:set_line(9, 1),
        erl_anno:set_line(9, {1, 5}),
        erl_anno:set_line(9, [{location, {1, 5}}, {text, "t"}]),
        erl_anno:set_line(9, [{location, 1}, {text, "t"}]),
        erl_anno:set_location(4, 1),
        erl_anno:set_location(4, {1, 2}),
        erl_anno:set_location({4, 5}, 1),
        erl_anno:set_location({4, 5}, {1, 2}),
        erl_anno:set_location({4, 5}, [{location, 1}, {text, "t"}]),
        erl_anno:set_location(4, [{text, "t"}]),
        erl_anno:set_end_location({7, 7}, 1),
        erl_anno:set_end_location(8, [{location, 1}, {end_location, 2}]),
        erl_anno:end_location(erl_anno:set_text("ab\nc", erl_anno:new({1, 1})))
    ]).

anno_errors() ->
    show(anno_errors, [
        catching(fun() -> erl_anno:set_file(id(file), 1) end),
        catching(fun() -> erl_anno:set_generated(id(yes), 1) end),
        catching(fun() -> erl_anno:set_record(id(1), 1) end),
        catching(fun() -> erl_anno:set_text(id(<<"t">>), 1) end),
        catching(fun() -> erl_anno:set_location(id(-1), 1) end),
        catching(fun() -> erl_anno:set_location(id({1, 0}), 1) end),
        catching(fun() -> erl_anno:set_end_location(id(x), 1) end),
        catching(fun() -> erl_anno:set_file("f", id([])) end),
        catching(fun() -> erl_anno:set_file("f", id(atom)) end),
        catching(fun() -> erl_anno:set_file("f", id(-1)) end),
        catching(fun() -> erl_anno:set_line(id(x), 1) end),
        catching(fun() -> erl_anno:file(id([{location, 1} | tail])) end),
        catching(fun() -> erl_anno:file(id([{file, "f"} | tail])) end),
        catching(fun() -> erl_anno:file(id([{file, "f", 3}])) end),
        catching(fun() -> erl_anno:generated(id([x])) end),
        catching(fun() -> erl_anno:end_location(id([{location, 1}, {text, [$a | b]}])) end),
        catching(fun() -> erl_anno:end_location(id([{location, {a, b}}, {text, "\n"}])) end),
        catching(fun() -> erl_anno:set_generated(true, id([{location, 1} | tail])) end),
        catching(fun() -> erl_anno:set_generated(false, id([{location, 1} | tail])) end),
        catching(fun() -> erl_anno:set_generated(false, id([{generated, true} | tail])) end),
        catching(fun() -> erl_anno:set_file("f", id([{file, "a"} | tail])) end),
        catching(fun() -> erl_anno:set_text("t", id([{location, 1}, {text, "a", b}])) end)
    ]).

catching(Fun) ->
    try Fun() of
        Value -> Value
    catch
        Class:Reason -> {Class, Reason}
    end.

show(Label, Value) -> io:format("~s: ~w~n", [Label, Value]).

id(X) -> X.
