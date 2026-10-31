%% Clause's erl_distribution (plan 30 step DIST4b1a, docs/distribution.md#starting-distribution): the net_sup
%% supervisor of erl_epmd, auth (DIST4b1c) and net_kernel, and the dynamic start and stop behind net_kernel:start/1,2 and stop/0.
%% Clause has no kernel_sup: a dynamic net_sup runs unlinked, and its start errors keep OTP's net_sup_dynamic child.
-module(erl_distribution).
-behaviour(supervisor).
-export([init/1, start/1, start_link/0, start_link/1, stop/0]).

% How long net_sup waits for each child to end, as OTP's.
-define(SHUTDOWN, 2000).

% net_sup of the node start options, as OTP's kernel starts it at boot: --sname or --name (undefined asks the first
% peer for a name). Added for distribution from the node start options (plan 30 step DIST4b1b).
start_link() ->
    {Name, Domain} =
        case init:get_argument(sname) of
            {ok, [[Short | _] | _]} ->
                {Short, shortnames};
            _ ->
                {ok, [[Long | _] | _]} = init:get_argument(name),
                {Long, longnames}
        end,
    start_link(#{name => list_to_atom(Name), name_domain => Domain, supervisor => net_sup, clean_halt => true}).

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

% Start net_sup for net_kernel:start/2's Options: {ok, NetSup}, or {error, {Reason, Child}} as OTP's kernel_sup
% answers for its net_sup_dynamic child.
start(Options) ->
    Arguments = Options#{supervisor => net_sup_dynamic, clean_halt => false},
    case whereis(net_sup) of
        undefined ->
            Init = {{local, net_sup}, ?MODULE, [Arguments]},
            case gen_server:start({local, net_sup}, supervisor, Init, []) of
                {ok, Pid} -> {ok, Pid};
                {error, Reason} -> {error, {Reason, dynamic_child(Arguments)}}
            end;
        Pid ->
            {error, {already_started, Pid}}
    end.

% The #child{} record of OTP's supervisor for kernel_sup's net_sup_dynamic child.
dynamic_child(Arguments) ->
    Start = {erl_distribution, start_link, [Arguments]},
    {child, undefined, net_sup_dynamic, Start, permanent, false, 1000, supervisor, [erl_distribution]}.

% End a net_sup net_kernel:start/1,2 started: not_found without one, not_allowed for a node started with its name.
stop() ->
    case net_kernel:get_state() of
        #{started := no} ->
            {error, not_found};
        #{started := dynamic} ->
            gen_server:stop(net_sup),
            ok;
        #{} ->
            {error, not_allowed}
    end.
