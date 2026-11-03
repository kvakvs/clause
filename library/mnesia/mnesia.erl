%% Clause's mnesia (plan 21 step MIM1, docs/libraries/mnesia.md): a database of ets tables owned by mnesia_monitor,
%% with OTP's results, errors and table properties. Step MIM1A provides starting and stopping, the RAM schema and its
%% information, dirty access and indexes; MIM1B transactions, activities and access modules. Step MIM1C adds the other
%% schema changes, user properties, master nodes and configuration changes; MIM1D the disc schema and disc tables; MIM1E
%% events, text files, query tables and the report on the terminal; MIM1F checkpoints, backups, restores and fallbacks;
%% MIM1G fragmented tables and SNMP indexes. add_backend_type/2 is declined: Clause keeps tables in ets and dets only.
-module(mnesia).
-export([
    start/0,
    start/1,
    stop/0,
    abort/1,
    create_schema/1,
    create_schema/2,
    delete_schema/1,
    dump_log/0,
    sync_log/0,
    dump_tables/1,
    create_table/1,
    create_table/2,
    delete_table/1,
    add_table_index/2,
    del_table_index/2,
    clear_table/1,
    clear_table/4,
    transform_table/3,
    transform_table/4,
    add_table_copy/3,
    del_table_copy/2,
    move_table_copy/3,
    change_table_copy_type/3,
    read_table_property/2,
    write_table_property/2,
    delete_table_property/2,
    change_table_access_mode/2,
    change_table_load_order/2,
    change_table_majority/2,
    set_master_nodes/1,
    set_master_nodes/2,
    force_load_table/1,
    change_config/2,
    set_debug_level/1,
    table_info/2,
    table_info/4,
    raw_table_info/2,
    system_info/1,
    schema/0,
    schema/1,
    wait_for_tables/2,
    error_description/1,
    has_var/1,
    dirty_write/1,
    dirty_write/2,
    dirty_delete/1,
    dirty_delete/2,
    dirty_delete_object/1,
    dirty_delete_object/2,
    dirty_update_counter/2,
    dirty_update_counter/3,
    dirty_read/1,
    dirty_read/2,
    dirty_match_object/1,
    dirty_match_object/2,
    dirty_select/2,
    dirty_select_reverse/2,
    dirty_all_keys/1,
    dirty_index_match_object/2,
    dirty_index_match_object/3,
    dirty_index_read/3,
    dirty_slot/2,
    dirty_first/1,
    dirty_last/1,
    dirty_next/2,
    dirty_prev/2,
    dirty_rpc/4,
    remote_dirty_match_object/2,
    remote_dirty_select/2,
    remote_dirty_select_reverse/2,
    activity/2,
    activity/3,
    activity/4,
    all_keys/1,
    all_keys/4,
    async_dirty/1,
    async_dirty/2,
    delete/1,
    delete/3,
    delete/5,
    delete_object/1,
    delete_object/3,
    delete_object/5,
    dirty_sel_init/5,
    ets/1,
    ets/2,
    first/1,
    first/3,
    foldl/3,
    foldl/4,
    foldl/6,
    foldr/3,
    foldr/4,
    foldr/6,
    fun_select/7,
    fun_select/10,
    get_activity_id/0,
    index_match_object/2,
    index_match_object/4,
    index_match_object/6,
    index_read/3,
    index_read/6,
    is_transaction/0,
    last/1,
    last/3,
    lock/2,
    lock/4,
    lock_table/2,
    match_object/1,
    match_object/3,
    match_object/5,
    next/2,
    next/4,
    prev/2,
    prev/4,
    put_activity_id/1,
    read/1,
    read/2,
    read/3,
    read/5,
    read_lock_table/1,
    s_delete/1,
    s_delete_object/1,
    s_write/1,
    select/1,
    select/2,
    select/3,
    select/4,
    select/5,
    select/6,
    select_cont/3,
    select_reverse/1,
    select_reverse/2,
    select_reverse/3,
    select_reverse/4,
    select_reverse/5,
    select_reverse/6,
    sync_dirty/1,
    sync_dirty/2,
    sync_transaction/1,
    sync_transaction/2,
    sync_transaction/3,
    transaction/1,
    transaction/2,
    transaction/3,
    wread/1,
    write/1,
    write/3,
    write/5,
    write_lock_table/1,
    subscribe/1,
    unsubscribe/1,
    report_event/1,
    load_textfile/1,
    dump_to_textfile/1,
    table/1,
    table/2,
    info/0,
    system_info/0,
    ms/0,
    kill/0,
    lkill/0,
    activate_checkpoint/1,
    deactivate_checkpoint/1,
    backup/1,
    backup/2,
    backup_checkpoint/2,
    backup_checkpoint/3,
    traverse_backup/4,
    traverse_backup/6,
    restore/2,
    install_fallback/1,
    install_fallback/2,
    uninstall_fallback/0,
    uninstall_fallback/1,
    change_table_frag/2,
    snmp_open_table/2,
    snmp_close_table/1,
    snmp_get_row/2,
    snmp_get_next_index/2,
    snmp_get_mnesia_key/2
]).
%% OTP 29 exports declined, raising error({not_implemented, F, A}) (docs/libraries/library.md#runtime-module-replacements):
%% external storage backends (mnesia_backend_type modules).
-export([
    add_backend_type/2
]).

-include("clause_mnesia.hrl").

%% Starting and stopping.

start() ->
    start([]).

% Start with configuration parameters set first.
start(Env) when is_list(Env) ->
    case application:load(mnesia) of
        ok -> configured(Env);
        {error, {already_loaded, mnesia}} -> configured(Env);
        Error -> Error
    end;
start(Env) ->
    {error, {badarg, Env}}.

configured([{Name, Value} | Rest]) when is_atom(Name) ->
    try clause_mnesia_monitor:check_env(Name, Value) of
        Checked ->
            ok = application:set_env(mnesia, Name, Checked),
            configured(Rest)
    catch
        error:_ -> {error, {bad_type, Name, Value}}
    end;
configured([Bad | _]) ->
    {error, {bad_type, Bad}};
configured([]) ->
    case application:start(mnesia, temporary) of
        ok -> ok;
        {error, {already_started, mnesia}} -> ok;
        {error, Reason} -> {error, Reason}
    end.

stop() ->
    case application:stop(mnesia) of
        ok -> stopped;
        {error, {not_started, mnesia}} -> stopped;
        Other -> Other
    end.

abort(Reason = {aborted, _}) -> exit(Reason);
abort(Reason) -> exit({aborted, Reason}).

%% The disc schema and its log (MIM1D).

create_schema(Nodes) ->
    create_schema(Nodes, []).

create_schema(Nodes, Properties) ->
    clause_mnesia_disc:create_schema(Nodes, Properties).

delete_schema(Nodes) ->
    clause_mnesia_disc:delete_schema(Nodes).

% The transaction log goes into the tables' files now.
dump_log() ->
    case clause_mnesia_monitor:is_running() of
        yes -> gen_server:call(mnesia_monitor, {dump_log, user}, infinity);
        _ -> {error, {node_not_running, node()}}
    end.

sync_log() ->
    case clause_mnesia_monitor:is_running() of
        yes -> clause_mnesia_disc:sync_log();
        _ -> {error, {node_not_running, node()}}
    end.

% RAM tables dumped to disc, loaded again at the next start.
dump_tables(Tabs) when is_list(Tabs) ->
    schema_operation({dump_tables, Tabs});
dump_tables(Tabs) ->
    {aborted, {bad_type, Tabs}}.

%% Schema changes, each a schema transaction: never inside an activity.

create_table(Name, Options) when is_list(Options) ->
    create_table([{name, Name} | Options]);
create_table(Name, Options) ->
    {aborted, {badarg, Name, Options}}.

create_table([_ | _] = Def) ->
    schema_operation({create_table, Def});
create_table(Arg) ->
    {aborted, {badarg, Arg}}.

delete_table(Tab) ->
    schema_operation({delete_table, Tab}).

add_table_index(Tab, Attribute) ->
    schema_operation({add_index, Tab, Attribute}).

del_table_index(Tab, Attribute) ->
    schema_operation({del_index, Tab, Attribute}).

% clear_table/1: a transaction of its own, also inside a dirty activity, never inside a transaction (MIM1C).
clear_table(Tab) ->
    case get(mnesia_activity_state) of
        {Mod, Tid, _} = State when element(1, Tid) =/= tid ->
            transaction(State, fun() -> access(clear_table, [Tab, '_']) end, [], infinity, Mod, sync);
        undefined ->
            transaction(undefined, fun() -> access(clear_table, [Tab, '_']) end, [], infinity, mnesia, sync);
        _ ->
            abort({aborted, nested_transaction})
    end.

clear_table(Tid, Ts, Tab, Pattern) ->
    clause_mnesia_access:clear_table(Tid, Ts, Tab, Pattern).

% transform_table/3 keeps the table's record name.
transform_table(Tab, Fun, Attributes) ->
    transform_table(Tab, Fun, Attributes, val({Tab, record_name})).

transform_table(Tab, Fun, Attributes, RecordName) when
    is_function(Fun) orelse Fun =:= ignore, is_list(Attributes), is_atom(RecordName)
->
    schema_operation({transform, Tab, Fun, Attributes, RecordName});
transform_table(Tab, Fun, Attributes, RecordName) ->
    {aborted, {bad_type, Tab, Fun, Attributes, RecordName}}.

add_table_copy(Tab, Node, Storage) ->
    schema_operation({add_copy, Tab, Node, Storage}).

del_table_copy(Tab, Node) ->
    schema_operation({del_copy, Tab, Node}).

move_table_copy(Tab, From, To) ->
    schema_operation({move_copy, Tab, From, To}).

change_table_copy_type(Tab, Node, Storage) ->
    schema_operation({copy_type, Tab, Node, Storage}).

read_table_property(Tab, Key) ->
    val({Tab, user_property, Key}).

write_table_property(Tab, Property) when tuple_size(Property) >= 1 ->
    schema_operation({write_property, Tab, Property});
write_table_property(Tab, Property) ->
    {aborted, {bad_type, Tab, Property}}.

delete_table_property(Tab, Key) ->
    schema_operation({delete_property, Tab, Key}).

change_table_access_mode(Tab, Mode) ->
    schema_operation({access_mode, Tab, Mode}).

change_table_load_order(Tab, Order) ->
    schema_operation({load_order, Tab, Order}).

change_table_majority(Tab, Majority) when is_boolean(Majority) ->
    schema_operation({majority, Tab, Majority}).

%% SNMP indexes (MIM1G): each row of a table with an snmp structure found and walked by its row index.

snmp_open_table(Tab, Snmp) ->
    schema_operation({add_snmp, Tab, Snmp}).

snmp_close_table(Tab) ->
    schema_operation({del_snmp, Tab}).

% {ok, Row} or undefined; a transaction sees its own writes.
snmp_get_row(Tab, RowIndex) when is_atom(Tab), Tab =/= schema, is_list(RowIndex) ->
    case get(mnesia_activity_state) of
        {Mod, Tid, #tidstore{store = Store} = Ts} when element(1, Tid) =:= tid ->
            case snmp_key(RowIndex, Tab) of
                unknown -> snmp_written(Store, Tab, RowIndex, fun(_, Row) -> {ok, Row} end);
                Key -> snmp_row(Mod:read(Tid, Ts, Tab, Key, read))
            end;
        _ ->
            dirty_rpc(Tab, clause_mnesia_snmp, get_row, [Tab, RowIndex])
    end;
snmp_get_row(Tab, _) ->
    abort({bad_type, Tab}).

snmp_row([Row]) -> {ok, Row};
snmp_row(_) -> undefined.

% A row index's key: from its parts, or from the index when a fix_string hides them; unknown when it is not there.
snmp_key(RowIndex, Tab) ->
    case clause_mnesia_snmp:oid_to_key(RowIndex, Tab) of
        unknown ->
            case dirty_rpc(Tab, clause_mnesia_snmp, get_mnesia_key, [Tab, RowIndex]) of
                {ok, Key} -> Key;
                undefined -> unknown
            end;
        Key ->
            Key
    end.

% The transaction's last operation on the row index's key, its write as Found(Key, Row) gives it, or undefined.
snmp_written(Store, Tab, RowIndex, Found) ->
    Snmp = val({Tab, snmp}),
    Pattern = val({Tab, wild_pattern}),
    Ops = ets:select(Store, [
        {{{Tab, '_'}, '_', write}, [], ['$_']},
        {{{Tab, '_'}, '_', delete}, [], ['$_']},
        {{{Tab, '_'}, Pattern, delete_object}, [], ['$_']}
    ]),
    lists:foldl(
        fun({{_, Key}, Row, Op}, Result) ->
            case clause_mnesia_snmp:key_to_oid(Tab, Key, Snmp) of
                RowIndex when Op =:= write -> Found(Key, Row);
                RowIndex -> undefined;
                _ -> Result
            end
        end,
        undefined,
        Ops
    ).

% The row index after RowIndex, {ok, Next}, or endOfTable; a transaction sees its own writes.
snmp_get_next_index(Tab, RowIndex) when is_atom(Tab), Tab =/= schema, is_list(RowIndex) ->
    {Next, Key} = dirty_rpc(Tab, clause_mnesia_snmp, get_next_index, [Tab, RowIndex]),
    case get(mnesia_activity_state) of
        {_, Tid, #tidstore{store = Store}} when element(1, Tid) =:= tid ->
            snmp_next_written(Store, Tab, RowIndex, Next, Key);
        _ when Next =:= endOfTable ->
            endOfTable;
        _ ->
            {ok, Next}
    end;
snmp_get_next_index(Tab, _) ->
    abort({bad_type, Tab}).

% The index's next key, unless the transaction deleted it last, among the keys the transaction writes.
snmp_next_written(Store, Tab, RowIndex, _, undefined) ->
    snmp_ordered(Store, Tab, RowIndex, []);
snmp_next_written(Store, Tab, RowIndex, Next, Key) ->
    case ets:match(Store, {{Tab, Key}, '_', '$1'}) of
        [] -> snmp_ordered(Store, Tab, RowIndex, [Key]);
        Ops -> snmp_after(lists:last(Ops), Store, Tab, RowIndex, Next, Key)
    end.

snmp_after([delete], _, Tab, _, Next, _) -> snmp_get_next_index(Tab, Next);
snmp_after(_, Store, Tab, RowIndex, _, Key) -> snmp_ordered(Store, Tab, RowIndex, [Key]).

snmp_ordered(Store, Tab, RowIndex, Keys) ->
    Snmp = val({Tab, snmp}),
    Written = written_keys(ets:match(Store, {{Tab, '$1'}, '_', '$2'}), Keys),
    first_after(RowIndex, lists:sort([clause_mnesia_snmp:key_to_oid(Tab, K, Snmp) || K <- Written])).

% The keys the transaction writes, with Keys: a write adds its key, a delete right after takes it away, as in OTP.
written_keys([[Key, write] | Rest], [Key | _] = Keys) -> written_keys(Rest, Keys);
written_keys([[Key, write] | Rest], Keys) -> written_keys(Rest, [Key | Keys]);
written_keys([[Key, delete] | Rest], [Key | Keys]) -> written_keys(Rest, Keys);
written_keys([_ | Rest], Keys) -> written_keys(Rest, Keys);
written_keys([], Keys) -> Keys.

first_after(Prev, [First | _]) when Prev < First -> {ok, First};
first_after(Prev, [_ | Rest]) -> first_after(Prev, Rest);
first_after(_, []) -> endOfTable.

% {ok, Key} or undefined; a transaction sees its own writes.
snmp_get_mnesia_key(Tab, RowIndex) when is_atom(Tab), Tab =/= schema, is_list(RowIndex) ->
    Found = dirty_rpc(Tab, clause_mnesia_snmp, get_mnesia_key, [Tab, RowIndex]),
    case get(mnesia_activity_state) of
        {_, Tid, #tidstore{store = Store}} when element(1, Tid) =:= tid -> snmp_filter_key(Found, RowIndex, Tab, Store);
        _ -> Found
    end;
snmp_get_mnesia_key(Tab, _) ->
    abort({bad_type, Tab}).

% The index's key unless the transaction's last operation on it is not a write; else a key the transaction wrote.
snmp_filter_key({ok, Key} = Found, _, Tab, Store) ->
    case ets:lookup(Store, {Tab, Key}) of
        [] -> Found;
        Ops -> snmp_last_write(lists:last(Ops), Found)
    end;
snmp_filter_key(undefined, RowIndex, Tab, Store) ->
    case clause_mnesia_snmp:oid_to_key(RowIndex, Tab) of
        unknown ->
            snmp_written(Store, Tab, RowIndex, fun(Key, _) -> {ok, Key} end);
        Key ->
            case ets:lookup(Store, {Tab, Key}) of
                [] -> undefined;
                Ops -> snmp_last_write(lists:last(Ops), {ok, Key})
            end
    end.

snmp_last_write({_, _, write}, Found) -> Found;
snmp_last_write(_, _) -> undefined.

% A fragmented table's fragments, node pool or fragmentation itself (MIM1G).
change_table_frag(Tab, Change) ->
    schema_operation({change_frag, Tab, Change}).

schema_operation(Operation) ->
    case get(mnesia_activity_state) of
        undefined -> schema_result(clause_mnesia_monitor:schema_transaction(Operation));
        _ -> {aborted, nested_transaction}
    end.

schema_result({atomic, {not_implemented, Feature}}) -> erlang:error({not_implemented, Feature});
schema_result(Result) -> Result.

%% Master nodes, loading and configuration (MIM1C).

% Every table's master nodes: those of the given nodes that hold a replica of it.
set_master_nodes(Nodes) when is_list(Nodes) ->
    case clause_mnesia_monitor:is_running() of
        yes ->
            Tables = [Cs || {_, Cs} <- ets:match_object(?GVAR, {{'_', cstruct}, '_'})],
            lists:foreach(fun(Cs) -> masters(Cs#cstruct.name, holders_among(Nodes, Cs)) end, Tables);
        _ ->
            ok
    end;
set_master_nodes(Nodes) ->
    {error, {bad_type, Nodes}}.

% A table's master nodes, each of which must hold a replica of it.
set_master_nodes(Tab, Nodes) when is_list(Nodes) ->
    case clause_mnesia_monitor:is_running() of
        yes ->
            case ets:lookup(?GVAR, {Tab, cstruct}) of
                [] ->
                    {error, {no_exists, Tab}};
                [{_, Cs}] ->
                    case Nodes -- holders(Cs) of
                        [] -> masters(Tab, Nodes);
                        Missing -> {error, {no_exists, Tab, Missing}}
                    end
            end;
        _ ->
            ok
    end;
set_master_nodes(Tab, Nodes) ->
    {error, {bad_type, Tab, Nodes}}.

masters(Tab, Nodes) ->
    clause_mnesia_disc:set_master_nodes(Tab, Nodes).

master_nodes(Tab) ->
    try
        ets:lookup_element(mnesia_decision, {tab, Tab}, 3)
    catch
        error:_ -> []
    end.

holders(#cstruct{ram_copies = Ram, disc_copies = Disc, disc_only_copies = DiscOnly}) ->
    Ram ++ Disc ++ DiscOnly.

holders_among(Nodes, Cs) ->
    [N || N <- holders(Cs), lists:member(N, Nodes)].

% A table loaded here is loaded; one without a replica here waits for its load, as OTP's does.
force_load_table(Tab) when is_atom(Tab), Tab =/= schema ->
    try ets:lookup_element(?GVAR, {Tab, storage_type}, 2) of
        unknown ->
            ets:insert(?GVAR, {{Tab, load_by_force}, true}),
            loaded_by_force(wait_for_tables([Tab], infinity));
        _ ->
            yes
    catch
        error:_ -> {error, {no_exists, Tab}}
    end;
force_load_table(Tab) ->
    {error, {bad_type, Tab}}.

loaded_by_force(ok) -> yes;
loaded_by_force(Other) -> Other.

% extra_db_nodes connects the nodes that run mnesia, after a schema merge; dc_dump_limit takes effect at once.
change_config(extra_db_nodes, Nodes) when is_list(Nodes) ->
    case clause_mnesia_monitor:is_running() of
        yes ->
            case clause_mnesia_monitor:schema_transaction(merge_schema) of
                {atomic, _} -> connected(Nodes);
                Aborted -> {error, Aborted}
            end;
        _ ->
            {error, {node_not_running, node()}}
    end;
change_config(dc_dump_limit, Limit) when is_number(Limit), Limit > 0 ->
    case clause_mnesia_monitor:is_running() of
        yes ->
            ets:insert(?GVAR, {dc_dump_limit, Limit}),
            {ok, Limit};
        _ ->
            {error, {not_started, mnesia}}
    end;
change_config(Key, _) ->
    {error, {badarg, Key}}.

% The given nodes that run mnesia with this one join extra_db_nodes.
connected(Nodes) ->
    Connected = [N || N <- val({current, db_nodes}), lists:member(N, Nodes)],
    Extra = clause_mnesia_monitor:get_env(extra_db_nodes),
    ets:insert(?GVAR, {extra_db_nodes, [N || N <- Extra, not lists:member(N, Connected)] ++ Connected}),
    {ok, Connected}.

% The debug level: the application environment's at once, the running level and its event subscriptions when mnesia
% runs (MIM1E); the previous level back.
set_debug_level(Level) ->
    Old = application:get_env(mnesia, debug),
    try clause_mnesia_monitor:check_env(debug, Level) of
        New ->
            ok = application:set_env(mnesia, debug, New),
            case clause_mnesia_monitor:is_running() of
                no when Old =:= undefined -> none;
                no -> element(2, Old);
                _ -> clause_mnesia_subscr:set_debug_level(New)
            end
    catch
        error:_ -> {error, {bad_type, debug, Level}}
    end.

%% Information.

% A table's information; an activity with another access module answers through it.
table_info(Tab, Item) ->
    case get(mnesia_activity_state) of
        undefined -> any_table_info(Tab, Item);
        {mnesia, _, _} -> any_table_info(Tab, Item);
        {Mod, Tid, Ts} -> Mod:table_info(Tid, Ts, Tab, Item);
        _ -> abort(no_transaction)
    end.

table_info(_Tid, _Ts, Tab, Item) ->
    any_table_info(Tab, Item).

any_table_info(Tab, Item) when is_atom(Tab) ->
    case Item of
        master_nodes -> master_nodes(Tab);
        size -> raw_table_info(Tab, Item);
        memory -> raw_table_info(Tab, Item);
        type -> gvar({Tab, setorbag}, {no_exists, Tab, type});
        all -> all_properties(Tab);
        name -> Tab;
        _ -> gvar({Tab, Item}, {no_exists, Tab, Item})
    end;
any_table_info(Tab, _) ->
    abort({bad_type, Tab}).

% A table's properties, its type under the name type.
all_properties(Tab) ->
    case clause_mnesia_schema:properties(Tab) of
        [] -> abort({no_exists, Tab, all});
        Properties -> lists:sort([renamed(P) || P <- Properties])
    end.

renamed({setorbag, Type}) -> {type, Type};
renamed(Property) -> Property.

% size and memory from the table's storage: 0 for a table without a replica here.
raw_table_info(Tab, Item) ->
    try ets:lookup_element(?GVAR, {Tab, storage_type}, 2) of
        unknown -> unmeasured(Tab, Item);
        _ -> clause_mnesia_db:info(Tab, Item)
    catch
        error:_ -> unmeasured(Tab, Item)
    end.

unmeasured(_, size) -> 0;
unmeasured(_, memory) -> 0;
unmeasured(Tab, Item) -> abort({no_exists, Tab, Item}).

gvar(Name, Error) ->
    try
        ets:lookup_element(?GVAR, Name, 2)
    catch
        error:_ -> abort(Error)
    end.

system_info(Item) ->
    try
        info(Item)
    catch
        _:Error -> abort(Error)
    end.

info(all) ->
    [{I, system_info(I)} || I <- info_items(clause_mnesia_monitor:is_running())];
info(db_nodes) ->
    case clause_mnesia_monitor:is_running() of
        no -> [node()];
        _ -> val({schema, disc_copies}) ++ val({schema, ram_copies}) ++ val({schema, external_copies})
    end;
info(running_db_nodes) ->
    loaded(),
    case clause_mnesia_monitor:is_running() of
        no -> [];
        _ -> val({current, db_nodes})
    end;
info(version) ->
    case lists:keyfind(mnesia, 1, application:loaded_applications()) of
        {mnesia, _, Version} -> Version;
        false -> {mnesia_not_loaded, node(), erlang:timestamp()}
    end;
info(is_running) ->
    clause_mnesia_monitor:is_running();
info(Item) when Item =:= directory; Item =:= extra_db_nodes; Item =:= schema_location ->
    loaded(),
    clause_mnesia_monitor:get_env(env_name(Item));
info(use_dir) ->
    loaded(),
    clause_mnesia_disc:use_dir();
info(fallback_activated) ->
    loaded(),
    clause_mnesia_disc:fallback_activated();
info(Item) ->
    running_info(Item).

env_name(directory) -> dir;
env_name(Name) -> Name.

% The information only a running mnesia has; configuration parameters always.
running_info(tables) ->
    val({schema, tables});
running_info(local_tables) ->
    val({schema, local_tables});
running_info(checkpoints) ->
    val(checkpoints);
running_info(subscribers) ->
    clause_mnesia_subscr:subscribers();
running_info(held_locks) ->
    counter(trans_commits),
    clause_mnesia_locker:held_locks();
running_info(lock_queue) ->
    counter(trans_commits),
    clause_mnesia_locker:lock_queue();
running_info(transaction_log_writes) ->
    counter(trans_log_writes);
running_info(transactions) ->
    {ok, _} = running(),
    clause_mnesia_tm:transactions();
running_info(transaction_commits) ->
    counter(trans_commits);
running_info(transaction_failures) ->
    counter(trans_failures);
running_info(transaction_restarts) ->
    counter(trans_restarts);
running_info(schema_version) ->
    schema_version();
running_info(master_node_tables) ->
    try
        [Tab || {master_nodes, {tab, Tab}, _} <- ets:tab2list(mnesia_decision)]
    catch
        error:badarg -> []
    end;
running_info(backend_types) ->
    [ram_copies, disc_copies, disc_only_copies];
running_info(log_version) ->
    "4.3";
running_info(protocol_version) ->
    {8, 6};
running_info(Item) ->
    configuration(Item).

configuration(Item) ->
    case lists:member(Item, configuration_items()) of
        true -> clause_mnesia_monitor:get_env(Item);
        false -> exit({badarg, Item})
    end.

configuration_items() ->
    [
        access_module,
        auto_repair,
        backup_module,
        event_module,
        debug,
        dump_log_load_regulation,
        dump_log_write_threshold,
        dump_log_time_threshold,
        dump_log_update_in_place,
        max_wait_for_decision,
        ignore_fallback_at_startup,
        fallback_error_function,
        core_dir,
        no_table_loaders,
        dc_dump_limit,
        send_compressed,
        max_transfer_size
    ].

counter(Name) -> ets:lookup_element(?GVAR, Name, 2).

running() ->
    case clause_mnesia_monitor:is_running() of
        yes -> {ok, node()};
        _ -> {error, {node_not_running, node()}}
    end.

% The schema's definition version; {0,0} without a schema.
schema_version() ->
    case clause_mnesia_monitor:is_running() of
        no ->
            {0, 0};
        _ ->
            {Version, _} = val({schema, version}),
            Version
    end.

loaded() ->
    _ = application:load(mnesia),
    ok.

info_items(yes) ->
    [
        access_module,
        auto_repair,
        backend_types,
        backup_module,
        checkpoints,
        db_nodes,
        debug,
        directory,
        dump_log_load_regulation,
        dump_log_time_threshold,
        dump_log_update_in_place,
        dump_log_write_threshold,
        event_module,
        extra_db_nodes,
        fallback_activated,
        held_locks,
        ignore_fallback_at_startup,
        fallback_error_function,
        is_running,
        local_tables,
        lock_queue,
        log_version,
        master_node_tables,
        max_wait_for_decision,
        protocol_version,
        running_db_nodes,
        schema_location,
        schema_version,
        subscribers,
        tables,
        transaction_commits,
        transaction_failures,
        transaction_log_writes,
        transaction_restarts,
        transactions,
        use_dir,
        core_dir,
        no_table_loaders,
        dc_dump_limit,
        send_compressed,
        max_transfer_size,
        version
    ];
info_items(_) ->
    [
        auto_repair,
        backup_module,
        db_nodes,
        debug,
        directory,
        dump_log_load_regulation,
        dump_log_time_threshold,
        dump_log_update_in_place,
        dump_log_write_threshold,
        event_module,
        extra_db_nodes,
        ignore_fallback_at_startup,
        fallback_error_function,
        is_running,
        log_version,
        max_wait_for_decision,
        protocol_version,
        running_db_nodes,
        schema_location,
        schema_version,
        use_dir,
        core_dir,
        version
    ].

% Every table's properties on the terminal, in table order.
schema() ->
    [clause_mnesia_schema:print(Tab) || Tab <- lists:sort(val({schema, tables}))],
    ok.

schema(Tab) ->
    clause_mnesia_schema:print(Tab).

% Wait until each table has a replica to read here; {timeout, Tables} lists those still without one.
wait_for_tables(Tabs, Timeout) when
    is_list(Tabs), Timeout =:= infinity; is_list(Tabs), is_integer(Timeout), Timeout >= 0
->
    case clause_mnesia_monitor:is_running() of
        yes -> wait(Tabs, deadline(Timeout));
        _ -> {error, {node_not_running, node()}}
    end;
wait_for_tables(Tabs, Timeout) ->
    {error, {badarg, Tabs, Timeout}}.

deadline(infinity) -> infinity;
deadline(Timeout) -> erlang:monotonic_time(millisecond) + Timeout.

wait(Tabs, Deadline) ->
    case [T || T <- Tabs, val({T, where_to_read}) =:= nowhere] of
        [] ->
            ok;
        Missing ->
            case remaining(Deadline) of
                0 ->
                    {timeout, Missing};
                Left ->
                    timer:sleep(min(Left, 10)),
                    wait(Missing, Deadline)
            end
    end.

remaining(infinity) -> 10;
remaining(Deadline) -> max(0, Deadline - erlang:monotonic_time(millisecond)).

error_description({'EXIT', Reason}) -> error_description(Reason);
error_description({error, Reason}) -> error_description(Reason);
error_description({aborted, Reason}) -> error_description(Reason);
error_description(Reason) when is_atom(Reason) -> description(Reason);
error_description(Reason) when tuple_size(Reason) > 0 -> setelement(1, Reason, error_description(element(1, Reason)));
error_description(Reason) -> Reason.

% The texts of mnesia's error reasons.
description(nested_transaction) ->
    "Nested transactions are not allowed";
description(badarg) ->
    "Bad or invalid argument, possibly bad type";
description(no_transaction) ->
    "Operation not allowed outside transactions";
description(combine_error) ->
    "Table options were ilegally combined";
description(bad_index) ->
    "Index already exists or was out of bounds";
description(already_exists) ->
    "Some schema option we try to set is already on";
description(index_exists) ->
    "Some ops cannot  be performed on tabs with index";
description(no_exists) ->
    "Tried to perform op on non-existing (non alive) item";
description(system_limit) ->
    "Some system_limit was exhausted";
description(mnesia_down) ->
    "A transaction involving objects at some remote node which died while transaction was executing"
    "*and* object(s) are no longer available elsewherein the network";
description(not_a_db_node) ->
    "A node which is non existent in the schema was mentioned";
description(bad_type) ->
    "Bad type on some provided arguments";
description(node_not_running) ->
    "Node not running";
description(truncated_binary_file) ->
    "Truncated binary in file";
description(active) ->
    "Some delete ops require that all active objects are removed";
description(illegal) ->
    "Operation not supported on object";
description(Reason) ->
    Reason.

%% The report on the terminal, text files and query tables (MIM1E).

% The locks, transactions, tables and the system's state on the terminal.
info() ->
    _ = clause_mnesia_info:print(full),
    ok.

% The system's state and its tables on the terminal; whether mnesia runs.
system_info() ->
    clause_mnesia_info:print(system).

load_textfile(File) ->
    clause_mnesia_text:load_textfile(File).

dump_to_textfile(File) ->
    clause_mnesia_text:dump_to_textfile(File).

table(Tab) ->
    table(Tab, []).

% A qlc query handle of a table, read in the caller's activity.
table(Tab, Options) ->
    clause_mnesia_qlc:table(Tab, Options).

% The names of OTP's mnesia processes and modules, which kill/0 and lkill/0 end; the servers last.
ms() ->
    [
        mnesia_sup,
        mnesia_kernel_sup,
        mnesia_checkpoint_sup,
        mnesia_snmp_sup,
        mnesia_ext_sup,
        mnesia,
        mnesia_app,
        mnesia_backup,
        mnesia_bup,
        mnesia_checkpoint,
        mnesia_controller,
        mnesia_dumper,
        mnesia_loader,
        mnesia_frag,
        mnesia_frag_hash,
        mnesia_index,
        mnesia_late_loader,
        mnesia_lib,
        mnesia_log,
        mnesia_schema,
        mnesia_snmp_hook,
        mnesia_subscr,
        mnesia_text,
        mnesia_tm,
        mnesia_recover,
        mnesia_locker,
        mnesia_monitor,
        mnesia_event
    ].

% mnesia's processes on this node killed at once, without the application stopping in order.
lkill() ->
    clause_mnesia_app:kill().

kill() ->
    rpc:multicall(clause_mnesia_app, kill, []).

%% Checkpoints and backups (MIM1F).

activate_checkpoint(Args) ->
    clause_mnesia_checkpoint:activate(Args).

deactivate_checkpoint(Name) ->
    clause_mnesia_checkpoint:deactivate(Name).

backup(Opaque) ->
    backup(Opaque, []).

% The whole database through the backup module.
backup(Opaque, ModuleOrArgs) ->
    clause_mnesia_backup:backup(Opaque, ModuleOrArgs).

backup_checkpoint(Name, Opaque) ->
    backup_checkpoint(Name, Opaque, []).

backup_checkpoint(Name, Opaque, ModuleOrArgs) ->
    clause_mnesia_backup:backup_checkpoint(Name, Opaque, ModuleOrArgs).

traverse_backup(Source, Target, Fun, Acc) ->
    Module = clause_mnesia_monitor:get_env(backup_module),
    traverse_backup(Source, Module, Target, Module, Fun, Acc).

traverse_backup(Source, SourceMod, Target, TargetMod, Fun, Acc) ->
    clause_mnesia_backup:traverse_backup(Source, SourceMod, Target, TargetMod, Fun, Acc).

% A backup's tables restored in one schema transaction: {atomic, Tabs}.
restore(Opaque, Args) ->
    case clause_mnesia_backup:restore_args(Opaque, Args) of
        {ok, Restore, Schema} -> schema_operation({restore, Restore, Schema});
        Aborted -> Aborted
    end.

install_fallback(Opaque) ->
    install_fallback(Opaque, []).

install_fallback(Opaque, ModuleOrArgs) ->
    loaded(),
    clause_mnesia_backup:install_fallback(Opaque, ModuleOrArgs).

uninstall_fallback() ->
    uninstall_fallback([{scope, global}]).

uninstall_fallback(Args) ->
    loaded(),
    clause_mnesia_backup:uninstall_fallback(Args).

%% Events (MIM1E): system events, completed transactions and table updates, sent to the subscribing processes.

subscribe(What) ->
    clause_mnesia_subscr:subscribe(self(), What).

unsubscribe(What) ->
    clause_mnesia_subscr:unsubscribe(self(), What).

% A user event, {mnesia_user, Event} to the system subscribers and the event handler, which prints it.
report_event(Event) ->
    clause_mnesia_subscr:report_system_event({mnesia_user, Event}).

% Whether a term holds a match variable: '_' or '$' followed by digits.
has_var('_') -> true;
has_var(X) when is_atom(X) -> variable_name(atom_to_list(X));
has_var(X) when is_tuple(X) -> has_var(tuple_to_list(X));
has_var([H | T]) -> has_var(H) orelse has_var(T);
has_var(_) -> false.

variable_name([$$ | Digits]) -> lists:all(fun(C) -> C >= $0 andalso C =< $9 end, Digits);
variable_name(_) -> false.

%% Transactions and activities (MIM1B): the caller's activity is {AccessModule, ActivityId, Opaque} in its process
%% dictionary under mnesia_activity_state, as in OTP.

is_transaction() ->
    case get(mnesia_activity_state) of
        {_, Tid, _} when element(1, Tid) =:= tid -> true;
        _ -> false
    end.

transaction(Fun) ->
    transaction(get(mnesia_activity_state), Fun, [], infinity, mnesia, async).

transaction(Fun, Retries) when is_integer(Retries), Retries >= 0; Retries =:= infinity ->
    transaction(get(mnesia_activity_state), Fun, [], Retries, mnesia, async);
transaction(Fun, Args) ->
    transaction(get(mnesia_activity_state), Fun, Args, infinity, mnesia, async).

transaction(Fun, Args, Retries) ->
    transaction(get(mnesia_activity_state), Fun, Args, Retries, mnesia, async).

sync_transaction(Fun) ->
    transaction(get(mnesia_activity_state), Fun, [], infinity, mnesia, sync).

sync_transaction(Fun, Retries) when is_integer(Retries), Retries >= 0; Retries =:= infinity ->
    transaction(get(mnesia_activity_state), Fun, [], Retries, mnesia, sync);
sync_transaction(Fun, Args) ->
    transaction(get(mnesia_activity_state), Fun, Args, infinity, mnesia, sync).

sync_transaction(Fun, Args, Retries) ->
    transaction(get(mnesia_activity_state), Fun, Args, Retries, mnesia, sync).

transaction(State, Fun, Args, Retries, Mod, Kind) when
    is_function(Fun), is_list(Args), is_atom(Mod), Retries =:= infinity;
    is_function(Fun), is_list(Args), is_atom(Mod), is_integer(Retries), Retries >= 0
->
    clause_mnesia_tm:transaction(State, Fun, Args, Retries, Mod, Kind);
transaction(_, Fun, Args, Retries, Mod, _) ->
    {aborted, {badarg, Fun, Args, Retries, Mod}}.

non_transaction(State, Fun, Args, Kind, Mod) when is_function(Fun), is_list(Args), is_atom(Mod) ->
    clause_mnesia_tm:non_transaction(State, Fun, Args, Kind, Mod);
non_transaction(_, Fun, Args, _, _) ->
    {aborted, {badarg, Fun, Args}}.

async_dirty(Fun) ->
    async_dirty(Fun, []).

async_dirty(Fun, Args) ->
    non_transaction(get(mnesia_activity_state), Fun, Args, async_dirty, mnesia).

sync_dirty(Fun) ->
    sync_dirty(Fun, []).

sync_dirty(Fun, Args) ->
    non_transaction(get(mnesia_activity_state), Fun, Args, sync_dirty, mnesia).

ets(Fun) ->
    ets(Fun, []).

ets(Fun, Args) ->
    non_transaction(get(mnesia_activity_state), Fun, Args, ets, mnesia).

activity(Kind, Fun) ->
    activity(Kind, Fun, []).

activity(Kind, Fun, Args) when is_list(Args) ->
    activity(Kind, Fun, Args, clause_mnesia_monitor:get_env(access_module));
activity(Kind, Fun, Mod) ->
    activity(Kind, Fun, [], Mod).

% An activity whose mnesia functions go through the access module Mod; a transaction's abort is an exit here.
activity(Kind, Fun, Args, Mod) ->
    State = get(mnesia_activity_state),
    case Kind of
        _ when Kind =:= ets; Kind =:= async_dirty; Kind =:= sync_dirty -> non_transaction(State, Fun, Args, Kind, Mod);
        transaction -> unwrapped(State, Fun, Args, infinity, Mod, async);
        {transaction, Retries} -> unwrapped(State, Fun, Args, Retries, Mod, async);
        sync_transaction -> unwrapped(State, Fun, Args, infinity, Mod, sync);
        {sync_transaction, Retries} -> unwrapped(State, Fun, Args, Retries, Mod, sync);
        _ -> {aborted, {bad_type, Kind}}
    end.

unwrapped(State, Fun, Args, Retries, Mod, Kind) ->
    case transaction(State, Fun, Args, Retries, Mod, Kind) of
        {atomic, Result} -> Result;
        Aborted -> exit(Aborted)
    end.

get_activity_id() ->
    get(mnesia_activity_state).

put_activity_id(undefined) ->
    erase(mnesia_activity_state);
put_activity_id(State) ->
    put(mnesia_activity_state, State).

% A function of the caller's activity, through its access module.
access(Function, Args) ->
    case get(mnesia_activity_state) of
        {Mod, Tid, Ts} -> apply(Mod, Function, [Tid, Ts | Args]);
        _ -> abort(no_transaction)
    end.

%% Access in activities: each function reaches the activity's access module, whose default is this module.

lock(Item, Kind) ->
    access(lock, [Item, Kind]).

lock_table(Tab, Kind) ->
    lock({table, Tab}, Kind).

read_lock_table(Tab) ->
    lock({table, Tab}, read),
    ok.

write_lock_table(Tab) ->
    lock({table, Tab}, write),
    ok.

write(Val) when is_tuple(Val), tuple_size(Val) > 2 -> write(element(1, Val), Val, write);
write(Val) -> abort({bad_type, Val}).

s_write(Val) when is_tuple(Val), tuple_size(Val) > 2 -> write(element(1, Val), Val, sticky_write).

write(Tab, Val, Kind) ->
    access(write, [Tab, Val, Kind]).

delete({Tab, Key}) -> delete(Tab, Key, write);
delete(Oid) -> abort({bad_type, Oid}).

s_delete({Tab, Key}) -> delete(Tab, Key, sticky_write);
s_delete(Oid) -> abort({bad_type, Oid}).

delete(Tab, Key, Kind) ->
    access(delete, [Tab, Key, Kind]).

delete_object(Val) when is_tuple(Val), tuple_size(Val) > 2 -> delete_object(element(1, Val), Val, write);
delete_object(Val) -> abort({bad_type, Val}).

s_delete_object(Val) when is_tuple(Val), tuple_size(Val) > 2 -> delete_object(element(1, Val), Val, sticky_write);
s_delete_object(Val) -> abort({bad_type, Val}).

delete_object(Tab, Val, Kind) ->
    access(delete_object, [Tab, Val, Kind]).

read({Tab, Key}) -> read(Tab, Key, read);
read(Oid) -> abort({bad_type, Oid}).

read(Tab, Key) ->
    read(Tab, Key, read).

wread({Tab, Key}) -> read(Tab, Key, write);
wread(Oid) -> abort({bad_type, Oid}).

read(Tab, Key, Kind) ->
    access(read, [Tab, Key, Kind]).

match_object(Pat) when is_tuple(Pat), tuple_size(Pat) > 2 -> match_object(element(1, Pat), Pat, read);
match_object(Pat) -> abort({bad_type, Pat}).

match_object(Tab, Pat, Kind) ->
    access(match_object, [Tab, Pat, Kind]).

all_keys(Tab) ->
    access(all_keys, [Tab, read]).

first(Tab) -> access(first, [Tab]).
last(Tab) -> access(last, [Tab]).
next(Tab, Key) -> access(next, [Tab, Key]).
prev(Tab, Key) -> access(prev, [Tab, Key]).

foldl(Fun, Acc, Tab) ->
    foldl(Fun, Acc, Tab, read).

foldl(Fun, Acc, Tab, Kind) when is_function(Fun) ->
    access(foldl, [Fun, Acc, Tab, Kind]).

foldr(Fun, Acc, Tab) ->
    foldr(Fun, Acc, Tab, read).

foldr(Fun, Acc, Tab, Kind) when is_function(Fun) ->
    access(foldr, [Fun, Acc, Tab, Kind]).

select(Tab, Spec) ->
    select(Tab, Spec, read).

select(Tab, Spec, Kind) when is_atom(Tab), Tab =/= schema, is_list(Spec) -> access(select, [Tab, Spec, Kind]);
select(Tab, Spec, _) -> abort({badarg, Tab, Spec}).

select(Tab, Spec, N, Kind) when is_atom(Tab), Tab =/= schema, is_list(Spec), is_integer(N) ->
    access(select, [Tab, Spec, N, Kind]);
select(Tab, Spec, N, _) ->
    abort({badarg, Tab, Spec, N}).

select(Cont) ->
    access(select_cont, [Cont]).

select_reverse(Tab, Spec) ->
    select_reverse(Tab, Spec, read).

select_reverse(Tab, Spec, Kind) when is_atom(Tab), Tab =/= schema, is_list(Spec) ->
    access(select_reverse, [Tab, Spec, Kind]);
select_reverse(Tab, Spec, _) ->
    abort({badarg, Tab, Spec}).

select_reverse(Tab, Spec, N, Kind) when is_atom(Tab), Tab =/= schema, is_list(Spec), is_integer(N) ->
    access(select_reverse, [Tab, Spec, N, Kind]);
select_reverse(Tab, Spec, N, _) ->
    abort({badarg, Tab, Spec, N}).

select_reverse(Cont) ->
    select(Cont).

index_match_object(Pat, Attr) when is_tuple(Pat), tuple_size(Pat) > 2 ->
    index_match_object(element(1, Pat), Pat, Attr, read);
index_match_object(Pat, _) ->
    abort({bad_type, Pat}).

index_match_object(Tab, Pat, Attr, Kind) ->
    access(index_match_object, [Tab, Pat, Attr, Kind]).

index_read(Tab, Key, Attr) ->
    access(index_read, [Tab, Key, Attr, read]).

%% The default access module's functions (clause_mnesia_access), which other access modules call back.

lock(Tid, Ts, Item, Kind) -> clause_mnesia_access:lock(Tid, Ts, Item, Kind).
write(Tid, Ts, Tab, Val, Kind) -> clause_mnesia_access:write(Tid, Ts, Tab, Val, Kind).
delete(Tid, Ts, Tab, Key, Kind) -> clause_mnesia_access:delete(Tid, Ts, Tab, Key, Kind).
delete_object(Tid, Ts, Tab, Val, Kind) -> clause_mnesia_access:delete_object(Tid, Ts, Tab, Val, Kind).
read(Tid, Ts, Tab, Key, Kind) -> clause_mnesia_access:read(Tid, Ts, Tab, Key, Kind).
match_object(Tid, Ts, Tab, Pat, Kind) -> clause_mnesia_access:match_object(Tid, Ts, Tab, Pat, Kind).
all_keys(Tid, Ts, Tab, Kind) -> clause_mnesia_access:all_keys(Tid, Ts, Tab, Kind).
first(Tid, Ts, Tab) -> clause_mnesia_access:first(Tid, Ts, Tab).
last(Tid, Ts, Tab) -> clause_mnesia_access:last(Tid, Ts, Tab).
next(Tid, Ts, Tab, Key) -> clause_mnesia_access:next(Tid, Ts, Tab, Key).
prev(Tid, Ts, Tab, Key) -> clause_mnesia_access:prev(Tid, Ts, Tab, Key).
foldl(Tid, Ts, Fun, Acc, Tab, Kind) -> clause_mnesia_access:foldl(Tid, Ts, Fun, Acc, Tab, Kind).
foldr(Tid, Ts, Fun, Acc, Tab, Kind) -> clause_mnesia_access:foldr(Tid, Ts, Fun, Acc, Tab, Kind).
select(Tid, Ts, Tab, Spec, Kind) -> clause_mnesia_access:select(Tid, Ts, Tab, Spec, Kind).
select(Tid, Ts, Tab, Spec, N, Kind) -> clause_mnesia_access:select(Tid, Ts, Tab, Spec, N, Kind).
select_cont(Tid, Ts, Cont) -> clause_mnesia_access:select_cont(Tid, Ts, Cont).
select_reverse(Tid, Ts, Tab, Spec, Kind) -> clause_mnesia_access:select_reverse(Tid, Ts, Tab, Spec, Kind).
select_reverse(Tid, Ts, Tab, Spec, N, Kind) -> clause_mnesia_access:select_reverse(Tid, Ts, Tab, Spec, N, Kind).
index_read(Tid, Ts, Tab, Key, Attr, Kind) -> clause_mnesia_access:index_read(Tid, Ts, Tab, Key, Attr, Kind).
index_match_object(Tid, Ts, Tab, Pat, Attr, Kind) ->
    clause_mnesia_access:index_match_object(Tid, Ts, Tab, Pat, Attr, Kind).
fun_select(Tid, Ts, Tab, Spec, Kind, TabPat, Select) ->
    clause_mnesia_access:fun_select(Tid, Ts, Tab, Spec, Kind, TabPat, Select).
fun_select(Tid, Ts, Tab, Spec, Kind, TabPat, Init, N, Node, Storage) ->
    clause_mnesia_access:fun_select(Tid, Ts, Tab, Spec, Kind, TabPat, Init, N, Node, Storage).
dirty_sel_init(Node, Tab, Spec, N, Storage) -> clause_mnesia_access:dirty_sel_init(Node, Tab, Spec, N, Storage).

%% Dirty writes: checked here, then applied to this node's replica.

dirty_write(Val) when is_tuple(Val), tuple_size(Val) > 2 ->
    dirty_write(element(1, Val), Val);
dirty_write(Val) ->
    abort({bad_type, Val}).

dirty_write(Tab, Val) when is_atom(Tab), Tab =/= schema, is_tuple(Val), tuple_size(Val) > 2 ->
    _ = clause_mnesia_access:validate_record(Tab, Val),
    commit(Tab, element(2, Val), {write, Val});
dirty_write(Tab, Val) ->
    abort({bad_type, Tab, Val}).

dirty_delete({Tab, Key}) -> dirty_delete(Tab, Key);
dirty_delete(Oid) -> abort({bad_type, Oid}).

dirty_delete(Tab, Key) when is_atom(Tab), Tab =/= schema -> commit(Tab, Key, delete);
dirty_delete(Tab, _) -> abort({bad_type, Tab}).

dirty_delete_object(Val) when is_tuple(Val), tuple_size(Val) > 2 -> dirty_delete_object(element(1, Val), Val);
dirty_delete_object(Val) -> abort({bad_type, Val}).

dirty_delete_object(Tab, Val) when is_atom(Tab), Tab =/= schema, is_tuple(Val), tuple_size(Val) > 2 ->
    case has_var(Val) of
        false -> commit(Tab, element(2, Val), {delete_object, Val});
        true -> abort({bad_type, Tab, Val})
    end;
dirty_delete_object(Tab, Val) ->
    abort({bad_type, Tab, Val}).

dirty_update_counter({Tab, Key}, Incr) -> dirty_update_counter(Tab, Key, Incr);
dirty_update_counter(Counter, _) -> abort({bad_type, Counter}).

% A counter is a {RecordName, Key, Integer} record of a set or ordered_set table; it never goes below zero.
dirty_update_counter(Tab, Key, Incr) when is_atom(Tab), Tab =/= schema, is_integer(Incr) ->
    case gvar({Tab, record_validation}, {no_exists, Tab}) of
        {RecordName, 3, Type} when Type =:= set; Type =:= ordered_set ->
            commit(Tab, Key, {update_counter, RecordName, Incr});
        _ ->
            abort({combine_error, Tab, update_counter})
    end;
dirty_update_counter(Tab, _, Incr) ->
    abort({bad_type, Tab, Incr}).

% An update applied where the table is committed on this node, logged first for a disc table (MIM1D), synchronously
% in a sync_dirty activity (MIM1H1); none without writable replicas.
commit(Tab, Key, Update) ->
    case val({Tab, where_to_commit}) of
        [] ->
            abort({no_exists, Tab});
        Replicas ->
            val({Tab, where_to_read}) =:= nowhere andalso abort({no_exists, Tab}),
            clause_mnesia_snmp:check(Tab, Key),
            clause_mnesia_disc:log([{Tab, Key, Update}], log_kind(get(mnesia_activity_state)), presume_commit),
            lists:foldl(fun(Replica, Result) -> replica_update(Replica, Tab, Key, Update, Result) end, ok, Replicas)
    end.

log_kind({_, {sync_dirty, _}, _}) -> sync;
log_kind(_) -> async.

% An update of this node's replica; a failed one leaves the result as it was, as OTP's commit does.
replica_update({Node, _Storage}, Tab, Key, Update, Result) when Node =:= node() ->
    try
        clause_mnesia_access:update({dirty, self()}, Tab, Key, Update)
    catch
        _:_ -> Result
    end;
replica_update(_, _, _, _, Result) ->
    Result.

%% Dirty reads, each run on the replica this node reads.

dirty_read({Tab, Key}) -> dirty_read(Tab, Key);
dirty_read(Oid) -> abort({bad_type, Oid}).

dirty_read(Tab, Key) when is_atom(Tab), Tab =/= schema ->
    local(Tab, [Tab, Key], fun() -> clause_mnesia_db:lookup(Tab, Key) end);
dirty_read(Tab, _) ->
    abort({bad_type, Tab}).

dirty_match_object(Pat) when is_tuple(Pat), tuple_size(Pat) > 2 -> dirty_match_object(element(1, Pat), Pat);
dirty_match_object(Pat) -> abort({bad_type, Pat}).

dirty_match_object(Tab, Pat) when is_atom(Tab), Tab =/= schema, is_tuple(Pat), tuple_size(Pat) > 2 ->
    local(Tab, [Tab, Pat], fun() -> remote_dirty_match_object(Tab, Pat) end);
dirty_match_object(Tab, Pat) ->
    abort({bad_type, Tab, Pat}).

% A match through the first index whose position the pattern binds, when its key is unbound.
remote_dirty_match_object(Tab, Pat) ->
    case has_var(element(2, Pat)) of
        false -> clause_mnesia_db:match_object(Tab, Pat);
        true -> indexed_match(Tab, Pat, regular_indexes(Tab))
    end.

indexed_match(Tab, Pat, [Pos | Rest]) when Pos =< tuple_size(Pat) ->
    case has_var(element(Pos, Pat)) of
        false -> clause_mnesia_index:match_object(Tab, Pat, Pos);
        true -> indexed_match(Tab, Pat, Rest)
    end;
indexed_match(Tab, Pat, []) ->
    clause_mnesia_db:match_object(Tab, Pat);
indexed_match(Tab, Pat, _) ->
    abort({bad_type, Tab, Pat}).

dirty_select(Tab, Spec) when is_atom(Tab), Tab =/= schema, is_list(Spec) ->
    local(Tab, [Tab, Spec], fun() -> remote_dirty_select(Tab, Spec) end);
dirty_select(Tab, Spec) ->
    abort({bad_type, Tab, Spec}).

dirty_select_reverse(Tab, Spec) when is_atom(Tab), Tab =/= schema, is_list(Spec) ->
    local(Tab, [Tab, Spec], fun() -> remote_dirty_select_reverse(Tab, Spec) end);
dirty_select_reverse(Tab, Spec) ->
    abort({bad_type, Tab, Spec}).

remote_dirty_select(Tab, Spec) ->
    selected(Tab, Spec, forward).

remote_dirty_select_reverse(Tab, Spec) ->
    selected(Tab, Spec, reverse).

% A select through the first index whose position a one-clause spec's head binds, when its key is unbound.
selected(Tab, [{Head, _, _}] = Spec, Direction) when is_tuple(Head), tuple_size(Head) > 2 ->
    case has_var(element(2, Head)) of
        false -> ets_select(Tab, Spec, Direction);
        true -> indexed_select(Tab, Spec, Direction, regular_indexes(Tab))
    end;
selected(Tab, Spec, Direction) ->
    ets_select(Tab, Spec, Direction).

indexed_select(Tab, [{Head, _, _}] = Spec, Direction, [Pos | Rest]) when Pos =< tuple_size(Head) ->
    case has_var(element(Pos, Head)) of
        false ->
            Records = clause_mnesia_index:records(Tab, Head, Pos),
            ets:match_spec_run(ordered(Tab, Records, Direction), ets:match_spec_compile(Spec));
        true ->
            indexed_select(Tab, Spec, Direction, Rest)
    end;
indexed_select(Tab, Spec, Direction, _) ->
    ets_select(Tab, Spec, Direction).

% An ordered_set table's index records in its order.
ordered(Tab, Records, Direction) ->
    case {val({Tab, setorbag}), Direction} of
        {ordered_set, forward} -> lists:sort(Records);
        {ordered_set, reverse} -> lists:reverse(lists:sort(Records));
        _ -> Records
    end.

ets_select(Tab, Spec, Direction) -> clause_mnesia_db:select(Tab, Spec, Direction).

regular_indexes(Tab) ->
    [P || P <- val({Tab, index}), is_integer(P)].

dirty_all_keys(Tab) when is_atom(Tab), Tab =/= schema ->
    Pattern = setelement(2, gvar({Tab, wild_pattern}, {no_exists, Tab}), '$1'),
    Keys = dirty_select(Tab, [{Pattern, [], ['$1']}]),
    case val({Tab, setorbag}) of
        bag -> clause_mnesia_access:uniq(Keys);
        _ -> Keys
    end;
dirty_all_keys(Tab) ->
    abort({bad_type, Tab}).

dirty_index_match_object(Pat, Attribute) when is_tuple(Pat), tuple_size(Pat) > 2 ->
    dirty_index_match_object(element(1, Pat), Pat, Attribute);
dirty_index_match_object(Pat, _) ->
    abort({bad_type, Pat}).

dirty_index_match_object(Tab, Pat, Attribute) when is_atom(Tab), Tab =/= schema, is_tuple(Pat), tuple_size(Pat) > 2 ->
    case table_position(Tab, Attribute) of
        {_} ->
            dirty_match_object(Tab, Pat);
        Pos when Pos =< tuple_size(Pat) ->
            index_match(Tab, Pat, Attribute, Pos);
        BadPos ->
            abort({bad_type, Tab, BadPos})
    end;
dirty_index_match_object(Tab, Pat, _) ->
    abort({bad_type, Tab, Pat}).

index_match(Tab, Pat, Attribute, Pos) ->
    case {has_var(element(2, Pat)), has_var(element(Pos, Pat))} of
        {false, _} -> dirty_match_object(Tab, Pat);
        {true, false} -> local(Tab, [Tab, Pat, Pos], fun() -> clause_mnesia_index:match_object(Tab, Pat, Pos) end);
        {true, true} -> abort({bad_type, Tab, Attribute, element(Pos, Pat)})
    end.

dirty_index_read(Tab, Key, Attribute) when is_atom(Tab), Tab =/= schema ->
    Pos = table_position(Tab, Attribute),
    case has_var(Key) of
        false -> local(Tab, [Tab, Key, Pos], fun() -> clause_mnesia_index:read(Tab, Key, Pos) end);
        true -> abort({bad_type, Tab, Attribute, Key})
    end;
dirty_index_read(Tab, _, _) ->
    abort({bad_type, Tab}).

% An attribute's position in a table's records.
table_position(_, Pos) when is_integer(Pos) -> Pos;
table_position(Tab, Attribute) -> clause_mnesia_schema:attribute_position(Attribute, val({Tab, attributes})).

dirty_slot(Tab, Slot) when is_atom(Tab), Tab =/= schema, is_integer(Slot) ->
    local(Tab, [Tab, Slot], fun() -> clause_mnesia_db:slot(Tab, Slot) end);
dirty_slot(Tab, Slot) ->
    abort({bad_type, Tab, Slot}).

dirty_first(Tab) when is_atom(Tab), Tab =/= schema -> local(Tab, [Tab], fun() -> clause_mnesia_db:first(Tab) end);
dirty_first(Tab) -> abort({bad_type, Tab}).

dirty_last(Tab) when is_atom(Tab), Tab =/= schema -> local(Tab, [Tab], fun() -> clause_mnesia_db:last(Tab) end);
dirty_last(Tab) -> abort({bad_type, Tab}).

dirty_next(Tab, Key) when is_atom(Tab), Tab =/= schema ->
    local(Tab, [Tab, Key], fun() -> clause_mnesia_db:next(Tab, Key) end);
dirty_next(Tab, _) ->
    abort({bad_type, Tab}).

dirty_prev(Tab, Key) when is_atom(Tab), Tab =/= schema ->
    local(Tab, [Tab, Key], fun() -> clause_mnesia_db:prev(Tab, Key) end);
dirty_prev(Tab, _) ->
    abort({bad_type, Tab}).

% M:F(Args) where the table's replica is read.
dirty_rpc(Tab, M, F, Args) ->
    local(Tab, Args, fun() -> apply(M, F, Args) end).

% A read on the replica of this node: no_exists without a replica, badarg for a failed read.
local(Tab, Args, Read) ->
    Node = node(),
    case val({Tab, where_to_read}) of
        nowhere ->
            abort({no_exists, Args});
        Node ->
            try
                Read()
            catch
                throw:Result -> Result;
                _:_ -> abort({badarg, Args})
            end;
        Other ->
            abort({node_not_running, Other})
    end.

val(Name) -> clause_mnesia_monitor:val(Name).

add_backend_type(_, _) -> erlang:error({not_implemented, add_backend_type, 2}).
