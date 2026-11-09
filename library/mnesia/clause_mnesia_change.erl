%% mnesia's schema changes as schema transactions prepare them (plan 21 step MIM1C, docs/libraries/mnesia.md#schema):
%% each change takes OTP's locks and makes OTP's checks in OTP's order inside the transaction, then hands
%% mnesia_monitor the changes to apply (clause_mnesia_schema:apply_change/1). Transform functions run here too.
-module(clause_mnesia_change).
-export([
    prepare/1, check_new_table/1, check_deleted_table/1, check_active/1, lock_table/2, transformed/2, local_copy_type/3
]).

-include("clause_mnesia.hrl").

%% The explanation of OTP's not_active abort for replicas on disc nodes that are not loaded.
-define(NOT_ACTIVE, "All replicas on diskfull nodes are not active yet").

% A schema operation's changes, or {not_implemented, Feature}.
prepare({create_table, Def}) ->
    lock(write, schema),
    create_table(Def);
prepare({delete_table, schema}) ->
    abort({bad_type, schema});
prepare({delete_table, Tab}) ->
    lock(write, schema),
    exists(Tab) orelse abort({no_exists, Tab}),
    [delete_table(T) || T <- whole_table(Tab)];
prepare({change_frag, Tab, Change}) when is_atom(Tab), Tab =/= schema ->
    lock(write, schema),
    mnesia_frag:change_table_frag(Tab, Change);
prepare({change_frag, Tab, _}) ->
    abort({bad_type, Tab});
prepare({Snmp, schema, _}) when Snmp =:= add_snmp; Snmp =:= del_snmp ->
    abort({bad_type, schema});
