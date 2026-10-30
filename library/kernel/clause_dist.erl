%% Distribution help for the runtime's builtins (plan 30 step DIST2c, docs/distribution.md#node-monitors):
%% monitor_node/3 and erlang:dmonitor_node/3 continue here for a passive connect.
-module(clause_dist).
-export([passive_monitor/3]).

%% Ask net_kernel whether a connection to Node is up: monitor it again when it is, else {nodedown, Node} at once.
passive_monitor(Node, Flag, Options) ->
    case net_kernel:passive_cnct(Node) of
        true ->
            erlang:monitor_node(Node, Flag, Options);
        false ->
            self() ! {nodedown, Node},
            true
    end.
