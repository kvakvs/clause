%% The worker of the applications test: a process of the application, so application:get_application/0 and friends
%% answer for it.
-module(tapp_worker).
-export([start_link/0, start_exiting/1, init/0, exiting/1]).

start_link() -> {ok, proc_lib:spawn_link(?MODULE, init, [])}.

%% A top process for start/2 that ends with Reason after a while.
start_exiting(Reason) -> proc_lib:spawn_link(?MODULE, exiting, [Reason]).

exiting(Reason) ->
    receive
    after 50 -> exit(Reason)
    end.

init() ->
    register(tapp_worker, self()),
    loop().

loop() ->
    receive
        {ask, From} ->
            From !
                {answer, [
                    application:get_application(),
                    application:get_env(color),
                    application:get_key(vsn),
                    lists:sort(application:get_all_env()),
                    application:start_type()
                ]},
            loop()
    end.
