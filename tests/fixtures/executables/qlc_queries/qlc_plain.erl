-module(qlc_plain).
-moduledoc """
qlc without compiled queries (plan 21 step LIB73): lists, tables, append, sort and keysort handles,
cursors, string_to_handle/1,2,3, qlc:q/1,2 in erl_eval, options and errors, and the printing helpers
of qlc_queries. References, pids and ports print as x; info strings with table identifiers have their
white space collapsed, since a reference's length decides where erl_pp breaks lines.
""".
-export([run/0, t/2, flat/1]).

run() ->
    sorts(),
    tables(),
    cursors(),
    strings(),
    evaluator(),
    errors(),
    helpers().

sorts() ->
    L = [{c, 3}, {a, 1}, {b, 2}, {a, 1}, {d, 0}],
    t(sort_default, fun() -> qlc:e(qlc:sort(L)) end),
    t(sort_unique, fun() -> qlc:e(qlc:sort(L, [unique])) end),
    t(sort_desc_unique, fun() -> qlc:e(qlc:sort(L, [{order, descending}, {unique, true}])) end),
    t(sort_fun, fun() -> qlc:e(qlc:sort(L, {order, fun({_, A}, {_, B}) -> A >= B end})) end),
    t(keysort_2, fun() -> qlc:e(qlc:keysort(2, L)) end),
    t(keysort_list, fun() -> qlc:e(qlc:keysort([2, 1], L, {order, descending})) end),
    t(keysort_unique, fun() -> qlc:e(qlc:keysort(1, L, {unique, true})) end),
    Gen = qlc:table(fun() -> gen(1, 400) end, []),
    t(sort_file, fun() -> lists:sublist(qlc:e(qlc:sort(Gen, [{size, 200}, {order, descending}])), 5) end),
    t(sort_file_unique, fun() -> length(qlc:e(qlc:sort(qlc:append(Gen, Gen), [{size, 200}, unique]))) end),
    Tuples = qlc:table(fun() -> gen_tuples(400) end, []),
    t(keysort_file, fun() -> lists:sublist(qlc:e(qlc:keysort(2, Tuples, [{size, 100}])), 3) end),
    t(keysort_compressed, fun() -> length(qlc:e(qlc:keysort(1, Tuples, [{size, 100}, {compressed, true}]))) end),
    t(sort_not_allowed, fun() -> qlc:e(qlc:sort(Gen, [{size, 200}]), {tmpdir_usage, not_allowed}) end),
    t(sort_info, fun() -> qlc:info(qlc:sort(qlc:keysort(1, L, [{tmpdir, "x"}]), [{size, 9}])) end),
    t(sort_sorted, fun() -> qlc:info(qlc:sort(qlc:sort(L))) end),
    t(sort_tmpdir_global, fun() -> qlc:info(qlc:sort(Gen), {tmpdir, "dir"}) end),
    t(append, fun() -> qlc:e(qlc:append([L, [x, y], [], Gen])) end),
    t(append_info, fun() -> qlc:info(qlc:append([[1, 2], qlc:append([], [3]), [4, 5]])) end),
    t(append_unique, fun() -> qlc:e(qlc:append([1, 2], [2, 3]), unique_all) end),
    t(fold, fun() -> qlc:fold(fun(X, A) -> X + A end, 0, Gen) end),
    t(unique_all, fun() -> qlc:e([1, 1, 2, 1], unique_all) end).

gen(N, Max) when N > Max -> [];
gen(N, Max) -> [N | fun() -> gen(N + 1, Max) end].

gen_tuples(0) -> [];
gen_tuples(N) -> [{N, -N} | fun() -> gen_tuples(N - 1) end].

