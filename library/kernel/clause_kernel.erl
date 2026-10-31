%% kernel_sup at the first use of a kernel service (plan 30 step DIST4b1d, docs/distribution.md#kernel-sup): OTP's
%% node starts kernel_sup at boot, Clause when rex, global_name_server, global_group or distribution is first needed.
%% A keeper process links to kernel_sup and halts the program when it ends, as OTP's node halts when the kernel
%% application ends.
-module(clause_kernel).
-export([ensure_started/0]).

% ok once kernel_sup runs with its children started, else {error, Reason} of its start.
ensure_started() ->
    case whereis(kernel_sup) of
        undefined -> start();
        Sup -> started(Sup)
    end.

start() ->
    Caller = self(),
    Ref = make_ref(),
    {_, Monitor} = spawn_monitor(fun() -> keep(Caller, Ref) end),
    receive
        {Ref, Result} ->
            erlang:demonitor(Monitor, [flush]),
            Result;
        {'DOWN', Monitor, process, _, Reason} ->
            {error, Reason}
    end.

% Start kernel_sup linked to this process, then wait for its end. Another first use may have won the name.
keep(Caller, Ref) ->
    process_flag(trap_exit, true),
    % The literal fun links kernel into programs, as supervisor:start_link/3 names it only as data.
    _ = fun kernel:init/1,
    case supervisor:start_link({local, kernel_sup}, kernel, []) of
        {ok, Sup} ->
            Caller ! {Ref, ok},
            receive
                {'EXIT', Sup, Reason} -> erlang:halt(kernel_terminated(Reason))
            end;
        {error, {already_started, Sup}} ->
            Caller ! {Ref, started(Sup)};
        {error, Reason} ->
            Caller ! {Ref, {error, Reason}}
    end.

% A kernel_sup another process started answers calls once its children started.
started(Sup) ->
    try supervisor:which_children(Sup) of
        _ -> ok
    catch
        exit:Reason -> {error, Reason}
    end.

% The slogan OTP's node halts with when the kernel application ends.
kernel_terminated(Reason) ->
    Text = lists:flatten(io_lib:format("~w", [{application_terminated, kernel, Reason}])),
    lists:flatten(["Kernel pid terminated (application_controller) (", io_lib:write_string(Text), ")"]).
