%% Clause's ms_transform (plan 21 step LIB72, docs/libraries/library.md): the translation of literal funs given to ets:fun2ms/1
%% and dbg:fun2ms/1 into match specifications, over the abstract format. parse_transform/2 rewrites the calls inside
%% a module's forms; transform_from_shell/3 translates the clauses of one shell fun into a match specification term.
%% The state OTP keeps in the process dictionary (file, records, warnings) is threaded through the walks instead.
-module(ms_transform).
-export([
    format_error/1,
    parse_transform/2,
    parse_transform_info/0,
    transform_from_shell/3
]).

%% Error codes of OTP's ms_transform; guard errors are the general codes, body errors add 100.
-define(BODY_BASE, 100).
-define(ERR_NOFUN, 1).
-define(ERR_ETS_HEAD, 2).
-define(ERR_DBG_HEAD, 3).
-define(ERR_HEADMATCH, 4).
-define(ERR_SEMI_GUARD, 5).
-define(ERR_UNBOUND_VARIABLE, 6).
-define(ERR_HEADBADREC, 7).
-define(ERR_HEADBADFIELD, 8).
-define(ERR_HEADMULTIFIELD, 9).
-define(ERR_HEADDOLLARATOM, 10).
-define(ERR_HEADBINMATCH, 11).
-define(ERR_GENMATCH, 16).
-define(ERR_GENLOCALCALL, 17).
-define(ERR_GENELEMENT, 18).
-define(ERR_GENBADFIELD, 19).
-define(ERR_GENBADREC, 20).
-define(ERR_GENMULTIFIELD, 21).
-define(ERR_GENREMOTECALL, 22).
-define(ERR_GENBINCONSTRUCT, 23).
-define(ERR_GENDISALLOWEDOP, 24).
-define(WARN_SHADOW_VAR, 50).

%% The state of a module's translation: the current file, the records defined so far as {Name, Fields} (each field
%% {Field, Position, Default}, last field first), the shadowing warnings (newest first) and the records whose uses
%% became tuples.
-record(st, {file = "TOP_LEVEL", records = [], warnings = [], used = []}).
%% A fun head's translation: its variables as {Name, '$N'} (newest first) with the next N, the variables bound
%% around the fun, and the module state.
-record(head, {next = 1, names = [{'_', '_'}], outer, st}).
%% A guard or body translation: the head's variables, guard or body, and the module state.
-record(tg, {names, part, st}).

format_error({?WARN_SHADOW_VAR, Name}) ->
    text("variable ~p shadowed in ms_transform fun head", [Name]);
format_error(?ERR_NOFUN) ->
    "Parameter of ets/dbg:fun2ms/1 is not a literal fun";
format_error(?ERR_ETS_HEAD) ->
    "ets:fun2ms requires fun with single variable or tuple parameter";
format_error(?ERR_DBG_HEAD) ->
    "dbg:fun2ms requires fun with single variable or list parameter";
format_error(?ERR_HEADMATCH) ->
    "in fun head, only matching (=) on toplevel can be translated into match_spec";
format_error(?ERR_SEMI_GUARD) ->
    "fun with semicolon (;) in guard cannot be translated into match_spec";
format_error(?ERR_GENMATCH) ->
    "fun with guard matching ('=' in guard) is illegal as match_spec as well";
format_error(?ERR_GENMATCH + ?BODY_BASE) ->
    "fun with body matching ('=' in body) is illegal as match_spec";
format_error({?ERR_UNBOUND_VARIABLE, Name}) ->
    text("the variable ~s is unbound, cannot translate into match_spec", [Name]);
format_error({?ERR_HEADBADREC, Name}) ->
    text("fun head contains unknown record type ~tw", [Name]);
format_error({?ERR_HEADBADFIELD, Record, Field}) ->
    text("fun head contains reference to unknown field ~tw in record type ~tw", [Field, Record]);
format_error({?ERR_HEADMULTIFIELD, Record, Field}) ->
    text("fun head contains already defined field ~tw in record type ~tw", [Field, Record]);
format_error({?ERR_HEADDOLLARATOM, Atom}) ->
    text("fun head contains atom ~w, which conflics with reserved atoms in match_spec heads", [Atom]);
format_error({?ERR_HEADBINMATCH, Name}) ->
    text(
        "fun head contains bit syntax matching of variable ~w, which cannot be translated into match_spec",
        [Name]
    );
format_error(Error) ->
    case part_error(Error) of
        {Part, Code, Details} -> part_message(Code, Part, Details);
        none -> text("Unknown error code ~tw", [Error])
    end.

%% A guard or body error: its part, general code and details; none for other terms.
part_error(Error) when is_tuple(Error), tuple_size(Error) >= 2, is_integer(element(1, Error)) ->
    [Code | Details] = tuple_to_list(Error),
    case Code >= ?BODY_BASE of
        true -> {body, Code - ?BODY_BASE, Details};
        false -> {guard, Code, Details}
    end;
part_error(_) ->
    none.

part_message(?ERR_GENLOCALCALL, Part, [Name, Arity]) ->
    text(
        "fun containing the local function call '~tw/~w' (called in ~w) cannot be translated into match_spec",
        [
            Name, Arity, Part
        ]
    );
part_message(?ERR_GENREMOTECALL, Part, [Module, Name, Arity]) ->
    text(
        "fun containing the remote function call '~w:~tw/~w' (called in ~w) cannot be translated into match_spec",
        [Module, Name, Arity, Part]
    );
part_message(?ERR_GENELEMENT, Part, [Element]) ->
    text("the language element ~ts (in ~w) cannot be translated into match_spec", [Element, Part]);
part_message(?ERR_GENBINCONSTRUCT, Part, [Name]) ->
    text("bit syntax construction with variable ~w (in ~w) cannot be translated into match_spec", [
        Name, Part
    ]);