tables() ->
    E = ets:new(e, [bag]),
    ets:insert(E, [{1, a}, {1, b}, {2, c}, {3, d}]),
    t(ets_select, fun() -> lists:sort(qlc:e(ets:table(E))) end),
    t(ets_first_next, fun() -> lists:sort(qlc:e(ets:table(E, {traverse, first_next}))) end),
    t(ets_select_ms, fun() -> lists:sort(qlc:e(ets:table(E, [{traverse, {select, [{{'$1', '_'}, [], ['$1']}]}}]))) end),
    t(ets_info, fun() -> flat(qlc:info(ets:table(E, [{n_objects, 2}]))) end),
    t(ets_info_select, fun() -> flat(qlc:info(ets:table(E, [{traverse, {select, [{'_', [], [true]}]}}]))) end),
    t(ets_bad_option, fun() -> ets:table(E, [{traverse, bad}]) end),
    t(ets_bad_n_objects, fun() -> ets:table(E, {n_objects, 0}) end),
    t(ets_no_table, fun() -> ets:table(no_such_table) end),
    O = ets:new(o, [ordered_set]),
    ets:insert(O, [{1.0, float}, {2, int}, {3, three}]),
    t(ets_last_prev, fun() -> qlc:e(ets:table(O, {traverse, last_prev})) end),
    File = "qlc_plain.dets",
    _ = file:delete(File),
    {ok, D} = dets:open_file(qlc_plain_dets, [{file, File}]),
    ok = dets:insert(D, [{N, N * N} || N <- lists:seq(1, 6)]),
    t(dets_select, fun() -> lists:sort(qlc:e(dets:table(D))) end),
    t(dets_first_next, fun() -> lists:sort(qlc:e(dets:table(D, {traverse, first_next}))) end),
    t(dets_select_ms, fun() -> lists:sort(qlc:e(dets:table(D, {traverse, {select, [{{'$1', '_'}, [], ['$1']}]}}))) end),
    t(dets_info, fun() -> qlc:info(dets:table(D, [{n_objects, 3}])) end),
    t(dets_bad_option, fun() -> dets:table(D, [{traverse, last_prev}]) end),
    ok = dets:close(D),
    _ = file:delete(File),
    Log = ets:new(log, [public, bag]),
    Logged = qlc:table(
        fun(MS) ->
            ets:insert(Log, {ms, MS}),
            [{1, one}]
        end,
        [
            {pre_fun, fun(Args) -> ets:insert(Log, {pre, [K || {K, _} <- Args]}) end},
            {post_fun, fun() -> ets:insert(Log, {post}) end}
        ]
    ),
    t(table_ms_fun, fun() -> qlc:e(Logged) end),
    t(table_log, fun() -> lists:sort(ets:tab2list(Log)) end),
    t(table_default_info, fun() -> qlc:info(qlc:table(fun() -> [] end, [])) end),
    t(table_string_info, fun() ->
        qlc:info(qlc:table(fun() -> [] end, [{format_fun, fun(_) -> "my_table(1, x)" end}]))
    end),
    t(table_mfa_info, fun() -> qlc:info(qlc:table(fun() -> [] end, [{format_fun, fun(_) -> {m, f, [1, "a"]} end}])) end),
    t(table_abstract_info, fun() ->
        qlc:info(qlc:table(fun() -> [] end, [{format_fun, fun(all) -> {call, 1, {atom, 1, f}, []} end}]))
    end),
    t(table_continuation, fun() -> qlc:e(qlc:table(fun() -> [1, 2 | fun() -> [3] end] end, [])) end),
    t(table_error, fun() -> qlc:e(qlc:table(fun() -> [1 | fun() -> {error, broken} end] end, [])) end).

cursors() ->
    C = qlc:cursor([X * 2 || X <- lists:seq(1, 25)], [{spawn_options, []}]),
    t(next1, fun() -> qlc:next_answers(C) end),
    t(next2, fun() -> qlc:next_answers(C, 3) end),
    t(rest, fun() -> qlc:next_answers(C, all_remaining) end),
    t(after_end, fun() -> qlc:next_answers(C, 1) end),
    t(bad_n, fun() -> qlc:next_answers(C, 0) end),
    Self = self(),
    spawn(fun() ->
        Self !
            {other,
                try
                    qlc:next_answers(C)
                catch
                    error:Reason -> Reason
                end}
    end),
    receive
        {other, Reason} -> t(not_owner, fun() -> Reason end)
    end,
    t(delete, fun() -> qlc:delete_cursor(C) end),
    t(deleted, fun() -> qlc:next_answers(C) end),
    C2 = qlc:cursor(qlc:table(fun() -> [1 | fun() -> {error, broken} end] end, [])),
    t(cursor_error, fun() -> qlc:next_answers(C2, 5) end),
    t(cursor_error_again, fun() -> qlc:next_answers(C2, 5) end),
    C3 = qlc:cursor(qlc:table(fun() -> [1 | fun() -> erlang:error(boom) end] end, [])),
    t(cursor_raise, fun() -> qlc:next_answers(C3, 5) end),
    Parent = fun() -> {parent, self() =:= Self} end,
    Pre = fun(Args) ->
        Self ! {pre, proplists:get_value(parent_value, Args), is_function(proplists:get_value(stop_fun, Args))}
    end,
    PT = qlc:table(fun() -> [p] end, [{parent_fun, Parent}, {pre_fun, Pre}]),
    C4 = qlc:cursor(PT),
    t(parent_answers, fun() -> qlc:next_answers(C4) end),
    receive
        {pre, PV, Stop} -> t(parent_value, fun() -> {PV, Stop} end)
    end,
    ok = qlc:delete_cursor(C4),
    _ = qlc:e(PT),
    receive
        {pre, PV2, Stop2} -> t(parent_value_eval, fun() -> {PV2, Stop2} end)
    end,
    t(cursor_bad_handle, fun() -> qlc:cursor(foo) end),
    t(cursor_bad_spawn, fun() -> qlc:cursor([], {spawn_options, [a | b]}) end).

