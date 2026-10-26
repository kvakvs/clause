-module(clause_shell_commands).
-export([local/5, command/2, non_builtin/4]).

% The shell's own commands (plan 21 step LIB74, docs/libraries/library.md#interactive-shell), run by the evaluator as local
% functions: b(), e(N) and v(N) (expanded by the server), f(), h(), the record commands rd/rf/rl/rp/rr, the local
% function, type and record commands fd/ff/fl/lf/lr/lt/td/tf/ft, and the settings. Other local calls go to
% user_default, shell_default, then the functions defined in the shell.

%% Evaluate the local call F(As) in the evaluator: {value, Value, Bindings} or {eval, Fun, Args, Bindings}.
%% Tables is {Shell, RecordTable, FunctionTable}; Handlers the {Local, External} handlers for arguments.
local(v, [{integer, _, V}], Bs, {Shell, _, _}, _) ->
    {_, Value, _} = request(Shell, {get_cmd, V}),
    {value, Value, Bs};
local(h, [], Bs, {Shell, RT, _}, _) ->
    {Commands, Values} = request(Shell, get_cmd),
    {value, list_commands(lists:sort(maps:to_list(Commands)), Values, RT), Bs};
local(b, [], Bs, {_, RT, _}, _) ->
    {value, list_bindings(erl_eval:bindings(Bs), RT), Bs};
local(f, [], _Bs, _, _) ->
    {value, ok, erl_eval:new_bindings()};
local(f, [{var, _, Name}], Bs, _, _) ->
    {value, ok, erl_eval:del_binding(Name, Bs)};
local(lf, [], Bs, {_, _, FT}, _) ->
    io:requests(standard_io, [{put_chars, unicode, local_functions_and_specs(FT) ++ "\n"}, nl]),
    {value, ok, Bs};
local(lt, [], Bs, {_, _, FT}, _) ->
    io:requests(standard_io, [{put_chars, unicode, local_types(FT)}, nl]),
    {value, ok, Bs};
local(lr, [], Bs, {_, _, FT}, _) ->
    io:requests(standard_io, [{put_chars, unicode, [list_to_binary(D) || {{record_def, _}, D} <- ets:tab2list(FT)]}, nl]),
    {value, ok, Bs};
local(save_module, [{string, _, Path}], Bs, {_, RT, FT}, _) ->
    {value, save_module(Path, RT, FT), Bs};
local(fd, [{atom, _, Name}, FunExpr, {string, _, Definition}], Bs, {_, _, FT}, _) ->
    {value, Fun, []} = erl_eval:expr(FunExpr, []),
    {arity, Arity} = erlang:fun_info(Fun, arity),
    ets:insert(FT, [{{function, {shell_default, Name, Arity}}, Fun}]),
    ets:insert(FT, [{{function_def, {shell_default, Name, Arity}}, Definition}]),
    {value, ok, Bs};
local(ft, [{string, _, Spec}], Bs, {_, _, FT}, _) ->
    {attribute, _, spec, {{Name, Arity}, _}} = Form = attribute_form(Spec, [spec]),
    ets:insert(FT, [{{function_type, {shell_default, Name, Arity}}, Form}]),
    ets:insert(FT, [{{function_type_def, {shell_default, Name, Arity}}, Spec}]),
    {value, ok, Bs};
local(fl, [], Bs, {_, RT, FT}, _) ->
    Records = [R || {{record_def, R}, _} <- ets:tab2list(FT)],
    true = ets:delete_all_objects(FT),
    lists:foreach(fun(Name) -> ets:delete(RT, Name) end, Records),
    {value, ok, Bs};
local(ff, [], Bs, {_, _, FT}, _) ->
    _ = [ets:match_delete(FT, {{Kind, '_'}, '_'}) || Kind <- function_kinds()],
    {value, ok, Bs};
local(ff, [{atom, _, F}, {integer, _, A}], Bs, {_, _, FT}, _) ->
    _ = [ets:delete(FT, {Kind, {shell_default, F, A}}) || Kind <- function_kinds()],
    {value, ok, Bs};
local(td, [{string, _, Type}], Bs, {_, _, FT}, _) ->
    {attribute, _, _, {Name, _, _}} = Form = attribute_form(Type, [type, nominal]),
    true = ets:insert(FT, [{{type, Name}, Form}]),
    true = ets:insert(FT, [{{type_def, Name}, Type}]),
    {value, ok, Bs};
