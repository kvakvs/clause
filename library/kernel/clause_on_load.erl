%% The -on_load functions of a program (plan 21 step MIM9d, docs/executables.md#on-load-functions): an executable has
%% every module from the start, as an OTP node booted in embedded mode, whose init runs each loaded module's on_load
%% function as the kernel starts. When a module of the program has one, the runtime runs boot/3 as the main process
%% instead of the entry: each function runs in a process of its own, in registration order, then the entry is called.
%% A function that returns anything but ok, or fails, halts the program with the report of OTP's kernel application.
%% clau links this module into programs with -on_load modules; clause_dist_start runs the functions after starting
%% distribution.
-module(clause_on_load).
-export([boot/3, handlers/0, kernel_failure/1, run/0]).

% Run the -on_load functions, then call Module:Function(Arguments...) in their place.
boot(Module, Function, Arguments) ->
    run(),
    apply(Module, Function, Arguments).

% Run every -on_load function; the first that fails halts the program.
run() ->
    lists:foreach(fun run/1, ?MODULE:handlers()).

% {Module, Fun} for each module with an -on_load function, in registration order: a runtime builtin, which only
% remote calls reach.
handlers() -> clause_builtin_placeholder.

% One -on_load function in a process of its own: its result is the process's exit reason, as OTP's init reads it.
run({Module, Fun}) ->
    {Pid, Ref} = spawn_monitor(fun() -> exit(Fun()) end),
    receive
        {'DOWN', Ref, process, Pid, ok} ->
            ok;
        {'DOWN', Ref, process, Pid, Reason} ->
            Failure = {shutdown, {failed_to_start_child, on_load, {on_load_function_failed, Module, Reason}}},
            erlang:halt(kernel_failure(Failure))
    end.

% The slogan OTP's node halts with when the kernel application cannot start. The term is on one line with strings as
% strings, as erts prints it (TLS distribution's file names, plan 30 step DIST12a).
kernel_failure(Reason) ->
    Failure = {Reason, {kernel, start, [normal, []]}},
    Text = lists:flatten(io_lib:format("~1000000p", [{application_start_failure, kernel, Failure}])),
    lists:flatten(["Kernel pid terminated (application_controller) (", io_lib:write_string(Text), ")"]).
