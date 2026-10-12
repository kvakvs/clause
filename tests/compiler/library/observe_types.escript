#!/usr/bin/env escript
%% Observe one module's exported types on the host OTP for Clause's library (docs/library.md#types): the exported
%% types and the local types and records they use, as OTP defines them (the beam's debug info, or the shipped source
%% of a preloaded module). Writes library/<App>/<Module>.types.hrl, which the Clause module includes; nothing when
%% the module exports no type. Run explicitly:
%%   escript observe_types.escript <library directory> <application> <module>
%%   escript observe_types.escript --check <library directory> <application> <module>
%% --check compares the forms of the existing header with a fresh observation (the freshness test).
%% Added for plan step 41K1.
-mode(compile).

main(["--check", Library, App, Module]) ->
    File = header(Library, App, Module),
    Observed = observe(list_to_atom(Module)),
    case {filelib:is_regular(File), Observed} of
        {false, []} -> io:format("~s: no exported types~n", [Module]);
        {false, _} -> fail("~s is missing", [File]);
        {true, _} -> compare(File, Observed)
    end;
main([Library, App, Module]) ->
    File = header(Library, App, Module),
    case observe(list_to_atom(Module)) of
        [] ->
            io:format("~s: no exported types~n", [Module]);
        Forms ->
            Header = io_lib:format(
                "%% OTP ~s ~s types: the exported types and the local types and records they use, as OTP defines\n"
                "%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).\n",
                [erlang:system_info(otp_release), Module]
            ),
            ok = file:write_file(File, [Header, [erl_pp:form(F) || F <- Forms]]),
            io:format("~s: ~p forms~n", [File, length(Forms)])
    end;
main(_) ->
    io:format(
        standard_error,
        "usage: observe_types.escript [--check] <library> <application> <module>~n",
        []
    ),
    halt(2).

header(Library, App, Module) -> filename:join([Library, App, Module ++ ".types.hrl"]).

% The -export_type attribute and the type and record forms it needs, in source order; [] without exported types.
observe(Module) ->
    Forms = forms(Module),
    Exported = lists:append([Names || {attribute, _, export_type, Names} <- Forms]),
    Types = maps:from_list([
        {{N, length(Vs)}, F}
     || {attribute, _, K, {N, _, Vs}} = F <- Forms, type_kind(K)
    ]),
    Records = maps:from_list([{N, F} || {attribute, _, record, {N, _}} = F <- Forms]),
    case Exported of
        [] ->
            [];
        _ ->
            {Needed, NeededRecords} = closure(Exported, Types, Records, #{}, #{}),
            Kept = [
                F
             || F <- Forms,
                kept(F, Needed, NeededRecords)
            ],
            [
                {attribute, 0, export_type, lists:usort(Exported)}
                | [erl_parse:map_anno(fun(_) -> 0 end, F) || F <- Kept]
            ]
    end.

type_kind(K) -> lists:member(K, [type, opaque, nominal]).

kept({attribute, _, K, {N, _, Vs}}, Needed, _) ->
    type_kind(K) andalso maps:is_key({N, length(Vs)}, Needed);
kept({attribute, _, record, {N, _}}, _, NeededRecords) ->
    maps:is_key(N, NeededRecords);
kept(_, _, _) ->
    false.

% Every local type and record reachable from the work list.
closure([], _, _, Needed, Records) ->
    {Needed, Records};
closure([Key | Rest], Types, AllRecords, Needed, Records) when is_map_key(Key, Needed) ->
    closure(Rest, Types, AllRecords, Needed, Records);
closure([Key | Rest], Types, AllRecords, Needed, Records) ->
    case maps:find(Key, Types) of
        {ok, {attribute, _, _, {_, Definition, _}}} ->
            {More, RecordNames} = references(Definition),
            {Fields, Records1} = records(RecordNames, AllRecords, Records),
            closure(More ++ Fields ++ Rest, Types, AllRecords, Needed#{Key => true}, Records1);
        error ->
            closure(Rest, Types, AllRecords, Needed, Records)
    end.

% The local types used by the fields of newly needed records, and the records now needed.
records(Names, AllRecords, Records) ->
    New = [N || N <- lists:usort(Names), not is_map_key(N, Records), is_map_key(N, AllRecords)],
    Fields = lists:append([element(1, references(maps:get(N, AllRecords))) || N <- New]),
    {Fields, maps:merge(Records, maps:from_list([{N, true} || N <- New]))}.

% Local user types {Name, Arity} and record names a term mentions.
references({user_type, _, Name, Args}) ->
    {Types, Records} = references(Args),
    {[{Name, length(Args)} | Types], Records};
references({type, _, record, [{atom, _, Name} | Fields]}) ->
    {Types, Records} = references(Fields),
    {Types, [Name | Records]};
references(Term) when is_tuple(Term) -> references(tuple_to_list(Term));
references(Terms) when is_list(Terms) ->
    lists:foldl(
        fun(T, {Ts, Rs}) ->
            {T1, R1} = references(T),
            {T1 ++ Ts, R1 ++ Rs}
        end,
        {[], []},
        Terms
    );
references(_) ->
    {[], []}.

% The module's abstract forms: the beam's debug info, or the shipped source of a preloaded module.
forms(Module) ->
    case code:ensure_loaded(Module) of
        {module, Module} -> ok;
        {error, Reason} -> fail("cannot load ~p: ~p", [Module, Reason])
    end,
    case debug_info(Module) of
        {ok, Forms} -> Forms;
        error -> source_forms(Module)
    end.

debug_info(Module) ->
    case beam_lib:chunks(code:which(Module), [debug_info]) of
        {ok, {_, [{debug_info, {debug_info_v1, Backend, Data}}]}} ->
            case Backend:debug_info(erlang_v1, Module, Data, []) of
                {ok, Forms} -> {ok, Forms};
                _ -> error
            end;
        _ ->
            error
    end.

source_forms(Module) ->
    Directory = filename:join(code:lib_dir(erts), "src"),
    Source = filename:join(Directory, atom_to_list(Module) ++ ".erl"),
    case
        epp:parse_file(Source, [
            {includes, [Directory, filename:join(code:lib_dir(erts), "include")]}
        ])
    of
        {ok, Forms} -> Forms;
        {error, Reason} -> fail("cannot parse ~s: ~p", [Source, Reason])
    end.

% The existing header's forms equal the observation (annotations ignored).
compare(File, Observed) ->
    {ok, Read} = epp:parse_file(File, []),
    Forms = [erl_parse:map_anno(fun(_) -> 0 end, F) || F <- Read, element(1, F) =/= eof, not file_attribute(F)],
    case Forms =:= Observed of
        true -> io:format("~s: fresh~n", [File]);
        false -> fail("~s differs from the host OTP; rerun observe_types.escript", [File])
    end.

file_attribute({attribute, _, file, _}) -> true;
file_attribute(_) -> false.

fail(Format, Arguments) ->
    io:format(standard_error, "observe_types: " ++ Format ++ "~n", Arguments),
    halt(1).