local(tf, [], Bs, {_, _, FT}, _) ->
    _ = [ets:match_delete(FT, {{Kind, '_'}, '_'}) || Kind <- [type_def, type]],
    {value, ok, Bs};
local(tf, [{atom, _, T}], Bs, {_, _, FT}, _) ->
    _ = [ets:delete(FT, {Kind, T}) || Kind <- [type_def, type]],
    {value, ok, Bs};
local(rd, [{string, _, Record}], Bs, {_, RT, FT}, _) ->
    {attribute, _, Kind, {Name, _}} = Form = attribute_form(Record, [record, native_record]),
    [_] = clause_shell_records:add([Form], Bs, RT),
    Key =
        case Kind of
            record -> Name;
            native_record -> {shell_default, Name}
        end,
    true = ets:insert(FT, [{{record_def, Key}, Record}]),
    {value, ok, Bs};
local(rd, [{atom, _, Name}, Definition0], Bs, {_, RT, FT}, _) ->
    Definition = expand_value(Definition0),
    Text = lists:concat(["-record(", io_lib:write_atom_as_latin1(Name), ",", erl_pp:expr(Definition), ")."]),
    {ok, Tokens, _} = erl_scan:string(Text),
    case erl_parse:parse_form(Tokens) of
        {ok, {attribute, _, _, {RecordName, _}} = Form} ->
            [Added] = clause_shell_records:add([Form], Bs, RT),
            true = ets:insert(FT, [{{record_def, RecordName}, Definition}]),
            {value, Added, Bs};
        {error, {_, M, Error}} ->
            exit(lists:flatten(io_lib:fwrite(<<"~ts">>, [M:format_error(Error)])))
    end;
local(rf, [], Bs, {_, RT, FT}, _) ->
    ets:match_delete(FT, {{record_def, '_'}, '_'}),
    true = ets:delete_all_objects(RT),
    {value, clause_shell_records:initiate(Bs, RT), Bs};
local(rf, [A], Bs0, {_, RT, FT}, {Lf, Ef}) ->
    {[Records], Bs} = erl_eval:expr_list([A], Bs0, Lf, Ef),
    forget_records(Records, RT, FT),
    {value, ok, Bs};
local(rl, [], Bs, {_, RT, _}, _) ->
    {value, clause_shell_records:list(ets:tab2list(RT)), Bs};
local(rl, [A], Bs0, {_, RT, _}, {Lf, Ef}) ->
    {[Records], Bs} = erl_eval:expr_list([A], Bs0, Lf, Ef),
    {value, clause_shell_records:list(clause_shell_records:definitions(RT, listify(Records))), Bs};
local(rp, [A], Bs0, {_, RT, _}, {Lf, Ef}) ->
    {[V], Bs} = erl_eval:expr_list([A], Bs0, Lf, Ef),
    io:requests(standard_io, [{put_chars, unicode, clause_shell_eval:pp(V, 1, -1, RT)}, nl]),
    {value, ok, Bs};
local(rr, As0, Bs0, {_, RT, _}, {Lf, Ef}) when length(As0) >= 1, length(As0) =< 3 ->
    {As, Bs} = erl_eval:expr_list(As0, Bs0, Lf, Ef),
    [File, Selected, Options] = As ++ lists:nthtail(length(As) - 1, ['_', []]),
    {value, clause_shell_records:read_and_add(File, Selected, Options, Bs, RT), Bs};
local(history, [{integer, _, N}], Bs, _, _) ->
    {value, shell:history(N), Bs};
local(results, [{integer, _, N}], Bs, _, _) ->
    {value, shell:results(N), Bs};
local(catch_exception, [{atom, _, Bool}], Bs, _, _) when is_boolean(Bool) ->
    {value, shell:catch_exception(Bool), Bs};
local(exit, [], _Bs, {Shell, _, _}, _) ->
    request(Shell, exit),
    exit(normal);
local(F, As, _Bs, _, _) when
    F =:= f, length(As) =:= 1;
    F =:= history, length(As) =:= 1;
    F =:= results, length(As) =:= 1;
    F =:= catch_exception, length(As) =:= 1;
    F =:= save_module, length(As) =:= 1;
    F =:= fd, length(As) =:= 3;
    F =:= ft, length(As) =:= 1;
    F =:= ff, length(As) =:= 2;
    F =:= td, length(As) =:= 1;
    F =:= tf, length(As) =:= 1;
    F =:= rd, length(As) =:= 1;
    F =:= rd, length(As) =:= 2
