-module(sup_bridged).
-behaviour(supervisor_bridge).
-export([init/1, terminate/2, bridged/0]).

%% The supervisor_bridge module of the supervisors case: init/1 starts a worker, terminate/2 stops it.
init(started) ->
    Pid = proc_lib:spawn(?MODULE, bridged, []),
    {ok, Pid, bridged_state};
init(Other) ->
    Other.

bridged() -> supervisors:worker(bridged, []).

terminate(Reason, bridged_state) ->
    sup_main ! {bridge_terminate, Reason},
    exit(whereis(bridged), kill).
