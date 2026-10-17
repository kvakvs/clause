-module(os).
-export([cmd/1]).
%% OTP 29 exports implemented natively or not yet: catalog builtins, whose clause_builtin_placeholder body the runtime's
%% builtin replaces, and stubs raising error({not_implemented, F, A}) (docs/library.md#runtime-module-replacements).
-export([
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

% The project-owned os module (docs/library.md): os:cmd/1 and the time functions in Erlang; os:type/0,
% os:getenv/0,1, os:env/0, os:putenv/2, os:unsetenv/1, os:getpid/0 and os:version/0 are builtins of the runtime.

% Run Command through the system's command processor (cmd.exe /c on Windows, /bin/sh -c elsewhere) and return
% what it writes to its standard output and error, as bytes.
cmd(Command) when is_atom(Command) ->
    cmd(atom_to_list(Command));
cmd(Command) when is_list(Command) ->
    Port = open_port(shell(os:type(), Command), [binary, stderr_to_stdout, stream, in, hide]),
    Monitor = monitor(port, Port),
    Bytes = collect(Port, Monitor, []),
    % The port was linked to the caller: drop its exit signal when the caller traps exits.
    receive
        {'EXIT', Port, _} -> ok
    after 0 -> ok
    end,
    binary_to_list(Bytes).

% The port name that runs Command.
shell({win32, _}, Command) ->
    Processor =
        case os:getenv("COMSPEC") of
            false -> "cmd";
            Path -> Path
        end,
    {spawn, Processor ++ " /c" ++ Command};
shell(_, Command) ->
    {spawn, Command}.

% The output of Port until it closed.
collect(Port, Monitor, Parts) ->
    receive
        {Port, {data, Bytes}} -> collect(Port, Monitor, [Bytes | Parts]);
        {'DOWN', Monitor, port, Port, _} -> iolist_to_binary(lists:reverse(Parts))
    end.

cmd(_, _) -> erlang:error({not_implemented, cmd, 2}).
env() -> clause_builtin_placeholder.
find_executable(_) -> erlang:error({not_implemented, find_executable, 1}).
find_executable(_, _) -> erlang:error({not_implemented, find_executable, 2}).
getenv() -> clause_builtin_placeholder.
getenv(_) -> clause_builtin_placeholder.
getpid() -> clause_builtin_placeholder.
internal_init_cmd_shell() -> erlang:error({not_implemented, internal_init_cmd_shell, 0}).
putenv(_, _) -> clause_builtin_placeholder.
set_signal(_, _) -> erlang:error({not_implemented, set_signal, 2}).
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
