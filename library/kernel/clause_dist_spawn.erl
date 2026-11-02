%% Spawns on other nodes for the runtime's builtins (plan 30 step DIST5b2, docs/distribution.md#spawning), a module
%% of its own so that a program spawning on a node links nothing else of distribution.
-module(clause_dist_spawn).
-export([spawn/5, crasher/6]).

%% spawn/2,4, spawn_link/2,4, spawn_monitor/2,4 and spawn_opt/3,5 naming another node continue here (plan 30 step
%% DIST5b2), as OTP's erlang:spawn_opt/5: a spawn request, then its reply. A node that cannot be reached or cannot
%% spawn gives a process that warns and exits with that reason at once (crasher/6); the reply options of
%% spawn_request are badarg.
spawn(Node, Module, Function, Args, Options) ->
    Monitor = spawns_monitor(Options, false),
    Ref = erlang:spawn_request(Node, Module, Function, Args, Options),
    receive
        {spawn_reply, Ref, ok, Pid} when Monitor -> {Pid, Ref};
        {spawn_reply, Ref, ok, Pid} ->
            Pid;
        {spawn_reply, Ref, error, badopt} ->
            erlang:error(badarg);
        {spawn_reply, Ref, error, Reason} when Reason =:= noconnection; Reason =:= notsup ->
            erlang:spawn_opt(?MODULE, crasher, [Node, Module, Function, Args, Options, Reason], Options);
        {spawn_reply, Ref, error, Reason} ->
            erlang:error(Reason)
    end.

%% The useless pid of a spawn that failed, as OTP's erts_internal:crasher/6: a warning report, then its exit with the
%% reason (DIST5e).
crasher(Node, Module, Function, Args, [], Reason) ->
    error_logger:warning_msg("** Can not start ~w:~w,~w on ~w **~n", [Module, Function, Args, Node]),
    erlang:exit(Reason);
crasher(Node, Module, Function, Args, Options, Reason) ->
    error_logger:warning_msg("** Can not start ~w:~w,~w (~w) on ~w **~n", [Module, Function, Args, Options, Node]),
    erlang:exit(Reason).

% Whether spawn options monitor the new process; badarg for an improper list or a reply option.
spawns_monitor([], Monitor) ->
    Monitor;
spawns_monitor([{Reply, _} | _], _) when Reply =:= reply; Reply =:= reply_tag ->
    erlang:error(badarg);
spawns_monitor([monitor | Options], _) ->
    spawns_monitor(Options, true);
spawns_monitor([{monitor, _} | Options], _) ->
    spawns_monitor(Options, true);
spawns_monitor([_ | Options], Monitor) ->
    spawns_monitor(Options, Monitor);
spawns_monitor(_, _) ->
    erlang:error(badarg).
