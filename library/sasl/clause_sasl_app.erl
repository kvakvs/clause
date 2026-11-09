%% The sasl application's callback and its supervisors, with OTP's names, order and child modules (plan 21 step
%% MIM6, docs/libraries/library.md#otp-applications): sasl_sup runs sasl_safe_sup, whose child is the
%% alarm_handler event manager, then release_handler (clause_release_handler, as release upgrades need code
%% loading).
-module(clause_sasl_app).
-behaviour(application).
-behaviour(supervisor).
-export([start/2, stop/1, init/1, start_safe_sup/0]).

start(_Type, _Args) ->
    supervisor:start_link({local, sasl_sup}, ?MODULE, []).

stop(_State) ->
    ok.

start_safe_sup() ->
    supervisor:start_link({local, sasl_safe_sup}, ?MODULE, safe).

% sasl_sup's children, then sasl_safe_sup's.
init([]) ->
    % The literal funs link into programs the servers child specs name only as data.
    _ = [fun clause_release_handler:start_link/0, fun alarm_handler:start_link/0],
    Safe = #{
        id => sasl_safe_sup,
        start => {?MODULE, start_safe_sup, []},
        shutdown => infinity,
        type => supervisor,
        modules => [sasl]
    },
    Release = #{
        id => release_handler,
        start => {clause_release_handler, start_link, []},
        shutdown => 2000,
        modules => []
    },
    {ok, {#{strategy => one_for_one, intensity => 0, period => 1}, [Safe, Release]}};
init(safe) ->
    Alarms = #{
        id => alarm_handler,
        start => {alarm_handler, start_link, []},
        shutdown => 2000,
        modules => dynamic
    },
    {ok, {#{strategy => one_for_one, intensity => 4, period => 3600}, [Alarms]}}.
