%% The top supervisor of the applications test, with one worker that answers questions about its application.
-module(tapp_sup).
-behaviour(supervisor).
-export([start_link/0, init/1]).

start_link() -> supervisor:start_link({local, tapp_sup}, ?MODULE, worker).

init(worker) ->
    Child = #{id => worker, start => {tapp_worker, start_link, []}},
    {ok, {#{strategy => one_for_one}, [Child]}}.
