%% Clause's compile (docs/libraries/library.md#runtime-module-replacements): the functions of OTP's compile that need
%% no compiler. Error texts, ERL_COMPILER_OPTIONS and whether options produce a .beam file answer as OTP. Compiling
%% (file, forms and their noenv variants) is not implemented yet (plan 21 LIB77E); the internal pass functions are
%% declined. Added for the transform host (plan 21 LIB77A), which formats transform errors with format_error/1.
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

%% The options as the compiler reads them: makedep_side_effect cancels makedep, a few options stand for others.
expand(Options0) ->
    Options =
        case proplists:is_defined(makedep_side_effect, Options0) of
            true -> proplists:delete(makedep, Options0);
            false -> Options0
        end,
    lists:foldr(fun expand/2, [], Options).

expand(basic_validation, Options) -> [no_code_generation, to_pp, binary | Options];
expand(strong_validation, Options) -> [no_code_generation, to_kernel, binary | Options];
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
    io_lib:format("Module name '~s' does not match file name '~ts'", [Module, File]).

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

%% Compiling needs a compiler in the program (plan 21 LIB77E).
file(_) -> erlang:error({not_implemented, file, 1}).
file(_, _) -> erlang:error({not_implemented, file, 2}).
forms(_) -> erlang:error({not_implemented, forms, 1}).
forms(_, _) -> erlang:error({not_implemented, forms, 2}).
noenv_file(_, _) -> erlang:error({not_implemented, noenv_file, 2}).
noenv_forms(_, _) -> erlang:error({not_implemented, noenv_forms, 2}).

%% OTP's compiler passes and their options; declined.
compile(_, _, _) -> erlang:error({not_implemented, compile, 3}).
compile_abstr(_, _, _) -> erlang:error({not_implemented, compile_abstr, 3}).
compile_asm(_, _, _) -> erlang:error({not_implemented, compile_asm, 3}).
compile_core(_, _, _) -> erlang:error({not_implemented, compile_core, 3}).
options() -> erlang:error({not_implemented, options, 0}).
run_sub_passes(_, _) -> erlang:error({not_implemented, run_sub_passes, 2}).
