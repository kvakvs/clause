-module(clause_shell_records).
-export([
    initiate/2,
    add/3,
    read_and_add/5,
    used/2,
    bindings/2,
    expand/2,
    definitions/2,
    print_definitions/1,
    list/1
]).

% The shell's record definitions (plan 21 step LIB74, docs/libraries/library.md#interactive-shell): an ETS table of
% {Name, RecordAttribute} (native records under {shell_default, Name}) filled by rd/2 and rr/1,2,3, used to check,
% expand and print the records of commands.

%% The records of shell_default and user_default. Clause's linked modules keep no abstract code, so there are none.
initiate(_Bs, _RT) ->
    [].

%% Read the records of files (wildcards allowed) and add the selected ones ('_' for all): their names, or an error.
read_and_add(File, Selected, Options, Bs, RT) ->
    case read(File, Selected, Options) of
        Attributes when is_list(Attributes) -> add(Attributes, Bs, RT);
        Error -> Error
    end.

read(File, Selected, Options) ->
    case read_records(File, listify(Options)) of
        Error when is_tuple(Error) ->
            Error;
        Attributes when Selected =:= '_' ->
            Attributes;
        Attributes ->
            Names = listify(Selected),
            [A || {attribute, _, _, {Name, _}} = A <- Attributes, lists:member(Name, Names)]
    end.

% A module's records come from its object code, which linked Clause modules do not keep.
read_records(Module, _Options) when is_atom(Module) ->
    {error, nofile};
read_records(File, Options0) ->
    Options = lists:delete(report_warnings, Options0),
    case catch filelib:wildcard(File) of
        {'EXIT', _} -> {error, invalid_filename};
        [One] -> file_records(One, Options);
        Files -> lists:flatmap(fun(F) -> listed(file_records(F, Options)) end, Files)
    end.

listed(Attributes) when is_list(Attributes) -> Attributes;
listed(_) -> [].

file_records(File, Options) ->
    case filename:extension(File) of
        ".beam" -> beam_records(File);
        _ -> parse_file(File, Options)
    end.

% The records of a BEAM file's abstract code; without one OTP looks for the source, which is not done here.
beam_records(File) ->
    case beam_lib:chunks(File, [abstract_code]) of
        {ok, {_, [{abstract_code, {_, Forms}}]}} -> [A || {attribute, _, record, _} = A <- Forms];
        {ok, {_, [{abstract_code, no_abstract_code}]}} -> {error, nofile};
        Error -> Error
    end.

% The record attributes of a source file, preprocessed with the include directories and macros of the options.
parse_file(File, Options) ->
    Path = [".", filename:dirname(File) | [P || {i, P} <- Options, is_list(P)]],
    Defines = [define(D) || D <- Options, is_define(D)],
    case epp:parse_file(File, Path, Defines) of
        {ok, Forms} -> [A || {attribute, _, record, _} = A <- Forms];
        Error -> Error
    end.

is_define({d, _, _}) -> true;
is_define({d, _}) -> true;
is_define(_) -> false.

define({d, M, V}) -> {M, V};
define({d, M}) -> M.

%% Add record attributes after checking them; their names.
add(Attributes, Bs0, RT) ->
    Records =
        [{Name, D} || {attribute, _, record, {Name, _}} = D <- Attributes] ++
            [{{shell_default, Name}, D} || {attribute, _, native_record, {Name, _}} = D <- Attributes],
    case erl_eval:check_command([], bindings(Records, Bs0)) of
        {error, {_, M, Error}} ->
            exit(lists:flatten(io_lib:fwrite(<<"~ts">>, [M:format_error(Error)])));
        ok ->
            true = ets:insert(RT, Records),
            lists:usort([Name || {Name, _} <- Records])
    end.

%% The definitions an expression uses, with the ones they use, those used coming first.
used(E, RT) ->
    case used_names(E, [], RT, []) of
        [] ->
            [];
        Names ->
            Numbered = lists:zip(Names, lists:seq(1, length(Names))),
            definitions(RT, [R || {R, _} <- lists:keysort(2, lists:ukeysort(1, Numbered))])
    end.

used_names(E, Used0, RT, Skip) ->
    case record_use(E) of
        {name, Name, E1} ->
            Used =
                case lists:member(Name, Skip) of
                    true -> Used0;
                    false -> definition_names(Name, Used0, RT, Skip)
                end,
            used_names(E1, Used, RT, Skip);
        {expr, [E1 | Es]} ->
            used_names(Es, used_names(E1, Used0, RT, Skip), RT, Skip);
        _ ->
            Used0
    end.

