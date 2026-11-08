%% Commits over several nodes as OTP's coordinator makes them (plan 21 steps MIM1H5, MIM1H8,
%% docs/libraries/mnesia-protocol.md#two-phase-commit-mim1h8): a transaction's writes go to every node holding a
%% written table as one #commit{} per node. Tables replicated on the same nodes commit with sym_trans (sync_sym_trans
%% in a sync transaction): the participants' transaction managers vote and apply at do_commit. Tables of different
%% nodes, majority tables and schema operations commit with asym_trans: each participant's process votes, accepts the
%% pre-commit and applies once committed. A participant going down before the commit restarts the transaction; a
%% refusal aborts it. The participants' side is clause_mnesia_participant.
-module(clause_mnesia_commit).
-export([updates/1, update/1, commit/4, schema/4]).

-include("clause_mnesia.hrl").

% A transaction's writes as {Tab, Key, Update}: the tables it clears first, then each written record's operations,
% records in term order (MIM1B). update/1 turns one store or commit item into its update, none for other entries.
updates(Store) ->
    Clears = ets:match_object(Store, {'_', '_', clear_table}),
    Oids = lists:usort(ets:select(Store, [{{'$1', '_', '_'}, [{is_tuple, '$1'}, {'=:=', {size, '$1'}, 2}], ['$1']}])),
    Ops = Clears ++ [Op || Oid <- Oids, Op <- ets:lookup(Store, Oid), element(3, Op) =/= clear_table],
    [U || Op <- Ops, U <- [update(Op)], U =/= none].

update({{Tab, Key}, Val, write}) -> {Tab, Key, {write, Val}};
update({{Tab, Key}, _, delete}) -> {Tab, Key, delete};
update({{Tab, Key}, Val, delete_object}) -> {Tab, Key, {delete_object, Val}};
update({{Tab, Key}, Pattern, clear_table}) -> {Tab, Key, {clear_table, Pattern}};
update({{Tab, Key}, {RecordName, Incr}, update_counter}) -> {Tab, Key, {update_counter, RecordName, Incr}};
update(_) -> none.