->
    erlang:raise(error, function_clause, [{shell, F, length(As)}]);
local(F, As0, Bs0, {_, _, FT}, {Lf, Ef}) when is_atom(F) ->
    {As, Bs} = erl_eval:expr_list(As0, Bs0, Lf, Ef),
    non_builtin(F, As, Bs, FT).

%% Whether F(As) is a command the shell runs itself: never restricted, its arguments not evaluated first.
command(F, As) ->
    lists:member({F, length(As)}, [
        {f, 0},
        {f, 1},
        {h, 0},
        {b, 0},
        {history, 1},
        {results, 1},
        {catch_exception, 1},
        {exit, 0},
        {fl, 0},
        {fd, 3},
        {ft, 1},
        {td, 1},
        {rd, 1},
        {rd, 2},
        {rf, 0},
        {rf, 1},
        {rl, 0},
        {rl, 1},
        {rp, 1},
        {rr, 1},
        {rr, 2},
        {rr, 3},
        {v, 1}
    ]).

%% A local call that is no shell command: user_default's, shell_default's or a function defined in the shell.
non_builtin(F, As, Bs, FT) ->
    Arity = length(As),
    case erlang:function_exported(user_default, F, Arity) of
        true -> {eval, erlang:make_fun(user_default, F, Arity), As, Bs};
        false -> shell_default(F, As, Bs, FT)
    end.

shell_default(F, As, Bs, FT) ->
    Arity = length(As),
    case lists:member({F, Arity}, shell_default:module_info(exports)) of
        true ->
            {eval, erlang:make_fun(shell_default, F, Arity), As, Bs};
        false ->
            case ets:lookup(FT, {function, {shell_default, F, Arity}}) of
                [] -> erlang:error({shell_undef, F, Arity, []});
                [{_, Fun}] -> {eval, Fun, As, Bs}
            end
    end.

request(Shell, Request) ->
    Shell ! {shell_req, self(), Request},
    receive
        {shell_rep, Shell, Reply} -> Reply
    end.

function_kinds() -> [function_def, function, function_type_def, function_type].

listify(L) when is_list(L) -> L;
listify(E) -> [E].

forget_records('_', RT, FT) ->
    ets:match_delete(FT, {{record_def, '_'}, '_'}),
    true = ets:delete_all_objects(RT);
forget_records(Records, RT, FT) ->
    lists:foreach(
        fun(Name) ->
            true = ets:delete(RT, Name),
            true = ets:delete(FT, {record_def, Name})
        end,
        listify(Records)
    ).

% The form of an attribute typed at the prompt, which must be one of Kinds; its error ends the evaluator.
attribute_form(Text, Kinds) ->
    Options = [text, {reserved_word_fun, fun erl_scan:reserved_word/1}],
    case erl_scan:tokens([], Text, {1, 1}, Options) of
        {done, {ok, Toks, _}, _} ->
            case erl_parse:parse_form(Toks) of
                {ok, {attribute, _, Kind, _} = Form} ->
                    true = lists:member(Kind, Kinds),
                    Form;
                {error, {_, M, Error}} ->
                    exit(lists:flatten(io_lib:fwrite(<<"~ts">>, [M:format_error(Error)])))
            end;
        {done, {error, {_, M, Error}, _}, _} ->
            exit(lists:flatten(io_lib:fwrite(<<"~ts">>, [M:format_error(Error)])))
    end.

%% --- Listings ---

% h(): each kept command with its value, values standing as v(N).
list_commands([{N, Es0} | Rest], Values, RT) ->
    Es = v_calls(Es0),
    Number = io_lib:fwrite(<<"~w: ">>, [N]),
    Indent = iolist_size(Number),
    Command = [{put_chars, latin1, Number}, {format, <<"~ts\n">>, [erl_pp:exprs(Es, Indent, enc())]}],
    Value =
        case Values of
            #{N := V} -> [{format, <<"-> ">>, []}, {put_chars, unicode, clause_shell_eval:pp(V, 4, RT)}, nl];
            #{} -> []
        end,
    io:requests(standard_io, Command ++ Value),
    list_commands(Rest, Values, RT);
list_commands([], _, _) ->
    ok.

