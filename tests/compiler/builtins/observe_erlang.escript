#!/usr/bin/env escript
%% Observe the erlang module of the host OTP (the pinned major, docs/otp-reference.md): every export with its
%% auto-import and guard flags. Writes library/erts/erlang.terms, keeping each existing entry's status; a new
%% export starts as unclassified. Run explicitly: escript observe_erlang.escript <repository root>; with an output
%% path it writes there instead (the freshness test).
-mode(compile).

main([Root]) ->
    File = filename:join([Root, "library", "erts", "erlang.terms"]),
    observe(File, File);
main([Root, Output]) ->
    observe(filename:join([Root, "library", "erts", "erlang.terms"]), Output);
main(_) ->
    io:format(standard_error, "usage: observe_erlang.escript <repository root> [output]~n", []),
    halt(2).

% Write the observation of this OTP to Output, keeping the statuses File records.
observe(File, Output) ->
    Statuses = statuses(File),
    Exports = lists:sort([{F, A} || {F, A} <- erlang:module_info(exports), F =/= module_info]),
    Entries = [entry(F, A, Statuses) || {F, A} <- Exports],
    Header = io_lib:format(
        "%% OTP ~s (ERTS ~s) erlang exports: {Name, Arity, Flags, Status}; observed by\n"
        "%% tests/compiler/builtins/observe_erlang.escript, statuses maintained by hand (docs/builtins.md#erlang-module).\n",
        [erlang:system_info(otp_release), erlang:system_info(version)]
    ),
    Body = [[text(E), ".\n"] || E <- Entries],
    ok = file:write_file(Output, [Header, Body]),
    io:format("~s: ~p exports~n", [Output, length(Entries)]).

% One export with its flags and its kept or initial status.
entry(F, A, Statuses) ->
    Flags = [auto_import || erl_internal:bif(F, A)] ++ [guard || erl_internal:guard_bif(F, A)],
    {F, A, Flags, maps:get({F, A}, Statuses, unclassified)}.

% A term as erlfmt lays it out: elements separated by ", ".
text(T) when is_tuple(T) -> ["{", join(tuple_to_list(T)), "}"];
text(L) when is_list(L) -> ["[", join(L), "]"];
text(T) -> io_lib:format("~0p", [T]).

join(Items) -> lists:join(", ", [text(I) || I <- Items]).

% The statuses an existing inventory records, by name and arity.
statuses(File) ->
    case file:consult(File) of
        {ok, Terms} -> maps:from_list([{{F, A}, S} || {F, A, _, S} <- Terms]);
        {error, enoent} -> #{}
    end.
