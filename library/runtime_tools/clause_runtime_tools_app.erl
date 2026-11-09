%% The runtime_tools application's callback and runtime_tools_sup, with OTP's names and child modules (plan 21
%% step MIM6, docs/libraries/library.md#otp-applications). Its ttb_autostart child restarts no trace: Clause has
%% no ttb.
-module(clause_runtime_tools_app).
-behaviour(application).
-behaviour(supervisor).
-export([start/2, stop/1, init/1, start_autostart/0, handle_call/3, handle_cast/2]).

% Started, the application names its ttb autostart module in its environment, as OTP's does.
start(_Type, _Args) ->
    ok = application:set_env(runtime_tools, ttb_autostart_module, ttb_autostart),
    supervisor:start_link({local, runtime_tools_sup}, ?MODULE, []).

stop(_State) ->
    ok.

init([]) ->
    Autostart = #{
        id => ttb_autostart,
        start => {?MODULE, start_autostart, []},
        restart => temporary,
        shutdown => 3000,
        modules => [ttb_autostart]
    },
    {ok, {#{strategy => one_for_one, intensity => 0, period => 3600}, [Autostart]}};
% The ttb_autostart server, as OTP's with no autostart file: idle, its state no_args.
init(no_args) ->
    {ok, no_args}.

start_autostart() ->
    gen_server:start_link(?MODULE, no_args, []).

handle_call(_, _From, State) ->
    {reply, ok, State}.

handle_cast(_, State) ->
    {noreply, State}.
