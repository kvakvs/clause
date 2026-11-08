%% Taking part in other nodes' commits as OTP's participants do (plan 21 steps MIM1H5, MIM1H8,
%% docs/libraries/mnesia-protocol.md#two-phase-commit-mim1h8). An asym_trans participant is a process of its own that
%% mnesia_tm starts: it votes, accepts the pre-commit and applies the commit's records (in mnesia_tm) and schema
%% operations (in mnesia_monitor) once committed. A sym_trans commit is voted and applied by mnesia_tm itself. When the
%% coordinator goes before deciding, the participant commits only if another node taking part knows it committed.
-module(clause_mnesia_participant).
-export([start/4, outcome/2]).

-include("clause_mnesia.hrl").

% An asym_trans participant of Coordinator's commit of Tid, on the nodes Nodes; its process.
start(Coordinator, Tid, Commit, Nodes) ->
    spawn(fun() -> participate(Coordinator, Tid, Commit, Nodes) end).

participate(Coordinator, Tid, #commit{schema_ops = Ops} = Commit, Nodes) ->
    Ref = erlang:monitor(process, Coordinator),
    _ = clause_mnesia_recover:connect_nodes(Nodes),
    case clause_mnesia_ops:changes(Ops) of
        {ok, Changes} ->
            clause_mnesia_recover:note_decision(Tid, unclear),
            answer(Coordinator, {vote_yes, Tid, self()}),
            voted({Ref, Coordinator, Tid, Commit, Changes, Nodes});
        {error, Reason} ->
            answer(Coordinator, {vote_no, Tid, Reason}),
            ended(Tid, aborted)
    end.

% The pre-commit, then the commit; an abort ends the transaction, and so does the coordinator going before the
% pre-commit.
voted({Ref, Coordinator, Tid, #commit{schema_ops = Ops}, _, _} = Part) ->
    receive
        {Tid, pre_commit} ->
            answer(Coordinator, {acc_pre_commit, Tid, self(), Ops =/= []}),
            pre_committed(Part);
        {Tid, {do_abort, _}} ->
            ended(Tid, aborted);
        {'DOWN', Ref, process, _, _} ->
            ended(Tid, aborted)
    end.

pre_committed({Ref, Coordinator, Tid, #commit{schema_ops = Ops}, _, Nodes} = Part) ->
    receive
        {Tid, committed} ->
            apply_commit(Part),
            Ops =:= [] orelse answer(Coordinator, {schema_commit, Tid, self()}),
            ended(Tid, committed);
        {Tid, {do_abort, _}} ->
            ended(Tid, aborted);
        {'DOWN', Ref, process, _, _} ->
            case outcome(Tid, Nodes) of
                committed ->
                    apply_commit(Part),
                    ended(Tid, committed);
                aborted ->
                    ended(Tid, aborted)
            end
    end.

% The commit's records applied here as this node's own commits apply them, its schema operations in mnesia_monitor.
apply_commit({_, _, Tid, #commit{schema_ops = Ops} = Commit, Changes, _}) ->
    ok = clause_mnesia_tm:apply_commit(Tid, Commit, async),
    Ops =:= [] orelse gen_server:call(mnesia_monitor, {schema, Changes, Ops, Tid}, infinity),
    ok.

answer(Coordinator, Reply) ->
    Coordinator ! {mnesia_tm, node(), Reply}.

ended(Tid, Outcome) ->
    clause_mnesia_recover:note_decision(Tid, Outcome),
    clause_mnesia_locker:release_tid(Tid),
    gen_server:cast(mnesia_tm, {participant_ended, Tid}).

% A transaction's outcome after its coordinator went: committed when another node taking part answers that it
% committed, aborted otherwise.
outcome(#tid{pid = Coordinator} = Tid, Nodes) ->
    Others = Nodes -- [node(), node(Coordinator)],
    case lists:member({ok, committed}, [what_happened(Node, Tid) || Node <- Others]) of
        true -> committed;
        false -> aborted
    end.

what_happened(Node, Tid) ->
    try
        gen_server:call({mnesia_recover, Node}, {what_happened, unclear, Tid}, 5000)
    catch
        exit:_ -> {ok, unclear}
    end.