% b(): each binding; funs made in the shell as their source.
list_bindings([{Name, Val} | Bs], RT) ->
    A = erl_anno:new(0),
    case erl_eval:fun_data(Val) of
        {fun_data, _, Clauses} ->
            Match = {match, A, {var, A, Name}, {'fun', A, {clauses, expand_value(Clauses)}}},
            io:fwrite(<<"~ts\n">>, [erl_pp:expr(Match, enc())]);
        {named_fun_data, _, FName, Clauses} ->
            Match = {match, A, {var, A, Name}, {named_fun, A, FName, expand_value(Clauses)}},
            io:fwrite(<<"~ts\n">>, [erl_pp:expr(Match, enc())]);
        false ->
            Prefix = io_lib:fwrite(<<"~s = ">>, [Name]),
            Text = clause_shell_eval:pp(Val, iolist_size(Prefix) + 1, RT),
            io:requests(standard_io, [{put_chars, latin1, Prefix}, {put_chars, unicode, Text}, nl])
    end,
    list_bindings(Bs, RT);
list_bindings([], _) ->
    ok.

% History values {value, N, V} as their abstract form, or v(N) for values without one (funs).
expand_value(E) ->
    substitute(
        fun({value, N, V}) ->
            try
                erl_parse:abstract(V)
            catch
                _:_ ->
                    A = erl_anno:new(0),
                    {call, A, {atom, A, v}, [{integer, A, N}]}
            end
        end,
        E
    ).

% History values as v(N) calls, so h() does not list them whole.
v_calls(E) ->
    A = erl_anno:new(0),
    substitute(fun({value, Anno, _}) -> {call, A, {atom, A, v}, [{integer, A, erl_anno:line(Anno)}]} end, E).

substitute(F, {value, _, _} = Value) -> F(Value);
substitute(F, T) when is_tuple(T) -> list_to_tuple(substitute(F, tuple_to_list(T)));
substitute(F, [E | Es]) -> [substitute(F, E) | substitute(F, Es)];
substitute(_, E) -> E.

% lf(): the functions defined in the shell with their specs.
local_functions_and_specs(FT) ->
    Defs = maps:from_list([{{F, A}, lists:flatten(D)} || {{function_def, {_, F, A}}, D} <- ets:tab2list(FT)]),
    Specs = maps:from_list([{{F, A}, S} || {{function_type_def, {_, F, A}}, S} <- ets:tab2list(FT)]),
    Keys = lists:uniq(maps:keys(Defs) ++ maps:keys(Specs)),
    string:trim(lists:join($\n, [function_text(Key, Specs, Defs) || Key <- Keys])).

function_text({F, A} = Key, Specs, Defs) ->
    case {maps:get(Key, Specs, nospec), maps:get(Key, Defs, nodef)} of
        {nospec, Def} ->
            Def;
        {Spec, nodef} ->
            Spec ++ "%% " ++ lists:flatten(io_lib:fwrite("~tw", [F])) ++ "/" ++ integer_to_list(A) ++
                " not implemented";
        {Spec, Def} ->
            Spec ++ Def
    end.

local_types(FT) ->
    lists:join("\n\n", [Def || {{type_def, _}, Def} <- ets:tab2list(FT)]).

% save_module(Path): the shell's functions, types and records as a module file. Compiling it needs the compiler,
% which a Clause program does not have.
save_module(Path, RT, FT) ->
    [_, FileName] = string:split("/" ++ Path, "/", trailing),
    [Module, _] = string:split(FileName, ".", leading),
    Exports = [
        lists:flatten(io_lib:fwrite("~tw", [F])) ++ "/" ++ integer_to_list(A)
     || {{function_def, {_, F, A}}, _} <- ets:tab2list(FT)
    ],
    Functions = [Def || {{function_def, _}, Def} <- ets:tab2list(FT)],
    Records = [erl_pp:attribute(Def) ++ "\n" || {_, Def} <- ets:tab2list(RT)],
    Output = [
        "-module(",
        io_lib:fwrite("~tw", [list_to_atom(Module)]),
        ").\n\n-export([",
        lists:join(",", Exports),
        "]).\n\n",
        local_types(FT),
        "\n",
        Records,
        lists:join($\n, Functions)
    ],
    file:write_file(Path, unicode:characters_to_binary(Output)).

enc() ->
    case lists:keyfind(encoding, 1, io:getopts()) of
        false -> [{encoding, latin1}];
        Enc -> [Enc]
    end.
