-module(clause_shell_eval).
-export([
    start/4,
    read/1,
    expand_history/2,
    report_exception/3,
    report_exception/4,
    help_text/0,
    format_error/2,
    pp/3,
    pp/4
]).

% The shell's reader and evaluator (plan 21 step LIB74, docs/libraries/library.md#interactive-shell): commands are read and
% turned into expressions here, then evaluated by an evaluator process with erl_eval; the shell's own commands
% (b(), f(), h(), v(N), rd/2, ...) are its local functions. shell's server keeps the history and restarts the
% evaluator after an exception.

-define(LINEMAX, 30).
-define(CHAR_MAX, 60).

%% --- Reading ---

%% Read one command: {ok, Expressions}, {error, ErrorInfo}, eof or an I/O error. Function definitions, records,
%% types and specs become the shell's fd/3, rd/1, td/1 and ft/1 commands.
read(Prompt) ->
    Options = [text, {reserved_word_fun, fun erl_scan:reserved_word/1}],
    case io:scan_erl_exprs(standard_io, Prompt, {1, 1}, Options) of
        {ok, Toks, _} ->
            parse(local_funs(Toks, []));
        {eof, _} ->
            eof;
        {error, ErrorInfo, _} ->
            skip_line(),
            {error, ErrorInfo};
        Else ->
            Else
    end.

% Skip the rest of a line that did not scan, without echoing it.
skip_line() ->
    Echo = proplists:get_value(echo, io:getopts(), false),
    _ = [io:setopts([{echo, false}]) || Echo],
    _ = io:get_line(standard_io, ''),
    _ = [io:setopts([{echo, true}]) || Echo],
    ok.

% fun F/A without a module refers to erlang's BIF or to shell_default.
local_funs([{'fun', Anno} = Fun, {atom, _, F} = Name, {'/', _} = Slash, {integer, _, A} = Arity | Rest], Acc) ->
    Module =
        case erl_internal:bif(F, A) of
            true -> erlang;
            false -> shell_default
        end,
    local_funs(Rest, [Arity, Slash, Name, {':', Anno}, {atom, Anno, Module}, Fun | Acc]);
local_funs([Tok | Rest], Acc) ->
    local_funs(Rest, [Tok | Acc]);
local_funs([], Acc) ->
    lists:reverse(Acc).

parse([{'-', _}, {atom, _, Attribute} | _] = Toks) ->
    case attribute_command(Attribute) of
        none -> erl_eval:extended_parse_exprs(Toks);
        Command -> attribute(Command, Toks)
    end;
parse([{atom, _, Name}, {'(', _} | _] = Toks) ->
    case erl_parse:parse_form(Toks) of
        {ok, Form} -> definition(Name, Form, Toks);
        _ -> erl_eval:extended_parse_exprs(Toks)
    end;
parse(Toks) ->
    erl_eval:extended_parse_exprs(Toks).

attribute_command(record) -> rd;
attribute_command(spec) -> ft;
attribute_command(type) -> td;
attribute_command(nominal) -> td;
attribute_command(_) -> none.

% An attribute typed at the prompt as Command("Attribute text").
attribute(Command, Toks) ->
    case erl_parse:parse_form(Toks) of
        {ok, Form} ->
            Text = lists:flatten(escape_quotes(lists:flatten(erl_pp:form(Form)))),
            scan_command(atom_to_list(Command) ++ "(\"" ++ Text ++ "\").\n");
        Error ->
            exit(Error)
    end.

% A function definition typed at the prompt as fd(Name, Fun, "Definition"), unless Name is a shell or BIF name.
definition(Name, Form, Toks) ->
    NameText = lists:flatten(io_lib:fwrite("~tw", [Name])),
    case {edlin_expand:shell_default_or_bif(NameText), shell:local_func(NameText)} of
        {"user_defined", false} ->
            Text = lists:flatten(escape_quotes(lists:flatten(erl_pp:form(Form)))),
            Fun = lists:flatten(erl_pp:expr(named_fun(Form, Name))),
            scan_command("fd(" ++ NameText ++ ", " ++ Fun ++ ", \"" ++ Text ++ "\").\n");
        _ ->
            erl_eval:extended_parse_exprs(Toks)
    end.

scan_command(Text) ->
    Options = [text, {reserved_word_fun, fun erl_scan:reserved_word/1}],
    {done, {ok, Toks, _}, _} = erl_scan:tokens([], Text, {1, 1}, Options),
    erl_eval:extended_parse_exprs(Toks).

