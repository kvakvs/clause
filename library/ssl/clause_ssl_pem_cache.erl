%% The ssl application's PEM cache, registered as ssl_pem_cache as in OTP (plan 21 step LIB81A,
%% docs/libraries/ssl.md#application): the decoded PEM files of certfile, keyfile and cacertfile options, by name.
%% Connections read through it from LIB81C; ssl:clear_pem_cache/0 empties it.
-module(clause_ssl_pem_cache).
-behaviour(gen_server).
-export([start_link/0, clear/0]).
-export([init/1, handle_call/3, handle_cast/2]).

start_link() ->
    gen_server:start_link({local, ssl_pem_cache}, ?MODULE, [], []).

% Empty the cache; exits with noproc when ssl is not started, as OTP's call does.
clear() ->
    put(ssl_pem_cache, ssl_pem_cache),
    gen_server:call(ssl_pem_cache, {unconditionally_clear_pem_cache, self()}, infinity).

% The state is the map of cached files.
init([]) ->
    {ok, #{}}.

handle_call({unconditionally_clear_pem_cache, _Caller}, _From, _Files) ->
    {reply, ok, #{}}.

handle_cast(_Request, Files) ->
    {noreply, Files}.