% A record name and the names its definition uses in turn (a native record's definition first).
definition_names(Name, Used, RT, Skip) ->
    case ets:lookup(RT, {shell_default, Name}) of
        [] ->
            used_names(ets:lookup(RT, Name), [Name | Used], RT, [Name | Skip]);
        Native ->
            Key = {shell_default, Name},
            used_names(Native, [Key | Used], RT, [Key | Skip])
    end.

record_use({record_index, _, Name, F}) -> {name, Name, F};
record_use({record, _, Name, Is}) -> {name, Name, Is};
record_use({record_field, _, R, Name, F}) -> {name, Name, [R | F]};
record_use({record_field, _, Name, F}) -> {name, Name, F};
record_use({record, _, R, Name, Ups}) -> {name, Name, [R | Ups]};
record_use({call, _, {atom, _, record}, [A, {atom, _, Name}]}) -> {name, Name, A};
record_use({call, _, {atom, _, is_record}, [A, {atom, _, Name}]}) -> {name, Name, A};
record_use({call, _, {remote, _, {atom, _, erlang}, {atom, _, is_record}}, [A, {atom, _, Name}]}) -> {name, Name, A};
record_use({call, _, {atom, _, record_info}, [A, {atom, _, Name}]}) -> {name, Name, A};
record_use({call, A, {tuple, _, [M, F]}, As}) -> record_use({call, A, {remote, A, M, F}, As});
record_use({type, _, record, [{atom, _, Name} | Fs]}) -> {name, Name, Fs};
record_use(T) when is_tuple(T) -> {expr, tuple_to_list(T)};
record_use(E) -> {expr, E}.

%% Bindings with the record definitions as erl_eval's {record, Number, Name} entries, in their order.
bindings([], Bs) ->
    Bs;
bindings(Records0, Bs0) ->
    {Records1, _} = lists:mapfoldl(fun({Name, Def}, I) -> {{Name, I, Def}, I + 1} end, 0, Records0),
    Records = lists:keysort(2, lists:ukeysort(1, Records1)),
    lists:foldl(fun({Name, I, Def}, Bs) -> erl_eval:add_binding({record, I, Name}, Def, Bs) end, Bs0, Records).

%% An expression with the records it uses expanded to tuple operations, as erl_expand_records does in a module.
expand([], E) ->
    E;
expand(Used, E0) ->
    A = erl_anno:new(1),
    Forms0 =
        [{attribute, A, module, shell_default}] ++ [Def || {_, Def} <- Used] ++
            [{function, A, foo, 0, [{clause, A, [], [], [prep(E0)]}]}],
    Forms = erl_expand_records:module(Forms0, [strict_record_tests, expand_inits]),
    {function, A, foo, 0, [{clause, A, [], [], [E]}]} = lists:last(Forms),
    prep(E).

% erl_expand_records does not know the history's {value, N, V}: hidden in an atom's annotation meanwhile.
prep({value, _, _} = Value) -> {atom, Value, ok};
prep({atom, {value, _, _} = Value, ok}) -> Value;
prep(T) when is_tuple(T) -> list_to_tuple(prep(tuple_to_list(T)));
prep([E | Es]) -> [prep(E) | prep(Es)];
prep(E) -> E.

%% The table entries of the given names.
definitions(RT, Names) ->
    lists:flatmap(fun(Name) -> ets:lookup(RT, Name) end, Names).

%% The definitions the printer uses for tuples: [{Name, [Field]}].
print_definitions(RT) ->
    [{Name, field_names(Fields)} || {Name, {attribute, _, record, {Name, Fields}}} <- ets:tab2list(RT)].

field_names([{record_field, _, {atom, _, Field}} | Fs]) -> [Field | field_names(Fs)];
field_names([{record_field, _, {atom, _, Field}, _} | Fs]) -> [Field | field_names(Fs)];
field_names([{typed_record_field, Field, _} | Fs]) -> field_names([Field | Fs]);
field_names([]) -> [].

%% rl(): the record definitions as attributes.
list(Records) ->
    Enc =
        case lists:keyfind(encoding, 1, io:getopts()) of
            false -> [{encoding, latin1}];
            E -> [E]
        end,
    lists:foreach(fun({_, Attr}) -> io:fwrite(<<"~ts">>, [erl_pp:attribute(Attr, Enc)]) end, Records).

listify(L) when is_list(L) -> L;
listify(E) -> [E].
