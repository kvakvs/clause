-module(os).
-export([cmd/1]).
%% OTP 29 exports Clause does not implement yet: stubs raising error({not_implemented, F, A}),
%% or catalog builtins the runtime puts in their place (docs/library.md#runtime-module-replacements).
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

% The project-owned os module (docs/library.md): os:cmd/1 in Erlang over a port; os:type/0 and os:getenv/1 are
% builtins of the runtime.

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
env() -> erlang:error({not_implemented, env, 0}).
find_executable(_) -> erlang:error({not_implemented, find_executable, 1}).
find_executable(_, _) -> erlang:error({not_implemented, find_executable, 2}).
getenv() -> erlang:error({not_implemented, getenv, 0}).
getenv(_) -> erlang:error({not_implemented, getenv, 1}).
getenv(_, _) -> erlang:error({not_implemented, getenv, 2}).
getpid() -> erlang:error({not_implemented, getpid, 0}).
internal_init_cmd_shell() -> erlang:error({not_implemented, internal_init_cmd_shell, 0}).
perf_counter() -> erlang:error({not_implemented, perf_counter, 0}).
perf_counter(_) -> erlang:error({not_implemented, perf_counter, 1}).
putenv(_, _) -> erlang:error({not_implemented, putenv, 2}).
set_signal(_, _) -> erlang:error({not_implemented, set_signal, 2}).
system_time() -> erlang:error({not_implemented, system_time, 0}).
system_time(_) -> erlang:error({not_implemented, system_time, 1}).
timestamp() -> erlang:error({not_implemented, timestamp, 0}).
type() -> erlang:error({not_implemented, type, 0}).
unsetenv(_) -> erlang:error({not_implemented, unsetenv, 1}).
version() -> erlang:error({not_implemented, version, 0}).
