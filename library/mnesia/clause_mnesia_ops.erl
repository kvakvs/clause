%% OTP's schema operations, the {op, ...} tuples of a schema transaction (plan 21 step MIM1H2,
%% docs/libraries/mnesia-protocol.md): built from the request and the changes clause_mnesia_change prepared, before
%% mnesia_monitor applies them, so that the transaction log holds the terms OTP's holds. Table definitions are the
%% schema's create lists. Another node's operations become this node's changes when it takes part in their commit
%% (MIM1H8).
-module(clause_mnesia_ops).
-export([ops/2, dumped/1, items/1, changes/1, changes/2, restore_items/1, restoring/1]).

-include("clause_mnesia.hrl").

%% The operations after which OTP dumps the log at once.
-define(DUMPED, [delete_table, transform, change_table_copy_type]).

% A request's operations; the definitions before the change are read from mnesia_gvar, as the transaction runs first.
ops({create_table, _}, Changes) ->
    [{op, create_table, list(Cs)} || {create, Cs} <- Changes];
ops({delete_table, _}, Changes) ->
    [deleted(Tab) || {delete, Tab} <- Changes];
ops({add_index, _, _}, [{add_index, Cs, Index}]) ->
    [{op, add_index, Index, list(Cs)}];
ops({del_index, _, _}, [{del_index, Cs, Pos}]) ->
    [{op, del_index, Pos, list(Cs)}];
ops({transform, _, Fun, _, _}, [{transform, Cs, _}]) ->
    [{op, transform, Fun, list(Cs)}];
