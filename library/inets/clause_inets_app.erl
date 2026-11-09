%% The inets application's callback and its supervisors, with OTP's names, order and child modules (plan 21 step
%% MIM6, docs/libraries/library.md#otp-applications): inets_sup runs httpc_sup, with the default profile's
%% httpc_manager under httpc_profile_sup and the request handlers' httpc_handler_sup, then httpd_sup, which has
%% no servers.
-module(clause_inets_app).
-behaviour(application).
-behaviour(supervisor).
-export([start/2, stop/1, init/1, start_sup/1]).

start(_Type, _Args) ->
    start_sup(inets_sup).

stop(_State) ->
    ok.

% One of the application's supervisors, registered under its name.
start_sup(Name) ->
    supervisor:start_link({local, Name}, ?MODULE, Name).

init(inets_sup) ->
    {ok, {flags(one_for_one, 10), [sup(httpc_sup), sup(httpd_sup)]}};
init(httpc_sup) ->
    {ok, {flags(one_for_one, 10), [sup(httpc_profile_sup), sup(httpc_handler_sup)]}};
init(httpc_profile_sup) ->
    % The literal fun links into programs the server the child spec names only as data.
    _ = fun clause_httpc_manager:start_link/1,
    Manager = #{
        id => httpc_manager,
        start => {clause_httpc_manager, start_link, [default]},
        shutdown => 4000,
        modules => [httpc_manager]
    },
    {ok, {flags(one_for_one, 10), [Manager]}};
% The request handlers it starts come with httpc (MIM4).
init(httpc_handler_sup) ->
    Handler = #{
        id => undefined,
        start => {clause_httpc_handler, start_link, []},
        restart => temporary,
        modules => [httpc_handler]
    },
    {ok, {flags(simple_one_for_one, 0), [Handler]}};
init(httpd_sup) ->
    {ok, {flags(one_for_one, 10), []}}.

flags(Strategy, Intensity) ->
    #{strategy => Strategy, intensity => Intensity, period => 3600}.

sup(Name) ->
    #{id => Name, start => {?MODULE, start_sup, [Name]}, shutdown => infinity, type => supervisor, modules => [Name]}.