part_message(?ERR_GENBADREC, Part, [Name]) ->
    text("fun ~w contains unknown record type ~tw", [Part, Name]);
part_message(?ERR_GENBADFIELD, Part, [Record, Field]) ->
    text("fun ~w contains reference to unknown field ~tw in record type ~tw", [Part, Field, Record]);
part_message(?ERR_GENMULTIFIELD, Part, [Record, Field]) ->
    text("fun ~w contains already defined field ~tw in record type ~tw", [Part, Field, Record]);
part_message(?ERR_GENDISALLOWEDOP, guard, [Operator]) ->
    text("the operator ~w is not allowed in guards", [Operator]);
part_message(?ERR_GENDISALLOWEDOP, body, [Operator]) ->
    text("the operator ~w is not allowed in function bodies", [Operator]);
part_message(Code, Part, Details) ->
    Base =
        case Part of
            body -> ?BODY_BASE;
            guard -> 0
        end,
    text("Unknown error code ~tw", [list_to_tuple([Code + Base | Details])]).

text(Format, Arguments) ->
    lists:flatten(io_lib:format(Format, Arguments)).

parse_transform_info() ->
    #{error_location => column}.

%% parse_transform(Forms, Options): the forms with every ets:fun2ms/1 and dbg:fun2ms/1 of a literal fun replaced by
%% its match specification; {warning, Forms, Warnings} when a head variable shadows a bound one, {error, ...} for
%% the first fun that cannot be translated.
parse_transform(Forms, _Options) ->
    guarded(fun() ->
        case transform_forms(Forms, [], #st{}) of
            {error, File, Anno, Reason} ->
                {error, [{File, [{location(Anno), ?MODULE, Reason}]}], []};
            {ok, Transformed, #st{warnings = [], used = Used}} ->
                silence_records(Transformed, Used);
            {ok, Transformed, #st{file = File, warnings = Warnings, used = Used}} ->
                Reports = [{File, [{Location, ?MODULE, Reason}]} || {Location, Reason} <- Warnings],
                {warning, silence_records(Transformed, Used), Reports}
        end
    end).

%% transform_from_shell(Dialect, Clauses, Bindings): the match specification of a shell fun's clauses, with the
%% variables imported from the shell replaced by their values from Bindings.
transform_from_shell(Dialect, Clauses, Bindings) ->
    try
        {Spec, _} = guarded(fun() -> clause_list(1, Clauses, Dialect, #{}, #st{}) end),
        normalise(bind_imports(Spec, Bindings))
    catch
        throw:{ms_error, Anno, Reason} ->
            {error, [{"TOP_LEVEL", [{location(Anno), ?MODULE, Reason}]}], []}
    end.

%% Run a translation; an error or exit in it becomes the exit OTP's catch gives, an ms_error throw passes.
guarded(Translate) ->
    try
        Translate()
    catch
        error:Reason:Stack -> exit({Reason, Stack});
        exit:Reason -> exit(Reason)
    end.

location(unknown) -> none;
location(Anno) -> erl_anno:location(Anno).

%% Records replaced by tuples get -compile({nowarn_unused_record, [Names]}) first, so lint does not report them.
silence_records(Forms, []) ->
    Forms;
silence_records(Forms, Used) ->
    [{attribute, erl_anno:new(0), compile, {nowarn_unused_record, [lists:usort(Used)]}} | Forms].

%% Translate the forms in order; file and record attributes update the state.
transform_forms([], Done, St) ->
    {ok, lists:reverse(Done), St};
transform_forms([{attribute, _, file, {File, _}} = Form | Rest], Done, St) ->
    transform_forms(Rest, [Form | Done], St#st{file = File});
transform_forms([{attribute, _, record, {Name, Fields}} = Form | Rest], Done, St) ->
    Record = {Name, record_fields(Fields)},
    transform_forms(Rest, [Form | Done], St#st{records = [Record | St#st.records]});
transform_forms([{function, Anno, Name, Arity, Clauses} | Rest], Done, St) ->
    try function_clauses(Clauses, St) of
        {Transformed, St1} ->
            transform_forms(Rest, [{function, Anno, Name, Arity, Transformed} | Done], St1)
    catch
        throw:{ms_error, ErrorAnno, Reason} -> {error, St#st.file, ErrorAnno, Reason}
    end;
transform_forms([Form | Rest], Done, St) ->
    transform_forms(Rest, [Form | Done], St);
transform_forms(Other, _, _) ->
    %% OTP walks the forms with a comprehension.
    error({bad_generator, Other}).

%% A record's fields as {Field, Position, Default}, last field first.
record_fields(Fields) ->
    {Numbered, _} = lists:foldl(
        fun(Field, {Acc, At}) -> {[record_field(Field, At) | Acc], At + 1} end, {[], 2}, Fields
    ),
    Numbered.

record_field({record_field, _, {atom, Anno, Name}}, At) -> {Name, At, {atom, Anno, undefined}};
record_field({record_field, _, {atom, _, Name}, Default}, At) -> {Name, At, Default};
record_field({typed_record_field, Field, _Type}, At) -> record_field(Field, At).

%% Each clause of a function starts with no bound variables; guards are left as they are.
function_clauses(Clauses, St) ->
    lists:mapfoldl(
        fun({clause, Anno, Head, Guard, Body}, St0) ->
            {Head1, Bound, St1} = copy(Head, #{}, St0),
            {Body1, _, St2} = copy(Body, Bound, St1),
            {{clause, Anno, Head1, Guard, Body1}, St2}
        end,
        St,
        Clauses
    ).

%% Copy a part of a function, tracking the variables bound so far, and translate the fun2ms calls in it.
copy({call, Anno, {remote, _, {atom, _, ets}, {atom, _, fun2ms}}, Arguments}, Bound, St) ->
    {Spec, St1} = fun2ms(ets, Anno, Arguments, Bound, St),
    {Spec, Bound, St1};
copy({call, Anno, {remote, _, {atom, _, dbg}, {atom, _, fun2ms}}, Arguments}, Bound, St) ->
    {Spec, St1} = fun2ms(dbg, Anno, Arguments, Bound, St),
    {Spec, Bound, St1};
copy({match, Anno, Pattern, Value}, Bound, St) ->
    {Value1, ValueBound, St1} = copy(Value, Bound, St),
    {Pattern1, PatternBound, St2} = copy(Pattern, Bound, St1),
    {{match, Anno, Pattern1, Value1}, maps:merge(ValueBound, PatternBound), St2};
copy({var, _, '_'} = Var, Bound, St) ->
    {Var, Bound, St};
copy({var, _, Name} = Var, Bound, St) ->
    {Var, Bound#{Name => true}, St};
copy({'fun', Anno, {clauses, Clauses}}, Bound, St) ->
    %% A fun exports no bindings.
    {Clauses1, _, St1} = copy(Clauses, Bound, St),
    {{'fun', Anno, {clauses, Clauses1}}, Bound, St1};
copy({named_fun, Anno, Name, Clauses}, Bound, St) ->
    Inner =
        case Name of
            '_' -> Bound;
            _ -> Bound#{Name => true}
        end,
    {Clauses1, _, St1} = copy(Clauses, Inner, St),
    {{named_fun, Anno, Name, Clauses1}, Bound, St1};
copy({'case', Anno, Subject, Clauses}, Bound, St) ->
    {Subject1, Bound1, St1} = copy(Subject, Bound, St),
    {Clauses1, Exported, St2} = case_clauses(Clauses, Bound1, St1),
    {{'case', Anno, Subject1, Clauses1}, Exported, St2};
copy(Tuple, Bound, St) when is_tuple(Tuple) ->
    {Elements, Bound1, St1} = copy(tuple_to_list(Tuple), Bound, St),
    {list_to_tuple(Elements), Bound1, St1};
copy([Head | Tail], Bound, St) ->
    {Head1, Bound1, St1} = copy(Head, Bound, St),
    {Tail1, Bound2, St2} = copy(Tail, Bound1, St1),
    {[Head1 | Tail1], Bound2, St2};
copy(Other, Bound, St) ->
    {Other, Bound, St}.

%% The clauses of a case export the variables every clause binds.
case_clauses(Clauses, Bound, St) ->
    {Copied, {Added, St1}} = lists:mapfoldl(
        fun({clause, Anno, Pattern, Guard, Body}, {Added0, St0}) ->
            {Pattern1, PatternBound, StP} = copy(Pattern, Bound, St0),
            {Guard1, GuardBound, StG} = copy(Guard, PatternBound, StP),
            {Body1, AllBound, StB} = copy(Body, GuardBound, StG),
            New = maps:without(maps:keys(Bound), AllBound),
            {{clause, Anno, Pattern1, Guard1, Body1}, {[New | Added0], StB}}
        end,
        {[], St},
        Clauses
    ),
    {Copied, maps:merge(Bound, intersection(Added)), St1}.

intersection([First | Rest]) ->
    lists:foldl(fun(Set, Acc) -> maps:with(maps:keys(Set), Acc) end, First, Rest).

%% fun2ms(Fun) with a literal fun: the match specification as an abstract list.
fun2ms(Dialect, _, [{'fun', Anno, {clauses, Clauses}}], Bound, St) ->
    clause_list(Anno, Clauses, Dialect, Bound, St);
fun2ms(_, Anno, _, _, _) ->
    throw({ms_error, Anno, ?ERR_NOFUN}).

%% The clauses as an abstract list of match specification clauses; a clause with alternative guards (;) gives one
%% clause per guard.
clause_list(
    Anno, [{clause, ClauseAnno, Head, [_, _ | _] = Guards, Body} | Rest], Dialect, Bound, St
) ->
    Split = [{clause, ClauseAnno, Head, [Guard], Body} || Guard <- Guards],
    clause_list(Anno, Split ++ Rest, Dialect, Bound, St);
clause_list(Anno, [Clause | Rest], Dialect, Bound, St) ->
    {Spec, St1} = ms_clause(Clause, Dialect, Bound, St),
    {Tail, St2} = clause_list(Anno, Rest, Dialect, Bound, St1),
    {{cons, Anno, Spec, Tail}, St2};
clause_list(Anno, [], _, _, St) ->
    {{nil, Anno}, St}.

%% One clause as {Head, Guards, Body}.
ms_clause({clause, Anno, Parameters, Guards, Body}, Dialect, Bound, St) ->
    check_head(Anno, Parameters, Dialect),
    {Head, #head{names = Names, st = St1}} = transform_head(Parameters, #head{
        outer = Bound, st = St
    }),
    {MsGuards, St2} = transform_guards(Anno, Guards, Names, St1),
    {MsBody, St3} = tg_list(Anno, Body, #tg{names = Names, part = body, st = St2}),
    {{tuple, Anno, [Head, MsGuards, MsBody]}, St3}.

%% ets funs take one variable, tuple or record; dbg funs one variable or list; either may be matched with a variable.
check_head(_, [{var, _, _}], _) ->
    ok;
check_head(_, [{tuple, _, _}], ets) ->
    ok;
check_head(_, [{record, _, _, _}], ets) ->
    ok;
check_head(_, [{cons, _, _, _}], dbg) ->
    ok;
check_head(_, [{nil, _}], dbg) ->
    ok;
check_head(Anno, [{match, _, {var, _, _}, Pattern}], Dialect) ->
    check_head(Anno, [Pattern], Dialect);
check_head(Anno, [{match, _, Pattern, {var, _, _}}], Dialect) ->
    check_head(Anno, [Pattern], Dialect);
check_head(Anno, _, ets) ->
    throw({ms_error, Anno, ?ERR_ETS_HEAD});
check_head(Anno, _, dbg) ->
    throw({ms_error, Anno, ?ERR_DBG_HEAD}).

transform_guards(Anno, [], _, St) ->
    {{nil, Anno}, St};
transform_guards(Anno, [Guard], Names, St) ->
    tg_list(Anno, Guard, #tg{names = Names, part = guard, st = St});
transform_guards(Anno, _, _, _) ->
    throw({ms_error, Anno, ?ERR_SEMI_GUARD}).

%% The head pattern: a variable matched with the whole pattern stands for '$_', the other variables become '$N'.
transform_head([{match, _, {var, Anno, Name}, Pattern}], H) ->
    th(Pattern, bind_object(Anno, Name, H));
transform_head([{match, _, Pattern, {var, Anno, Name}}], H) ->
    th(Pattern, bind_object(Anno, Name, H));
transform_head([Pattern], H) ->
    th(Pattern, H).

bind_object(Anno, Name, #head{names = Names} = H) ->
    shadow_check(Anno, Name, H#head{names = [{Name, '$_'} | Names]}).

%% A head variable bound outside the fun gets a warning.
shadow_check(Anno, Name, #head{outer = Outer, st = St} = H) ->
    case Outer of
        #{Name := _} ->
            Warning = {erl_anno:location(Anno), {?WARN_SHADOW_VAR, Name}},
            H#head{st = St#st{warnings = [Warning | St#st.warnings]}};
        _ ->
            H
    end.

th({record, Anno, Name, Fields}, H) ->
    head_record(Anno, Name, Fields, H);
th({match, Anno, _, _}, _) ->
    throw({ms_error, Anno, ?ERR_HEADMATCH});
th({atom, Anno, Atom} = Form, H) ->
    case atom_to_list(Atom) of
        [$$ | Number] ->
            case integer_text(Number) of
                true -> throw({ms_error, Anno, {?ERR_HEADDOLLARATOM, Atom}});
                false -> {Form, H}
            end;
        _ ->
            {Form, H}
    end;
th({bin_element, _, {var, Anno, Name}, _, _}, _) ->
    throw({ms_error, Anno, {?ERR_HEADBINMATCH, Name}});
th({var, Anno, Name}, H) ->
    H1 = shadow_check(Anno, Name, H),
    case lists:keyfind(Name, 1, H1#head.names) of
        {Name, Translated} ->
            {{atom, Anno, Translated}, H1};
        false ->
            #head{next = Next, names = Names} = H1,
            Translated = list_to_atom([$$ | integer_to_list(Next)]),
            {{atom, Anno, Translated}, H1#head{
                next = Next + 1, names = [{Name, Translated} | Names]
            }}
    end;
th({map_field_exact, Anno, Key, Value}, H) ->
    {[Key1, Value1], H1} = th([Key, Value], H),
    {{map_field_assoc, Anno, Key1, Value1}, H1};
th([Head | Tail], H) ->
    {Head1, H1} = th(Head, H),
    {Tail1, H2} = th(Tail, H1),
    {[Head1 | Tail1], H2};
th(Tuple, H) when is_tuple(Tuple) ->
    {Elements, H1} = th(tuple_to_list(Tuple), H),
    {list_to_tuple(Elements), H1};
th(Other, H) ->
    {Other, H}.

%% Whether text is an integer as list_to_integer/1 reads it ('$1', '$-1' and '$+1' are reserved).
integer_text(Text) ->
    try list_to_integer(Text) of
        _ -> true
    catch
        error:badarg -> false
    end.

%% A record pattern as its tuple: unnamed fields are '_' or the value of a `_ = Value` field.
head_record(Anno, Name, Fields, H) ->
    {Given, H1} = lists:foldl(
        fun
            ({record_field, _, {atom, _, Field}, Value}, {Acc, H0}) ->
                {Value1, HV} = th(Value, H0),
                {[{Field, Value1} | Acc], HV};
            ({record_field, _, {var, _, '_'}, Value}, {Acc, H0}) ->
                {Value1, HV} = th(Value, H0),
                {[{{default}, Value1} | Acc], HV};
            (_, _) ->
                throw({ms_error, Anno, {?ERR_HEADBADREC, Name}})
        end,
        {[], H},
        Fields
    ),
    {Default, Named} = split_default(Given, {atom, Anno, '_'}),
    lists:keymember({default}, 1, Named) andalso
        throw({ms_error, Anno, {?ERR_HEADMULTIFIELD, Name, '_'}}),
    {Definition, St} = record(Name, H1#head.st),
    Definition =:= not_found andalso throw({ms_error, Anno, {?ERR_HEADBADREC, Name}}),
    Values = lists:foldl(
        fun({Field, _, _}, Acc) -> [field_value(Field, Named, Default) | Acc] end, [], Definition
    ),
    check_fields(Name, Anno, Named, Definition, ?ERR_HEADMULTIFIELD, ?ERR_HEADBADFIELD),
    {{tuple, Anno, [{atom, Anno, Name} | Values]}, H1#head{st = St}}.

%% The value of the first `_ = Value` field (or Fallback), and the fields without it.
split_default(Given, Fallback) ->
    case lists:keyfind({default}, 1, Given) of
        {{default}, Value} -> {Value, lists:keydelete({default}, 1, Given)};
        false -> {Fallback, Given}
    end.

field_value(Field, Named, Default) ->
    case lists:keyfind(Field, 1, Named) of
        {Field, Value} -> Value;
        false -> Default
    end.

%% A field named twice, then a field the record does not have, is an error.
check_fields(Name, Anno, Named, Definition, MultiCode, BadCode) ->
    check_multi(Name, Anno, Named, MultiCode),
    [
        throw({ms_error, Anno, {BadCode, Name, Field}})
     || {Field, _} <- Named, not lists:keymember(Field, 1, Definition)
    ],
    ok.

check_multi(_, _, [], _) ->
    ok;
check_multi(Name, Anno, [{Field, _} | Rest], Code) ->
    lists:keymember(Field, 1, Rest) andalso throw({ms_error, Anno, {Code, Name, Field}}),
    check_multi(Name, Anno, Rest, Code).

%% A record's fields, noting that its uses became tuples; not_found when no record of that name is defined.
record(Name, #st{records = Records, used = Used} = St) ->
    case lists:keyfind(Name, 1, Records) of
        {Name, Fields} -> {Fields, St#st{used = [Name | Used]}};
        false -> {not_found, St}
    end.

%% A guard or body list as an abstract list; old type tests at a guard's top level get their is_ names.
tg_list(Anno, [], T) ->
    {{nil, Anno}, T#tg.st};
tg_list(Anno, [Expr | Rest], #tg{part = Part} = T) ->
    Top =
        case Part of
            guard -> old_test(Expr);
            body -> Expr
        end,
    {Ms, St1} = tg(Top, T),
    {Tail, St2} = tg_list(Anno, Rest, T#tg{st = St1}),
    {{cons, Anno, Ms, Tail}, St2}.

old_test({call, Anno, {atom, NameAnno, Name}, Arguments} = Call) ->
    case old_test_name(Name, length(Arguments)) of
        undefined -> Call;
        New -> {call, Anno, {atom, NameAnno, New}, Arguments}
    end;
old_test(Expr) ->
    Expr.

old_test_name(atom, 1) -> is_atom;
old_test_name(float, 1) -> is_float;
old_test_name(integer, 1) -> is_integer;
old_test_name(list, 1) -> is_list;
old_test_name(number, 1) -> is_number;
old_test_name(pid, 1) -> is_pid;
old_test_name(port, 1) -> is_port;
old_test_name(reference, 1) -> is_reference;
old_test_name(tuple, 1) -> is_tuple;
old_test_name(binary, 1) -> is_binary;
old_test_name(function, 1) -> is_function;
old_test_name(record, 2) -> is_record;
old_test_name(_, _) -> undefined.

%% Translate a guard or body expression; returns the abstract match specification term and the module state.
tg({match, Anno, _, _}, T) ->
    throw({ms_error, Anno, base(?ERR_GENMATCH, T)});
tg({op, Anno, Operator, Left, Right} = Expr, T) ->
    case fold_constant(Expr) of
        Expr -> tg_tuple(Anno, [{atom, Anno, Operator}], [Left, Right], T);
        Value -> {Value, T#tg.st}
    end;
tg({op, Anno, Operator, Operand} = Expr, T) ->
    case fold_constant(Expr) of
        Expr -> tg_tuple(Anno, [{atom, Anno, Operator}], [Operand], T);
        Value -> {Value, T#tg.st}
    end;
tg({call, _, {atom, Anno, bindings}, []}, T) ->
    {{atom, Anno, '$*'}, T#tg.st};
tg({call, _, {atom, Anno, object}, []}, T) ->
    {{atom, Anno, '$_'}, T#tg.st};
tg({call, Anno, {atom, _, is_record} = Call, [Object, {atom, NameAnno, Name} = Record]}, T) ->
    {MsObject, St1} = tg(Object, T),
    case record(Name, St1) of
        {not_found, _} ->
            throw({ms_error, NameAnno, {base(?ERR_GENBADREC, T), Name}});
        {Fields, St2} ->
            {{tuple, Anno, [Call, MsObject, Record, {integer, NameAnno, length(Fields) + 1}]}, St2}
    end;
tg({call, Anno, {atom, NameAnno, Name}, Arguments}, T) ->
    case ms_function(Name, length(Arguments), T#tg.part) of
        true -> tg_tuple(Anno, [{atom, NameAnno, Name}], Arguments, T);
        false -> throw({ms_error, Anno, {base(?ERR_GENLOCALCALL, T), Name, length(Arguments)}})
    end;
tg({call, Anno, {remote, _, {atom, _, erlang}, {atom, NameAnno, Name}}, Arguments}, T) ->
    Arity = length(Arguments),
    case from_erlang(Name, Arity) of
        false -> throw({ms_error, Anno, {base(?ERR_GENREMOTECALL, T), erlang, Name, Arity}});
        true when Arity =:= 1; Arity =:= 2 -> erlang_call(Anno, NameAnno, Name, Arguments, T);
        true -> tg({call, Anno, {atom, NameAnno, Name}, Arguments}, T)
    end;
tg({call, Anno, {remote, _, {atom, _, Module}, {atom, _, Name}}, Arguments}, T) ->
    throw({ms_error, Anno, {base(?ERR_GENREMOTECALL, T), Module, Name, length(Arguments)}});
tg({cons, Anno, Head, Tail}, T) ->
    {Head1, St1} = tg(Head, T),
    {Tail1, St2} = tg(Tail, T#tg{st = St1}),
    {{cons, Anno, Head1, Tail1}, St2};
tg({nil, _} = Nil, T) ->
    {Nil, T#tg.st};
tg({tuple, Anno, Elements}, T) ->
    {Inner, St1} = tg_tuple(Anno, [], Elements, T),
    {{tuple, Anno, [Inner]}, St1};
tg({Kind, _, _} = Literal, T) when
    Kind =:= integer; Kind =:= char; Kind =:= float; Kind =:= string
->
    {Literal, T#tg.st};
tg({atom, Anno, Atom} = Literal, T) ->
    case atom_to_list(Atom) of
        [$$ | _] -> {{tuple, Anno, [{atom, Anno, const}, Literal]}, T#tg.st};
        _ -> {Literal, T#tg.st}
    end;
tg({var, Anno, Name} = Var, T) ->
    case lists:keyfind(Name, 1, T#tg.names) of
        {Name, Translated} -> {{atom, Anno, Translated}, T#tg.st};
        false -> {{tuple, Anno, [{atom, Anno, const}, Var]}, T#tg.st}
    end;
tg({record_field, Anno, Object, Name, {atom, _, Field}}, T) ->
    case record(Name, T#tg.st) of
        {not_found, _} ->
            throw({ms_error, Anno, {base(?ERR_GENBADREC, T), Name}});
        {Fields, St1} ->
            case lists:keyfind(Field, 1, Fields) of
                {Field, Position, _} ->
                    {MsObject, St2} = tg(Object, T#tg{st = St1}),
                    {
                        {tuple, Anno, [{atom, Anno, element}, {integer, Anno, Position}, MsObject]},
                        St2
                    };
                false ->
                    throw({ms_error, Anno, {base(?ERR_GENBADFIELD, T), Name, Field}})
            end
    end;
tg({record, Anno, Name, Fields}, T) ->
    body_record(Anno, Name, Fields, T);
tg({record_index, Anno, Name, {atom, FieldAnno, Field}}, T) ->
    case record(Name, T#tg.st) of
        {not_found, _} ->
            throw({ms_error, Anno, {base(?ERR_GENBADREC, T), Name}});
        {Fields, St1} ->
            case lists:keyfind(Field, 1, Fields) of
                {Field, Position, _} -> {{integer, FieldAnno, Position}, St1};
                false -> throw({ms_error, FieldAnno, {base(?ERR_GENBADFIELD, T), Name, Field}})
            end
    end;
tg({record, Anno, {var, VarAnno, _} = Var, Name, Fields}, T) ->
    record_update(Anno, VarAnno, Var, Name, Fields, T);
tg({bin_element, _, {var, Anno, Name}, _, _} = Element, T) ->
    case lists:keymember(Name, 1, T#tg.names) of
        %% A variable from around the fun.
        false -> {Element, T#tg.st};
        true -> throw({ms_error, Anno, {base(?ERR_GENBINCONSTRUCT, T), Name}})
    end;
tg(default, T) ->
    {default, T#tg.st};
tg({bin_element, Anno, Value, Size, Types}, T) ->
    {Value1, St1} = tg(Value, T),
    {Size1, St2} = tg(Size, T#tg{st = St1}),
    {{bin_element, Anno, Value1, Size1, Types}, St2};
tg({bin, Anno, Elements}, T) ->
    {Elements1, St1} = tg_each(Elements, T),
    {{bin, Anno, Elements1}, St1};
tg({map_field_assoc, Anno, Key, Value}, T) ->
    {Key1, St1} = tg(Key, T),
    {Value1, St2} = tg(Value, T#tg{st = St1}),
    {{map_field_assoc, Anno, Key1, Value1}, St2};
tg({map, Anno, Fields}, T) ->
    {Fields1, St1} = tg_each(Fields, T),
    {{map, Anno, Fields1}, St1};
tg(Tuple, T) when is_tuple(Tuple), tuple_size(Tuple) >= 2 ->
    throw(
        {ms_error, element(2, Tuple), {base(?ERR_GENELEMENT, T), element_name(element(1, Tuple))}}
    );
tg(Other, T) ->
    throw(
        {ms_error, erl_anno:new(0), {
            base(?ERR_GENELEMENT, T), io_lib:format("unknown element ~tw", [Other])
        }}
    ).

%% erlang:F(...) of an operator is the operator, of anything else the local call.
erlang_call(Anno, NameAnno, Name, Arguments, T) ->
    case operator(Name, length(Arguments)) of
        true -> tg(list_to_tuple([op, NameAnno, Name | Arguments]), T);
        false -> tg({call, Anno, {atom, NameAnno, Name}, Arguments}, T)
    end.

%% {Lead..., Translated arguments...} as an abstract tuple.
tg_tuple(Anno, Lead, Arguments, T) ->
    {Translated, St1} = tg_each(Arguments, T),
    {{tuple, Anno, Lead ++ Translated}, St1}.

tg_each(Exprs, T) ->
    lists:mapfoldl(fun(Expr, St) -> tg(Expr, T#tg{st = St}) end, T#tg.st, Exprs).

%% A guard error code, or its body counterpart.
base(Code, #tg{part = guard}) -> Code;
base(Code, #tg{part = body}) -> Code + ?BODY_BASE.

%% A record construction as its tuple: unnamed fields are the `_ = Value` field's value or the record's defaults.
body_record(Anno, Name, Fields, T) ->
    {Given, St1} = lists:foldl(
        fun
            ({record_field, _, {atom, _, Field}, Value}, {Acc, St0}) ->
                {Value1, StV} = tg(Value, T#tg{st = St0}),
                {[{Field, Value1} | Acc], StV};
            ({record_field, _, {var, _, '_'}, Value}, {Acc, St0}) ->
                {Value1, StV} = tg(Value, T#tg{st = St0}),
                {[{{default}, Value1} | Acc], StV};
            (_, _) ->
                throw({ms_error, Anno, {base(?ERR_GENBADREC, T), Name}})
        end,
        {[], T#tg.st},
        Fields
    ),
    {Default, Named} = split_default(Given, none),
    lists:keymember({default}, 1, Named) andalso
        throw({ms_error, Anno, {base(?ERR_GENMULTIFIELD, T), Name, '_'}}),
    case record(Name, St1) of
        {not_found, _} ->
            throw({ms_error, Anno, {base(?ERR_GENBADREC, T), Name}});
        {Definition, St2} ->
            Values = lists:foldl(
                fun({Field, _, FieldDefault}, Acc) ->
                    Fallback =
                        case Default of
                            none -> FieldDefault;
                            _ -> Default
                        end,
                    [field_value(Field, Named, Fallback) | Acc]
                end,
                [],
                Definition
            ),
            check_record(Name, Anno, Named, Definition, T),
            {{tuple, Anno, [{tuple, Anno, [{atom, Anno, Name} | Values]}]}, St2}
    end.

%% Var#name{...}: the named fields' values and element/2 of the record for the others.
record_update(Anno, VarAnno, Var, Name, Fields, T) ->
    {MsVar, St0} = tg(Var, T),
    {Named, St1} = lists:foldl(
        fun
            ({record_field, _, {atom, _, Field}, Value}, {Acc, StA}) ->
                {Value1, StV} = tg(Value, T#tg{st = StA}),
                {[{Field, Value1} | Acc], StV};
            (_, _) ->
                throw({ms_error, Anno, ?ERR_HEADBADREC})
        end,
        {[], St0},
        Fields
    ),
    case record(Name, St1) of
        {not_found, _} ->
            throw({ms_error, Anno, {base(?ERR_GENBADREC, T), Name}});
        {Definition, St2} ->
            Values = lists:foldl(
                fun({Field, Position, _}, Acc) ->
                    Old =
                        {tuple, VarAnno, [
                            {atom, VarAnno, element}, {integer, VarAnno, Position}, MsVar
                        ]},
                    [field_value(Field, Named, Old) | Acc]
                end,
                [],
                Definition
            ),
            check_record(Name, Anno, Named, Definition, T),
            {{tuple, Anno, [{tuple, Anno, [{atom, Anno, Name} | Values]}]}, St2}
    end.

check_record(Name, Anno, Named, Definition, T) ->
    check_fields(
        Name, Anno, Named, Definition, base(?ERR_GENMULTIFIELD, T), base(?ERR_GENBADFIELD, T)
    ).

%% The value of an operator expression over constants, as an integer, float or atom literal; the expression itself
%% when it is not constant or evaluates to something else.
fold_constant(Expr) ->
    Anno = element(2, Expr),
    try constant(Expr) of
        Value when is_integer(Value) -> {integer, Anno, Value};
        Value when is_float(Value) -> {float, Anno, Value};
        Value when is_atom(Value) -> {atom, Anno, Value};
        _ -> Expr
    catch
        error:_ -> Expr
    end.

constant({op, _, Operator, Left, Right}) ->
    apply(erlang, Operator, [constant(Left), constant(Right)]);
constant({op, _, Operator, Operand}) ->
    apply(erlang, Operator, [constant(Operand)]);
constant({integer, _, Value}) ->
    Value;
constant({char, _, Value}) ->
    Value;
constant({float, _, Value}) ->
    Value;
constant({atom, _, Value}) ->
    Value;
constant({tuple, _, Elements}) ->
    list_to_tuple([constant(Element) || Element <- Elements]);
constant({nil, _}) ->
    [];
constant({cons, _, Head, Tail}) ->
    [constant(Head) | constant(Tail)].

element_name(lc) -> "list comprehension";
element_name(bc) -> "binary comprehension";
element_name(block) -> "begin/end block";
element_name('if') -> "if";
element_name('case') -> "case";
element_name('receive') -> "receive";
element_name('try') -> "try";
element_name('catch') -> "catch";
element_name(match) -> "match (=)";
element_name(remote) -> "external function call";
element_name(Element) -> atom_to_list(Element).

type_test(is_atom, 1) -> true;
type_test(is_float, 1) -> true;
type_test(is_integer, 1) -> true;
type_test(is_list, 1) -> true;
type_test(is_number, 1) -> true;
type_test(is_pid, 1) -> true;
type_test(is_port, 1) -> true;
type_test(is_reference, 1) -> true;
type_test(is_tuple, 1) -> true;
type_test(is_map, 1) -> true;
type_test(is_map_key, 2) -> true;
type_test(is_binary, 1) -> true;
type_test(is_function, 1) -> true;
type_test(is_record, 2) -> true;
type_test(is_seq_trace, 0) -> true;
type_test(_, _) -> false.

guard_function(Name, Arity) ->
    lists:member({Name, Arity}, [
        {abs, 1},
        {element, 2},
        {hd, 1},
        {length, 1},
        {max, 2},
        {min, 2},
        {node, 0},
        {node, 1},
        {round, 1},
        {size, 1},
        {bit_size, 1},
        {byte_size, 1},
        {map_size, 1},
        {map_get, 2},
        {binary_part, 2},
        {binary_part, 3},
        {tl, 1},
        {trunc, 1},
        {self, 0},
        {float, 1}
    ]).

action_function(Name, Arity) ->
    lists:member({Name, Arity}, [
        {set_seq_token, 2},
        {get_seq_token, 0},
        {message, 1},
        {return_trace, 0},
        {exception_trace, 0},
        {process_dump, 0},
        {enable_trace, 1},
        {enable_trace, 2},
        {disable_trace, 1},
        {disable_trace, 2},
        {display, 1},
        {caller, 0},
        {set_tcw, 1},
        {silent, 1},
        {trace, 2},
        {trace, 3},
        {caller_line, 0},
        {current_stacktrace, 0},
        {current_stacktrace, 1}
    ]).

operator(Name, 2) ->
    lists:member(Name, [
        'and',
        'or',
        'xor',
        'andalso',
        'orelse',
        '+',
        '-',
        '*',
        '/',
        'div',
        'rem',
        'band',
        'bor',
        'bxor',
        'bsl',
        'bsr',
        '>',
        '>=',
        '<',
        '=<',
        '==',
        '=:=',
        '/=',
        '=/='
    ]);
operator(Name, 1) ->
    lists:member(Name, ['not', '+', '-', 'bnot']);
operator(_, _) ->
    false.

%% The erlang functions a match specification can call by their remote name.
from_erlang(Name, Arity) ->
    guard_function(Name, Arity) orelse type_test(Name, Arity) orelse operator(Name, Arity).

%% The local calls a guard or body can make; get_tcw/0 is a pseudo guard function.
ms_function(Name, Arity, Part) ->
    guard_function(Name, Arity) orelse {Name, Arity} =:= {get_tcw, 0} orelse type_test(Name, Arity) orelse
        (Part =:= body andalso action_function(Name, Arity)).

%% Replace the imported variables of a shell fun's specification by their values; an unknown one is an error.
bind_imports(List, Bindings) when is_list(List) ->
    [bind_imports(Element, Bindings) || Element <- List];
bind_imports({var, Anno, Name}, Bindings) ->
    case lists:keyfind(Name, 1, Bindings) of
        {Name, Value} -> {frozen, Anno, Value};
        false -> throw({ms_error, Anno, {?ERR_UNBOUND_VARIABLE, atom_to_list(Name)}})
    end;
bind_imports(Tuple, Bindings) when is_tuple(Tuple) ->
    list_to_tuple(bind_imports(tuple_to_list(Tuple), Bindings));
bind_imports(Other, _) ->
    Other.

%% The term an abstract match specification stands for.
normalise({frozen, _, Term}) ->
    Term;
normalise({char, _, Char}) ->
    Char;
normalise({integer, _, Integer}) ->
    Integer;
normalise({float, _, Float}) ->
    Float;
normalise({atom, _, Atom}) ->
    Atom;
normalise({string, _, String}) ->
    String;
normalise({nil, _}) ->
    [];
normalise({bin, _, Elements}) ->
    <<<<(segment(Element))/bitstring>> || Element <- Elements>>;
normalise({cons, _, Head, Tail}) ->
    [normalise(Head) | normalise(Tail)];
normalise({op, _, '++', Left, Right}) ->
    normalise(Left) ++ normalise(Right);
normalise({tuple, _, Elements}) ->
    list_to_tuple([normalise(Element) || Element <- Elements]);
normalise({map, _, Fields}) ->
    maps:from_list([normalise_field(Field) || Field <- Fields]);
normalise({op, _, '+', {Kind, _, Value}}) when Kind =:= char; Kind =:= integer; Kind =:= float ->
    Value;
normalise({op, _, '-', {Kind, _, Value}}) when Kind =:= char; Kind =:= integer; Kind =:= float ->
    -Value.

normalise_field({map_field_assoc, _, Key, Value}) -> {normalise(Key), normalise(Value)}.

%% One constant bit syntax segment; a string value gives one segment per character.
segment({bin_element, Anno, {string, _, String}, Size, Types}) ->
    <<
        <<(segment({bin_element, Anno, {integer, Anno, Char}, Size, Types}))/bitstring>>
     || Char <- String
    >>;
segment({bin_element, _, Value, Size, Types}) ->
    {Type, Unit, Endian} = segment_types(Types),
    Bits =
        case Size of
            default -> default;
            _ -> normalise(Size) * Unit
        end,
    build(normalise(Value), Type, Bits, Endian).

%% The type, unit and endianness of a segment's type list, with their defaults.
segment_types(default) ->
    segment_types([]);
segment_types(Specifiers) ->
    Type = last_of(
        [integer, float, binary, bytes, bitstring, bits, utf8, utf16, utf32], Specifiers, integer
    ),
    Endian = last_of([big, little, native], Specifiers, big),
    DefaultUnit =
        case Type of
            binary -> 8;
            bytes -> 8;
            _ -> 1
        end,
    Unit = lists:foldl(
        fun
            ({unit, U}, _) -> U;
            (_, Acc) -> Acc
        end,
        DefaultUnit,
        Specifiers
    ),
    {Type, Unit, Endian}.

last_of(Choices, Specifiers, Default) ->
    lists:foldl(
        fun(Specifier, Acc) ->
            case lists:member(Specifier, Choices) of
                true -> Specifier;
                false -> Acc
            end
        end,
        Default,
        Specifiers
    ).

build(Value, integer, default, Endian) -> build(Value, integer, 8, Endian);
build(Value, integer, Bits, big) -> <<Value:Bits/big>>;
build(Value, integer, Bits, little) -> <<Value:Bits/little>>;
build(Value, integer, Bits, native) -> <<Value:Bits/native>>;
build(Value, float, default, Endian) -> build(Value, float, 64, Endian);
build(Value, float, Bits, big) -> <<Value:Bits/float-big>>;
build(Value, float, Bits, little) -> <<Value:Bits/float-little>>;
build(Value, float, Bits, native) -> <<Value:Bits/float-native>>;
build(Value, utf8, _, _) -> <<Value/utf8>>;
build(Value, utf16, _, little) -> <<Value/utf16-little>>;
build(Value, utf16, _, native) -> <<Value/utf16-native>>;
build(Value, utf16, _, big) -> <<Value/utf16>>;
build(Value, utf32, _, little) -> <<Value/utf32-little>>;
build(Value, utf32, _, native) -> <<Value/utf32-native>>;
build(Value, utf32, _, big) -> <<Value/utf32>>;
build(Value, _, default, _) when is_bitstring(Value) -> Value;
build(Value, _, Bits, _) -> <<Value:Bits/bitstring>>.
