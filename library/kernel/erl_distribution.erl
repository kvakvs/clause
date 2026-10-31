%% Clause's erl_distribution (plan 30 step DIST4b1a, docs/distribution.md#starting-distribution): the net_sup
%% supervisor of erl_epmd, auth (DIST4b1c) and net_kernel, kernel_sup's net_sup child, and the net_sup_dynamic child
%% net_kernel:start/1,2 adds to kernel_sup and stop/0 removes (DIST4b1d).
-module(erl_distribution).
-behaviour(supervisor).
-export([init/1, start/1, start_link/0, start_link/1, stop/0]).

% How long net_sup waits for each child to end, as OTP's.
-define(SHUTDOWN, 2000).

% kernel_sup's net_sup child: distribution of the node start options --sname or --name (undefined asks the first peer
% for a name), else ignore. Added for distribution from the node start options (plan 30 step DIST4b1b).
start_link() ->
    case {init:get_argument(sname), init:get_argument(name)} of
        {{ok, [[Short | _] | _]}, _} -> start_link(boot_options(Short, shortnames));
        {_, {ok, [[Long | _] | _]}} -> start_link(boot_options(Long, longnames));
        _ -> ignore
    end.

boot_options(Name, Domain) ->
    #{name => list_to_atom(Name), name_domain => Domain, supervisor => net_sup, clean_halt => true}.

start_link(Options) -> supervisor:start_link({local, net_sup}, ?MODULE, [Options]).

% The children in start order: the epmd client, the cookies' auth server, then net_kernel; one ending ends them all.
init([Options]) ->
    Epmd = net_kernel:epmd_module(),
    EpmdChild = #{id => Epmd, start => {Epmd, start_link, []}, shutdown => ?SHUTDOWN, modules => [Epmd]},
    % The literal fun links the auth server into programs, as its child spec names it only as data.
    _ = fun auth:start_link/0,
    Auth = #{id => auth, start => {auth, start_link, []}, shutdown => ?SHUTDOWN},
    Kernel = #{id => net_kernel, start => {net_kernel, start_link, [Options]}, shutdown => ?SHUTDOWN},
    Flags = #{strategy => one_for_all, intensity => 0, period => 1},
    {ok, {Flags, [EpmdChild || epmd_child(Epmd)] ++ [Auth, Kernel]}}.

% Whether the epmd module runs a server of its own, as erl_epmd does.
epmd_child(Epmd) ->
    _ = code:ensure_loaded(Epmd),
    erlang:function_exported(Epmd, start_link, 0).

% Start net_sup for net_kernel:start/2's Options as kernel_sup's net_sup_dynamic child: {ok, NetSup}, or
% kernel_sup's error, {error, {Reason, Child}} when net_sup cannot start.
start(Options) ->
    Arguments = Options#{supervisor => net_sup_dynamic, clean_halt => false},
    case {whereis(net_sup), clause_kernel:ensure_started()} of
        {undefined, ok} ->
            Child = #{
                id => net_sup_dynamic,
                start => {erl_distribution, start_link, [Arguments]},
                shutdown => 1000,
                type => supervisor,
                modules => [erl_distribution]
            },
            supervisor:start_child(kernel_sup, Child);
        {undefined, Error} ->
            Error;
        {Pid, _} ->
            {error, {already_started, Pid}}
    end.

% End the net_sup net_kernel:start/1,2 started: not_found without one, not_allowed for a node started with its name.
stop() ->
    case net_kernel:get_state() of
        #{started := no} ->
            {error, not_found};
        #{started := dynamic} ->
            ok = supervisor:terminate_child(kernel_sup, net_sup_dynamic),
            supervisor:delete_child(kernel_sup, net_sup_dynamic);
        #{} ->
            {error, not_allowed}
    end.