% An outer transaction's commit, from its process: Local applies this node's part. The other nodes that took part, or
% exit({aborted, Reason}) when one refused or went.
commit(Tid, Store, Kind, Local) ->
    Updates = updates(Store),
    Commits = records(Updates),
    case [C || #commit{node = N} = C <- Commits, N =/= node()] of
        [] ->
            Local(),
            [];
        Remote ->
            Nodes = [N || #commit{node = N} <- Commits],
            case protocol(Updates, Kind) of
                asym_trans -> asym(Tid, Remote, Nodes, Local, false);
                Protocol -> sym(Tid, Protocol, Remote, Nodes, Local)
            end,
            [N || #commit{node = N} <- Remote]
    end.

% A schema transaction's operations committed on this node and on the other nodes of Nodes (asym_trans); Local
% applies them here.
schema(_, [_], _, Local) ->
    Local();
schema(Tid, Nodes, Ops, Local) ->
    Remote = [#commit{node = N, schema_ops = Ops} || N <- Nodes, N =/= node()],
    asym(Tid, Remote, Nodes, Local, true).

%% Preparing the commits.

% One commit per node holding a written table, each update in the field of the table's storage there; a node is
% added in front when its first update comes, and this node's commit, empty or not, is always there.
records(Updates) ->
    Commits = lists:foldl(fun add_update/2, [], Updates),
    case lists:keymember(node(), #commit.node, Commits) of
        true -> Commits;
        false -> [#commit{node = node(), decision = presume_commit} | Commits]
    end.

add_update({Tab, Key, Update}, Commits) ->
    Item = clause_mnesia_disc:item(Tab, Key, Update),
    lists:foldl(fun({Node, Storage}, Acc) -> add_item(Node, Storage, Item, Acc) end, Commits, commit_nodes(Tab)).

add_item(Node, Storage, Item, Commits) ->
    case lists:keyfind(Node, #commit.node, Commits) of
        false -> [with_item(#commit{node = Node, decision = presume_commit}, Storage, Item) | Commits];
        Commit -> lists:keyreplace(Node, #commit.node, Commits, with_item(Commit, Storage, Item))
    end.

with_item(#commit{ram_copies = Items} = C, ram_copies, Item) ->
    C#commit{ram_copies = Items ++ [Item]};
with_item(#commit{disc_copies = Items} = C, disc_copies, Item) ->
    C#commit{disc_copies = Items ++ [Item]};
with_item(#commit{disc_only_copies = Items} = C, disc_only_copies, Item) ->
    C#commit{disc_only_copies = Items ++ [Item]}.

% The nodes a table's writes commit on; a local_content table's only on this node.
commit_nodes(Tab) ->
    Where = val({Tab, where_to_commit}),
    case val({Tab, local_content}) of
        true -> [W || {Node, _} = W <- Where, Node =:= node()];
        false -> Where
    end.

% asym_trans when a written table has a majority or the written tables commit on different nodes.
protocol(Updates, Kind) ->
    Tabs = lists:usort([Tab || {Tab, _, _} <- Updates]),
    Placements = lists:usort([lists:sort([N || {N, _} <- commit_nodes(Tab)]) || Tab <- Tabs]),
    Majority = lists:any(fun(Tab) -> val({Tab, majority}) end, Tabs),
    if
        Majority; length(Placements) > 1 -> asym_trans;
        Kind =:= sync -> sync_sym_trans;
        true -> sym_trans
    end.

%% The protocols.

% sym_trans: the participants' votes, then do_commit to each and this node's part; a sync one waits until each
% participant committed.
sym(Tid, Protocol, Remote, Nodes, Local) ->
    Monitors = ask(Tid, Protocol, Remote, Nodes),
    case answers(Tid, vote_yes, Monitors) of
        {ok, _} ->
            [{mnesia_tm, N} ! {Tid, do_commit} || {N, _} <- Monitors],
            clause_mnesia_recover:note_decision(Tid, committed),
            Local(),
            Protocol =:= sync_sym_trans andalso answers(Tid, committed, Monitors),
            end_watch(Monitors);
        {error, Reason} ->
            [{mnesia_tm, N} ! {Tid, {do_abort, Reason}} || {N, _} <- Monitors],
            end_watch(Monitors),
            exit({aborted, Reason})
    end.

% asym_trans: the participants' votes, the pre-commit, then committed to each and this node's part; participants
% applying schema operations confirm them. A participant that has not voted hears the abort through its transaction
% manager.
asym(Tid, Remote, Nodes, Local, Schema) ->
    {Disc, Ram} = split_nodes(Nodes),
    Decision = #decision{tid = Tid, outcome = presume_abort, disc_nodes = Disc, ram_nodes = Ram},
    Monitors = ask(Tid, asym_trans, [C#commit{decision = Decision} || C <- Remote], Nodes),
    Result =
        case answers(Tid, vote_yes, Monitors) of
            {ok, Pids} -> pre_commit(Tid, Monitors, Pids, Local, Schema);
            {error, _} = Error -> Error
        end,
    case Result of
        ok ->
            end_watch(Monitors);
        {error, Reason} ->
            [{mnesia_tm, N} ! {Tid, {do_abort, Reason}} || {N, _} <- Monitors],
            clause_mnesia_recover:note_decision(Tid, aborted),
            end_watch(Monitors),
            exit({aborted, Reason})
    end.

pre_commit(Tid, Monitors, Pids, Local, Schema) ->
    clause_mnesia_recover:note_decision(Tid, unclear),
    [Pid ! {Tid, pre_commit} || Pid <- Pids],
    case answers(Tid, acc_pre_commit, Monitors) of
        {ok, _} ->
            [Pid ! {Tid, committed} || Pid <- Pids],
            clause_mnesia_recover:note_decision(Tid, committed),
            Local(),
            Schema andalso answers(Tid, schema_commit, Monitors),
            ok;
        {error, Reason} = Error ->
            [Pid ! {Tid, {do_abort, Reason}} || Pid <- Pids],
            Error
    end.

% Each participant's transaction manager asked to commit, watched until the commit ends.
ask(Tid, Protocol, Remote, Nodes) ->
    {Disc, Ram} = split_nodes(Nodes),
    [
        begin
            Ref = erlang:monitor(process, {mnesia_tm, N}),
            {mnesia_tm, N} ! {self(), {ask_commit, Protocol, Tid, Commit, Disc, Ram}},
            {N, Ref}
        end
     || #commit{node = N} = Commit <- Remote
    ].

% The disc nodes of a commit, those whose schema is on disc, and its RAM nodes.
split_nodes(Nodes) ->
    lists:partition(fun(N) -> lists:member(N, val({schema, disc_copies})) end, Nodes).

% Each participant's answer Kind in turn, as {ok, Answerers}: their processes, or their nodes when the transaction
% manager answers itself. A refusal or a participant gone is an error; a gone participant's monitor message stays for
% a later phase.
answers(Tid, Kind, Monitors) ->
    answers(Tid, Kind, Monitors, []).

answers(_, _, [], Answerers) ->
    {ok, lists:reverse(Answerers)};
answers(Tid, Kind, [{Node, Ref} | Rest], Answerers) ->
    receive
        {mnesia_tm, Node, Reply} when element(1, Reply) =:= Kind, element(2, Reply) =:= Tid ->
            answers(Tid, Kind, Rest, [answerer(Node, Reply) | Answerers]);
        {mnesia_tm, _, {vote_no, Tid, Reason}} ->
            {error, refused(Reason)};
        {'DOWN', Ref, process, _, _} ->
            {error, {node_not_running, Node}}
    end.

answerer(_, Reply) when tuple_size(Reply) >= 3 -> element(3, Reply);
answerer(Node, _) -> Node.

% A participant's refusal names the reason it aborted with.
refused({aborted, Reason}) -> Reason;
refused(Reason) -> Reason.

end_watch(Monitors) ->
    [erlang:demonitor(Ref, [flush]) || {_, Ref} <- Monitors],
    ok.

val(Name) -> clause_mnesia_monitor:val(Name).
