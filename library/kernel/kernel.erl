%% Clause's kernel (plan 30 step DIST4b1d, docs/distribution.md#kernel-sup): OTP's callback module of the kernel
%% application and of its kernel_sup and kernel_safe_sup supervisors. kernel_sup holds the kernel services Clause runs
%% as processes, in OTP's order: rex, global_name_server, net_sup, global_group and kernel_safe_sup; clause_kernel
%% starts it at the first use of one of them. clau's generated clause_kernel_services lists the services the program
%% does not exclude (exclude_kernel_services, plan 30 step DIST4b1e) and links their modules.
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
    % The literal fun links erl_distribution into programs, as net_sup's child spec names it only as data.
    _ = fun erl_distribution:start_link/0,
    Included = clause_kernel_services:services(),
    Children = [Child || #{id := Id} = Child <- children(), Id =:= net_sup orelse lists:member(Id, Included)],
    {ok, {#{strategy => one_for_all, intensity => 0, period => 1}, Children}};
init(safe) ->
    {ok, {#{strategy => one_for_one, intensity => 4, period => 3600}, []}}.

% Every child kernel_sup may have, in start order.
children() ->
    [
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
    ].

% A service whose module starts it with start_link/0.
worker(Id, Module) -> #{id => Id, start => {Module, start_link, []}, shutdown => 2000, modules => [Module]}.
