-module(os).
%% OTP 29 exports implemented natively or not yet: catalog builtins, whose clause_builtin_placeholder body the runtime's
%% builtin replaces, and stubs raising error({not_implemented, F, A}) (docs/libraries/library.md#runtime-module-replacements).
-export([
    cmd/1,
    cmd/2,
    env/0,
    find_executable/1,
    find_executable/2,
    getenv/0,
    getenv/1,
    getenv/2,
    getpid/0,
    internal_init_cmd_shell/0,
    perf_counter/0,
    perf_counter/1,
    putenv/2,
    set_signal/2,
    system_time/0,
    system_time/1,
    timestamp/0,
    type/0,
    unsetenv/1,
    version/0
]).
-include("os.types.hrl").
-include("include/file.hrl").

% The project-owned os module (docs/libraries/library.md): os:cmd/1,2 and the time functions in Erlang; os:type/0,
% os:getenv/0,1, os:env/0, os:putenv/2, os:unsetenv/1, os:getpid/0, os:version/0, os:find_executable/2 and
% os:set_signal/2 are builtins of the runtime.

-define(SHELL_KEY, kernel_os_cmd_shell).

%% Run Command in the system's command shell and return what it writes to its standard output and error, as
%% characters when the output is UTF-8, as bytes otherwise (plan 21 step LIB61).
cmd(Command) ->
    try
        run_command(Command, #{})
    catch
        throw:{open_port, Reason} -> badarg_with_cause([Command], {open_port, Reason});
        throw:badarg -> badarg_with_info([Command])
    end.

%% cmd/1 with options: max_size limits the output kept, exception_on_failure raises
%% {command_failed, Output, ExitCode} for a command that exits with another status than 0.
cmd(Command, Options) ->
    try
        run_command(Command, Options)
    catch
        throw:{command_failed, Result, ExitStatus} -> erlang:error({command_failed, Result, ExitStatus});
        throw:badopt -> badarg_with_cause([Command, Options], badopt);
        throw:{open_port, Reason} -> badarg_with_cause([Command, Options], {open_port, Reason});
        throw:badarg -> badarg_with_info([Command, Options])
    end.

run_command(Command, Options) ->
    MaxSize = command_option(max_size, Options, infinity),
    Failures = command_option(exception_on_failure, Options, false),
    {Spawn, PortOptions, Input, Eot} = shell_command(os:type(), command_text(Command), Failures),
    Port =
        try
            open_port({spawn, Spawn}, [binary, stderr_to_stdout, stream, in, hide | PortOptions])
        catch
            error:Reason -> throw({open_port, Reason})
        end,
    Monitor = erlang:monitor(port, Port),
    send_input(Port, Input),
    {Bytes, ExitStatus} = collect(Port, Monitor, {Eot, MaxSize}, [], 0, Failures),
    erlang:demonitor(Monitor, [flush]),
    Result =
        case unicode:characters_to_list(Bytes) of
            String when is_list(String) -> String;
            _ -> binary_to_list(iolist_to_binary(Bytes))
        end,
    case Failures andalso ExitStatus =/= 0 of
        true -> throw({command_failed, Result, ExitStatus});
        false -> Result
    end.

% A Windows command needs no input, and its port has no input pipe.
send_input(_, <<>>) -> true;
send_input(Port, Input) -> port_command(Port, Input).

command_option(Name, Options, Default) when is_map(Options) -> maps:get(Name, Options, Default);
command_option(_, _, _) -> throw(badopt).

% The spawn command, port options, input and end-of-output marker of Command. A Unix shell reads the command from
% its input and echoes a ^D marker (and the exit status) when it ends, so background jobs do not hold the output open.
shell_command({win32, _}, Command, Failures) ->
    {lists:flatten(lists:concat([shell(), " /c", Command])), [exit_status || Failures], <<>>, <<>>};
shell_command(_, Command, Failures) ->
    Status = ["$?\^D" || Failures],
    Input = ["(", unicode:characters_to_binary(Command), "\n) </dev/null; echo \"\^D", Status, "\"\n"],
    {shell() ++ " -s unix:cmd", [out | [exit_status || Failures]], iolist_to_binary(Input), <<"\^D">>}.

shell() ->
    case persistent_term:get(?SHELL_KEY, undefined) of
        undefined ->
            internal_init_cmd_shell(),
            persistent_term:get(?SHELL_KEY);
        Shell ->
            Shell
    end.

%% The shell os:cmd/1,2 runs: kernel's os_cmd_shell, else COMSPEC or cmd on Windows, /bin/sh elsewhere.
internal_init_cmd_shell() ->
    Shell =
        case application:get_env(kernel, os_cmd_shell) of
            {ok, Configured} -> Configured;
            undefined -> default_shell(os:type())
        end,
    persistent_term:put(?SHELL_KEY, Shell).

default_shell({win32, Type}) ->
    case {os:getenv("COMSPEC"), Type} of
        {false, windows} -> "command.com";
        {false, _} -> "cmd";
        {Path, _} -> Path
    end;
default_shell(_) ->
    case {regular_file("/bin/sh"), regular_file("/system/bin/sh")} of
        {false, true} -> "/system/bin/sh";
        _ -> "/bin/sh"
    end.

regular_file(Name) ->
    case file:read_file_info(Name, [raw]) of
        {ok, #file_info{type = Type}} -> Type =:= regular;
        {error, _} -> false
    end.

% A command as characters: an atom's name or a (deep) character list; NUL characters only at its end, dropped.
command_text(Command) ->
    try command_chars(Command) of
        Chars -> Chars
    catch
        error:_ -> throw(badarg)
    end.

command_chars(Command) when is_atom(Command) ->
    command_chars(atom_to_list(Command));
command_chars(Command) when is_list(Command) ->
    case has_nul(Command) of
        false -> Command;
        true -> string:trim(Command, trailing, [0])
    end.

% Whether the characters have a NUL, which only more NULs may follow; error otherwise.
has_nul([0 | Rest]) -> only_nuls(Rest);
has_nul([C | Rest]) when is_integer(C), C > 0 -> has_nul(Rest);
has_nul([List | Rest]) when is_list(List) -> has_nul(List) orelse has_nul(Rest);
has_nul([]) -> false.

only_nuls([]) ->
    true;
only_nuls([0 | Rest]) ->
    only_nuls(Rest);
only_nuls([List | Rest]) when is_list(List) ->
    only_nuls(List),
    only_nuls(Rest).

% The output until the end marker, the port's end or MaxSize bytes, and the command's exit status.
collect(Port, Monitor, {Eot, Max} = End, Sofar, Size, Failures) ->
    receive
        {Port, {data, Bytes}} ->
            case output_end(Bytes, Eot, Size, Max) of
                more ->
                    collect(Port, Monitor, End, [Sofar, Bytes], Size + byte_size(Bytes), Failures);
                {Last, Rest} ->
                    catch port_close(Port),
                    flush_until_down(Port, Monitor),
                    {[Sofar, Last], echoed_status(Failures, Rest, Eot, Max)}
            end;
        {Port, {exit_status, Status}} ->
            flush_until_down(Port, Monitor),
            flush_exit(Port),
            {Sofar, Status};
        {'DOWN', Monitor, _, _, _} ->
            % The port was closed by someone else: as if the command was killed.
            flush_exit(Port),
            {Sofar, 128 + 9}
    end.

% more, or {Kept, AfterMarker} once the marker or MaxSize ends the output.
output_end(Bytes, <<>>, Size, Max) when Size + byte_size(Bytes) < Max ->
    more;
output_end(Bytes, <<>>, Size, Max) ->
    {binary:part(Bytes, {0, Max - Size}), <<>>};
output_end(Bytes, Eot, Size, Max) ->
    case binary:match(Bytes, Eot) of
        {Pos, _} when Size + Pos < Max ->
            {binary:part(Bytes, 0, Pos), binary:part(Bytes, Pos + 1, byte_size(Bytes) - (Pos + 1))};
        _ ->
            output_end(Bytes, <<>>, Size, Max)
    end.

% The exit status the shell echoed after the marker, or 0.
echoed_status(true, Rest, Eot, Max) ->
    case output_end(Rest, Eot, byte_size(Rest), Max) of
        {Code, _} -> binary_to_integer(Code);
        more -> 0
    end;
echoed_status(false, _, _, _) ->
    0.

flush_until_down(Port, Monitor) ->
    receive
        {Port, {data, _}} -> flush_until_down(Port, Monitor);
        {'DOWN', Monitor, _, _, _} -> flush_exit(Port)
    end.

% The port was linked to the caller: drop its exit signal when the caller traps exits.
flush_exit(Port) ->
    receive
        {'EXIT', Port, _} -> ok
    after 0 -> ok
    end.

badarg_with_cause(Args, Cause) ->
    erlang:error(badarg, Args, [{error_info, #{module => erl_kernel_errors, cause => Cause}}]).

badarg_with_info(Args) ->
    erlang:error(badarg, Args, [{error_info, #{module => erl_kernel_errors}}]).

%% The program Name on the PATH search path (plan 21 step LIB61).
find_executable(Name) ->
    os:find_executable(Name, getenv("PATH", "")).

env() -> clause_builtin_placeholder.
find_executable(_, _) -> clause_builtin_placeholder.
getenv() -> clause_builtin_placeholder.
getenv(_) -> clause_builtin_placeholder.
getpid() -> clause_builtin_placeholder.
putenv(_, _) -> clause_builtin_placeholder.
set_signal(_, _) -> clause_builtin_placeholder.
type() -> clause_builtin_placeholder.
unsetenv(_) -> clause_builtin_placeholder.
version() -> clause_builtin_placeholder.

%% The time functions over the erlang ones (plan step 41X): OS system time, the {MegaSecs, Secs, MicroSecs}
%% timestamp and the performance counter, whose native unit is nanoseconds.
timestamp() -> erlang:timestamp().

system_time() -> erlang:system_time().

system_time(Unit) -> erlang:system_time(Unit).

perf_counter() -> erlang:monotonic_time(perf_counter).

perf_counter(Unit) -> erlang:convert_time_unit(os:perf_counter(), perf_counter, Unit).

%% The value of variable Name, or Default when it is not set (plan step 41X).
getenv(Name, Default) ->
    case os:getenv(Name) of
        false -> Default;
        Value -> Value
    end.
