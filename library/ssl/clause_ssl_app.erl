%% The ssl application's callback and its top supervisor, registered as ssl_sup as in OTP (plan 21 step LIB81A,
%% docs/libraries/ssl.md#application). The supervisor runs the PEM cache and tls_connection_sup, whose temporary
%% children are the connection processes (LIB81C1).
-module(clause_ssl_app).
-behaviour(application).
-behaviour(supervisor).
-export([start/2, stop/1, init/1, start_connection_sup/0]).

% Start the top supervisor of the ssl application.
start(_Type, _Args) ->
    supervisor:start_link({local, ssl_sup}, ?MODULE, []).

stop(_State) ->
    ok.

start_connection_sup() ->
    supervisor:start_link({local, tls_connection_sup}, ?MODULE, connections).

% The ssl application's servers, and the connection supervisor's children.
init(connections) ->
    Connection = #{
        id => undefined,
        start => {clause_ssl_connection, start_link, []},
        restart => temporary,
        modules => [clause_ssl_connection]
    },
    {ok, {#{strategy => simple_one_for_one, intensity => 0, period => 3600}, [Connection]}};
init([]) ->
    PemCache = #{
        id => ssl_pem_cache,
        start => {clause_ssl_pem_cache, start_link, []},
        modules => [clause_ssl_pem_cache]
    },
    Connections = #{
        id => tls_connection_sup,
        start => {?MODULE, start_connection_sup, []},
        type => supervisor,
        modules => [?MODULE]
    },
    {ok, {#{strategy => one_for_all, intensity => 10, period => 3600}, [PemCache, Connections]}}.