strings() ->
    Bs = erl_eval:add_binding('L', [{1, a}, {2, b}, {3, c}], erl_eval:new_bindings()),
    t(string, fun() -> qlc:e(qlc:string_to_handle("[X + 1 || {X, _} <- L].", [], Bs)) end),
    t(string_filter, fun() -> qlc:e(qlc:string_to_handle("[Y || {X, Y} <- L, X > 1].", [], Bs)) end),
    S = qlc:string_to_handle("[{X, Y} || X <- L, Y <- qlc:q([Z || Z <- L, Z > X])].", [], [{'L', [1, 2, 3]}]),
    t(string_nested, fun() -> qlc:e(S) end),
    t(string_info, fun() -> qlc:info(S) end),
    t(string_join, fun() -> qlc:e(qlc:string_to_handle("[{X, Z} || {X, Y} <- L, {Y2, Z} <- L, Y =:= Y2].", [], Bs)) end),
    t(string_options, fun() -> qlc:e(qlc:string_to_handle("[X || X <- [1, 1, 2]].", [unique])) end),
    t(string_error, fun() -> qlc:string_to_handle("[X || X <- ].") end),
    t(string_scan_error, fun() -> qlc:string_to_handle("[X || X <- [1], $") end),
    t(string_not_qlc, fun() -> qlc:string_to_handle("foo.") end),
    t(string_two, fun() -> qlc:string_to_handle("[X || X <- [1]]. [Y || Y <- [2]].") end),
    t(string_badarg, fun() -> qlc:string_to_handle(foo) end),
    t(string_bad_option, fun() -> qlc:string_to_handle("[X || X <- [1]].", [bad]) end).

evaluator() ->
    t(eval_q, fun() -> ev("qlc:e(qlc:q([X * 2 || X <- [1, 2, 3], X > 1])).") end),
    t(eval_join, fun() ->
        ev("qlc:e(qlc:q([{X, Y} || {X, K} <- [{1, a}, {2, b}], {K2, Y} <- [{a, p}, {b, q}], K =:= K2])).")
    end),
    t(eval_local, fun() -> ev("F = fun(X) -> X + 1 end, qlc:e(qlc:q([F(X) || X <- [1, 2]])).") end),
    t(eval_options, fun() -> ev("qlc:e(qlc:q([X || X <- [1, 1, 2]], unique)).") end),
    t(eval_error, fun() -> ev("qlc:q([X || X <- [1, 2], Y <- X]).") end),
    t(eval_old, fun() -> ev("qlc:e(''.qlc:q([X || X <- [3]])).") end),
    t(eval_binary_generator, fun() -> ev("qlc:q([X || <<X>> <= <<1, 2>>]).") end),
    t(eval_not_lc, fun() -> ev("qlc:q(foo).") end).

ev(S) ->
    {ok, T, _} = erl_scan:string(S),
    {ok, Es} = erl_parse:parse_exprs(T),
    {value, V, _} = erl_eval:exprs(Es, []),
    V.

