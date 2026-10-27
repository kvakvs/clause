%% compile's file/forms (plan 21 LIB77E1): options, reports with source quotes, returned errors and warnings, parse
%% transforms, listings, .beam files and loading the binaries with code:load_binary/3 and code:load_abs/1. Clause
%% returns interpreter images, so binaries are shown only as `binary`, and the compiler version and vsn not at all.
%% With the argument unsupported (a Clause-only run) it shows the options that need OTP's code generator.
-module(compile_forms).
-export([main/1]).

main(["unsupported"]) ->
    stage("cf_plain"),
    {ok, Plain} = epp:parse_file("cf_plain.erl", []),
    show(assembly, compile:forms(Plain, ['S', return_errors])),
    show(core_transform, compile:forms(Plain, [{core_transform, cf_core}, return_errors])),
    show(from_core, compile:file("cf_plain", [from_core, return_errors])),
    show(reported, compile:forms(Plain, [to_core, report_errors])),
    show(listing_first, element(2, compile:forms(Plain, ['P', 'S']))),
    io:format("compile_forms: ok~n");
main(_) ->
    lists:foreach(fun stage/1, ["cf_plain", "cf_bad", "cf_lint", "cf_transform", "cf_user"]),
    {ok, Plain} = epp:parse_file("cf_plain.erl", []),
    forms(Plain),
    loading(Plain),
    failures(Plain),
    transforms(),
    listings(Plain),
    files(),
    environment(Plain),
    io:format("compile_forms: ok~n").

%% Each source as Name.erl in the working directory.
stage(Name) -> {ok, _} = file:copy(Name ++ ".erl.in", Name ++ ".erl").

forms(Plain) ->
    show(default, shape(compile:forms(Plain))),
    show(return, shape(compile:forms(Plain, [return]))),
    show(atom_option, shape(compile:forms(Plain, return_warnings))),
    show(noenv, shape(compile:noenv_forms(Plain, [return_warnings]))),
    show(silent, shape(compile:forms(Plain, []))),
    show(line_locations, shape(compile:forms(Plain, [return, {error_location, line}]))),
    show(no_module, shape(compile:forms(tl(tl(Plain)), [return]))),
    show(source_option, shape(compile:forms(Plain, [return, {source, "elsewhere.erl"}]))).

loading(Plain) ->
    {ok, cf_plain, Binary} = compile:forms(Plain, [debug_info]),
    Module = list_to_atom("cf_plain"),
    show(load_binary, code:load_binary(cf_plain, "cf_plain.erl", Binary)),
    show(calls, [Module:double(21), Module:make(3)]),
    show(which, code:which(cf_plain)),
    show(exports, lists:sort(Module:module_info(exports))),
    show(attribute, proplists:get_value(vsn_note, Module:module_info(attributes))),
    show(compile_options, proplists:get_value(options, Module:module_info(compile))),
    {ok, {cf_plain, [{abstract_code, {raw_abstract_v1, Forms}}]}} = beam_lib:chunks(Binary, [abstract_code]),
    show(abstract_code, Forms =:= Plain),
    {ok, {cf_plain, [{debug_info, {debug_info_v1, Backend, {_, Recorded}}}]}} = beam_lib:chunks(Binary, [debug_info]),
    show(debug_info, {Backend, Recorded}),
    {ok, cf_plain, Bare} = compile:forms(Plain, []),
    show(no_debug_info, beam_lib:chunks(Bare, [abstract_code])),
    show(other_module, code:load_binary(cf_other, "cf_other.erl", Binary)).

failures(Plain) ->
    show(warnings_as_errors, shape(compile:forms(Plain, [warnings_as_errors, report]))),
    show(warnings_as_errors_return, shape(compile:forms(Plain, [warnings_as_errors, return]))),
    show(obsolete, shape(compile:forms(Plain, [r24, return]))),
    show(undefined_transform, shape(compile:forms(Plain, [{parse_transform, cf_missing}, return]))),
    show(syntax_error, compile:file("cf_bad")),
    show(syntax_error_return, compile:file("cf_bad", [return])),
    show(lint_errors, compile:file("cf_lint", [report, binary])),
    show(lint_errors_brief, compile:file("cf_lint", [report, brief])),
    show(missing_file, compile:file("cf_none")).

