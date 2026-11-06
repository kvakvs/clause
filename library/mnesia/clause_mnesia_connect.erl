%% Connections between mnesia monitors, as OTP's (plan 21 step MIM1H4,
%% docs/libraries/mnesia-protocol.md#connecting-mim1h4): the protocol negotiation, as the asking node, from a process of
%% its own, and as the answering monitor, which links the asker's monitor; and the check of a partitioned network, when
%% two nodes that logged each other down meet again.
-module(clause_mnesia_connect).
-export([negotiate/2, answer/2, node_up/1, detect_inconsistency/2, has_remote_mnesia_down/1]).

-include("clause_mnesia.hrl").

-define(CURRENT, {8, 6}).
-define(PREVIOUS, {8, 5}).

% The asking node: each monitor of Nodes is asked for a protocol from a process of its own, which replies to From with
% the nodes that accepted one; this node's monitor and theirs are linked.
negotiate(Nodes, From) ->
    Monitor = self(),
    Protocols = [protocol_version(), ?PREVIOUS, {8, 4}, {8, 3}],
    Msg = {negotiate_protocol, Monitor, val(version), Protocols},
    spawn_link(fun() ->
        {Replies, _} = rpc:multicall(Nodes, mnesia_monitor, call, [Msg]),
        Accepted = accepted(Replies, Protocols),
        set(protocol_version, hd(Protocols)),
        gen_server:reply(From, Accepted),
        unlink(Monitor)
    end),
    ok.

% The nodes that accepted one of Protocols, each with its {protocol, Node} variable: {Protocol, NeedsConversion}.
accepted(Replies, Protocols) ->
    [
        protocol(Node, node(Mon), Protocol)
     || {Node, {accept, Mon, _, Protocol}} <- Replies, lists:member(Protocol, Protocols)
    ].

protocol(Node, MonitorNode, Protocol) ->
    set({protocol, Node}, {Protocol, Protocol =/= protocol_version()}),
    MonitorNode.

% The answering monitor: this node's protocol when the asker accepts it, the previous one when the asker prefers it, or
% a refusal; an accepted asker's monitor is linked, so that each sees the other's mnesia go down.
answer(Mon, Protocols) ->
    Protocol = protocol_version(),
    Version = val(version),
    case lists:member(Protocol, Protocols) orelse hd(Protocols) =:= ?PREVIOUS of
        true ->
            Agreed =
                case lists:member(Protocol, Protocols) of
                    true -> Protocol;
                    false -> ?PREVIOUS
                end,
            link(Mon),
            set({protocol, node(Mon)}, {Agreed, Agreed =/= protocol_version()}),
            {node(), {accept, self(), Version, Agreed}};
        false ->
            {node(), {reject, self(), Version, Protocol}}
    end.

protocol_version() ->
    try
        val(protocol_version)
    catch
        error:badarg -> ?CURRENT
    end.

% A node came up: when this running node logged its mnesia down, the network may have been partitioned.
node_up(Node) ->
    case clause_mnesia_recover:has_mnesia_down(Node) andalso clause_mnesia_monitor:is_running() =:= yes of
        true -> spawn(fun() -> detect_inconsistency([Node], running_partitioned_network) end);
        false -> ok
    end.

% Each of Nodes that this node logged down and that logged this node down: an inconsistent_database system event.
detect_inconsistency(Nodes, Context) ->
    Downs = [N || N <- Nodes, clause_mnesia_recover:has_mnesia_down(N)],
    {Replies, _} = rpc:multicall(Downs, mnesia_monitor, has_remote_mnesia_down, [node()]),
    [clause_mnesia_subscr:report_system_event({inconsistent_database, Context, N}) || {true, N} <- Replies],
    ok.

% {true, node()} when this node logged Node down and the schema has no master nodes.
has_remote_mnesia_down(Node) ->
    {clause_mnesia_recover:has_mnesia_down(Node) andalso not ets:member(mnesia_decision, {tab, schema}), node()}.

val(Name) -> ets:lookup_element(?GVAR, Name, 2).

set(Name, Value) -> ets:insert(?GVAR, {Name, Value}).