escape_quotes(String) -> escape_quotes(String, []).

escape_quotes([], Acc) -> lists:reverse(Acc);
escape_quotes([$\\, $" | Rest], Acc) -> escape_quotes(Rest, [$", $\\, $\\, $\\ | Acc]);
escape_quotes([$" | Rest], Acc) -> escape_quotes(Rest, [$", $\\ | Acc]);
escape_quotes([C | Rest], Acc) -> escape_quotes(Rest, [C | Acc]).

% The function as a named fun calling itself through RecursiveFuncVar; other local calls go to shell_default (or
% erlang for BIFs) unless they are shell commands.
named_fun({function, Anno, Name, Arity, Clauses}, Name) ->
    {named_fun, Anno, 'RecursiveFuncVar', calls(Clauses, Name, Arity)}.

calls({call, Anno, {atom, NameAnno, Name}, Args}, Name, Arity) when length(Args) =:= Arity ->
    {call, Anno, {var, NameAnno, 'RecursiveFuncVar'}, calls(Args, Name, Arity)};
calls({call, Anno, {atom, NameAnno, Fun}, Args}, Name, Arity) ->
    {call, Anno, local_callee(Fun, NameAnno, Fun =:= Name), calls(Args, Name, Arity)};
calls(T, Name, Arity) when is_tuple(T) ->
    list_to_tuple(calls(tuple_to_list(T), Name, Arity));
calls([E | Es], Name, Arity) ->
    [calls(E, Name, Arity) | calls(Es, Name, Arity)];
calls(E, _, _) ->
    E.

local_callee(Fun, Anno, true) ->
    {remote, Anno, {atom, Anno, shell_default}, {atom, Anno, Fun}};
local_callee(Fun, Anno, false) ->
    case {edlin_expand:shell_default_or_bif(atom_to_list(Fun)), shell:local_func(Fun)} of
        {_, true} -> {atom, Anno, Fun};
        {"erlang", false} -> {remote, Anno, {atom, Anno, erlang}, {atom, Anno, Fun}};
        _ -> {remote, Anno, {atom, Anno, shell_default}, {atom, Anno, Fun}}
    end.

%% The text of an error a module reports. The front end's modules are named, so a program with the shell links their
%% format_error/1 even when it calls only their builtins.
format_error(erl_lint, Error) -> erl_lint:format_error(Error);
format_error(erl_parse, Error) -> erl_parse:format_error(Error);
format_error(erl_scan, Error) -> erl_scan:format_error(Error);
format_error(Module, Error) -> Module:format_error(Error).

%% --- History expansion ---

%% Expressions with e(N) replaced by command N and v(N) by v(M) of the command it names; Lookup(NumExpr) gives
%% {Expressions, Value, M} of a command, undefined for one that is gone.
expand_history(Es, Lookup) ->
    expand(Es, Lookup).

expand({call, A, {atom, _, e}, [N]}, Lookup) ->
    case Lookup(N) of
        {undefined, _, _} -> no_command(N);
        {[Ce], _, _} -> Ce;
        {Ces, _, _} when is_list(Ces) -> {block, A, Ces}
    end;
expand({call, CA, {atom, VA, v}, [N]}, Lookup) ->
    case Lookup(N) of
        {undefined, _, _} -> no_command(N);
        {Ces, _, CommandN} when is_list(Ces) -> {call, CA, {atom, VA, v}, [{integer, VA, CommandN}]}
    end;
% Patterns, guards and values stand as typed.
expand({clause, A, H, G, B}, Lookup) ->
    {clause, A, H, G, expand(B, Lookup)};
expand({match, A, Lhs, Rhs}, Lookup) ->
    {match, A, Lhs, expand(Rhs, Lookup)};
expand({Generate, A, P, E}, Lookup) when
    Generate =:= generate;
    Generate =:= generate_strict;
    Generate =:= b_generate;
    Generate =:= b_generate_strict;
    Generate =:= m_generate;
    Generate =:= m_generate_strict
->
    {Generate, A, P, expand(E, Lookup)};
expand({bin_element, A, E, Size, Types}, Lookup) ->
    {bin_element, A, expand(E, Lookup), Size, Types};
expand({'fun', A, {clauses, Cs}}, Lookup) ->
    {'fun', A, {clauses, expand(Cs, Lookup)}};
expand({'fun', _, _} = Fun, _) ->
    Fun;
