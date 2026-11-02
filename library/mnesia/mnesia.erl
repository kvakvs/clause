%% Clause's mnesia (plan 21 step MIM1A, docs/libraries/mnesia.md): a database of ets tables owned by mnesia_monitor,
%% with OTP's results, errors and table properties. Step MIM1A provides starting and stopping, the RAM schema and its
%% information, dirty access and indexes; transactions, disc storage, events, backups and fragments follow in
%% MIM1B-MIM1G, and their functions raise not_implemented until then.
-module(mnesia).
-export([
    start/0,
    start/1,
    stop/0,
    abort/1,
    create_table/1,
    create_table/2,
    delete_table/1,
    add_table_index/2,
    del_table_index/2,
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
    remote_dirty_select_reverse/2
]).
%% OTP 29 exports implemented natively or not yet: catalog builtins, whose clause_builtin_placeholder body the runtime's
%% builtin replaces, and stubs raising error({not_implemented, F, A}) (docs/libraries/library.md#runtime-module-replacements).
-export([
    activate_checkpoint/1,
    activity/2,
    activity/3,
    activity/4,
    add_backend_type/2,
    add_table_copy/3,
    all_keys/1,
    all_keys/4,
    async_dirty/1,
    async_dirty/2,
    backup/1,
    backup/2,
    backup_checkpoint/2,
    backup_checkpoint/3,
    change_config/2,
    change_table_access_mode/2,
    change_table_copy_type/3,
    change_table_frag/2,
    change_table_load_order/2,
    change_table_majority/2,
    clear_table/1,
    clear_table/4,
    create_schema/1,
    create_schema/2,
    deactivate_checkpoint/1,
    del_table_copy/2,
    delete/1,
    delete/3,
    delete/5,
    delete_object/1,
    delete_object/3,
    delete_object/5,
    delete_schema/1,
    delete_table_property/2,
    dirty_sel_init/5,
    dump_log/0,
    dump_tables/1,
    dump_to_textfile/1,
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
    force_load_table/1,
    fun_select/7,
    fun_select/10,
    get_activity_id/0,
    index_match_object/2,
    index_match_object/4,
    index_match_object/6,
    index_read/3,
    index_read/6,
    info/0,
    install_fallback/1,
    install_fallback/2,
    is_transaction/0,
    kill/0,
    last/1,
    last/3,
    lkill/0,
    load_textfile/1,
    lock/2,
    lock/4,
    lock_table/2,
    match_object/1,
    match_object/3,
    match_object/5,
    move_table_copy/3,
    ms/0,
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
    read_table_property/2,
    report_event/1,
    restore/2,
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
    set_debug_level/1,
    set_master_nodes/1,
    set_master_nodes/2,
    snmp_close_table/1,
    snmp_get_mnesia_key/2,
    snmp_get_next_index/2,
    snmp_get_row/2,
    snmp_open_table/2,
    subscribe/1,
    sync_dirty/1,
    sync_dirty/2,
    sync_log/0,
    sync_transaction/1,
    sync_transaction/2,
    sync_transaction/3,
    system_info/0,
    table/1,
    table/2,
    transaction/1,
    transaction/2,
    transaction/3,
    transform_table/3,
    transform_table/4,
    traverse_backup/4,
    traverse_backup/6,
    uninstall_fallback/0,
    uninstall_fallback/1,
    unsubscribe/1,
    wread/1,
    write/1,
    write/3,
    write/5,
    write_lock_table/1,
    write_table_property/2
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

%% Schema changes, each a schema transaction run by mnesia_monitor.

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

schema_operation(Operation) ->
    case clause_mnesia_monitor:schema_operation(Operation) of
        {not_implemented, Feature} -> erlang:error({not_implemented, Feature});
        Result -> Result
    end.

%% Information.

table_info(Tab, Item) when is_atom(Tab) ->
    case Item of
        master_nodes -> [];
        size -> raw_table_info(Tab, Item);
        memory -> raw_table_info(Tab, Item);
        type -> gvar({Tab, setorbag}, {no_exists, Tab, type});
        all -> all_properties(Tab);
        name -> Tab;
        _ -> gvar({Tab, Item}, {no_exists, Tab, Item})
    end;
table_info(Tab, _) ->
    abort({bad_type, Tab}).

table_info(_Tid, _Ts, Tab, Item) ->
    table_info(Tab, Item).

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
        _ -> ets:info(Tab, Item)
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
info(Item) when Item =:= use_dir; Item =:= fallback_activated ->
    loaded(),
    false;
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
    val({schema, tables}),
    [];
running_info(subscribers) ->
    val({schema, tables}),
    [whereis(mnesia_event)];
running_info(Item) when Item =:= held_locks; Item =:= lock_queue ->
    counter(trans_commits),
    [];
running_info(transaction_log_writes) ->
    counter(trans_commits),
    0;
running_info(transactions) ->
    {ok, _} = running(),
    [];
running_info(transaction_commits) ->
    counter(trans_commits);
running_info(transaction_failures) ->
    counter(trans_failures);
running_info(transaction_restarts) ->
    counter(trans_restarts);
running_info(schema_version) ->
    schema_version();
running_info(master_node_tables) ->
    [];
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

% Whether a term holds a match variable: '_' or '$' followed by digits.
has_var('_') -> true;
has_var(X) when is_atom(X) -> variable_name(atom_to_list(X));
has_var(X) when is_tuple(X) -> has_var(tuple_to_list(X));
has_var([H | T]) -> has_var(H) orelse has_var(T);
has_var(_) -> false.

variable_name([$$ | Digits]) -> lists:all(fun(C) -> C >= $0 andalso C =< $9 end, Digits);
variable_name(_) -> false.

%% Dirty writes: checked here, then applied to this node's replica.

dirty_write(Val) when is_tuple(Val), tuple_size(Val) > 2 ->
    dirty_write(element(1, Val), Val);
dirty_write(Val) ->
    abort({bad_type, Val}).

dirty_write(Tab, Val) when is_atom(Tab), Tab =/= schema, is_tuple(Val), tuple_size(Val) > 2 ->
    validate_record(Tab, Val),
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

% A record of the table's record name and arity.
validate_record(Tab, Val) ->
    case gvar({Tab, record_validation}, {no_exists, Tab}) of
        {RecordName, Arity, _} when tuple_size(Val) =:= Arity, element(1, Val) =:= RecordName -> ok;
        _ -> abort({bad_type, Val})
    end.

% An update applied where the table is committed on this node; none without writable replicas.
commit(Tab, Key, Update) ->
    case val({Tab, where_to_commit}) of
        [] ->
            abort({no_exists, Tab});
        Replicas ->
            val({Tab, where_to_read}) =:= nowhere andalso abort({no_exists, Tab}),
            lists:foldl(fun(Replica, Result) -> replica_update(Replica, Tab, Key, Update, Result) end, ok, Replicas)
    end.

% An update of this node's replica; a failed one leaves the result as it was, as OTP's commit does.
replica_update({Node, _Storage}, Tab, Key, Update, Result) when Node =:= node() ->
    try
        update(Tab, Key, Update)
    catch
        _:_ -> Result
    end;
replica_update(_, _, _, _, Result) ->
    Result.

update(Tab, Key, {write, Val}) ->
    clause_mnesia_index:written(Tab, Key, Val, undefined),
    true = ets:insert(Tab, Val),
    ok;
update(Tab, Key, delete) ->
    clause_mnesia_index:deleted(Tab, Key),
    true = ets:delete(Tab, Key),
    ok;
update(Tab, Key, {delete_object, Val}) ->
    clause_mnesia_index:object_deleted(Tab, Key, Val),
    true = ets:match_delete(Tab, Val),
    ok;
update(Tab, Key, {update_counter, RecordName, Incr}) ->
    {New, Old} = counted(Tab, Key, RecordName, Incr),
    clause_mnesia_index:written(Tab, Key, New, Old),
    element(3, New).

% The counter's new record and the one it replaced; a counter that is missing, not an integer or would go below zero
% becomes the increment, or zero.
counted(Tab, Key, RecordName, Incr) ->
    try ets:update_counter(Tab, Key, Incr) of
        Value when is_integer(Value), Value >= 0 -> {{RecordName, Key, Value}, [{RecordName, Key, Value - Incr}]};
        _ -> reset(Tab, {RecordName, Key, max(Incr, 0)})
    catch
        error:_ -> reset(Tab, {RecordName, Key, max(Incr, 0)})
    end.

reset(Tab, Record) ->
    true = ets:insert(Tab, Record),
    {Record, []}.

%% Dirty reads, each run on the replica this node reads.

dirty_read({Tab, Key}) -> dirty_read(Tab, Key);
dirty_read(Oid) -> abort({bad_type, Oid}).

dirty_read(Tab, Key) when is_atom(Tab), Tab =/= schema ->
    local(Tab, [Tab, Key], fun() -> ets:lookup(Tab, Key) end);
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
        false -> ets:match_object(Tab, Pat);
        true -> indexed_match(Tab, Pat, regular_indexes(Tab))
    end.

indexed_match(Tab, Pat, [Pos | Rest]) when Pos =< tuple_size(Pat) ->
    case has_var(element(Pos, Pat)) of
        false -> clause_mnesia_index:match_object(Tab, Pat, Pos);
        true -> indexed_match(Tab, Pat, Rest)
    end;
indexed_match(Tab, Pat, []) ->
    ets:match_object(Tab, Pat);
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

ets_select(Tab, Spec, forward) -> ets:select(Tab, Spec);
ets_select(Tab, Spec, reverse) -> ets:select_reverse(Tab, Spec).

regular_indexes(Tab) ->
    [P || P <- val({Tab, index}), is_integer(P)].

dirty_all_keys(Tab) when is_atom(Tab), Tab =/= schema ->
    Pattern = setelement(2, gvar({Tab, wild_pattern}, {no_exists, Tab}), '$1'),
    Keys = dirty_select(Tab, [{Pattern, [], ['$1']}]),
    case val({Tab, setorbag}) of
        bag -> lists:usort(Keys);
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
    local(Tab, [Tab, Slot], fun() -> ets:slot(Tab, Slot) end);
dirty_slot(Tab, Slot) ->
    abort({bad_type, Tab, Slot}).

dirty_first(Tab) when is_atom(Tab), Tab =/= schema -> local(Tab, [Tab], fun() -> ets:first(Tab) end);
dirty_first(Tab) -> abort({bad_type, Tab}).

dirty_last(Tab) when is_atom(Tab), Tab =/= schema -> local(Tab, [Tab], fun() -> ets:last(Tab) end);
dirty_last(Tab) -> abort({bad_type, Tab}).

dirty_next(Tab, Key) when is_atom(Tab), Tab =/= schema -> local(Tab, [Tab, Key], fun() -> ets:next(Tab, Key) end);
dirty_next(Tab, _) -> abort({bad_type, Tab}).

dirty_prev(Tab, Key) when is_atom(Tab), Tab =/= schema -> local(Tab, [Tab, Key], fun() -> ets:prev(Tab, Key) end);
dirty_prev(Tab, _) -> abort({bad_type, Tab}).

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

activate_checkpoint(_) -> erlang:error({not_implemented, activate_checkpoint, 1}).
activity(_, _) -> erlang:error({not_implemented, activity, 2}).
activity(_, _, _) -> erlang:error({not_implemented, activity, 3}).
activity(_, _, _, _) -> erlang:error({not_implemented, activity, 4}).
add_backend_type(_, _) -> erlang:error({not_implemented, add_backend_type, 2}).
add_table_copy(_, _, _) -> erlang:error({not_implemented, add_table_copy, 3}).
all_keys(_) -> erlang:error({not_implemented, all_keys, 1}).
all_keys(_, _, _, _) -> erlang:error({not_implemented, all_keys, 4}).
async_dirty(_) -> erlang:error({not_implemented, async_dirty, 1}).
async_dirty(_, _) -> erlang:error({not_implemented, async_dirty, 2}).
backup(_) -> erlang:error({not_implemented, backup, 1}).
backup(_, _) -> erlang:error({not_implemented, backup, 2}).
backup_checkpoint(_, _) -> erlang:error({not_implemented, backup_checkpoint, 2}).
backup_checkpoint(_, _, _) -> erlang:error({not_implemented, backup_checkpoint, 3}).
change_config(_, _) -> erlang:error({not_implemented, change_config, 2}).
change_table_access_mode(_, _) -> erlang:error({not_implemented, change_table_access_mode, 2}).
change_table_copy_type(_, _, _) -> erlang:error({not_implemented, change_table_copy_type, 3}).
change_table_frag(_, _) -> erlang:error({not_implemented, change_table_frag, 2}).
change_table_load_order(_, _) -> erlang:error({not_implemented, change_table_load_order, 2}).
change_table_majority(_, _) -> erlang:error({not_implemented, change_table_majority, 2}).
clear_table(_) -> erlang:error({not_implemented, clear_table, 1}).
clear_table(_, _, _, _) -> erlang:error({not_implemented, clear_table, 4}).
create_schema(_) -> erlang:error({not_implemented, create_schema, 1}).
create_schema(_, _) -> erlang:error({not_implemented, create_schema, 2}).
deactivate_checkpoint(_) -> erlang:error({not_implemented, deactivate_checkpoint, 1}).
del_table_copy(_, _) -> erlang:error({not_implemented, del_table_copy, 2}).
delete(_) -> erlang:error({not_implemented, delete, 1}).
delete(_, _, _) -> erlang:error({not_implemented, delete, 3}).
delete(_, _, _, _, _) -> erlang:error({not_implemented, delete, 5}).
delete_object(_) -> erlang:error({not_implemented, delete_object, 1}).
delete_object(_, _, _) -> erlang:error({not_implemented, delete_object, 3}).
delete_object(_, _, _, _, _) -> erlang:error({not_implemented, delete_object, 5}).
delete_schema(_) -> erlang:error({not_implemented, delete_schema, 1}).
delete_table_property(_, _) -> erlang:error({not_implemented, delete_table_property, 2}).
dirty_sel_init(_, _, _, _, _) -> erlang:error({not_implemented, dirty_sel_init, 5}).
dump_log() -> erlang:error({not_implemented, dump_log, 0}).
dump_tables(_) -> erlang:error({not_implemented, dump_tables, 1}).
dump_to_textfile(_) -> erlang:error({not_implemented, dump_to_textfile, 1}).
ets(_) -> erlang:error({not_implemented, ets, 1}).
ets(_, _) -> erlang:error({not_implemented, ets, 2}).
first(_) -> erlang:error({not_implemented, first, 1}).
first(_, _, _) -> erlang:error({not_implemented, first, 3}).
foldl(_, _, _) -> erlang:error({not_implemented, foldl, 3}).
foldl(_, _, _, _) -> erlang:error({not_implemented, foldl, 4}).
foldl(_, _, _, _, _, _) -> erlang:error({not_implemented, foldl, 6}).
foldr(_, _, _) -> erlang:error({not_implemented, foldr, 3}).
foldr(_, _, _, _) -> erlang:error({not_implemented, foldr, 4}).
foldr(_, _, _, _, _, _) -> erlang:error({not_implemented, foldr, 6}).
force_load_table(_) -> erlang:error({not_implemented, force_load_table, 1}).
fun_select(_, _, _, _, _, _, _) -> erlang:error({not_implemented, fun_select, 7}).
fun_select(_, _, _, _, _, _, _, _, _, _) -> erlang:error({not_implemented, fun_select, 10}).
get_activity_id() -> erlang:error({not_implemented, get_activity_id, 0}).
index_match_object(_, _) -> erlang:error({not_implemented, index_match_object, 2}).
index_match_object(_, _, _, _) -> erlang:error({not_implemented, index_match_object, 4}).
index_match_object(_, _, _, _, _, _) -> erlang:error({not_implemented, index_match_object, 6}).
index_read(_, _, _) -> erlang:error({not_implemented, index_read, 3}).
index_read(_, _, _, _, _, _) -> erlang:error({not_implemented, index_read, 6}).
info() -> erlang:error({not_implemented, info, 0}).
install_fallback(_) -> erlang:error({not_implemented, install_fallback, 1}).
install_fallback(_, _) -> erlang:error({not_implemented, install_fallback, 2}).
is_transaction() -> erlang:error({not_implemented, is_transaction, 0}).
kill() -> erlang:error({not_implemented, kill, 0}).
last(_) -> erlang:error({not_implemented, last, 1}).
last(_, _, _) -> erlang:error({not_implemented, last, 3}).
lkill() -> erlang:error({not_implemented, lkill, 0}).
load_textfile(_) -> erlang:error({not_implemented, load_textfile, 1}).
lock(_, _) -> erlang:error({not_implemented, lock, 2}).
lock(_, _, _, _) -> erlang:error({not_implemented, lock, 4}).
lock_table(_, _) -> erlang:error({not_implemented, lock_table, 2}).
match_object(_) -> erlang:error({not_implemented, match_object, 1}).
match_object(_, _, _) -> erlang:error({not_implemented, match_object, 3}).
match_object(_, _, _, _, _) -> erlang:error({not_implemented, match_object, 5}).
move_table_copy(_, _, _) -> erlang:error({not_implemented, move_table_copy, 3}).
ms() -> erlang:error({not_implemented, ms, 0}).
next(_, _) -> erlang:error({not_implemented, next, 2}).
next(_, _, _, _) -> erlang:error({not_implemented, next, 4}).
prev(_, _) -> erlang:error({not_implemented, prev, 2}).
prev(_, _, _, _) -> erlang:error({not_implemented, prev, 4}).
put_activity_id(_) -> erlang:error({not_implemented, put_activity_id, 1}).
read(_) -> erlang:error({not_implemented, read, 1}).
read(_, _) -> erlang:error({not_implemented, read, 2}).
read(_, _, _) -> erlang:error({not_implemented, read, 3}).
read(_, _, _, _, _) -> erlang:error({not_implemented, read, 5}).
read_lock_table(_) -> erlang:error({not_implemented, read_lock_table, 1}).
read_table_property(_, _) -> erlang:error({not_implemented, read_table_property, 2}).
report_event(_) -> erlang:error({not_implemented, report_event, 1}).
restore(_, _) -> erlang:error({not_implemented, restore, 2}).
s_delete(_) -> erlang:error({not_implemented, s_delete, 1}).
s_delete_object(_) -> erlang:error({not_implemented, s_delete_object, 1}).
s_write(_) -> erlang:error({not_implemented, s_write, 1}).
select(_) -> erlang:error({not_implemented, select, 1}).
select(_, _) -> erlang:error({not_implemented, select, 2}).
select(_, _, _) -> erlang:error({not_implemented, select, 3}).
select(_, _, _, _) -> erlang:error({not_implemented, select, 4}).
select(_, _, _, _, _) -> erlang:error({not_implemented, select, 5}).
select(_, _, _, _, _, _) -> erlang:error({not_implemented, select, 6}).
select_cont(_, _, _) -> erlang:error({not_implemented, select_cont, 3}).
select_reverse(_) -> erlang:error({not_implemented, select_reverse, 1}).
select_reverse(_, _) -> erlang:error({not_implemented, select_reverse, 2}).
select_reverse(_, _, _) -> erlang:error({not_implemented, select_reverse, 3}).
select_reverse(_, _, _, _) -> erlang:error({not_implemented, select_reverse, 4}).
select_reverse(_, _, _, _, _) -> erlang:error({not_implemented, select_reverse, 5}).
select_reverse(_, _, _, _, _, _) -> erlang:error({not_implemented, select_reverse, 6}).
set_debug_level(_) -> erlang:error({not_implemented, set_debug_level, 1}).
set_master_nodes(_) -> erlang:error({not_implemented, set_master_nodes, 1}).
set_master_nodes(_, _) -> erlang:error({not_implemented, set_master_nodes, 2}).
snmp_close_table(_) -> erlang:error({not_implemented, snmp_close_table, 1}).
snmp_get_mnesia_key(_, _) -> erlang:error({not_implemented, snmp_get_mnesia_key, 2}).
snmp_get_next_index(_, _) -> erlang:error({not_implemented, snmp_get_next_index, 2}).
snmp_get_row(_, _) -> erlang:error({not_implemented, snmp_get_row, 2}).
snmp_open_table(_, _) -> erlang:error({not_implemented, snmp_open_table, 2}).
subscribe(_) -> erlang:error({not_implemented, subscribe, 1}).
sync_dirty(_) -> erlang:error({not_implemented, sync_dirty, 1}).
sync_dirty(_, _) -> erlang:error({not_implemented, sync_dirty, 2}).
sync_log() -> erlang:error({not_implemented, sync_log, 0}).
sync_transaction(_) -> erlang:error({not_implemented, sync_transaction, 1}).
sync_transaction(_, _) -> erlang:error({not_implemented, sync_transaction, 2}).
sync_transaction(_, _, _) -> erlang:error({not_implemented, sync_transaction, 3}).
system_info() -> erlang:error({not_implemented, system_info, 0}).
table(_) -> erlang:error({not_implemented, table, 1}).
table(_, _) -> erlang:error({not_implemented, table, 2}).
transaction(_) -> erlang:error({not_implemented, transaction, 1}).
transaction(_, _) -> erlang:error({not_implemented, transaction, 2}).
transaction(_, _, _) -> erlang:error({not_implemented, transaction, 3}).
transform_table(_, _, _) -> erlang:error({not_implemented, transform_table, 3}).
transform_table(_, _, _, _) -> erlang:error({not_implemented, transform_table, 4}).
traverse_backup(_, _, _, _) -> erlang:error({not_implemented, traverse_backup, 4}).
traverse_backup(_, _, _, _, _, _) -> erlang:error({not_implemented, traverse_backup, 6}).
uninstall_fallback() -> erlang:error({not_implemented, uninstall_fallback, 0}).
uninstall_fallback(_) -> erlang:error({not_implemented, uninstall_fallback, 1}).
unsubscribe(_) -> erlang:error({not_implemented, unsubscribe, 1}).
wread(_) -> erlang:error({not_implemented, wread, 1}).
write(_) -> erlang:error({not_implemented, write, 1}).
write(_, _, _) -> erlang:error({not_implemented, write, 3}).
write(_, _, _, _, _) -> erlang:error({not_implemented, write, 5}).
write_lock_table(_) -> erlang:error({not_implemented, write_lock_table, 1}).
write_table_property(_, _) -> erlang:error({not_implemented, write_table_property, 2}).
