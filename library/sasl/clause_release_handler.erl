%% The release_handler process sasl starts (plan 21 step MIM6, docs/libraries/library.md#otp-applications): a
%% Clause executable is its one permanent release, named by init:script_id/0; installing and upgrading releases
%% needs code loading.
-module(clause_release_handler).
-behaviour(gen_server).
-export([start_link/0, init/1, handle_call/3, handle_cast/2]).

start_link() ->
    gen_server:start_link({local, release_handler}, ?MODULE, [], []).

init([]) ->
    {Name, Vsn} = init:script_id(),
    {ok, [{Name, Vsn, [], permanent}]}.

% The releases, as OTP's release_handler:which_releases/0 asks for them.
handle_call(which_releases, _From, Releases) ->
    {reply, Releases, Releases};
handle_call(_, _From, Releases) ->
    {reply, {error, notsup}, Releases}.

handle_cast(_, Releases) ->
    {noreply, Releases}.
