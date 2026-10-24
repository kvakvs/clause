%% erl_error over synthetic exceptions (plan 21 step LIB34): the shell's explanation of each reason, stack traces with
%% arities, arguments, operators and locations, error_info modules, the trim, column, format fun and characters
%% limit options, and the internal formatters.
-module(erl_error_formats).
-export([main/1, format_error/2, explained/2]).

main(_) ->
    [show(Label, C, R, S) || {Label, C, R, S} <- exceptions()],
    options(),
    internal(),
    io:format("erl_error_formats: ok~n").

show(Label, Class, Reason, Stack) ->
    io:format("~s:~n~ts~n", [Label, erl_error:format_exception(Class, Reason, Stack)]).

frame(M, F, A) -> {M, F, A, []}.

located(M, F, A, Line) -> {M, F, A, [{file, "src/m.erl"}, {line, Line}]}.

exceptions() ->
    Big = lists:seq(1, 40),
    Deep = [{key, lists:seq(1, 12), "text"} || _ <- lists:seq(1, 4)],
    Stack = [located(m, f, 2, 10), frame(m, g, 0), located(lists, map, 2, 20)],
    [
        {"badarg", error, badarg, Stack},
        {"badarg value", error, {badarg, x}, Stack},
        {"badkey", error, {badkey, k}, []},
        {"badmap", error, {badmap, [1]}, []},
        {"badarith", error, badarith, [frame(erlang, '+', [1, a]), frame(m, f, 1)]},
        {"badarity", error, {badarity, {fun lists:reverse/1, [1, 2]}}, []},
        {"badarity none", error, {badarity, {fun lists:reverse/1, []}}, []},
        {"badarity one", error, {badarity, {fun lists:reverse/2, [1]}}, []},
        {"badarity many", error, {badarity, {fun lists:reverse/1, [1, 2, 3, 4]}}, []},
        {"badarity other", error, {badarity, not_a_fun}, []},
        {"badfield", error, {badfield, {{m, r}, f}}, []},
        {"badfun", error, {badfun, 3}, []},
        {"badmatch", error, {badmatch, {a, b}}, Stack},
        {"badmatch big", error, {badmatch, Deep}, Stack},
        {"badmatch big column", error, {badmatch, Big}, []},
        {"case_clause", error, {case_clause, x}, []},
        {"else_clause", error, {else_clause, x}, []},
        {"try_clause", error, {try_clause, x}, []},
        {"if_clause", error, if_clause, []},
        {"function_clause", error, function_clause, [
            {m, f, [1, a], [{file, "m.erl"}, {line, 3}]}, frame(m, g, 1)
        ]},
        {"function_clause long", error, function_clause, [{m, f, [Deep, Big], []}]},
        {"function_clause string", error, function_clause, [{m, f, "abc", []}]},
        {"function_clause op", error, function_clause, [{erlang, '+', [1, a], []}]},
        {"function_clause op1", error, function_clause, [{erlang, '-', [a], []}]},
        {"function_clause arity", error, function_clause, [frame(m, f, 2)]},
        {"function_clause shell", error, function_clause, [{shell, cmd, 2, []}]},
        {"noproc", error, noproc, []},
        {"notalive", error, notalive, []},
        {"system_limit", error, system_limit, []},
        {"timeout_value", error, timeout_value, []},
        {"undef", error, undef, [frame(nomod, nofun, 2), frame(m, f, 0)]},
        {"undef args", error, undef, [{nomod, nofun, [1, 2, 3], []}]},
        {"undef bif", error, undef, [frame(erlang, length, 1)]},
        {"shell_undef", error, {shell_undef, foo, 2, []}, []},
        {"argument_limit", error, {argument_limit, x}, []},
        {"bad_filter", error, {bad_filter, 3}, []},
        {"bad_generator", error, {bad_generator, a}, []},
        {"bad_generators", error, {bad_generators, {[1], []}}, []},
        {"unbound", error, {unbound, 'X'}, []},
        {"novalue", error, {novalue, {{m, r}, f}}, []},
        {"other error", error, {my, reason}, [frame(m, f, 1)]},
        {"not a stack", error, oops, not_a_stack},
        {"bad stack", error, oops, [x]},
        {"exit", exit, normal, []},
        {"exit badarg", exit, badarg, []},
        {"exit tuple", exit, {badmatch, 1}, []},
        {"restricted bad return", exit, {restricted_shell_bad_return, x}, []},
        {"restricted disallowed", exit, {restricted_shell_disallowed, {{m, f}, [1, 2]}}, []},
        {"restricted disallowed op", exit, {restricted_shell_disallowed, {{erlang, '!'}, [p, m]}},
            []},
        {"restricted started", exit, restricted_shell_started, []},
        {"restricted stopped", exit, restricted_shell_stopped, []},
        {"calling_self", exit, calling_self, []},
        {"throw", throw, ball, Stack},
        {"throw badarg", throw, badarg, []},
        {"operator frame", error, badarith, [frame(erlang, '/', 2), frame(erlang, '+', [1, x])]},
        {"bif frame", error, badarg, [{erlang, atom_to_list, ["text"], []}]},
        {"error_info", error, badarg, [
            {?MODULE, call, [1, 2], [{error_info, #{module => ?MODULE}}]}, frame(m, f, 1)
        ]},
        {"error_info function", error, badarg, [
            {?MODULE, call, 3, [{error_info, #{function => explained, cause => two}}]}
        ]},
        {"error_info failing", error, badarg, [
            {?MODULE, call, [x], [{error_info, #{function => nope}}]}
        ]},
        {"error_info reason", error, {custom, 1}, [
            {?MODULE, call, [x], [{error_info, #{cause => reason}}]}
        ]}
    ].

%% error_info callbacks: argument errors (one with two lines), general words, a reason of their own.
format_error(_Reason, [{_, _, _, Info} | _]) ->
    case proplists:get_value(error_info, Info) of
        #{cause := reason} ->
            #{reason => "custom reason text"};
        _ ->
            #{
                1 => "first is bad",
                2 => "second line one\nsecond line two",
                general => "general words"
            }
    end.

explained(_Reason, _Stack) -> #{2 => "the second", 3 => "the third"}.

options() ->
    Stack = [
        located(m, f, 2, 10), frame(m, g, 0), frame(erl_eval, exprs, 2), frame(shell, eval, 3)
    ],
    Trim = fun(M, _, _) -> M =:= erl_eval orelse M =:= shell end,
    io:format("trim:~n~ts~n", [
        erl_error:format_exception(error, badarg, Stack, #{stack_trim_fun => Trim})
    ]),
    io:format("column:~n~ts~n", [
        erl_error:format_exception(error, {badmatch, lists:seq(1, 30)}, Stack, #{column => 5})
    ]),
    Print = fun(Term, _I) -> io_lib:write(Term) end,
    io:format("format fun:~n~ts~n", [
        erl_error:format_exception(error, {badmatch, x}, [{m, f, [1, 2], []}], #{
            format_fun => Print
        })
    ]),
    Limited = fun(Term, I, Limit) -> {io_lib:format("~w@~w/~w", [Term, I, Limit]), Limit - 10} end,
    Many = [frame(m, f, N) || N <- lists:seq(1, 8)],
    [
        io:format("limit ~w:~n~ts~n", [
            L,
            erl_error:format_exception(
                1, error, {my, reason}, Many, fun(_, _, _) -> false end, Limited, unicode, L
            )
        ])
     || L <- [-1, 0, 30, 90, 300]
    ],
    io:format("latin1:~n~s~n", [
        erl_error:format_exception(
            3, error, badarg, [frame(m, f, 1)], fun(_, _, _) -> false end, fun(T, _) ->
                io_lib:format("~p", [T])
            end
        )
    ]),
    io:format("encoding:~n~ts~n", [
        erl_error:format_exception(
            1,
            throw,
            x,
            [frame('quoted m', f, 1)],
            fun(_, _, _) -> false end,
            fun(T, _) ->
                io_lib:format("~p", [T])
            end,
            unicode
        )
    ]).

internal() ->
    Print = fun(Term, I) -> io_lib:print(Term, I, 80, 30) end,
    Never = fun(_, _, _) -> false end,
    io:format("stacktrace:~n~ts~n", [
        erl_error:format_stacktrace(3, [frame(m, f, 1), {m, g, [x], []}], Never, Print)
    ]),
    io:format("stacktrace unicode:~n~ts~n", [
        erl_error:format_stacktrace(1, [frame(m, f, 1)], Never, Print, unicode)
    ]),
    io:format("call:~n~ts~n", [erl_error:format_call(4, {m, f}, [1, "two", {3}], Print)]),
    io:format("call op:~n~ts~n", [
        erl_error:format_call(1, {erlang, '*'}, [lists:seq(1, 30), 2], Print, unicode)
    ]),
    io:format("call fun:~n~ts~n", [erl_error:format_call(1, fun lists:sort/1, [[3, 1]], Print)]),
    io:format("call name:~n~ts~n", [erl_error:format_call(1, 'local name', [a], Print)]),
    io:format("fun: ~ts ~ts~n", [
        erl_error:format_fun(fun lists:sort/1), erl_error:format_fun(fun erlang:abs/1, unicode)
    ]),
    {value, Interpreted, _} = erl_eval:exprs(
        element(2, erl_parse:parse_exprs(element(2, erl_scan:string("fun(A, B) -> A end.")))), []
    ),
    io:format("interpreted: ~ts~n", [erl_error:format_fun(Interpreted)]),
    io:format("internal 6:~n~s~n", [
        erl_error:format_exception(2, exit, x, [frame(m, f, 0)], Never, Print)
    ]).
