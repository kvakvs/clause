%% Clause's compile (docs/libraries/library.md#runtime-module-replacements). Error texts, ERL_COMPILER_OPTIONS and
%% whether options produce a .beam file answer as OTP (added for the transform host, plan 21 LIB77A). file, forms and
%% their noenv variants (plan 21 LIB77E1) run OTP's front passes (epp, parse transforms, erl_lint, records), listings
%% and reports, and return an interpreter image instead of BEAM code: code:load_binary/3 interprets it with
%% clause_interp. Options that need OTP's code generator fail with clause_unsupported; the internal pass functions
%% are declined.
-module(compile).
-export([
    compile/3,
    compile_abstr/3,
    compile_asm/3,
    compile_core/3,
    env_compiler_options/0,
    file/1,
    file/2,
    format_error/1,
    forms/1,
    forms/2,
    noenv_file/2,
    noenv_forms/2,
    noenv_output_generated/1,
    options/0,
    output_generated/1,
    run_sub_passes/2
]).
-include("compile.types.hrl").

-define(ENVIRONMENT, "ERL_COMPILER_OPTIONS").

%% The options of ERL_COMPILER_OPTIONS: a list as it is, another term in a list, none when unset or unreadable.
env_compiler_options() ->
    case os:getenv(?ENVIRONMENT) of
        false -> [];
        Text -> environment_options(erl_scan:string(Text))
    end.

environment_options({ok, Tokens, _}) ->
    case erl_parse:parse_term(Tokens ++ [{dot, erl_anno:new(1)}]) of
        {ok, Options} when is_list(Options) -> Options;
        {ok, Option} -> [Option];
        {error, _} -> ignored()
    end;
environment_options({error, _, _}) ->
    ignored().

ignored() ->
    io:format("Ignoring bad term in ~s\n", [?ENVIRONMENT]),
    [].

%% Whether compiling with Options writes a .beam file: neither binary nor an option that stops at a listing.
output_generated(Options) ->
    noenv_output_generated(Options ++ env_compiler_options()).

noenv_output_generated(Options0) ->
    Options = expand(Options0),
    not lists:member(binary, Options) andalso
        not lists:any(fun(Flag) -> lists:member(Flag, Options) end, stopping_flags(Options)).

%% The options as the compiler reads them: a debug_info key implies debug_info, makedep_side_effect cancels makedep,
%% a few options stand for others.
expand(Options0) ->
    Options1 =
        case
            proplists:get_value(debug_info_key, Options0) =/= undefined andalso
                not proplists:is_defined(debug_info, Options0)
        of
            true -> [debug_info | Options0];
            false -> Options0
        end,
    Options =
        case proplists:is_defined(makedep_side_effect, Options1) of
            true -> proplists:delete(makedep, Options1);
            false -> Options1
        end,
    lists:foldr(fun expand/2, [], Options).

expand(basic_validation, Options) -> [no_code_generation, to_pp, binary | Options];
expand(strong_validation, Options) -> [no_code_generation, to_kernel, binary | Options];
expand(report, Options) -> [report_errors, report_warnings | Options];
expand(return, Options) -> [return_errors, return_warnings | Options];
expand({debug_info_key, _} = Option, Options) -> [encrypt_debug_info, Option | Options];
expand(Option, Options) -> [Option | Options].

%% The flags that end compilation with a listing (or nothing) before the .beam file is saved, from the input the
%% first from_* option selects.
stopping_flags([from_abstr | _]) -> abstract_flags();
stopping_flags([from_core | _]) -> [dprep | core_flags()];
stopping_flags([from_asm | _]) -> assembly_flags();
stopping_flags([_ | Options]) -> stopping_flags(Options);
stopping_flags([]) -> [makedep, dpp, 'P', to_pp, dabstr, to_abstr | abstract_flags()].

abstract_flags() -> [dcover, dexp, 'E', to_exp, dcore, to_core0 | core_flags()].

core_flags() -> [doldinline, dcorefold, dinline, dcopt, dalias, to_core | kernel_flags()].

kernel_flags() ->
    [dcbsm, dssa, dbool, dssashare, drecv, dssabsm, dssaopt, dthrow, dprecg, dcg, doldcg | assembly_flags()].

assembly_flags() -> [da, dblk, djmp, dclean, dtrim, diffable, dz, dopt, 'S', to_asm].

%% The text of an error descriptor compile reports.
format_error({obsolete_option, Version}) ->
    io_lib:fwrite("the ~p option is no longer supported", [Version]);
format_error(no_crypto) ->
    "this system is not configured with crypto support.";