expand({value, _, _} = Value, _) ->
    Value;
expand({Leaf, _, _} = E, _) when
    Leaf =:= atom; Leaf =:= integer; Leaf =:= float; Leaf =:= char; Leaf =:= string; Leaf =:= var
->
    E;
expand({record, A, Name, Fields}, Lookup) when is_atom(Name) ->
    {record, A, Name, expand(Fields, Lookup)};
expand({record_field, A, Field, Value}, Lookup) ->
    {record_field, A, expand(Field, Lookup), expand(Value, Lookup)};
expand(T, Lookup) when is_tuple(T), tuple_size(T) >= 2 ->
    [Tag, Anno | Elements] = tuple_to_list(T),
    list_to_tuple([Tag, Anno | expand(Elements, Lookup)]);
expand([E | Es], Lookup) ->
    [expand(E, Lookup) | expand(Es, Lookup)];
expand(E, _) ->
    E.

no_command(N) ->
    throw({error, io_lib:fwrite(<<"~ts: command not found">>, [erl_pp:expr(N, enc())])}).

%% --- The evaluator process ---

%% A linked evaluator with the shell's bindings, record and function tables and process dictionary.
start(Bs, RT, FT, Ds) ->
    Shell = self(),
    Ancestors = [self() | get('$ancestors')],
    Eval = spawn_link(fun() ->
        put('$ancestors', Ancestors),
        evaluator(Shell, Bs, RT, FT, Ds)
    end),
    put(evaluator, Eval),
    Eval.

evaluator(Shell, Bs, RT, FT, Ds) ->
    _ = [put(K, V) || {K, V} <- Ds],
    case application:get_env(stdlib, restricted_shell) of
        undefined ->
            eval_loop(Shell, Bs, RT, FT);
        {ok, Module} ->
            _ = [put(restricted_shell_state, []) || get(restricted_shell_state) =:= undefined],
            put(restricted_expr_state, []),
            restricted_loop(Shell, Bs, RT, FT, Module)
    end.

eval_loop(Shell, Bs0, RT, FT) ->
    receive
        {shell_cmd, Shell, {eval, Es}, W} ->
            Ef = {value, fun(MForFun, As) -> apply_fun(MForFun, As, Shell) end},
            Lf = local_handler(Shell, RT, FT, Ef),
            Bs = eval_exprs(Es, Shell, Bs0, RT, {Lf, Ef}, W),
            eval_loop(Shell, Bs, RT, FT)
    end.

restricted_loop(Shell, Bs0, RT, FT, Module) ->
    receive
        {shell_cmd, Shell, {eval, Es}, W} ->
            {Local, NonLocal} = restricted_handlers(Module, Shell, RT, FT),
            put(restricted_expr_state, []),
            Bs = eval_exprs(Es, Shell, Bs0, RT, {{eval, Local}, {value, NonLocal}}, W),
            restricted_loop(Shell, Bs, RT, FT, Module)
    end.

% Evaluate and send the value; an exception is caught (catch_exception) or ends this evaluator.
eval_exprs(Es, Shell, Bs0, RT, Handlers, W) ->
    try
        {Reply, Bs} = exprs(Es, Bs0, RT, Handlers, W),
        Shell ! {shell_rep, self(), Reply},
        Bs
    catch
        exit:normal ->
            exit(normal);
        Class:Reason:Stacktrace ->
            Report = {self(), Class, {Reason, Stacktrace}},
            case do_catch(Class, Reason) of
                true ->
                    Shell ! {ev_caught, Report},
                    Bs0;
                false ->
                    {links, Links} = process_info(self(), links),
                    Exit = nocatch(Class, {Reason, Stacktrace}),
                    lists:foreach(fun(P) -> exit(P, Exit) end, Links -- [Shell]),
                    Shell ! {ev_exit, Report},
                    exit(normal)
            end
    end.

do_catch(exit, restricted_shell_stopped) -> false;
do_catch(exit, restricted_shell_started) -> false;
do_catch(_, _) -> application:get_env(stdlib, shell_catch_exception, false) =:= true.

nocatch(throw, {Term, Stack}) -> {{nocatch, Term}, Stack};
nocatch(_, Reason) -> Reason.

