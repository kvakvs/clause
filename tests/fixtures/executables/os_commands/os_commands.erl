-module(os_commands).
-moduledoc "os:cmd/1,2, os:find_executable/1,2 and os:set_signal/2 (plan 21 LIB61).".
-export([main/1]).

main(_) ->
    commands(),
    executables(),
    signals(),
    io:format("os_commands: ok~n").

commands() ->
    show(cmd, lines(os:cmd("echo hello"))),
    show(cmd_atom, lines(os:cmd('echo atom'))),
    show(cmd_deep, lines(os:cmd(["echo", [" deep"]]))),
    show(cmd_nul, lines(os:cmd("echo nul" ++ [0, 0]))),
    show(cmd_inner_nul, catch_error(fun() -> os:cmd(id("echo a" ++ [0] ++ "b")) end)),
    show(cmd_bad, catch_error(fun() -> os:cmd(id(<<"echo">>)) end)),
    show(cmd_options, lines(os:cmd("echo options", #{}))),
    show(max_size, os:cmd("echo 0123456789", #{max_size => 4})),
    show(max_size_zero, os:cmd("echo 0123456789", #{max_size => 0})),
    show(max_size_large, lines(os:cmd("echo 01234", #{max_size => 1000}))),
    show(exit_ignored, os:cmd("exit 3")),
    show(exit_ok, lines(os:cmd("echo fine", #{exception_on_failure => true}))),
    show(exit_failed, catch_error(fun() -> os:cmd("exit 3", #{exception_on_failure => true}) end)),
    show(bad_options, catch_error(fun() -> os:cmd("echo", id([max_size])) end)),
    show(stderr, lines(os:cmd("echo to-stderr 1>&2"))).

executables() ->
    Name =
        case os:type() of
            {win32, _} -> "cmd";
            _ -> "sh"
        end,
    Found = os:find_executable(Name),
    show(found, is_list(Found) andalso filename:pathtype(Found)),
    Dir = filename:dirname(Found),
    show(same_dir, os:find_executable(Name, Dir) =:= Found),
    show(quoted_dir, os:find_executable(Name, "\"" ++ Dir ++ "\"") =:= Found),
    show(absolute, os:find_executable(Found) =:= Found),
    show(atom_name, os:find_executable(list_to_atom(Name), Dir) =:= Found),
    show(missing, os:find_executable("no-such-program-anywhere")),
    show(missing_dir, os:find_executable(Name, "no-such-directory")),
    show(directory, os:find_executable(filename:basename(Dir), filename:dirname(Dir))).

signals() ->
    Expected =
        case os:type() of
            {win32, _} -> {error, badarg};
            _ -> {value, ok}
        end,
    show(set_signal, catch_error(fun() -> os:set_signal(sigusr2, ignore) end) =:= Expected),
    show(
        set_signal_default, catch_error(fun() -> os:set_signal(sigusr2, default) end) =:= Expected
    ),
    show(set_signal_handle, catch_error(fun() -> os:set_signal(sighup, handle) end) =:= Expected),
    show(set_signal_bad_option, catch_error(fun() -> os:set_signal(sighup, id(bad)) end)),
    show(set_signal_bad_signal, catch_error(fun() -> os:set_signal(id(sigint), ignore) end)),
    show(set_signal_not_atom, catch_error(fun() -> os:set_signal(id(1), ignore) end)).

% The output's lines, without the trailing spaces and carriage returns Windows' shell writes.
lines(Output) ->
    [string:trim(Line, trailing) || Line <- string:split(Output, "\n", all), Line =/= ""].

show(Label, Value) -> io:format("~p ~p~n", [Label, Value]).

catch_error(Fun) ->
    try Fun() of
        Value -> {value, Value}
    catch
        Class:Reason -> {Class, Reason}
    end.

id(X) -> X.