prepare({add_snmp, Tab, Ustruct}) ->
    lock(write, schema),
    lock(read, Tab),
    Cs = changed(Tab),
    Cs#cstruct.snmp =:= [] orelse abort({already_exists, Tab, snmp}),
    clause_mnesia_snmp:check_ustruct(Ustruct) orelse abort({badarg, Tab, snmp, Ustruct}),
    [{redefine, verify(Cs#cstruct{snmp = Ustruct})}];
prepare({del_snmp, Tab}) ->
    lock(write, schema),
    lock(read, Tab),
    [{redefine, verify((changed(Tab))#cstruct{snmp = []})}];
prepare({Op, schema, _}) when Op =:= add_index; Op =:= del_index ->
    abort({bad_type, schema});
prepare({Op, Tab, Attribute}) when Op =:= add_index; Op =:= del_index ->
    lock(write, schema),
    lock(read, Tab),
    [index(Op, Tab, Attribute)];
prepare({transform, schema, _, _, _}) ->
    abort({bad_type, schema});
prepare({transform, Tab, Fun, Attributes, RecordName}) ->
    lock(write, schema),
    lock(write, Tab),
    transform(Tab, Fun, Attributes, RecordName);
prepare({add_copy, Tab, Node, Storage}) when is_atom(Tab), is_atom(Node) ->
    lock(write, schema),
    [add_copy(Tab, Node, Storage)];
prepare({add_copy, Tab, Node, _}) ->
    abort({badarg, Tab, Node});
prepare({del_copy, Tab, Node}) when is_atom(Node) ->
    lock(write, schema),
    del_copy(Tab, Node);
prepare({del_copy, Tab, Node}) ->
    abort({badarg, Tab, Node});
prepare({move_copy, schema, _, _}) ->
    abort({bad_type, schema});
prepare({move_copy, Tab, From, To}) when is_atom(From), is_atom(To) ->
    lock(write, schema),
    lock(write, Tab),
    move_copy(Tab, From, To);
prepare({move_copy, Tab, From, To}) ->
    abort({badarg, Tab, From, To});
prepare({copy_type, Tab, Node, To}) when is_atom(Node) ->
    lock(write, schema),
    lock(write, Tab),
    copy_type(Tab, Node, To);
prepare({copy_type, Tab, Node, _}) ->
    abort({badarg, Tab, Node});
prepare({Property, Tab, Prop}) when Property =:= write_property; Property =:= delete_property ->
    lock(write, schema),
    Cs = changed(Tab),
    Props = lists:keydelete(property_key(Property, Prop), 1, Cs#cstruct.user_properties),
    [{redefine, verify(Cs#cstruct{user_properties = new_properties(Property, Prop, Props)})}];
prepare({access_mode, Tab, Mode}) ->
    lock(write, schema),
    lock_absent(schema),
    lock_absent(Tab),
    Cs = changed(Tab),
    Cs#cstruct.access_mode =:= Mode andalso abort({already_exists, Tab, Mode}),
    [{redefine, verify(Cs#cstruct{access_mode = Mode})}];
prepare({Field, schema, _}) when Field =:= load_order; Field =:= majority ->
    abort({bad_type, schema});
prepare({load_order, Tab, Order}) ->
    lock(write, schema),
    [{redefine, verify((changed(Tab))#cstruct{load_order = Order})}];
prepare({majority, Tab, Majority}) ->
    lock(write, schema),
    Cs = changed(Tab),
    [{redefine, verify(C#cstruct{majority = Majority})} || C <- [Cs | majority_fragments(Cs)]];
prepare({dump_tables, Tabs}) ->
    lock(write, schema),
    dump_tables(Tabs);
prepare({restore, Restore, Schema}) ->
    lock(write, schema),
    Plan = restore_plan(Schema, Restore, []),
    {value, [T || {T, _} <- Plan, T =/= deleted], [
        {restore, Plan, clause_mnesia_backup:restore_updates(Plan, Restore)}
    ]}.

% restore/2's tables (MIM1F), the last first as OTP returns them: each with its operation's locks, the schema's own
% definition left alone; a table the backup deletes goes.
restore_plan([{schema, schema, _} | Rest], Restore, Plan) ->
    restore_plan(Rest, Restore, Plan);
restore_plan([{schema, Tab, List} | Rest], Restore, Plan) ->
    case clause_mnesia_backup:restore_op(Tab, Restore) of
        skip_tables -> restore_plan(Rest, Restore, Plan);
        Op -> restore_plan(Rest, Restore, [{Tab, restore_table(Tab, Op, List)} | Plan])
    end;
restore_plan([{schema, Tab} | Rest], Restore, Plan) ->
    exists(Tab) andalso lock(write, Tab),
    restore_plan(Rest, Restore, [{deleted, Tab} | lists:keydelete(Tab, 1, Plan)]);
restore_plan([], _, Plan) ->
    Plan.

restore_table(Tab, clear_tables, _) ->
    lock(write, Tab),
    {clear, val({Tab, record_name})};
restore_table(Tab, keep_tables, _) ->
    lock(write, Tab),
    {keep, val({Tab, record_name})};
restore_table(Tab, recreate_tables, List) ->
    exists(Tab) andalso lock(write, Tab),
    Cs = clause_mnesia_schema:list2cs(lists:keystore(cookie, 1, List, {cookie, undefined})),
    {recreate, Cs}.

% A fragmented base table's majority changes in each fragment too; a fragment's alone is refused (MIM1G).
majority_fragments(#cstruct{name = Tab, frag_properties = Props}) ->
    case lists:keyfind(base_table, 1, Props) of
        {_, Tab} -> [check_active_cs(next_version(val({F, cstruct}))) || F <- mnesia_frag:frag_names(Tab) -- [Tab]];
        false -> [];
        {_, _} -> abort({bad_type, Tab})
    end.

check_active_cs(Cs) ->
    ensure_active(Cs),
    Cs.

% dump_tables/1: RAM tables only, each under its read lock (MIM1D).
dump_tables([schema | _]) ->
    abort({bad_type, schema});
dump_tables([Tab | Tabs]) ->
    lock(read, Tab),
    Cs = val({Tab, cstruct}),
    case Cs#cstruct.disc_copies ++ Cs#cstruct.disc_only_copies of
        [] -> ok;
        Disc -> abort({"Only allowed on ram_copies", Tab, Disc})
    end,
    clause_mnesia_disc:use_dir() orelse abort({has_no_disc, node()}),
    [{dump, Tab} | dump_tables(Tabs)];
dump_tables([]) ->
    [].

%% Creating and deleting tables and indexes (MIM1A's checks).

% create_table/1: the definition checked, then the table on this node; a fragmented table's fragments, each checked
% in turn (MIM1G). Each new table is write locked on its running nodes (MIM1H8).
create_table(Def) ->
    Cs = clause_mnesia_schema:cstruct(Def),
    Created =
        case Cs#cstruct.frag_properties of
            [] -> [check_new_table(Cs)];
            _ -> [check_new_table(F) || F <- mnesia_frag:expand_cstruct(Cs)]
        end,
    [lock_new(C) || C <- Created],
    [{create, C} || C <- Created].

% The new table's write lock on its running nodes, the last first, as OTP takes it.
lock_new(#cstruct{name = Tab} = Cs) ->
    {_, Tid, #tidstore{store = Store}} = get(mnesia_activity_state),
    Running = val({current, db_nodes}),
    Nodes = [N || N <- copy_holders(Cs), lists:member(N, Running)],
    clause_mnesia_locker:wlock_new(Tid, Store, Tab, lists:reverse(Nodes)).

% A new table's checks: it does not exist, its definition is sound, and its disc nodes run.
check_new_table(#cstruct{name = Tab} = Cs0) ->
    exists(Tab) andalso abort({already_exists, Tab}),
    Cs = verify(Cs0),
    Running = val({current, db_nodes}),
    [
        abort({not_active, Tab, N})
     || N <- Cs#cstruct.disc_copies ++ Cs#cstruct.disc_only_copies, not lists:member(N, Running)
    ],
    check_local_storage(Cs),
    clause_mnesia_schema:check_storage(Cs),
    Cs.

% The tables delete_table/1 deletes: a fragmented table's fragments with it, when no other table's foreign key names
% it (MIM1G).
whole_table(Tab) ->
    case val({Tab, frag_properties}) of
        [] ->
            [Tab];
        _ ->
            mnesia_frag:lookup_frag_hash(Tab),
            case mnesia_frag:lookup_foreigners(Tab) of
                [] -> mnesia_frag:frag_names(Tab);
                Foreigners -> abort({combine_error, Tab, "Too many foreigners", Foreigners})
            end
    end.

% A fragment deleted by change_table_frag/2, as delete_table/1 checks it (MIM1G).
check_deleted_table(Tab) ->
    {delete, Tab} = delete_table(Tab),
    ok.

check_active(Cs) ->
    ensure_active(Cs).

lock_table(Kind, Tab) ->
    lock(Kind, Tab).

% A RAM schema holds no disc tables on this node.
check_local_storage(#cstruct{name = Tab} = Cs) ->
    case clause_mnesia_schema:storage(Cs) of
        ram_copies -> ok;
        unknown -> ok;
        Disc -> clause_mnesia_disc:use_dir() orelse abort({bad_type, Tab, Disc, node()})
    end.

% A table's deletion: its write lock, then a replica to delete written somewhere.
delete_table(Tab) ->
    lock(write, Tab),
    ensure_active(val({Tab, cstruct})),
    ensure_writable(Tab),
    {delete, Tab}.

% add_table_index/2 and del_table_index/2: a position after the checks of the table and the index; its replicas on
% disc nodes are loaded (MIM1H10a).
index(Op, Tab, Attribute) ->
    readable(Tab),
    Pos = clause_mnesia_schema:attribute_position(Attribute, val({Tab, attributes})),
    Cs = next_version(val({Tab, cstruct})),
    ensure_active(Cs),
    case Op of
        add_index ->
            lists:keymember(index_position(Pos), 1, Cs#cstruct.index) andalso abort({already_exists, Tab, Pos}),
            Cs2 = verify(Cs#cstruct{index = lists:sort([Pos | Cs#cstruct.index])}),
            {add_index, Cs2, lists:keyfind(Pos, 1, Cs2#cstruct.index)};
        del_index ->
            lists:keymember(Pos, 1, Cs#cstruct.index) orelse abort({no_exists, Tab, Pos}),
            {del_index, verify(Cs#cstruct{index = lists:keydelete(Pos, 1, Cs#cstruct.index)}), Pos}
    end.

% A table read lock's check: the table has a replica to read.
readable(Tab) ->
    val({Tab, where_to_read}) =:= nowhere andalso abort({no_exists, Tab}).

index_position({P, _}) -> P;
index_position(P) -> P.

%% Transforming a table (MIM1C).

% transform_table/4: new attributes and record name, and each record through Fun unless it is ignore.
transform(Tab, Fun, Attributes, RecordName) ->
    ensure_writable(schema),
    Cs = next_version(val({Tab, cstruct})),
    ensure_active(Cs),
    ensure_writable(Tab),
    Cs2 = verify(Cs#cstruct{attributes = Attributes, record_name = RecordName}),
    [{transform, Cs2, transformed(Cs2, Fun)}].

% Each key's {Key, Written, Deleted} records of this node's replica when Fun changes them, keys in term order; a
% failure aborts the change and mnesia_event reports it, as OTP's commit does. Another node's transform of a table
% held here runs it too (MIM1H8).
transformed(_, ignore) ->
    [];
transformed(#cstruct{name = Tab} = Cs, Fun) ->
    case val({Tab, storage_type}) of
        unknown -> [];
        _ -> transformed_replica(Cs, Fun)
    end.

transformed_replica(#cstruct{name = Tab} = Cs, Fun) ->
    try
        Keys = lists:usort(clause_mnesia_db:select(Tab, [{'$1', [], [{element, 2, '$1'}]}], forward)),
        [
            Change
         || Key <- Keys, Change <- [record_changes(Fun, Cs, Key, clause_mnesia_db:lookup(Tab, Key))], Change =/= none
        ]
    catch
        _:Reason:Stack ->
            clause_mnesia_monitor:important("Transform function failed: '~tp' in '~tp'", [Reason, Stack]),
            abort({"Bad transform function", Tab, Fun, node(), Reason})
    end.

% A bag's records that Fun deletes are left out of the key's written records, as in OTP: only writes change a bag.
record_changes(Fun, #cstruct{type = Type, record_name = RecordName, attributes = Attributes}, Key, Records) ->
    Arity = length(Attributes) + 1,
    case lists:foldl(fun(R, Acc) -> record_change(Fun(R), R, RecordName, Key, Arity, Acc) end, {[], []}, Records) of
        {[], _} when Type =:= bag -> none;
        {[], []} -> none;
        {Written, Deleted} -> {Key, lists:reverse(Written), Deleted}
    end.

% A record's transformation: unchanged, written, deleted or refused with OTP's reasons.
record_change(New, Old, RecordName, Key, Arity, {Written, Deleted}) ->
    if
        tuple_size(New) =/= Arity -> exit({"Bad arity", Old, New});
        New =:= Old -> {Written, Deleted};
        element(1, New) =:= RecordName, element(2, New) =:= Key -> {[New | Written], Deleted};
        New =:= delete -> {Written, [Old | Deleted]};
        true -> exit({"Bad key or Record Name", Old, New})
    end.

%% Table copies (MIM1C).

% add_table_copy/3: a replica on another node of the schema, or on this node for a table held elsewhere; a copy here
% of a table read on another node is loaded from it before the commit (MIM1H9).
add_copy(Tab, Node, Storage) ->
    ensure_writable(schema),
    Cs = next_version(val({Tab, cstruct})),
    lists:member(Node, copy_holders(Cs)) andalso abort({already_exists, Tab, Node}),
    Cs2 = verify(with_copy(Cs, Node, Storage)),
    Running = lists:member(Node, val({current, db_nodes})),
    if
        Tab =:= schema, Storage =/= ram_copies -> abort({badarg, Tab, Storage});
        Tab =:= schema, Running -> abort({already_exists, Tab, Node});
        Tab =:= schema; Storage =:= ram_copies; Running -> ok;
        true -> abort({not_active, schema, Node})
    end,
    case Tab of
        schema -> {redefine, Cs2};
        _ -> {add_copy, Cs2, clause_mnesia_loader:copy(Tab, Node, Storage, tid())}
    end.

% del_table_copy/2: a replica goes; the last one deletes the table, and the schema's takes the node's tables along.
del_copy(Tab, Node) ->
    ensure_writable(schema),
    Cs = next_version(val({Tab, cstruct})),
    Cs2 = without_copy(Cs, Node, schema_storage(Node, Cs)),
    case copy_holders(Cs2) of
        [] when Tab =:= schema ->
            abort({combine_error, Tab, "Last replica"});
        [] ->
            ensure_active(Cs),
            [delete_table(Tab)];
        _ when Tab =:= schema ->
            not_active_schema(Node),
            [{redefine, verify(Cs2)} | without_node(lists:delete(schema, val({schema, tables})), Node)];
        _ ->
            ensure_active(Cs),
            Cs3 = verify(Cs2),
            lock(write, Tab),
            [{redefine, Cs3}]
    end.

% A node leaves the schema only when mnesia does not run there.
not_active_schema(Node) ->
    case val({schema, active_replicas}) of
        [] -> abort({no_exists, schema});
        Active -> lists:member(Node, Active) andalso abort({active, "Mnesia is running", Node})
    end.

% The node's replicas of the tables go; a table left without replicas goes too.
without_node(Tabs, Node) ->
    lists:append([table_without_node(Tab, Node) || Tab <- Tabs]).

table_without_node(Tab, Node) ->
    Cs = next_version(val({Tab, cstruct})),
    case clause_mnesia_schema:storage(Node, Cs) of
        unknown ->
            [];
        Storage ->
            Cs2 = without_copy(Cs, Node, Storage),
            case copy_holders(Cs2) of
                [] ->
                    [{delete, Tab}];
                _ ->
                    Cs3 = verify(Cs2),
                    val({Tab, active_replicas}) =/= [] andalso lock(write, Tab),
                    [{redefine, Cs3}]
            end
    end.

% move_table_copy/3: the replica of a running node moves to another running node, as a copy added there, loaded
% before the commit when it is this node, then the old one deleted (MIM1H9).
move_copy(Tab, From, To) ->
    ensure_writable(schema),
    Cs = next_version(val({Tab, cstruct})),
    lists:member(To, copy_holders(Cs)) andalso abort({already_exists, Tab, To}),
    lists:member(From, val({Tab, where_to_write})) orelse abort({not_active, Tab, From}),
    val({Tab, local_content}) andalso abort({"Cannot move table with local content", Tab}),
    ensure_active(Cs),
    Storage = clause_mnesia_schema:storage(From, Cs),
    lists:member(To, val({current, db_nodes})) orelse abort({not_active, schema, To}),
    Both = verify(with_copy(Cs, To, Storage)),
    Load = clause_mnesia_loader:copy(Tab, To, Storage, tid()),
    [{add_copy, Both, Load}, {redefine, verify(without_copy(Both, From, Storage))}].

% change_table_copy_type/3: a node's replica gets another storage; unknown deletes it.
copy_type(Tab, Node, unknown) ->
    del_copy(Tab, Node);
copy_type(Tab, Node, To) ->
    ensure_writable(schema),
    Cs = next_version(val({Tab, cstruct})),
    From = storage_at(Node, Tab),
    lists:member(To, [ram_copies, disc_copies, disc_only_copies]) orelse abort({bad_type, {storage_type, To}}),
    case compared_storage(From, To) of
        same -> abort({already_exists, Tab, Node, To});
        diff -> ok;
        incompatible -> ensure_active(Cs)
    end,
    Cs2 = verify(with_copy(without_copy(Cs, Node, From), Node, To)),
    Node =:= node() andalso local_copy_type(Tab, From, To),
    [{redefine, Cs2}].

compared_storage(Same, Same) -> same;
compared_storage(unknown, _) -> diff;
compared_storage(ram_copies, disc_copies) -> diff;
compared_storage(disc_copies, disc_only_copies) -> diff;
compared_storage(_, _) -> incompatible.

% The schema goes to disc only with an opt_disc schema location, and back to RAM only without disc tables here; a
% table's replica goes to disc only on a disc schema and without a dump of the RAM table (MIM1D).
local_copy_type(schema, _, disc_copies) ->
    clause_mnesia_monitor:get_env(schema_location) =:= opt_disc orelse
        abort({combine_error, schema, node(), "schema_location must be opt_disc"});
local_copy_type(schema, _, ram_copies) ->
    Tabs = lists:delete(schema, val({schema, tables})),
    Storages = [{T, clause_mnesia_schema:storage(val({T, cstruct}))} || T <- Tabs],
    case [T || {T, S} <- Storages, S =:= disc_copies orelse S =:= disc_only_copies] of
        [] -> ok;
        Disc -> abort({"Disc resident tables", Disc, node()})
    end;
local_copy_type(Tab, ram_copies, To) when To =:= disc_copies; To =:= disc_only_copies ->
    clause_mnesia_disc:use_dir() orelse abort({has_no_disc, node()}),
    clause_mnesia_disc:dump_exists(Tab) andalso abort({combine_error, Tab, node(), "Table dump exists"});
local_copy_type(_, _, _) ->
    ok.

% A node's storage as the table's properties name it.
storage_at(Node, Tab) ->
    Holders = [{S, val({Tab, S})} || S <- [disc_copies, ram_copies, disc_only_copies]],
    case [S || {S, Nodes} <- Holders, lists:member(Node, Nodes)] of
        [S | _] -> S;
        [] -> unknown
    end.

% The schema's storage on a node outside it is ram_copies.
schema_storage(Node, #cstruct{name = schema} = Cs) ->
    case clause_mnesia_schema:storage(Node, Cs) of
        unknown -> ram_copies;
        Storage -> Storage
    end;
schema_storage(Node, Cs) ->
    clause_mnesia_schema:storage(Node, Cs).

% A replica added first in its storage's node list; other storages name backends, and there are none.
with_copy(Cs, Node, ram_copies) ->
    Cs#cstruct{ram_copies = [Node | lists:delete(Node, Cs#cstruct.ram_copies)]};
with_copy(Cs, Node, disc_copies) ->
    Cs#cstruct{disc_copies = [Node | lists:delete(Node, Cs#cstruct.disc_copies)]};
with_copy(Cs, Node, disc_only_copies) ->
    Cs#cstruct{disc_only_copies = [Node | lists:delete(Node, Cs#cstruct.disc_only_copies)]};
with_copy(#cstruct{name = Tab}, _, Storage) ->
    abort({badarg, Tab, Storage}).

without_copy(Cs, Node, ram_copies) ->
    Cs#cstruct{ram_copies = lists:delete(Node, Cs#cstruct.ram_copies)};
without_copy(Cs, Node, disc_copies) ->
    Cs#cstruct{disc_copies = lists:delete(Node, Cs#cstruct.disc_copies)};
without_copy(Cs, Node, disc_only_copies) ->
    Cs#cstruct{disc_only_copies = lists:delete(Node, Cs#cstruct.disc_only_copies)};
without_copy(#cstruct{name = Tab}, _, Storage) ->
    abort({badarg, Tab, Storage}).

copy_holders(#cstruct{ram_copies = Ram, disc_copies = Disc, disc_only_copies = DiscOnly}) ->
    Ram ++ Disc ++ DiscOnly.

%% User properties (MIM1C).

property_key(write_property, Prop) -> element(1, Prop);
property_key(delete_property, Key) -> Key.

new_properties(write_property, Prop, Props) -> lists:merge(Props, [Prop]);
new_properties(delete_property, _, Props) -> Props.

%% Helpers.

% The definition of an active table, with the next version, for a change of its properties.
changed(Tab) ->
    ensure_writable(schema),
    Cs = next_version(val({Tab, cstruct})),
    ensure_active(Cs),
    Cs.

% A changed definition gets the next major version, stamped with this node and the time.
next_version(#cstruct{version = {{Major, _}, _}} = Cs) ->
    Cs#cstruct{version = {{Major + 1, 0}, {node(), erlang:timestamp()}}}.

% A change needs the table active here and its replicas on disc nodes loaded.
ensure_active(#cstruct{name = Tab} = Cs) ->
    Active = val({Tab, active_replicas}),
    Active =:= [] andalso abort({no_exists, Tab}),
    case [N || N <- copy_holders(Cs), lists:member(N, val({schema, disc_copies})), not lists:member(N, Active)] of
        [] -> ok;
        Missing -> abort({not_active, ?NOT_ACTIVE, Tab, Missing})
    end.

ensure_writable(Tab) ->
    val({Tab, where_to_write}) =:= [] andalso abort({read_only, Tab}).

exists(Tab) ->
    ets:member(?GVAR, {Tab, cstruct}).

verify(Cs) ->
    clause_mnesia_schema:verify(Cs).

% A table lock of the schema transaction: write locks check that the table is written somewhere.
lock(Kind, Tab) ->
    {_, Tid, #tidstore{store = Store}} = get(mnesia_activity_state),
    case Kind of
        read -> clause_mnesia_locker:rlock_table(Tid, Store, Tab);
        write -> clause_mnesia_locker:wlock_table(Tid, Store, Tab)
    end.

% A write lock on a table with active replicas, whether it is written anywhere or not.
lock_absent(Tab) ->
    {_, Tid, #tidstore{store = Store}} = get(mnesia_activity_state),
    clause_mnesia_locker:wlock_no_exist(Tid, Store, Tab, val({Tab, active_replicas})).

val(Name) -> clause_mnesia_monitor:val(Name).

tid() ->
    {_, Tid, _} = get(mnesia_activity_state),
    Tid.

abort(Reason) -> mnesia:abort(Reason).
