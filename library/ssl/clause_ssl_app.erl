%% The ssl application's callback and its top supervisor, registered as ssl_sup as in OTP (plan 21 step LIB81A,
%% docs/libraries/ssl.md#application). The supervisor runs the PEM cache; connection supervisors join it in LIB81C.
-module(clause_ssl_app).
-behaviour(application).
-behaviour(supervisor).
-export([start/2, stop/1, init/1]).

% Start the top supervisor of the ssl application.
start(_Type, _Args) ->
    supervisor:start_link({local, ssl_sup}, ?MODULE, []).

stop(_State) ->
    ok.

% The ssl application's servers.
init([]) ->
    PemCache = #{
        id => ssl_pem_cache,
        start => {clause_ssl_pem_cache, start_link, []},
        modules => [clause_ssl_pem_cache]
    },
    {ok, {#{strategy => one_for_all, intensity => 10, period => 3600}, [PemCache]}}.