% Check, expand records in and evaluate each expression; the last one's value is printed (cmd) or returned (pmt).
exprs([E0 | Es], Bs1, RT, {Lf, Ef} = Handlers, W) ->
    Used = clause_shell_records:used(E0, RT),
    case erl_eval:check_command(prep_check([E0]), clause_shell_records:bindings(Used, Bs1)) of
        ok ->
            E1 = clause_shell_records:expand(Used, E0),
            {value, V0, Bs2} = erl_eval:expr(E1, Bs1, Lf, Ef),
            Bs = orddict:from_list([VV || {X, _} = VV <- erl_eval:bindings(Bs2), not expand_variable(X)]),
            case Es of
                [] -> {{value, last_value(V0, RT, W), Bs, get()}, Bs};
                _ -> exprs(Es, Bs, RT, Handlers, W)
            end;
        {error, Error} ->
            {{command_error, Error}, Bs1}
    end.

last_value(V, _RT, pmt) ->
    {pmt, V};
last_value(V, RT, cmd) ->
    io:requests(standard_io, [{put_chars, unicode, pp(V, 1, RT)}, nl]),
    case result_saved() of
        true -> V;
        false -> ignored
    end.

result_saved() ->
    case application:get_env(stdlib, shell_saved_results, 20) of
        0 -> false;
        _ -> application:get_env(stdlib, shell_history_length, 20) =/= 0
    end.

% Variables erl_expand_records introduces (recN).
expand_variable(V) ->
    case catch atom_to_list(V) of
        "rec" ++ _ -> true;
        _ -> false
    end.

% erl_lint does not know f(V) of an unbound V or the history's {value, N, V}.
prep_check({call, Anno, {atom, _, f}, [{var, _, _}]}) -> {atom, Anno, ok};
prep_check({value, _, _}) -> {atom, erl_anno:new(0), ok};
prep_check(T) when is_tuple(T) -> list_to_tuple(prep_check(tuple_to_list(T)));
prep_check([E | Es]) -> [prep_check(E) | prep_check(Es)];
prep_check(E) -> E.

% erlang:garbage_collect() collects the shell too.
apply_fun({erlang, garbage_collect}, [], Shell) ->
    erlang:garbage_collect(Shell),
    erlang:garbage_collect();
apply_fun({M, F}, As, _Shell) ->
    apply(M, F, As);
apply_fun(MForFun, As, _Shell) ->
    apply(MForFun, As).

local_handler(Shell, RT, FT, Ef) ->
    Handler = fun(Self) ->
        fun(F, As, Bs) -> clause_shell_commands:local(F, As, Bs, {Shell, RT, FT}, {{eval, Self(Self)}, Ef}) end
    end,
    {eval, Handler(Handler)}.

% The handlers of a restricted shell: commands the shell runs itself, then the callback module's answers.
restricted_handlers(Module, Shell, RT, FT) ->
    {
        fun(F, As, Bs) -> restricted_local(F, As, Bs, Module, {Shell, RT, FT}) end,
        fun(MF, As) -> restricted_remote(MF, As, Module, Shell) end
    }.

restricted_local(F, As, Bs, Module, {Shell, RT, FT} = Tables) when is_atom(F) ->
    {Local, NonLocal} = restricted_handlers(Module, Shell, RT, FT),
    Handlers = {{eval, Local}, {value, NonLocal}},
    case clause_shell_commands:command(F, As) of
        true ->
            clause_shell_commands:local(F, As, Bs, Tables, Handlers);
        false ->
            {Values, Bs1} = erl_eval:expr_list(As, Bs, {eval, Local}, {value, NonLocal}),
            case Module:local_allowed(F, Values, restricted_state()) of
                {Result, State} ->
                    restricted_state(State),
                    restricted_result(Result, {F, Values}, Shell),
                    clause_shell_commands:non_builtin(F, Values, Bs1, FT);
                Unexpected ->
                    bad_return(Module, local_allowed, Unexpected)
            end
    end.

restricted_remote(MForFun, As, Module, Shell) ->
    case Module:non_local_allowed(MForFun, As, restricted_state()) of
        {Result, State} ->
            restricted_state(State),
            case Result of
                {redirect, NewMForFun, NewAs} ->
                    apply_fun(NewMForFun, NewAs, Shell);
                _ ->
                    restricted_result(Result, {MForFun, As}, Shell),
                    apply_fun(MForFun, As, Shell)
            end;
        Unexpected ->
            bad_return(Module, non_local_allowed, Unexpected)
    end.

restricted_state() -> {get(restricted_shell_state), get(restricted_expr_state)}.

