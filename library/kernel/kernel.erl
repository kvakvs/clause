%% Clause's kernel (plan 30 step DIST4b1d, docs/distribution.md#kernel-sup): OTP's callback module of the kernel
%% application and of its kernel_sup and kernel_safe_sup supervisors. kernel_sup holds the kernel services Clause runs
%% as processes, in OTP's order: rex, global_name_server, net_sup, global_group and kernel_safe_sup; clause_kernel
%% starts it at the first use of one of them.
-module(kernel).
-behaviour(application).
-behaviour(supervisor).
-export([config_change/3, init/1, start/2, stop/1]).

start(_, []) -> supervisor:start_link({local, kernel_sup}, kernel, []).

stop(_) -> ok.

config_change(_, _, _) -> ok.

% kernel_sup: one service ending ends them all, and then the program (clause_kernel). kernel_safe_sup: an empty
% supervisor other libraries add children to.
init([]) ->
    % The literal funs link the services into programs, as their child specs name them only as data.
    _ = [
        fun rpc:start_link/0, fun global:start_link/0, fun erl_distribution:start_link/0, fun global_group:start_link/0
    ],
    Children = [
        worker(rex, rpc),
        worker(global_name_server, global),
        #{
            id => net_sup,
            start => {erl_distribution, start_link, []},
            shutdown => infinity,
            type => supervisor,
            modules => [erl_distribution]
        },
        worker(global_group, global_group),
        #{
            id => kernel_safe_sup,
            start => {supervisor, start_link, [{local, kernel_safe_sup}, kernel, safe]},
            shutdown => infinity,
            type => supervisor,
            modules => [kernel]
        }
    ],
    {ok, {#{strategy => one_for_all, intensity => 0, period => 1}, Children}};
init(safe) ->
    {ok, {#{strategy => one_for_one, intensity => 4, period => 3600}, []}}.

% A service whose module starts it with start_link/0.
worker(Id, Module) -> #{id => Id, start => {Module, start_link, []}, shutdown => 2000, modules => [Module]}.
