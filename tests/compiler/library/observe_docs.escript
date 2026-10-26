#!/usr/bin/env escript
%% Observe the documented public functions of OTP's erts, kernel, stdlib, crypto and public_key on the host OTP (the
%% pinned major, docs/otp-reference.md) from its doc chunks, for Clause's library
%% (docs/libraries/library.md#documented-functions). Writes
%% library/documented.terms: one {Module, Application, Name, Arity} per function whose module and function docs are
%% not hidden and which is not deprecated. Run explicitly:
%%   escript observe_docs.escript <library directory> [output]
%% Added for plan 21 step LIB0.
-mode(compile).

main([Library]) ->
    observe(filename:join(Library, "documented.terms"));
main([_Library, Output]) ->
    observe(Output);
main(_) ->
    io:format(standard_error, "usage: observe_docs.escript <library> [output]~n", []),
    halt(2).

% Write every documented function of the applications Clause's library provides to Output (crypto added for plan 21
% step LIB79, public_key for LIB80).
observe(Output) ->
    Rows = lists:append([application_rows(App) || App <- [erts, kernel, stdlib, crypto, public_key]]),
    Header = io_lib:format(
        "%% OTP ~s documented functions of erts, kernel, stdlib, crypto and public_key: "
        "{Module, Application, Name, Arity};\n"
        "%% observed by tests/compiler/library/observe_docs.escript (docs/libraries/library.md#documented-functions).\n",
        [erlang:system_info(otp_release)]
    ),
    Lines = [io_lib:format("{~s, ~s, ~s, ~w}.~n", [quoted(M), App, quoted(F), A]) || {M, App, F, A} <- Rows],
    ok = file:write_file(Output, [Header | Lines]).

% The documented functions of one application's modules, sorted by module, name and arity.
application_rows(App) ->
    _ = application:load(App),
    {ok, Modules} = application:get_key(App, modules),
    lists:sort([{M, App, F, A} || M <- Modules, {F, A} <- documented(M)]).

% The functions of a module whose module and function docs are not hidden, without the deprecated ones; none when
% the module is hidden or has no doc chunk.
documented(Module) ->
    case code:get_doc(Module) of
        {ok, {docs_v1, _, _, _, ModuleDoc, _, Docs}} when ModuleDoc =/= hidden ->
            [{F, A} || {{function, F, A}, _, _, Doc, Meta} <- Docs, Doc =/= hidden, not maps:is_key(deprecated, Meta)];
        _ ->
            []
    end.

quoted(Atom) -> io_lib:format("~0tp", [Atom]).