restricted_state({ShellState, ExprState}) ->
    put(restricted_shell_state, ShellState),
    put(restricted_expr_state, ExprState).

restricted_result(false, Call, Shell) ->
    shell_request(Shell, {update_dict, get()}),
    exit({restricted_shell_disallowed, Call});
restricted_result(_, _, _) ->
    ok.

bad_return(Module, Function, Value) ->
    try
        erlang:error(reason)
    catch
        _:_:S -> erlang:raise(exit, {restricted_shell_bad_return, Value}, [{Module, Function, 3} | S])
    end.

shell_request(Shell, Request) ->
    Shell ! {shell_req, self(), Request},
    receive
        {shell_rep, Shell, Reply} -> Reply
    end.

%% --- Printing ---

%% Report an exception the evaluator raised: ** for one that ended the evaluator, * for a caught one.
report_exception(Class, Reason, RT) ->
    report_exception(Class, serious, Reason, RT).

report_exception(Class, Severity, {Reason, Stacktrace}, RT) ->
    Tag =
        case Severity of
            serious -> <<"** ">>;
            benign -> <<"* ">>
        end,
    PF = fun(Term, I) -> pp(Term, I, RT) end,
    Text = erl_error:format_exception(
        iolist_size(Tag) + 1, Class, Reason, Stacktrace, fun shell_frame/3, PF, encoding()
    ),
    io:requests(standard_io, [{put_chars, latin1, Tag}, {put_chars, unicode, Text}, nl]).

% The frames of the shell and erl_eval below the user's code are not shown.
shell_frame(M, _F, _A) ->
    lists:member(M, [erl_eval, clause_erl_eval, shell, clause_shell_eval, clause_shell_commands]).

%% A term as the shell prints it from column I: depth 30, lines of at most 60 characters, with its records.
pp(V, I, RT) ->
    pp(V, I, ?LINEMAX, RT).

pp(V, I, Depth, RT) ->
    Strings = application:get_env(stdlib, shell_strings, true) =/= false,
    clause_io_lib:pretty(V, [
        {column, I},
        {line_length, columns()},
        {depth, Depth},
        {line_max_chars, ?CHAR_MAX},
        {strings, Strings},
        {records, clause_shell_records:print_definitions(RT)}
        | enc()
    ]).

columns() ->
    case io:columns() of
        {ok, N} -> N;
        _ -> 80
    end.

encoding() ->
    [{encoding, Encoding}] = enc(),
    Encoding.

enc() ->
    case lists:keyfind(encoding, 1, io:getopts()) of
        false -> [{encoding, latin1}];
        Enc -> [Enc]
    end.

%% The help of the shell's own commands.
help_text() ->
    <<
        "** shell internal commands **\n"
        "b()        -- display all variable bindings\n"
        "e(N)       -- repeat the expression in query <N>\n"
        "exit()     -- terminate the shell instance\n"
        "f()        -- forget all variable bindings\n"
        "f(X)       -- forget the binding of variable X\n"
        "ff()       -- forget all locally defined functions\n"
        "ff(F,A)    -- forget locally defined function named as atom F and arity A\n"
        "fl()       -- forget all locally defined functions, types and records\n"
        "h()        -- history\n"
        "h(Mod)     -- help about module\n"
        "h(Mod,Func) -- help about function in module\n"
        "h(Mod,Func,Arity) -- help about function with arity in module\n"
        "lf()       -- list locally defined functions\n"
        "lr()       -- list locally defined records\n"
        "lt()       -- list locally defined types\n"
        "rd(R,D)    -- define a record\n"
        "rf()       -- remove all record information\n"
        "rf(R)      -- remove record information about R\n"
        "rl()       -- display all record information\n"
        "rl(R)      -- display record information about R\n"
        "rp(Term)   -- display Term using the shell's record information\n"
        "rr(File)   -- read record information from File (wildcards allowed)\n"
        "rr(F,R)    -- read selected record information from file(s)\n"
        "rr(F,R,O)  -- read selected record information with options\n"
        "tf()       -- forget all locally defined types\n"
        "tf(T)      -- forget locally defined type named as atom T\n"
        "v(N)       -- use the value of query <N>\n"
        "catch_exception(B) -- how exceptions are handled\n"
        "history(N) -- set how many previous commands to keep\n"
        "results(N) -- set how many previous command results to keep\n"
        "save_module(FilePath) -- save all locally defined functions, types and records to a file"
    >>.