transforms() ->
    {ok, Transform} = epp:parse_file("cf_transform.erl", []),
    {ok, cf_transform, Binary} = compile:forms(Transform),
    show(transform_loaded, code:load_binary(cf_transform, "cf_transform.erl", Binary)),
    show(transformed, shape(compile:file("cf_user", [binary, return]))),
    {ok, cf_user, User} = compile:file("cf_user", [binary]),
    {module, cf_user} = code:load_binary(cf_user, "cf_user.erl", User),
    show(greet, (list_to_atom("cf_user")):greet()),
    {error, [{_, [{none, compile, {parse_transform, cf_transform, {Class, Reason, _}}}]}], []} =
        compile:file("cf_user", [binary, crash_please, return]),
    show(crashed_transform, {Class, Reason}).

listings(Plain) ->
    show(p_listing, shape(compile:forms(Plain, ['P', return]))),
    show(p_file, read("cf_plain.P")),
    show(to_pp, shape(compile:forms(Plain, [to_pp]))),
    show(e_listing, shape(compile:file("cf_plain", ['E', brief]))),
    show(e_file, read("cf_plain.E")),
    show(to_exp, shape(compile:file("cf_plain", [to_exp, binary, brief]))),
    show(dabstr, shape(compile:file("cf_plain", [dabstr, brief]))),
    show(abstr_file, read("cf_plain.abstr")),
    show(basic_validation, shape(compile:file("cf_plain", [basic_validation, return]))).

files() ->
    ok = filelib:ensure_dir("cf_out/"),
    show(beam_file, compile:file("cf_plain", [{outdir, "cf_out"}, return, debug_info])),
    show(beam_written, filelib:is_regular("cf_out/cf_plain.beam")),
    show(load_abs, code:load_abs("cf_out/cf_plain")),
    show(which_abs, filename:basename(code:which(cf_plain))),
    show(call_abs, (list_to_atom("cf_plain")):double(5)),
    {ok, {cf_plain, [{compile_info, Info}]}} = beam_lib:chunks("cf_out/cf_plain.beam", [compile_info]),
    show(compile_info, {proplists:get_value(options, Info), filename:basename(proplists:get_value(source, Info))}),
    {ok, _} = file:copy("cf_plain.erl", "cf_renamed.erl"),
    show(module_name, compile:file("cf_renamed", [{outdir, "cf_out"}, return_errors])),
    show(renamed_written, filelib:is_regular("cf_out/cf_renamed.beam")),
    show(macro_option, shape(compile:file("cf_plain", [binary, {d, 'EXTRA', 1}, return_errors]))),
    show(file_atom, shape(compile:file(cf_plain, [binary]))).

environment(Plain) ->
    true = os:putenv("ERL_COMPILER_OPTIONS", "[warnings_as_errors]"),
    show(env_forms, shape(compile:forms(Plain, [return]))),
    show(env_noenv, shape(compile:noenv_forms(Plain, [return]))),
    true = os:putenv("ERL_COMPILER_OPTIONS", "{bad"),
    show(env_bad, shape(compile:forms(Plain, []))),
    true = os:unsetenv("ERL_COMPILER_OPTIONS").

%% A result with its binary or forms replaced by what they are.
shape({ok, Module, Binary}) when is_binary(Binary) -> {ok, Module, binary};
shape({ok, Module, Binary, Warnings}) when is_binary(Binary) -> {ok, Module, binary, Warnings};
shape({ok, Module, [_ | _] = Forms}) -> {ok, Module, {forms, length(Forms)}};
shape({ok, Module, [_ | _] = Forms, Warnings}) -> {ok, Module, {forms, length(Forms)}, Warnings};
shape(Other) -> Other.

read(File) ->
    {ok, Text} = file:read_file(File),
    Text.

show(Tag, Value) -> io:format("~p: ~tp~n", [Tag, Value]).