ops({copy_type, _, Node, To}, [{redefine, #cstruct{name = Tab} = Cs}]) when To =/= unknown ->
    [{op, change_table_copy_type, Node, clause_mnesia_schema:storage(Node, old(Tab)), To, list(Cs)}];
ops({copy_type, _, Node, unknown}, Changes) ->
    [copy_op(Change, Node, del_table_copy) || Change <- Changes];
ops({del_copy, _, Node}, Changes) ->
    [copy_op(Change, Node, del_table_copy) || Change <- Changes];
ops({add_copy, _, Node, _}, Changes) ->
    [copy_op(Change, Node, add_table_copy) || Change <- Changes];
ops({move_copy, _, From, To}, [{add_copy, Both, _}, {redefine, Final}]) ->
    Storage = clause_mnesia_schema:storage(To, Both),
    [{op, add_table_copy, Storage, To, list(Both)}, {op, sync_trans}, {op, del_table_copy, Storage, From, list(Final)}];
ops({access_mode, _, _}, Changes) ->
    [changed(change_table_access_mode, #cstruct.access_mode, Cs) || {redefine, Cs} <- Changes];
ops({load_order, _, _}, Changes) ->
    [changed(change_table_load_order, #cstruct.load_order, Cs) || {redefine, Cs} <- Changes];
ops({majority, _, _}, Changes) ->
    [changed(change_table_majority, #cstruct.majority, Cs) || {redefine, Cs} <- Changes];
ops({write_property, _, Prop}, [{redefine, Cs}]) ->
    [{op, write_property, list(Cs), Prop}];
ops({delete_property, _, Key}, [{redefine, Cs}]) ->
    [{op, delete_property, list(Cs), Key}];
ops({add_snmp, _, Ustruct}, [{redefine, Cs}]) ->
    [{op, add_snmp, Ustruct, list(Cs)}];
ops({del_snmp, _}, [{redefine, Cs}]) ->
    [{op, del_snmp, list(Cs)}];
ops({dump_tables, _}, Changes) ->
    [{op, dump_table, clause_mnesia_db:info(Tab, size), list(old(Tab))} || {dump, Tab} <- Changes];
ops({change_frag, _, Change}, Changes) ->
    frag_ops(frag_op(Change), Changes);
ops({restore, _, _}, [{restore, Plan, _}]) ->
    lists:append([restore_ops(Entry) || Entry <- lists:reverse(Plan)]);
ops(_, _) ->
    [].

% The changes another node's commit of operations makes here (MIM1H5, MIM1H8), the nodes announced running last; an
% operation this node cannot make is {error, {aborted, Reason}}, its refusal. A copy added here in transaction Tid is
% loaded first (MIM1H9).
changes(Ops) ->
    changes(Ops, none).

changes(Ops, Tid) ->
    try lists:append([change(Op, Tid) || Op <- Ops]) of
        Changes -> {ok, [C || C <- Changes, element(1, C) =/= announce] ++ [C || {announce, _, _} = C <- Changes]}
    catch
        exit:{aborted, Reason} -> {error, {aborted, Reason}};
        throw:Reason -> {error, {aborted, Reason}}
    end.

change({op, add_table_copy, _, _, List}, _) when hd(List) =:= {name, schema} ->
    [{redefine, cs(List)}];
change({op, add_table_copy, Storage, Node, List}, Tid) ->
    #cstruct{name = Tab} = Cs = cs(List),
    [{add_copy, Cs, clause_mnesia_loader:copy(Tab, Node, Storage, Tid)}];
change({op, del_table_copy, _, _, List}, _) ->
    [{redefine, cs(List)}];
% Another node's change of this node's replica storage is checked here first, as this node's own change is (MIM1H10b).
change({op, change_table_copy_type, Node, From, To, List}, _) ->
    #cstruct{name = Tab} = Cs = cs(List),
    Node =:= node() andalso clause_mnesia_change:local_copy_type(Tab, From, To),
    [{redefine, Cs}];
change({op, sync_trans}, #tid{pid = Coordinator}) ->
    sync_trans(Coordinator),
    [];
change(Op, _) ->
    change(Op).

change({op, merge_schema, List}) ->
    [{merge, cs(List)}];
change({op, announce_im_running, _, _, Running, RemoteRunning}) ->
    [{announce, Running, RemoteRunning}];
change({op, create_table, List}) ->
    Cs = cs(List),
    clause_mnesia_schema:check_storage(Cs),
    [{create, Cs}];
change({op, delete_table, List}) ->
    [{delete, proplists:get_value(name, List)}];
change({op, add_index, Index, List}) ->
    [{add_index, cs(List), Index}];
change({op, del_index, Pos, List}) ->
    [{del_index, cs(List), Pos}];
change({op, transform, Fun, List}) ->
    Cs = cs(List),
    [{transform, Cs, clause_mnesia_change:transformed(Cs, Fun)}];
change({op, Name, List, _, _}) when
    Name =:= change_table_access_mode; Name =:= change_table_load_order; Name =:= change_table_majority
->
    [{redefine, cs(List)}];
change({op, Property, List, _}) when Property =:= write_property; Property =:= delete_property ->
    [{redefine, cs(List)}];
change({op, add_snmp, _, List}) ->
    [{redefine, cs(List)}];
change({op, del_snmp, List}) ->
    [{redefine, cs(List)}];
% Another node's fragment change (MIM1H12): the base table's definition and the records moving between fragments,
% applied to the fragments held here.
change({op, change_table_frag, _, List}) ->
    [{redefine, cs(List)}];
change({op, rec, _, {{Frag, Key}, Records, write}}) ->
    [{records, [{Frag, Key, {write, R}} || R <- Records]}];
change({op, rec, _, {{Frag, Key}, _, delete}}) ->
    [{records, [{Frag, Key, delete}]}];
% Another node's restore (MIM1H11b): a table cleared or made again here; its records come in the commit.
change({op, clear_table, List}) ->
    #cstruct{name = Tab, record_name = RecordName} = cs(List),
    [{restore, [{Tab, {clear, RecordName}}], []}];
change({op, restore_recreate, List}) ->
    #cstruct{name = Tab} = Cs = cs(List),
    [{restore, [{Tab, {recreate, Cs}}], []}];
change(Op) ->
    throw({not_supported, Op}).

cs(List) -> clause_mnesia_schema:list2cs(List).

% A move's participant meets its coordinator between the copy added and the copy deleted, as OTP's (MIM1H9).
sync_trans(Coordinator) ->
    Ref = erlang:monitor(process, Coordinator),
    Coordinator ! {sync_trans, self()},
    receive
        {sync_trans, Coordinator} -> erlang:demonitor(Ref, [flush]);
        {'DOWN', Ref, process, _, _} -> throw({node_not_running, node(Coordinator)})
    end.

% Whether OTP dumps the log right after these operations.
dumped(Ops) ->
    lists:any(fun(Op) -> lists:member(element(2, Op), ?DUMPED) end, Ops).

% The log items the operations write or clear (MIM1H2): a cleared or recreated table's clear, a moved record's write
% or delete; the replay and the dump of the log treat them as the commit's own items, before them.
items(Ops) ->
    lists:append([op_items(Op) || Op <- Ops]).

op_items({op, Clear, List}) when Clear =:= clear_table; Clear =:= restore_recreate ->
    Tab = proplists:get_value(name, List),
    [{{Tab, '_'}, '_', clear_table}];
op_items({op, rec, _, {Oid, Records, write}}) ->
    [{Oid, Record, write} || Record <- Records];
op_items({op, rec, _, {Oid, _, delete}}) ->
    [{Oid, Oid, delete}];
op_items(_) ->
    [].

deleted(Tab) -> {op, delete_table, list(old(Tab))}.

% A copy's change: the table goes with its last copy.
copy_op({delete, Tab}, _, _) ->
    deleted(Tab);
copy_op(Change, Node, Name) ->
    #cstruct{name = Tab} = Cs = element(2, Change),
    Storage =
        case Name of
            add_table_copy -> clause_mnesia_schema:storage(Node, Cs);
            del_table_copy -> clause_mnesia_schema:storage(Node, old(Tab))
        end,
    case Storage of
        % A fragmented table without a replica on the node leaves its pool only (MIM1H12).
        unknown -> {op, change_table_frag, {del_node, Node}, list(Cs)};
        _ -> {op, Name, Storage, Node, list(Cs)}
    end.

% An operation that changes one field: the definition, the old value and the new.
changed(Name, Field, #cstruct{name = Tab} = Cs) ->
    {op, Name, list(Cs), element(Field, old(Tab)), element(Field, Cs)}.

% change_table_frag/2's operation as OTP names it.
frag_op({activate, _}) -> activate;
frag_op(Change) -> Change.

% Each base table's change, then the fragments created or deleted, then the records moved, as OTP orders them.
frag_ops(FragOp, Changes) ->
    Tables = [{op, change_table_frag, FragOp, list(Cs)} || {redefine, Cs} <- Changes],
    Created = [{op, create_table, list(Cs)} || {create, Cs} <- Changes],
    Deleted = [deleted(Tab) || {delete, Tab} <- Changes],
    New = [Cs || {create, Cs} <- Changes],
    Moved = [rec(Move, New) || {records, Moves} <- Changes, Move <- Moves],
    Tables ++ Created ++ Deleted ++ Moved.

% A moved record as OTP's rec operation, with the storage of its fragment here.
rec({Frag, Key, Update}, New) ->
    Storage =
        case lists:keyfind(Frag, #cstruct.name, New) of
            false -> clause_mnesia_monitor:val({Frag, storage_type});
            Cs -> clause_mnesia_schema:storage(Cs)
        end,
    case Update of
        {write, Record} -> {op, rec, Storage, {{Frag, Key}, [Record], write}};
        delete -> {op, rec, Storage, {{Frag, Key}, [{Frag, Key}], delete}}
    end.

% The records a restore writes on each node holding their table after it, as {Node, Storage, Item} (MIM1H11b): the
% other nodes get them in their commits, as OTP's.
restore_items(Changes) ->
    [
        {Node, Storage, clause_mnesia_disc:item(Tab, Key, Update)}
     || {restore, Plan, Updates} <- Changes,
        {Tab, Key, Update} <- Updates,
        {Node, Storage} <- restored_nodes(Tab, Plan)
    ].

restored_nodes(Tab, Plan) ->
    Cs =
        case lists:keyfind(Tab, 1, Plan) of
            {_, {recreate, New}} -> New;
            _ -> old(Tab)
        end,
    #cstruct{ram_copies = Ram, disc_copies = Disc, disc_only_copies = DiscOnly} = Cs,
    [{N, ram_copies} || N <- Ram] ++ [{N, disc_copies} || N <- Disc] ++ [{N, disc_only_copies} || N <- DiscOnly].

% Whether operations restore tables, which come before the commit's records (MIM1H11b).
restoring(Ops) ->
    lists:any(fun(Op) -> lists:member(element(2, Op), [clear_table, restore_recreate]) end, Ops).

% A restored table's operation: its records cleared, or the table made again from the backup's definition.
restore_ops({deleted, Tab}) -> [deleted(Tab)];
restore_ops({Tab, {clear, _}}) -> [{op, clear_table, list(old(Tab))}];
restore_ops({_, {recreate, Cs}}) -> [{op, restore_recreate, list(Cs)}];
restore_ops({_, {keep, _}}) -> [].

old(Tab) -> clause_mnesia_monitor:val({Tab, cstruct}).

list(Cs) -> clause_mnesia_schema:cs2list(Cs).
