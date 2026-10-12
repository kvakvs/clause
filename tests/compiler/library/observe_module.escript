#!/usr/bin/env escript
%% Observe one module's exports on the host OTP (the pinned major, docs/otp-reference.md) for Clause's library
%% (docs/library.md#runtime-module-replacements). Writes library/<App>/<Module>.terms, keeping each existing entry's
%% group and status; a new export starts as {Name, Arity, default, stub}. Run explicitly:
%%   escript observe_module.escript <library directory> <application> <module> [output]
%% With an output path it writes there instead (the freshness test). Added for plan step 41J.
-mode(compile).

main([Library, App, Module]) ->
    File = filename:join([Library, App, Module ++ ".terms"]),
    observe(list_to_atom(Module), File, File);
main([Library, App, Module, Output]) ->
    observe(list_to_atom(Module), filename:join([Library, App, Module ++ ".terms"]), Output);
main(_) ->
    io:format(standard_error, "usage: observe_module.escript <library> <application> <module> [output]~n", []),
    halt(2).

% Write the observation of Module on this OTP to Output, keeping what File records.
observe(Module, File, Output) ->
    case code:ensure_loaded(Module) of
        {module, Module} -> ok;
        {error, Reason} -> fail("cannot load ~p: ~p", [Module, Reason])
    end,
    Kept = kept(File),
    Exports = lists:sort([{F, A} || {F, A} <- Module:module_info(exports), F =/= module_info]),
    Entries = [maps:get({F, A}, Kept, {F, A, default, stub}) || {F, A} <- Exports],
    Header = io_lib:format(
        "%% OTP ~s ~p exports: {Name, Arity, Group, Status}; observed by tests/compiler/library/observe_module.escript,\n"
        "%% groups and statuses maintained by hand (docs/library.md#runtime-module-replacements).\n",
        [erlang:system_info(otp_release), Module]
    ),
    ok = filelib:ensure_dir(Output),
    ok = file:write_file(Output, [Header, [[text(E), ".\n"] || E <- Entries]]),
    io:format("~s: ~p exports~n", [Output, length(Entries)]).

% A term as erlfmt lays it out: elements separated by ", ".
text(T) when is_tuple(T) -> ["{", lists:join(", ", [text(I) || I <- tuple_to_list(T)]), "}"];
text(T) -> io_lib:format("~0p", [T]).

% The entries an existing inventory records, by name and arity.
kept(File) ->
    case file:consult(File) of
        {ok, Terms} -> maps:from_list([{{F, A}, E} || {F, A, _, _} = E <- Terms]);
        {error, enoent} -> #{};
        {error, Reason} -> fail("cannot read ~s: ~p", [File, Reason])
    end.

fail(Format, Arguments) ->
    io:format(standard_error, "observe_module: " ++ Format ++ "~n", Arguments),
    halt(1).