format_error(bad_crypto_key) ->
    "invalid crypto key.";
format_error(no_crypto_key) ->
    "no crypto key supplied.";
format_error({open, Reason}) ->
    io_lib:format("open error '~ts'", [file:format_error(Reason)]);
format_error({epp, Descriptor}) ->
    epp:format_error(Descriptor);
format_error(write_error) ->
    "error writing file";
format_error({write_error, Reason}) ->
    io_lib:format("error writing file: ~ts", [file:format_error(Reason)]);
format_error({rename, From, To, Reason}) ->
    io_lib:format("failed to rename ~ts to ~ts: ~ts", [From, To, file:format_error(Reason)]);
format_error({parse_transform, Module, {Class, Reason, Stack}}) ->
    io_lib:format("error in parse transform '~ts':\n~ts", [Module, exception(Class, Reason, Stack)]);
format_error({undef_parse_transform, Module}) ->
    io_lib:format("undefined parse transform '~ts'", [Module]);
format_error({core_transform, Module, {Class, Reason, Stack}}) ->
    io_lib:format("error in core transform '~s':\n~ts", [Module, exception(Class, Reason, Stack)]);
format_error({crash, Pass, Reason, Stack}) when is_list(Stack) ->
    io_lib:format("internal error in pass ~p:\n~ts", [Pass, exception(error, Reason, Stack)]);
format_error({bad_return, Pass, Reason}) ->
    io_lib:format("internal error in pass ~p: bad return value:\n~tP", [Pass, Reason, 20]);
format_error({module_name, Module, File}) ->
    io_lib:format("Module name '~s' does not match file name '~ts'", [Module, File]);
format_error({clause_unsupported, Option}) ->
    io_lib:format("the ~tp option needs OTP's code generator, which Clause does not have", [Option]).