errors() ->
    Self = self(),
    T1 = qlc:table(fun() -> [1 | fun() -> {error, stopped} end] end, [{post_fun, fun() -> Self ! post1 end}]),
    t(traverse_error, fun() -> qlc:e(T1) end),
    t(post_called, fun() ->
        receive
            post1 -> yes
        after 100 -> no
        end
    end),
    t(fold_error, fun() -> qlc:fold(fun(X, A) -> [X | A] end, [], T1) end),
    t(post_called_fold, fun() ->
        receive
            post1 -> yes
        after 100 -> no
        end
    end),
    T2 = qlc:table(fun() -> [1] end, [
        {pre_fun, fun(_) -> erlang:error(pre_fails) end}, {post_fun, fun() -> Self ! post2 end}
    ]),
    t(pre_error, fun() -> qlc:e(T2) end),
    t(post2_called, fun() ->
        receive
            post2 -> yes
        after 100 -> no
        end
    end),
    t(table_badarg, fun() -> qlc:table(fun(_, _) -> [] end, []) end),
    t(table_not_fun, fun() -> qlc:table(foo, []) end),
    t(table_bad_option, fun() -> qlc:table(fun() -> [] end, [{info_fun, fun() -> x end}]) end),
    t(table_unknown_option, fun() -> qlc:table(fun() -> [] end, [{size, 1}]) end),
    t(q_badarg, fun() -> (qlc_module()):q(foo) end),
    t(append_badarg, fun() -> qlc:append([[1], foo]) end),
    t(append2_badarg, fun() -> qlc:append(foo, [1]) end),
    t(e_badarg, fun() -> qlc:e(foo) end),
    t(e_bad_option, fun() -> qlc:e([], [bad]) end),
    t(e_dup_option, fun() -> qlc:e([1], [unique_all, unique_all]) end),
    t(e_bad_cache, fun() -> qlc:e([1], {cache_all, 'maybe'}) end),
    t(e_options, fun() -> qlc:e([1, 2], [{cache_all, ets}, {unique_all, false}, {max_list_size, 3}, {tmpdir, ""}]) end),
    t(info_badarg, fun() -> qlc:info([1], {format, xml}) end),
    t(info_bad_depth, fun() -> qlc:info([1], {depth, -1}) end),
    t(sort_bad, fun() -> qlc:sort([], {order, sideways}) end),
    t(keysort_bad, fun() -> qlc:keysort(0, []) end),
    t(keysort_bad_list, fun() -> qlc:keysort([1, a], []) end),
    t(keysort_empty_list, fun() -> qlc:keysort([], []) end),
    t(delete_badarg, fun() -> qlc:delete_cursor(foo) end),
    t(next_badarg, fun() -> qlc:next_answers(foo, 1) end),
    t(fold_badarg, fun() -> qlc:fold(fun(_, A) -> A end, 0, foo) end),
    t(unsupported, fun() -> qlc:e({qlc_handle, foo}) end),
    t(format_error, fun() -> [lists:flatten(qlc:format_error(E)) || E <- format_errors()] end).

format_errors() ->
    [
        not_a_query_list_comprehension,
        {used_generator_variable, 'X'},
        binary_generator,
        too_complex_join,
        too_many_joins,
        nomatch_pattern,
        nomatch_filter,
        {1, erl_parse, ["syntax error before: ", "x"]},
        {{2, 3}, qlc, nomatch_filter},
        {bad_object, "f"},
        bad_object,
        {file_error, "f", enoent},
        {premature_eof, "f"},
        {tmpdir_usage, sorting},
        {error, qlc, too_many_joins},
        other
    ].

helpers() ->
    t(template_state, fun() -> qlc:template_state() end),
    t(aux_name, fun() -> qlc:aux_name('V', 1, gb_sets:from_list(['V1', 'V2'])) end),
    t(name_suffix, fun() -> qlc:name_suffix('P', 7) end),
    t(vars, fun() -> qlc:vars({op, 1, '+', {var, 1, 'X'}, {tuple, 1, [{var, 1, '_'}, {var, 1, 'A'}]}}) end),
    t(var_fold, fun() -> qlc:var_fold(fun({var, _, V}) -> V end, [], [{var, 1, 'B'}, {var, 2, 'A'}, {var, 3, 'B'}]) end),
    t(all_selections, fun() -> qlc:all_selections([{1, [a, b]}, {2, [c, d]}]) end),
    t(parse_transform_info, fun() -> qlc:parse_transform_info() end).

%% Created for qlc: a call through a runtime name, which qlc's transform does not check.
qlc_module() -> qlc.

%% Print a test's answer, or the class and reason of its exception.
t(Tag, F) ->
    R =
        try F() of
            V -> V
        catch
            C:Reason -> {C, Reason}
        end,
    io:format("~p: ~tp~n", [Tag, hide(R)]).

hide(S) when is_list(S) ->
    case io_lib:printable_unicode_list(S) of
        true -> re:replace(S, "#Ref<[0-9.]+>", "x", [global, {return, list}, unicode]);
        false -> [hide(E) || E <- S]
    end;
hide(T) when is_tuple(T) -> list_to_tuple(hide(tuple_to_list(T)));
hide(R) when is_reference(R); is_pid(R); is_port(R) -> x;
hide(X) ->
    X.

%% An info string with its white space collapsed.
flat(S) ->
    re:replace(S, "\\s+", " ", [global, {return, list}, unicode]).
