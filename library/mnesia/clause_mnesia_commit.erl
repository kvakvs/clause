%% Commits of schema operations over several nodes, as OTP's asym_trans (plan 21 step MIM1H5,
%% docs/libraries/mnesia-protocol.md#schema-merge-mim1h5): the coordinator asks each participant's mnesia_tm, which
%% answers from a process of its own: it votes, accepts the pre-commit and applies the operations in its mnesia_monitor
%% once committed. Schema merges use it; other operations wait for MIM1H8.
-module(clause_mnesia_commit).
-export([participate/5, coordinate/4]).

-include("clause_mnesia.hrl").

% Takes part in Coordinator's commit of Tid on the nodes Nodes, from a process of its own.
participate(Coordinator, _Protocol, Tid, #commit{schema_ops = Ops}, Nodes) ->
    spawn(fun() -> participant(Coordinator, Tid, Ops, Nodes) end),
    ok.

participant(Coordinator, Tid, Ops, Nodes) ->
    Ref = erlang:monitor(process, Coordinator),
    _ = clause_mnesia_recover:connect_nodes(Nodes),
    case clause_mnesia_merge:changes(Ops) of
        {ok, Changes} ->
            answer(Coordinator, {vote_yes, Tid, self()}),
            voted(Ref, Coordinator, Tid, Ops, Changes);
        {error, Reason} ->
            answer(Coordinator, {vote_no, Tid, Reason}),
            ended(Tid)
    end.

% The pre-commit, then the commit, which applies the operations; an abort or a coordinator gone ends the transaction.
voted(Ref, Coordinator, Tid, Ops, Changes) ->
    receive
        {Tid, pre_commit} ->
            answer(Coordinator, {acc_pre_commit, Tid, self(), true}),
            pre_committed(Ref, Coordinator, Tid, Ops, Changes);
        {Tid, {do_abort, _}} ->
            ended(Tid);
        {'DOWN', Ref, process, _, _} ->
            ended(Tid)
    end.

pre_committed(Ref, Coordinator, Tid, Ops, Changes) ->
    receive
        {Tid, committed} ->
            _ = gen_server:call(mnesia_monitor, {schema, Changes, Ops, Tid}, infinity),
            answer(Coordinator, {schema_commit, Tid, self()}),
            ended(Tid);
        {Tid, {do_abort, _}} ->
            ended(Tid);
        {'DOWN', Ref, process, _, _} ->
            ended(Tid)
    end.

ended(Tid) ->
    clause_mnesia_locker:release_tid(Tid).

answer(Coordinator, Reply) ->
    Coordinator ! {mnesia_tm, node(), Reply}.

% Commits Tid's operations on this node and on Participants, from the transaction's process: ok, or an abort of the
% transaction when a participant refuses or goes.
coordinate(Tid, Ops, Changes, Participants) ->
    Nodes = [node() | Participants],
    Decision = #decision{tid = Tid, outcome = presume_abort, disc_nodes = [], ram_nodes = Nodes},
    Monitors = [{N, erlang:monitor(process, {mnesia_tm, N})} || N <- Participants],
    [ask(N, Tid, #commit{node = N, decision = Decision, schema_ops = Ops}, Nodes) || N <- Participants],
    Result =
        case answers(Tid, vote_yes, Monitors, []) of
            {ok, Pids} -> commit(Tid, Ops, Changes, Monitors, Pids);
            {error, _} = Error -> Error
        end,
    [erlang:demonitor(Ref, [flush]) || {_, Ref} <- Monitors],
    case Result of
        ok -> ok;
        {error, Reason} -> mnesia:abort(Reason)
    end.

ask(Node, Tid, Commit, Nodes) ->
    {mnesia_tm, Node} ! {self(), {ask_commit, asym_trans, Tid, Commit, [], Nodes}}.

% Every participant voted yes: the pre-commit, the operations applied here, then the commit.
commit(Tid, Ops, Changes, Monitors, Pids) ->
    [Pid ! {Tid, pre_commit} || Pid <- Pids],
    case answers(Tid, acc_pre_commit, Monitors, []) of
        {ok, _} ->
            ok = gen_server:call(mnesia_monitor, {schema, Changes, Ops, Tid}, infinity),
            [Pid ! {Tid, committed} || Pid <- Pids],
            _ = answers(Tid, schema_commit, Monitors, []),
            ok;
        {error, Reason} = Error ->
            [Pid ! {Tid, {do_abort, Reason}} || Pid <- Pids],
            Error
    end.

% Each participant's answer Kind, as {ok, Pids} of their processes; the first refusal or participant gone is an error.
answers(_, _, [], Pids) ->
    {ok, lists:reverse(Pids)};
answers(Tid, Kind, Monitors, Pids) ->
    receive
        {mnesia_tm, Node, Reply} when element(1, Reply) =:= Kind, element(2, Reply) =:= Tid ->
            answers(Tid, Kind, lists:keydelete(Node, 1, Monitors), [element(3, Reply) | Pids]);
        {mnesia_tm, _, {vote_no, Tid, Reason}} ->
            {error, Reason};
        {'DOWN', Ref, process, _, _} when is_reference(Ref) ->
            case lists:keyfind(Ref, 2, Monitors) of
                {Node, _} -> {error, {bad_commit, Node}};
                false -> answers(Tid, Kind, Monitors, Pids)
            end
    end.
