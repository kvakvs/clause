%% Distribution from the node start options (plan 30 step DIST4b1b, docs/distribution.md#node-start-options): when
%% --sname or --name names the node, the runtime runs boot/3 as the main process instead of the entry. It starts
%% erl_distribution's net_sup as OTP's kernel does at boot, then calls the entry; clau links this module with
%% --distribution and into programs that call net_kernel.
-module(clause_dist_start).
-export([boot/3]).

% Start distribution, then call Module:Function(Arguments...) in its place. A failed start halts the program with the
% report of OTP's kernel application; exits are trapped meanwhile, so that the failed net_sup's exit leaves this
% process running.
boot(Module, Function, Arguments) ->
    Trap = process_flag(trap_exit, true),
    Started = erl_distribution:start_link(),
    process_flag(trap_exit, Trap),
    case Started of
        {ok, _} -> apply(Module, Function, Arguments);
        {error, Reason} -> erlang:halt(kernel_failure(Reason))
    end.

% The slogan OTP's node halts with when kernel cannot start net_sup.
kernel_failure(Reason) ->
    Failure = {{shutdown, {failed_to_start_child, net_sup, Reason}}, {kernel, start, [normal, []]}},
    Text = lists:flatten(io_lib:format("~w", [{application_start_failure, kernel, Failure}])),
    lists:flatten(["Kernel pid terminated (application_controller) (", io_lib:write_string(Text), ")"]).
