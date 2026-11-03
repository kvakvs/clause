%% The mnesia application's callback and its supervisors, registered as in OTP (plan 21 step MIM1A,
%% docs/libraries/mnesia.md#processes): mnesia_sup runs the mnesia_event manager, mnesia_ext_sup for backend
%% processes and mnesia_kernel_sup: mnesia_monitor owns the tables, mnesia_locker keeps the locks and mnesia_tm the
%% transactions (MIM1B). Later steps add mnesia's other servers.
-module(clause_mnesia_app).
-behaviour(application).
-behaviour(supervisor).
-export([start/2, stop/1, init/1, start_supervisor/1, start_event/0]).

start(normal, []) ->
    supervisor:start_link({local, mnesia_sup}, ?MODULE, mnesia_sup);
start(Type, Args) ->
    {error, {badarg, Type, Args}}.

stop(_State) ->
    ok.

start_supervisor(Name) ->
    supervisor:start_link({local, Name}, ?MODULE, Name).

% The event manager mnesia reports system events to, with the event_module parameter's handler (MIM1C).
start_event() ->
    {ok, Pid} = gen_event:start_link({local, mnesia_event}),
    case clause_mnesia_event:add_handler() of
        ok -> {ok, Pid};
        Error -> Error
    end.

% Every child stops mnesia as a whole: none restarts.
init(mnesia_sup) ->
    Event = #{id => mnesia_event, start => {?MODULE, start_event, []}, shutdown => 30000, modules => dynamic},
    {ok,
        {#{strategy => one_for_all, intensity => 0, period => 3600}, [
            Event, supervisor(mnesia_ext_sup), supervisor(mnesia_kernel_sup)
        ]}};
init(mnesia_ext_sup) ->
    {ok, {#{strategy => one_for_all, intensity => 0, period => 3600}, []}};
init(mnesia_kernel_sup) ->
    Servers = [
        {mnesia_monitor, clause_mnesia_monitor}, {mnesia_locker, clause_mnesia_locker}, {mnesia_tm, clause_mnesia_tm}
    ],
    Children = [#{id => Name, start => {Module, start_link, []}, shutdown => 3000} || {Name, Module} <- Servers],
    {ok, {#{strategy => one_for_all, intensity => 0, period => 86400000}, Children}}.

supervisor(Name) ->
    #{id => Name, start => {?MODULE, start_supervisor, [Name]}, shutdown => infinity, type => supervisor}.
