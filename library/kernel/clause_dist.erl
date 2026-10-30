%% Distribution help for the runtime's builtins (plan 30 step DIST2c, docs/distribution.md#node-monitors):
%% monitor_node/3 and erlang:dmonitor_node/3 continue here for a passive connect. start_epmd/0 is a runtime builtin
%% (plan 30 step DIST3b, docs/distribution.md#epmd) that replaces its body at registration.
-module(clause_dist).
-export([passive_monitor/3, start_epmd/0]).

%% What a registration that found no epmd does under --epmd: ok once the bundled epmd (or another that won its port)
%% listens, none under --epmd none, error when the bundled epmd cannot listen.
start_epmd() -> clause_builtin_placeholder.

%% Ask net_kernel whether a connection to Node is up: monitor it again when it is, else {nodedown, Node} at once.
passive_monitor(Node, Flag, Options) ->
    case net_kernel:passive_cnct(Node) of
        true ->
            erlang:monitor_node(Node, Flag, Options);
        false ->
            self() ! {nodedown, Node},
            true
    end.
