%% A profile's httpc manager, registered as httpc_manager for the default profile as in OTP (plan 21 step MIM6,
%% docs/libraries/library.md#otp-applications). It runs with the inets application; the requests it serves come
%% with httpc (MIM4).
-module(clause_httpc_manager).
-behaviour(gen_server).
-export([start_link/1, init/1, handle_call/3, handle_cast/2]).

start_link(default) ->
    gen_server:start_link({local, httpc_manager}, ?MODULE, default, []).

init(Profile) ->
    {ok, Profile}.

handle_call(_, _From, Profile) ->
    {reply, {error, notsup}, Profile}.

handle_cast(_, Profile) ->
    {noreply, Profile}.
