%% Distribution from the node start options (plan 30 step DIST4b1b, docs/distribution.md#node-start-options): when
%% --sname or --name names the node, the runtime runs boot/3 as the main process instead of the entry. It starts
%% kernel_sup with net_sup as OTP's kernel does at boot (DIST4b1d), then calls the entry; clau links this module with
%% --distribution and into programs that call net_kernel.
-module(clause_dist_start).
-export([boot/3]).

% Start kernel_sup and its services, distribution among them, then call Module:Function(Arguments...) in its place.
% A failed start halts the program with the report of OTP's kernel application.
boot(Module, Function, Arguments) ->
    case clause_kernel:ensure_started() of
        ok -> apply(Module, Function, Arguments);
        {error, Reason} -> erlang:halt(kernel_failure(Reason))
    end.

% The slogan OTP's node halts with when kernel_sup cannot start.
kernel_failure(Reason) ->
    Failure = {Reason, {kernel, start, [normal, []]}},
    Text = lists:flatten(io_lib:format("~w", [{application_start_failure, kernel, Failure}])),
    lists:flatten(["Kernel pid terminated (application_controller) (", io_lib:write_string(Text), ")"]).