%% An exception without the frames of the code that started the compiler.
exception(Class, Reason, Stack) ->
    Starter = fun
        (escript, run, 2) -> true;
        (escript, start, 1) -> true;
        (init, start_it, 1) -> true;
        (init, start_em, 1) -> true;
        (_, _, _) -> false
    end,
    Format = fun(Term, _) -> io_lib:format("~tp", [Term]) end,
    erl_error:format_exception(Class, Reason, Stack, #{stack_trim_fun => Starter, format_fun => Format}).

%% Compiling (plan 21 LIB77E1)

%% One compilation: its files, the module once known, the options in effect and the messages so far.
-record(compile, {
    %% The file argument (forms: the source option) and its directory, searched for includes.
    filename = "",
    dir = "",
    %% The stem of listing and .beam files, the module name for forms once linted.
    base = "",
    %% The source file, which compile's own messages name, and the .beam file written without binary.
    ifile = "",
    ofile = "",
    %% The module, set when the image is built: listings return [] as OTP's do.
    module = [],
    %% The options in effect (the module's compile attributes added), and the caller's that CInf and Dbgi record.
    options = [],
    mod_options = [],
    %% The source encoding epp found; listings name it.
    encoding = none,
    errors = [],
    warnings = [],
    %% The forms debug_info keeps, and chunks for the image (extra_chunks, features).
    abstract_code = none,
    extra_chunks = []
}).

-define(DEFAULT_OPTIONS, [verbose, report_errors, report_warnings]).
%% The version of OTP 29's compiler application that compile information tells.
-define(VERSION, "10.0.7").

file(File) -> file(File, ?DEFAULT_OPTIONS).

file(File, Options) when is_list(Options) -> do_compile({file, File}, env_compiler_options() ++ Options);
file(File, Option) -> file(File, [Option | ?DEFAULT_OPTIONS]).

forms(Forms) -> forms(Forms, ?DEFAULT_OPTIONS).

forms(Forms, Options) when is_list(Options) -> do_compile({forms, Forms}, [binary | Options ++ env_compiler_options()]);
forms(Forms, Option) when is_atom(Option) -> forms(Forms, [Option | ?DEFAULT_OPTIONS]).

noenv_file(File, Options) when is_list(Options) -> do_compile({file, File}, Options);
noenv_file(File, Option) -> noenv_file(File, [Option | ?DEFAULT_OPTIONS]).

noenv_forms(Forms, Options) when is_list(Options) -> do_compile({forms, Forms}, [binary | Options]);
noenv_forms(Forms, Option) when is_atom(Option) -> noenv_forms(Forms, [Option | ?DEFAULT_OPTIONS]).

%% Compile in a process of its own unless asked not to; a crash prints OTP's internal error report.
do_compile(Input, Options0) ->
    Options = expand(Options0),
    Run = fun() -> internal(Input, Options) end,
    case lists:member(no_spawn_compiler_process, Options) of
        true ->
            Run();
        false ->
            {Pid, Ref} = spawn_monitor(fun() -> exit(Run()) end),
            receive
                {'DOWN', Ref, process, Pid, Result} -> Result
            end
    end.

internal(Input, Options) ->
    try
        compile_input(Input, Options)
    catch
        Class:Reason:Stack ->
            io:put_chars(["\n*** Internal compiler error ***\n", exception(Class, Reason, Stack), "\n"]),
            error
    end.

compile_input({forms, Forms0}, Options0) ->
    Options = proplists:delete(source, Options0),
    Forms =
        case with_columns(Options) of
            true -> Forms0;
            false -> strip_columns(Forms0)
        end,
    run(passes(forms, Options), Forms, state(proplists:get_value(source, Options0, ""), "", Options));
compile_input({file, File}, Options) ->
    Suffix =
        case input_option(Options) of
            from_abstr -> ".abstr";
            _ -> ".erl"
        end,
    run(passes(file, Options), none, state(File, Suffix, Options)).

state(File0, Suffix, Options) ->
    File = filename_string(File0),
    Dir = filename:dirname(File),
    Base = filename:basename(File, Suffix),
    Kept = proplists:delete(extra_chunks, Options),
    #compile{
        filename = File,
        dir = Dir,
        base = Base,
        ifile = source_file(Dir, Base, Suffix),
        ofile = output_file(Base, "beam", Kept),
        options = Kept,
        mod_options = Kept,
        extra_chunks = [{chunk_id(Id), Data} || {Id, Data} <- proplists:get_value(extra_chunks, Options, [])]
    }.

filename_string(File) when is_atom(File) -> atom_to_list(File);
filename_string(File) -> File.

chunk_id(Id) when is_binary(Id) -> binary_to_list(Id);
chunk_id(Id) -> Id.

source_file(".", Base, Suffix) -> Base ++ Suffix;
source_file(Dir, Base, Suffix) -> filename:join(Dir, Base ++ Suffix).

output_file(Base, Extension, Options) ->
    Stem =
        case lists:keyfind(outdir, 1, Options) of
            {outdir, Dir} -> filename:join(Dir, Base);
            _ -> Base
        end,
    Stem ++ "." ++ Extension.

%% Running the passes and answering as compile does.

run(Passes, Code, St0) ->
    case run_passes(Passes, Code, St0) of
        {ok, Result, St} -> succeeded(Result, St);
        {error, St} -> failed(St)
    end.

%% Each pass returns {ok, Code, St} or {error, St}; a pass that raises an error fails with OTP's crash descriptor.
run_passes([{Name, Pass} | Passes], Code0, St0) ->
    try Pass(Code0, St0) of
        {ok, Code, St} -> run_passes(Passes, Code, St);
        {error, St} -> {error, St}
    catch
        error:Reason:Stack -> {error, add_error(St0, {crash, Name, Reason, Stack})}
    end;
run_passes([], Code, St) ->
    {ok, Code, St}.

succeeded(Code, #compile{module = Module, options = Options, warnings = Warnings0} = St) ->
    case Warnings0 =/= [] andalso lists:member(warnings_as_errors, Options) of
        true ->
            report_warnings_as_errors(lists:member(report_warnings, Options)),
            failed(St);
        false ->
            Warnings = messages_per_file(Warnings0),
            report_warnings(St#compile{warnings = Warnings}),
            Binary = [Code || lists:member(binary, Options), not lists:member(no_code_generation, Options)],
            Returned = [Warnings || lists:member(return_warnings, Options)],
            list_to_tuple([ok, Module | Binary ++ Returned])
    end.

report_warnings_as_errors(true) -> io:format("~p: warnings being treated as errors\n", [?MODULE]);
report_warnings_as_errors(false) -> ok.

failed(#compile{options = Options} = St) ->
    Warnings = messages_per_file(St#compile.warnings),
    Errors = messages_per_file(St#compile.errors),
    report_errors(St#compile{errors = Errors}),
    report_warnings(St#compile{warnings = Warnings}),
    case lists:member(return_errors, Options) of
        true -> {error, Errors, Warnings};
        false -> error
    end.

add_error(#compile{ifile = File, errors = Errors} = St, Descriptor) ->
    St#compile{errors = Errors ++ [{File, [{none, ?MODULE, Descriptor}]}]}.

%% Messages grouped by file: the scanner's, epp's and the parser's first by location, then the rest in order.
messages_per_file(Groups) ->
    Sorted = lists:sort([{File, Message} || {File, Messages} <- Groups, Message <- Messages]),
    {Early, Rest} = lists:partition(fun early_message/1, Sorted),
    ByLocation = lists:sort(fun({_, {L1, _, _}}, {_, {L2, _, _}}) -> L1 =< L2 end, Early),
    per_file(ByLocation) ++ per_file(Rest).

early_message({_, {_, Module, _}}) -> lists:member(Module, [erl_scan, epp, erl_parse]);
early_message(_) -> false.

per_file(Messages) ->
    [{File, [M || {F, M} <- Messages, F =:= File]} || File <- lists:usort([F || {F, _} <- Messages])].

%% The passes of an input, up to the first listing the options ask for.

passes(Kind, Options) ->
    select(input_passes(Kind, input_option(Options)) ++ code_passes(), Options).

input_option(Options) ->
    case [Option || Option <- Options, lists:member(Option, [from_abstr, from_core, from_asm])] of
        [Input | _] -> Input;
        [] -> source
    end.

%% Passes and listings of each input: OTP's front passes, their listing options and checks.
input_passes(file, source) ->
    [{parse_module, fun parse_module/2} | source_passes()];
input_passes(forms, source) ->
    source_passes();
input_passes(file, from_abstr) ->
    [{consult_abstr, fun consult_abstr/2} | abstract_passes()];
input_passes(forms, from_abstr) ->
    abstract_passes();
input_passes(_, Input) ->
    [{unsupported, [Input]}].

source_passes() ->
    [
        {transform_module, fun transform_module/2},
        {unsupported, [makedep, makedep_side_effect]},
        {listing, dpp, "pp"},
        {lint_module, fun lint_module/2},
        {source_listing, 'P', "P"},
        {done, to_pp, "P"},
        {listing, dabstr, "abstr"},
        {done, to_abstr, "abstr"}
    ].

abstract_passes() ->
    [{unless, no_lint, {lint_module, fun lint_module/2}}].

code_passes() ->
    [
        {remove_doc_attributes, fun remove_doc_attributes/2},
        {compile_directives, fun compile_directives/2},
        {save_abstract_code, fun save_abstract_code/2},
        {unsupported, [dcover]},
        {expand_records, fun expand_records/2},
        {listing, dexp, "expand"},
        {iff, 'E', {legalize_vars, fun legalize_vars/2}},
        {source_listing, 'E', "E"},
        {done, to_exp, "abstr"},
        {unsupported, code_generation_flags()},
        {image, fun image/2},
        {unless, binary, {save_binary, fun save_binary/2}}
    ].

%% The options that need OTP's code generator: its listings, Core Erlang transforms and encrypted debug_info.
code_generation_flags() ->
    [dcore, to_core0, to_kernel, to_dis, encrypt_debug_info, core_transform | core_flags()].

%% Plain passes from the conditional ones; a listing the options ask for ends the passes, as done does (which lists
%% only without binary); a .beam to be saved is removed first.
select(Items, Options) ->
    Passes = select_items(Items, Options),
    case lists:keymember(save_binary, 1, Passes) of
        true -> [{remove_file, fun remove_file/2} | Passes];
        false -> Passes
    end.

select_items([{iff, Flag, Pass} | Items], Options) ->
    [Pass || lists:member(Flag, Options)] ++ select_items(Items, Options);
select_items([{unless, Flag, Pass} | Items], Options) ->
    [Pass || not lists:member(Flag, Options)] ++ select_items(Items, Options);
select_items([{unsupported, Flags} | Items], Options) ->
    case [Option || Option <- Options, unsupported(Option, Flags)] of
        [] -> select_items(Items, Options);
        [Option | _] -> [{unsupported, fun(_, St) -> {error, add_error(St, {clause_unsupported, Option})} end}]
    end;
select_items([{listing, Flag, Extension} | Items], Options) ->
    listing_item(
        lists:member(Flag, Options), {listing, fun(Code, St) -> listing(Extension, Code, St) end}, Items, Options
    );
select_items([{source_listing, Flag, Extension} | Items], Options) ->
    Listing = {listing, fun(Code, St) -> source_listing(Extension, Code, St) end},
    listing_item(lists:member(Flag, Options), Listing, Items, Options);
select_items([{done, Flag, Extension} | Items], Options) ->
    case lists:member(Flag, Options) of
        true -> select_items([{unless, binary, {listing, fun(Code, St) -> listing(Extension, Code, St) end}}], Options);
        false -> select_items(Items, Options)
    end;
select_items([Pass | Items], Options) ->
    [Pass | select_items(Items, Options)];
select_items([], _Options) ->
    [].

listing_item(true, Listing, _Items, _Options) -> [Listing];
listing_item(false, _Listing, Items, Options) -> select_items(Items, Options).

unsupported({core_transform, _}, Flags) -> lists:member(core_transform, Flags);
unsupported(Option, Flags) -> lists:member(Option, Flags).

%% The passes.

remove_file(Code, St) ->
    _ = file:delete(St#compile.ofile),
    {ok, Code, St}.

parse_module(_Code, #compile{ifile = File, options = Options, dir = Dir} = St) ->
    case erl_features:init_parse_state(Options, fun erl_scan:f_reserved_word/1) of
        {ok, {Features, Reserved}} ->
            parsed(epp:parse_file(File, epp_options(Dir, Options, Features, Reserved, St)), St);
        {error, {Module, Reason}} ->
            {error, St#compile{errors = St#compile.errors ++ [{File, [{none, Module, Reason}]}]}}
    end.

epp_options(Dir, Options, Features, Reserved, St) ->
    Start =
        case with_columns(Options) of
            true -> {1, 1};
            false -> 1
        end,
    [
        {includes, [".", Dir | [Path || {i, Path} <- Options, is_list(Path)]]},
        {source_name, source_name(St)},
        {deterministic, lists:member(deterministic, Options)},
        {macros, macros(Options)},
        {default_encoding, utf8},
        {location, Start},
        {reserved_word_fun, Reserved},
        {features, Features},
        extra
    ].

macros([{d, Name, Value} | Options]) -> [{Name, Value} | macros(Options)];
macros([{d, Name} | Options]) -> [Name | macros(Options)];
macros([_ | Options]) -> macros(Options);
macros([]) -> [].

%% The source file name forms and messages carry: the source option, its base name when deterministic.
source_name(#compile{ifile = File, options = Options}) ->
    Source = proplists:get_value(source, Options, File),
    case lists:member(deterministic, Options) of
        true -> filename:basename(Source);
        false -> absolute_source(lists:member(absolute_source, Options), Source)
    end.

absolute_source(true, Source) -> absolute(Source);
absolute_source(false, Source) -> Source.

absolute("") -> "";
absolute(File) -> filename:absname(File).

parsed({ok, Forms0, Extra}, #compile{options = Options} = St) ->
    Features = proplists:get_value(features, Extra, []),
    Forms =
        case with_columns(Options ++ compile_options(Forms0)) of
            true -> Forms0;
            false -> strip_columns(Forms0)
        end,
    St1 = add_features(Features, St),
    {ok, Forms, St1#compile{
        encoding = proplists:get_value(encoding, Extra), options = [{features, Features} | Options]
    }};
parsed({error, Reason}, St) ->
    {error, add_error(St, {epp, Reason})}.

%% The features a module uses go to the Meta chunk, as OTP records them.
add_features([], St) ->
    St;
add_features(Features, #compile{extra_chunks = Chunks} = St) ->
    Meta = term_to_binary([{enabled_features, Features}], [
        deterministic
     || lists:member(deterministic, St#compile.options)
    ]),
    St#compile{extra_chunks = lists:keystore("Meta", 1, Chunks, {"Meta", Meta})}.

consult_abstr(_Code, #compile{ifile = File} = St) ->
    case file:consult(File) of
        {ok, Forms} -> {ok, Forms, St#compile{encoding = epp:read_encoding(File)}};
        {error, Reason} -> {error, add_error(St, {open, Reason})}
    end.

with_columns(Options) -> proplists:get_value(error_location, Options, column) =:= column.

compile_options(Forms) ->
    lists:append([as_list(Option) || {attribute, _, compile, Option} <- Forms]).

as_list(Option) when is_list(Option) -> Option;
as_list(Option) -> [Option].

strip_columns(Forms) ->
    Strip = fun(Anno) -> erl_anno:set_location(erl_anno:line(Anno), Anno) end,
    [strip_form(Form, Strip) || Form <- Forms].

strip_form({eof, {Line, _Column}}, _Strip) ->
    {eof, Line};
strip_form({Kind, {{Line, _Column}, Module, Reason}}, _Strip) when Kind =:= error; Kind =:= warning ->
    {Kind, {Line, Module, Reason}};
strip_form(Form, Strip) ->
    erl_parse:map_anno(Strip, Form).

%% Parse transforms of the options and the module's compile attributes, each once, in order.
transform_module(Code, #compile{options = Options} = St) ->
    case [Module || {parse_transform, Module} <- Options ++ compile_options(Code)] of
        [] -> {ok, Code, St};
        Transforms -> transform(Transforms, without_transforms(Code), St)
    end.

without_transforms(Forms) ->
    [Form || Form0 <- Forms, Form <- without_transform(Form0)].

without_transform({attribute, Anno, compile, Options}) when is_list(Options) ->
    [{attribute, Anno, compile, [Option || Option <- Options, not is_transform(Option)]}];
without_transform({attribute, _, compile, {parse_transform, _}}) ->
    [];
without_transform(Form) ->
    [Form].

is_transform({parse_transform, _}) -> true;
is_transform(_) -> false.

transform([Transform | Transforms], Code, St) ->
    case
        code:ensure_loaded(Transform) =:= {module, Transform} andalso
            erlang:function_exported(Transform, parse_transform, 2)
    of
        true -> transformed(Transform, Transforms, run_transform(Transform, Code, St), St);
        false -> {error, add_error(St, {undef_parse_transform, Transform})}
    end;
transform([], Code, St) ->
    {ok, maybe_strip_columns(Code, ?MODULE, St), St}.

run_transform(Transform, Code, #compile{options = Options} = St) ->
    try
        Transform:parse_transform(maybe_strip_columns(Code, Transform, St), Options)
    catch
        Class:Reason:Stack -> {'$crashed', Class, Reason, Stack}
    end.

transformed(Transform, _Transforms, {'$crashed', Class, Reason, Stack}, St) ->
    {error, add_error(St, {parse_transform, Transform, {Class, Reason, Stack}})};
transformed(_Transform, _Transforms, {error, Errors, Warnings}, St) ->
    {error, St#compile{errors = St#compile.errors ++ Errors, warnings = St#compile.warnings ++ Warnings}};
transformed(_Transform, Transforms, {warning, Forms, Warnings}, St) ->
    transform(Transforms, Forms, St#compile{warnings = St#compile.warnings ++ Warnings});
transformed(_Transform, Transforms, Forms, St) ->
    transform(Transforms, Forms, St).

%% Columns go when the transform or the options ask for line locations.
maybe_strip_columns(Code, Transform, #compile{options = Options}) ->
    Wanted =
        case erlang:function_exported(Transform, parse_transform_info, 0) of
            true -> maps:get(error_location, Transform:parse_transform_info(), column);
            false -> column
        end,
    case Wanted =:= line orelse not with_columns(Options) of
        true -> strip_columns(Code);
        false -> Code
    end.

lint_module(Code, #compile{ifile = File, options = Options} = St) ->
    case erl_lint:module(Code, File, Options) of
        {ok, Warnings} ->
            {ok, Code, (default_base(St, Code))#compile{warnings = St#compile.warnings ++ Warnings}};
        {error, Errors, Warnings} ->
            {error, St#compile{warnings = St#compile.warnings ++ Warnings, errors = St#compile.errors ++ Errors}}
    end.

%% Listings of forms are named after the module.
default_base(#compile{filename = ""} = St, Forms) ->
    case [Module || {attribute, _, module, Module} <- Forms] of
        [Module | _] -> St#compile{base = atom_to_list(Module)};
        [] -> St
    end;
default_base(St, _Forms) ->
    St.

remove_doc_attributes(Code, St) ->
    {ok, [Form || Form <- Code, not is_doc_attribute(Form)], St}.

is_doc_attribute({attribute, _, Name, _}) -> lists:member(Name, [doc, moduledoc, docformat]);
is_doc_attribute(_) -> false.

%% The module's compile attributes join the options; an obsolete one is an error.
compile_directives(Code, #compile{options = Options0} = St) ->
    Options = expand(lists:flatten([Option || {attribute, _, compile, Option} <- Code])) ++ Options0,
    case [Option || Option <- Options, obsolete(Option)] of
        [] -> {ok, Code, St#compile{options = Options}};
        [Obsolete | _] -> {error, add_error(St#compile{options = Options}, {obsolete_option, Obsolete})}
    end.

obsolete(Option) ->
    lists:member(Option, [
        r18,
        r19,
        r20,
        r21,
        r22,
        r23,
        r24,
        r25,
        no_badrecord,
        no_bs_create_bin,
        no_bs_match,
        no_bsm3,
        no_get_hd_tl,
        no_init_yregs,
        no_make_fun3,
        no_min_max_bifs,
        no_put_tuple2,
        no_shared_fun_wrappers,
        no_swap,
        no_utf8_atoms
    ]).

save_abstract_code(Code, #compile{options = Options} = St) ->
    case lists:member(debug_info, Options) of
        true -> {ok, Code, St#compile{abstract_code = erl_parse:anno_to_term(Code)}};
        false -> {ok, Code, St}
    end.

expand_records(Code, #compile{options = Options} = St) ->
    {ok, erl_expand_records:module(Code, Options), St}.

legalize_vars(Code, St) ->
    {ok, [legalized(Form) || Form <- Code], St}.

legalized({function, _, _, _, _} = Function) -> erl_pp:legalize_vars(Function);
legalized(Form) -> Form.

%% The interpreter image of the code, with OTP's CInf, Dbgi and the extra chunks.
image(Code, #compile{ifile = File, options = Options, extra_chunks = Extra} = St) ->
    {DebugInfo, ModuleOptions} = debug_info(St),
    Kept = [Option || Option <- ModuleOptions, affects_code(Option)],
    Chunks = [{"CInf", term_to_binary(compile_info(File, Options, Kept))}, {"Dbgi", DebugInfo} | Extra],
    {ok, Binary} = clause_interp:beam(Code, Chunks),
    {ok, Binary, St#compile{module = hd([Module || {attribute, _, module, Module} <- Code])}}.

debug_info(#compile{mod_options = ModuleOptions, options = Options, abstract_code = Code}) ->
    Recorded = [Option || Option <- ModuleOptions, recorded(Option, lists:member(deterministic, ModuleOptions))],
    {Backend, Metadata, Kept} =
        case proplists:get_value(debug_info, Options, false) of
            {Own, OwnMetadata} when is_atom(Own) ->
                {Own, OwnMetadata, proplists:delete(debug_info, ModuleOptions)};
            true ->
                {erl_abstract_code, {Code, Recorded}, [debug_info | proplists:delete(debug_info, ModuleOptions)]};
            false ->
                {erl_abstract_code, {none, Recorded}, ModuleOptions}
        end,
    Encoding = [compressed | [deterministic || lists:member(deterministic, Options)]],
    {term_to_binary({debug_info_v1, Backend, Metadata}, Encoding), Kept}.

%% The options debug_info records: include paths and the directory only when not deterministic, no input kinds,
%% transforms or macros, nothing that only changes reporting.
recorded({i, _}, Deterministic) -> not Deterministic;
recorded({cwd, _}, Deterministic) -> not Deterministic;
recorded({parse_transform, _}, _) -> false;
recorded({d, _, _}, _) -> false;
recorded(Option, _) -> not lists:member(Option, [from_asm, from_core, from_abstr]) andalso affects_code(Option).

affects_code({cwd, _}) ->
    false;
affects_code({outdir, _}) ->
    false;
affects_code(Option) ->
    not lists:member(Option, [
        beam, report_warnings, report_errors, return_errors, return_warnings, warnings_as_errors, binary, verbose
    ]).

compile_info(File, Options, Kept) ->
    Info0 = proplists:get_value(compile_info, Kept, []),
    Info1 =
        case absolute(File) of
            [_ | _] = Source ->
                [{source, Source} || not lists:member(slim, Options), not lists:member(deterministic, Options)];
            _ ->
                []
        end ++ Info0,
    Info2 =
        case lists:member(deterministic, Options) of
            false -> [{options, proplists:delete(compile_info, Kept)} | Info1];
            true -> Info1
        end,
    [{version, ?VERSION} | Info2].

%% The .beam file, written beside as Name.bea# first, unless warnings fail the compilation; its name must be the
%% module's unless asked otherwise.
save_binary(Binary, #compile{module = Module, ofile = File, options = Options, warnings = Warnings} = St) ->
    Base = filename:rootname(filename:basename(File)),
    Matches = lists:member(no_error_module_mismatch, Options) orelse atom_to_list(Module) =:= Base,
    case Warnings =/= [] andalso lists:member(warnings_as_errors, Options) of
        true ->
            {ok, Binary, St};
        false when Matches -> write_binary(Binary, St);
        false ->
            {error, St#compile{errors = St#compile.errors ++ [{File, [{none, ?MODULE, {module_name, Module, Base}}]}]}}
    end.

write_binary(Binary, #compile{ofile = File, options = Options} = St) ->
    Temporary = lists:droplast(File) ++ "#",
    case file:write_file(Temporary, Binary, [compressed || lists:member(compressed, Options)]) of
        ok ->
            renamed(file:rename(Temporary, File), Temporary, St);
        {error, Reason} ->
            {error, St#compile{errors = St#compile.errors ++ [{Temporary, [{none, ?MODULE, {write_error, Reason}}]}]}}
    end.

renamed(ok, _Temporary, St) ->
    {ok, none, St};
renamed({error, Reason}, Temporary, #compile{ofile = File} = St) ->
    _ = file:delete(Temporary),
    {error, St#compile{errors = St#compile.errors ++ [{File, [{none, ?MODULE, {rename, Temporary, File, Reason}}]}]}}.

%% Listings: forms as terms, or as source text with erl_pp, in Base.Extension beside the output.

listing(Extension, Code, St) ->
    write_listing(Extension, [io_lib:format("~kp.\n", [Form]) || Form <- Code], Code, St#compile{encoding = none}).

source_listing(Extension, Code, St) ->
    Encoding = listing_encoding(St),
    write_listing(Extension, [[erl_pp:form(Form, [{encoding, Encoding}]), "\n"] || Form <- Code], Code, St).

write_listing(Extension, Text, Code, #compile{base = Base, options = Options} = St) ->
    File = output_file(Base, Extension, Options),
    Encoding = listing_encoding(St),
    case file:write_file(File, unicode:characters_to_binary([encoding_line(St) | Text], unicode, Encoding)) of
        ok ->
            {ok, Code, St};
        {error, Reason} ->
            {error, St#compile{errors = St#compile.errors ++ [{File, [{none, ?MODULE, {write_error, Reason}}]}]}}
    end.

listing_encoding(#compile{encoding = none}) -> epp:default_encoding();
listing_encoding(#compile{encoding = Encoding}) -> Encoding.

encoding_line(#compile{encoding = none}) -> [];
encoding_line(#compile{encoding = Encoding}) -> io_lib:format("%% ~s\n", [epp:encoding_to_string(Encoding)]).

%% Reports, as OTP's sys_messages prints them: File:Line:Column: Text, then the source line with a marker under the
%% column unless brief.

report_errors(#compile{options = Options, errors = Errors}) ->
    case lists:member(report_errors, Options) of
        true -> lists:foreach(fun({File, Items}) -> list_errors(file_of(File), Items, Options) end, Errors);
        false -> ok
    end.

report_warnings(#compile{options = Options, warnings = Warnings}) ->
    AsErrors = lists:member(warnings_as_errors, Options),
    Prefix =
        case AsErrors of
            true -> "";
            false -> "Warning: "
        end,
    case lists:member(report_warnings, Options) orelse (AsErrors andalso lists:member(report_errors, Options)) of
        true ->
            Messages = [
                Message
             || {File, Items} <- Warnings, Message <- messages(file_of(File), Prefix, Items, Options)
            ],
            lists:foreach(fun({_, Text}) -> io:put_chars(Text) end, lists:sort(Messages));
        false ->
            ok
    end.

file_of({File, _Line}) -> File;
file_of(File) -> File.

list_errors(File, Items, Options) ->
    lists:foreach(fun({_, Text}) -> io:put_chars(Text) end, messages(File, "", Items, Options)).

messages(File, Prefix, Items, Options) ->
    [message(File, Prefix, Item, Options) || Item <- Items].

message(File, Prefix, {none, Module, Descriptor}, _Options) ->
    {none, io_lib:format("~ts: ~ts~ts\n", [File, Prefix, Module:format_error(Descriptor)])};
message(File, Prefix, {Location, Module, Descriptor}, Options) ->
    Quote = quote_source(File, Location, Options),
    Text = io_lib:format("~ts:~ts: ~ts~ts\n~ts", [
        File, position(Location), Prefix, Module:format_error(Descriptor), Quote
    ]),
    {{File, Location}, Text}.

position({Line, Column}) -> io_lib:format("~w:~w", [Line, Column]);
position(Line) -> io_lib:format("~w", [Line]).

%% The source line of a location with a marker under its column, as a C++ builtin (clause_compile).
quote_source(File, Location, Options) ->
    case not proplists:get_bool(brief, Options) andalso file:read_file(File) of
        {ok, Source} -> clause_compile:quote_source(Source, source_encoding(Source), Location);
        _ -> ""
    end.

source_encoding(Source) ->
    case epp:read_encoding_from_binary(Source) of
        none -> epp:default_encoding();
        Encoding -> Encoding
    end.

%% OTP's compiler passes and their options; declined.
compile(_, _, _) -> erlang:error({not_implemented, compile, 3}).
compile_abstr(_, _, _) -> erlang:error({not_implemented, compile_abstr, 3}).
compile_asm(_, _, _) -> erlang:error({not_implemented, compile_asm, 3}).
compile_core(_, _, _) -> erlang:error({not_implemented, compile_core, 3}).
options() -> erlang:error({not_implemented, options, 0}).
run_sub_passes(_, _) -> erlang:error({not_implemented, run_sub_passes, 2}).
