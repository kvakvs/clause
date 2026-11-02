%% mnesia's foundation (plan 21 step MIM1A): starting and stopping, the in-memory schema, table definitions and their
%% checks, table and system information, dirty access to set, ordered_set and bag tables, and indexes.
-module(mnesia_dirty).
-export([main/1]).

-record(person, {name, age, city}).

main(_) ->
    not_started(),
    started(),
    create_options(),
    table_properties(),
    dirty_access(),
    indexes(),
    counters(),
    argument_errors(),
    schema_changes(),
    printed(),
    stopped(),
    io:format("mnesia_dirty: ok~n").

% Answers before mnesia runs.
not_started() ->
    t(pre_read, fun() -> mnesia:dirty_read(person, a) end),
    t(pre_write, fun() -> mnesia:dirty_write({person, a, 1, x}) end),
    t(pre_delete, fun() -> mnesia:dirty_delete(person, a) end),
    t(pre_first, fun() -> mnesia:dirty_first(person) end),
    t(pre_keys, fun() -> mnesia:dirty_all_keys(person) end),
    t(pre_select, fun() -> mnesia:dirty_select(person, [{'_', [], ['$_']}]) end),
    t(pre_counter, fun() -> mnesia:dirty_update_counter({person, a}, 1) end),
    t(pre_index_read, fun() -> mnesia:dirty_index_read(person, x, city) end),
    t(pre_table_info, fun() -> [mnesia:table_info(schema, size), mnesia:table_info(person, memory)] end),
    t(pre_table_type, fun() -> mnesia:table_info(schema, type) end),
    t(pre_table_name, fun() -> mnesia:table_info(person, name) end),
    t(pre_running, fun() -> mnesia:system_info(is_running) end),
    t(pre_items, fun() -> [{I, system_info(I)} || I <- pre_items()] end),
    t(pre_all_keys, fun() -> [K || {K, _} <- mnesia:system_info(all)] end),
    t(pre_bad_item, fun() -> mnesia:system_info(foo) end),
    t(pre_create, fun() -> mnesia:create_table(person, []) end),
    t(pre_delete_table, fun() -> mnesia:delete_table(person) end),
    t(pre_add_index, fun() -> mnesia:add_table_index(person, city) end),
    t(pre_del_index, fun() -> mnesia:del_table_index(person, city) end),
    t(pre_wait, fun() -> mnesia:wait_for_tables([schema], 100) end),
    t(pre_schema, fun() -> mnesia:schema(person) end),
    t(pre_stop, fun() -> mnesia:stop() end).

pre_items() ->
    [
        tables,
        local_tables,
        db_nodes,
        running_db_nodes,
        extra_db_nodes,
        use_dir,
        schema_location,
        version,
        schema_version,
        transaction_commits,
        transaction_failures,
        held_locks,
        lock_queue,
        transactions,
        checkpoints,
        subscribers,
        master_node_tables,
        access_module,
        debug
    ].

% Starting, the processes and tables mnesia keeps, and the system information.
started() ->
    t(start, fun() -> mnesia:start() end),
    t(start_again, fun() -> mnesia:start() end),
    t(running, fun() -> mnesia:system_info(is_running) end),
    t(processes, fun() -> lists:all(fun(N) -> is_pid(whereis(N)) end, [mnesia_sup, mnesia_monitor, mnesia_event]) end),
    t(gvar, fun() -> [ets:info(mnesia_gvar, I) || I <- [type, protection, keypos, named_table]] end),
    t(schema_ets, fun() -> ets_info(schema) end),
    t(all_keys, fun() -> [K || {K, _} <- mnesia:system_info(all)] end),
    t(all, fun() -> [{K, info_value(K, V)} || {K, V} <- mnesia:system_info(all)] end),
    t(bad_items, fun() -> [system_info(foo), system_info("foo")] end),
    t(schema_props, fun() -> props(schema) end).

% create_table/2: options, defaults and OTP's checks in their order.
create_options() ->
    c(person, [{attributes, record_info(fields, person)}, {index, [city]}]),
    c(person, [{attributes, record_info(fields, person)}]),
    c("str", []),
    c(1, []),
    c(schema, []),
    c(t_opts, foo),
    c(t_opts, {a}),
    c(t_opts, [{foo, bar}]),
    c(t_opts, [foo]),
    c(t_opts, [{type, set} | foo]),
    c(t_opts, [{type, bag}, {type, set}]),
    c(t_opts, [{type}]),
    c(t_opts, [{type, foo}]),
    c(t_ord, [{type, ordered_set}]),
    c(t_bag, [{type, bag}]),
    c(t_attrs, [{attributes, [a]}]),
    c(t_attrs, [{attributes, [a, a]}]),
    c(t_attrs, [{attributes, [a, "b"]}]),
    c(t_attrs, [{attributes, a}]),
    c(t_attrs, [{attributes, [a, snmp]}]),
    c(t_attrs, [{attributes, [a, b]}, {index, [a]}]),
    c(t_attrs, [{attributes, [a, b]}, {index, [c]}]),
    c(t_attrs, [{attributes, [a, b]}, {index, [2]}]),
    c(t_attrs, [{attributes, [a, b]}, {index, [0]}]),
    c(t_attrs, [{attributes, [a, b]}, {index, [5]}]),
    c(t_attrs, [{attributes, [a, b, c]}, {index, b}]),
    c(t_attrs, [{attributes, [a, b, c]}, {index, ["b"]}]),
    c(t_attrs, [{attributes, [a, b, c]}, {index, [{b, foo}]}]),
    c(t_attrs, [{attributes, [a, b, c]}, {index, [{b}]}]),
    c(t_ix_dup, [{attributes, [a, b, c]}, {index, [b, b]}]),
    c(t_ix_two, [{attributes, [a, b, c]}, {index, [{b, ordered}, c]}]),
    c(t_ix_bag, [{attributes, [a, b, c]}, {index, [{b, bag}]}]),
    c(t_ix_pos, [{attributes, [a, b, c]}, {index, [4]}]),
    c(t_nodes, [{ram_copies, foo}]),
    c(t_nodes, [{ram_copies, ["n"]}]),
    c(t_nodes, [{ram_copies, [node()]}, {disc_copies, [node()]}]),
    c(t_nodes, [{disc_copies, [node()]}]),
    c(t_nodes, [{disc_only_copies, [node()]}]),
    c(t_nodes, [{disc_copies, [other@host]}]),
    c(t_nodes, [{type, ordered_set}, {disc_only_copies, [node()]}]),
    c(t_nodes, [{load_order, foo}]),
    c(t_no_nodes, [{ram_copies, []}]),
    c(t_remote, [{ram_copies, [other@host]}, {index, [val]}]),
    c(t_external, [{external_copies, [{x, [node()]}]}]),
    c(t_record, [{record_name, rec}, {attributes, [k, v]}]),
    c(t_record2, [{record_name, "rec"}]),
    c(t_local, [{local_content, true}]),
    c(t_local2, [{local_content, foo}]),
    c(t_local3, [{local_content, true}, {majority, true}]),
    c(t_load, [{load_order, 5}]),
    c(t_read_only, [{access_mode, read_only}]),
    c(t_access, [{access_mode, foo}]),
    c(t_majority, [{majority, true}]),
    c(t_majority2, [{majority, foo}]),
    c(t_user, [{user_properties, [{b, 2}, {a, 1}, {c}]}]),
    c(t_user2, [{user_properties, [a]}]),
    c(t_user3, [{user_properties, a}]),
    c(t_storage, [{storage_properties, [{ets, [compressed]}, {dets, [{auto_save, 5}]}]}]),
    c(t_storage2, [{storage_properties, [{ets, [bag]}]}]),
    c(t_storage2, [{storage_properties, [{ets, [{keypos, 3}]}]}]),
    c(t_storage2, [{storage_properties, [{ets, ["x"]}]}]),
    c(t_storage2, [{storage_properties, [{ets, foo}]}]),
    c(t_storage2, [{storage_properties, [{dets, [{file, "x"}]}]}]),
    c(t_snmp, [{snmp, [{key, integer}]}]),
    c(t_snmp2, [{snmp, [{key, {integer, string}}]}, {attributes, [k, v]}]),
    c(t_snmp3, [{snmp, [{key, foo}]}]),
    c(t_snmp4, [{snmp, foo}]),
    c(t_frag, [{frag_properties, foo}]),
    c(t_cookie, [{cookie, my_cookie}]),
    c(t_cookie2, [{cookie, {{1, 2, 3}, node()}}]),
    c(t_version, [{version, {{3, 1}, []}}]),
    c(t_version2, [{version, foo}]),
    c(t_name, [{attributes, [t_name, b]}]),
    t(tables, fun() -> lists:sort(mnesia:system_info(tables)) end),
    t(local_tables, fun() -> lists:sort(mnesia:system_info(local_tables)) end),
    t(failures, fun() -> [mnesia:system_info(I) || I <- [transaction_commits, transaction_failures]] end).

% Each table's properties as table_info/2 gives them.
table_properties() ->
    Tables = [person, t_ord, t_bag, t_ix_dup, t_ix_two, t_ix_bag, t_remote, t_record, t_read_only, t_user, t_cookie2],
    [t({props, T}, fun() -> props(T) end) || T <- Tables],
    t(person_ets, fun() -> ets_info(person) end),
    t(remote_ets, fun() -> ets:info(t_remote) end),
    t(items, fun() ->
        [
            mnesia:table_info(person, I)
         || I <- [name, type, setorbag, arity, wild_pattern, size, master_nodes, record_validation]
        ] ++ [is_integer(mnesia:table_info(person, memory))]
    end),
    t(info_errors, fun() ->
        [
            catch_all(fun() -> mnesia:table_info(T, I) end)
         || {T, I} <- [{nope, type}, {nope, all}, {nope, foo}, {person, foo}, {person, "foo"}, {"x", type}]
        ]
    end),
    t(no_table_sizes, fun() -> [mnesia:table_info(nope, I) || I <- [size, memory, name, master_nodes]] end),
    t(wait, fun() ->
        [
            mnesia:wait_for_tables(Tabs, Timeout)
         || {Tabs, Timeout} <- [
                {[person, schema], 100},
                {[nope, person, other], 50},
                {[], 10},
                {foo, 10},
                {[person], foo},
                {[t_ord], infinity}
            ]
        ]
    end).

% Dirty reads and writes of set, ordered_set and bag tables.
dirty_access() ->
    {atomic, ok} = mnesia:create_table(s, [{attributes, [k, a, b]}]),
    {atomic, ok} = mnesia:create_table(o, [{type, ordered_set}, {attributes, [k, a, b]}]),
    {atomic, ok} = mnesia:create_table(g, [{type, bag}, {attributes, [k, a, b]}]),
    t(writes, fun() -> [mnesia:dirty_write(T, {T, K, K rem 3, x}) || T <- [s, o, g], K <- [3, 1, 2, 5, 4]] end),
    t(write_one, fun() -> [mnesia:dirty_write({g, 1, 1, y}), mnesia:dirty_write({g, 1, 1, y})] end),
    t(write_record, fun() -> mnesia:dirty_write(#person{name = ann, age = 31, city = paris}) end),
    t(reads, fun() -> [mnesia:dirty_read(T, 1) || T <- [s, o, g]] ++ [mnesia:dirty_read({s, 9})] end),
    t(sizes, fun() -> [mnesia:table_info(T, size) || T <- [s, o, g]] end),
    t(keys, fun() -> [lists:sort(mnesia:dirty_all_keys(T)) || T <- [s, g]] ++ [mnesia:dirty_all_keys(o)] end),
    t(ordered_walk, fun() ->
        [mnesia:dirty_first(o), mnesia:dirty_last(o), mnesia:dirty_next(o, 3), mnesia:dirty_prev(o, 3)]
    end),
    t(ordered_ends, fun() -> [mnesia:dirty_next(o, 5), mnesia:dirty_prev(o, 1), mnesia:dirty_next(o, 77)] end),
    t(set_walk, fun() -> walk(s) end),
    t(bag_walk, fun() -> walk(g) end),
    t(set_next_missing, fun() -> mnesia:dirty_next(s, 77) end),
    t(empty, fun() ->
        {atomic, ok} = mnesia:create_table(e, []),
        [mnesia:dirty_first(e), mnesia:dirty_last(e), mnesia:dirty_all_keys(e), mnesia:dirty_read(e, 1)]
    end),
    t(match, fun() -> [lists:sort(mnesia:dirty_match_object({T, '_', 1, '_'})) || T <- [s, o, g]] end),
    t(match_ordered, fun() -> mnesia:dirty_match_object(o, {o, '$1', '_', x}) end),
    t(match_key, fun() -> mnesia:dirty_match_object(g, {g, 1, '_', '_'}) end),
    t(match_other_record, fun() -> mnesia:dirty_match_object(o, {zz, '_', '_', '_'}) end),
    t(select, fun() -> mnesia:dirty_select(o, [{{o, '$1', '$2', '_'}, [{'>', '$1', 2}], [{{'$1', '$2'}}]}]) end),
    t(select_reverse, fun() -> mnesia:dirty_select_reverse(o, [{{o, '$1', '_', '_'}, [], ['$1']}]) end),
    t(select_set, fun() -> lists:sort(mnesia:dirty_select(s, [{{s, '$1', 0, '_'}, [], ['$_']}])) end),
    t(select_key, fun() -> mnesia:dirty_select(g, [{{g, 1, '$1', '$2'}, [], [{{'$1', '$2'}}]}]) end),
    t(delete, fun() -> [mnesia:dirty_delete(s, 1), mnesia:dirty_delete({s, 77}), mnesia:dirty_read(s, 1)] end),
    t(delete_object_bag, fun() -> [mnesia:dirty_delete_object({g, 1, 1, y}), mnesia:dirty_read(g, 1)] end),
    t(delete_object_tab, fun() -> [mnesia:dirty_delete_object(g, {g, 1, 1, x}), mnesia:dirty_read(g, 1)] end),
    t(delete_object_set, fun() ->
        [
            mnesia:dirty_delete_object({s, 2, 0, wrong}),
            mnesia:dirty_read(s, 2),
            mnesia:dirty_delete_object({s, 2, 2, x}),
            mnesia:dirty_read(s, 2)
        ]
    end),
    t(record_name, fun() ->
        [
            mnesia:dirty_write(t_record, {rec, 1, one}),
            mnesia:dirty_read(t_record, 1),
            mnesia:dirty_match_object(t_record, {rec, '_', '_'}),
            mnesia:dirty_select(t_record, [{{rec, '$1', '_'}, [], ['$1']}]),
            mnesia:dirty_all_keys(t_record)
        ]
    end),
    t(record_name_errors, fun() ->
        [
            catch_all(fun() -> mnesia:dirty_write({t_record, 1, one}) end),
            catch_all(fun() -> mnesia:dirty_match_object({rec, '_', '_'}) end),
            catch_all(fun() -> mnesia:dirty_read(rec, 1) end)
        ]
    end),
    t(read_only, fun() ->
        [
            catch_all(fun() -> mnesia:dirty_write({t_read_only, 1, 2}) end),
            catch_all(fun() -> mnesia:dirty_delete(t_read_only, 1) end),
            mnesia:dirty_read(t_read_only, 1)
        ]
    end),
    t(remote, fun() ->
        [
            catch_all(fun() -> mnesia:dirty_write({t_remote, 1, 2}) end),
            catch_all(fun() -> mnesia:dirty_read(t_remote, 1) end),
            catch_all(fun() -> mnesia:dirty_first(t_remote) end),
            catch_all(fun() -> mnesia:dirty_all_keys(t_remote) end),
            catch_all(fun() -> mnesia:dirty_select(t_remote, []) end),
            catch_all(fun() -> mnesia:dirty_delete(t_remote, 1) end)
        ]
    end),
    t(local_content, fun() -> [mnesia:dirty_write({t_local, 1, 2}), mnesia:dirty_read(t_local, 1)] end),
    t(no_table, fun() ->
        [
            catch_all(F)
         || F <- [
                fun() -> mnesia:dirty_write({nope, 1, 2}) end,
                fun() -> mnesia:dirty_read(nope, 1) end,
                fun() -> mnesia:dirty_delete(nope, 1) end,
                fun() -> mnesia:dirty_delete_object({nope, 1, 2}) end,
                fun() -> mnesia:dirty_first(nope) end,
                fun() -> mnesia:dirty_last(nope) end,
                fun() -> mnesia:dirty_next(nope, 1) end,
                fun() -> mnesia:dirty_prev(nope, 1) end,
                fun() -> mnesia:dirty_all_keys(nope) end,
                fun() -> mnesia:dirty_match_object({nope, '_', '_'}) end,
                fun() -> mnesia:dirty_select(nope, [{'_', [], ['$_']}]) end,
                fun() -> mnesia:dirty_select_reverse(nope, [{'_', [], ['$_']}]) end,
                fun() -> mnesia:dirty_update_counter(nope, 1, 1) end,
                fun() -> mnesia:dirty_index_read(nope, 1, a) end,
                fun() -> mnesia:dirty_index_match_object({nope, '_', 1}, a) end
            ]
        ]
    end).

% Indexes: created with the table or added later, kept on every write and delete, and the index functions.
indexes() ->
    {atomic, ok} = mnesia:create_table(xs, [{attributes, [k, a, b]}, {index, [a]}]),
    {atomic, ok} = mnesia:create_table(xo, [{type, ordered_set}, {attributes, [k, a, b]}, {index, [{a, bag}]}]),
    {atomic, ok} = mnesia:create_table(xg, [{type, bag}, {attributes, [k, a, b]}, {index, [a, b]}]),
    [mnesia:dirty_write(T, {T, K, K rem 3, x}) || T <- [xs, xo, xg], K <- [3, 1, 2, 5, 4]],
    ok = mnesia:dirty_write({xg, 1, 1, y}),
    ok = mnesia:dirty_write({xg, 1, 1, y}),
    t(index_items, fun() -> [{mnesia:table_info(T, index), index_info(T)} || T <- [xs, xo, xg]] end),
    t(index_read, fun() -> [mnesia:dirty_index_read(T, 1, a) || T <- [xs, xo, xg]] end),
    t(index_read_b, fun() -> mnesia:dirty_index_read(xg, y, b) end),
    t(index_read_pos, fun() -> [mnesia:dirty_index_read(xs, 2, 3), mnesia:dirty_index_read(xs, 7, a)] end),
    t(index_match, fun() -> [mnesia:dirty_index_match_object({T, '_', 1, '_'}, a) || T <- [xs, xo, xg]] end),
    t(index_match3, fun() -> mnesia:dirty_index_match_object(xs, {xs, '_', 1, '_'}, 3) end),
    t(index_match_key, fun() -> mnesia:dirty_index_match_object({xs, 4, 1, '_'}, a) end),
    t(index_match_noindex, fun() -> mnesia:dirty_index_match_object({xs, '_', '_', x}, b) end),
    t(match_by_index, fun() -> [mnesia:dirty_match_object({T, '_', 2, '_'}) || T <- [xs, xo, xg]] end),
    t(select_by_index, fun() ->
        [
            mnesia:dirty_select(xo, [{{xo, '$1', 1, '_'}, [], ['$1']}]),
            mnesia:dirty_select_reverse(xo, [{{xo, '$1', 1, '_'}, [], ['$1']}]),
            lists:sort(mnesia:dirty_select(xg, [{{xg, '$1', '_', y}, [], ['$1']}]))
        ]
    end),
    t(index_errors, fun() ->
        [
            catch_all(F)
         || F <- [
                fun() -> mnesia:dirty_index_read(xs, 1, b) end,
                fun() -> mnesia:dirty_index_read(xs, 1, k) end,
                fun() -> mnesia:dirty_index_read(xs, 1, 9) end,
                fun() -> mnesia:dirty_index_read(xs, 1, {a}) end,
                fun() -> mnesia:dirty_index_read(xs, 1, "a") end,
                fun() -> mnesia:dirty_index_read(xs, '$1', a) end,
                fun() -> mnesia:dirty_index_match_object({xs, '_', '$1', '_'}, a) end,
                fun() -> mnesia:dirty_index_match_object({xs, '_', 1, '_'}, b) end,
                fun() -> mnesia:dirty_index_match_object(xs, {xs, '_'}, a) end,
                fun() -> mnesia:dirty_index_match_object(xs, {xs, '_', 1, '_'}, 9) end,
                fun() -> mnesia:dirty_index_match_object(xs, {xs, '_', 1, '_'}, zz) end
            ]
        ]
    end),
    t(index_update, fun() ->
        ok = mnesia:dirty_write({xs, 1, 7, z}),
        ok = mnesia:dirty_write({xs, 1, 7, w}),
        [mnesia:dirty_index_read(xs, 1, a), mnesia:dirty_index_read(xs, 7, a)]
    end),
    t(index_delete, fun() ->
        ok = mnesia:dirty_delete(xs, 1),
        ok = mnesia:dirty_delete_object({xg, 1, 1, y}),
        ok = mnesia:dirty_delete_object({xs, 2, 0, wrong}),
        [mnesia:dirty_index_read(xs, 7, a), mnesia:dirty_index_read(xg, 1, a), mnesia:dirty_index_read(xg, y, b)]
    end),
    t(bag_duplicates, fun() ->
        {atomic, ok} = mnesia:create_table(bd, [{type, bag}, {attributes, [k, a]}, {index, [a]}]),
        [ok, ok, ok] = [mnesia:dirty_write(R) || R <- [{bd, 1, x}, {bd, 1, x}, {bd, 1, y}]],
        ok = mnesia:dirty_delete_object({bd, 1, x}),
        [mnesia:dirty_read(bd, 1), mnesia:dirty_index_read(bd, x, a), mnesia:dirty_index_read(bd, y, a)]
    end),
    t(add_index, fun() ->
        [mnesia:add_table_index(xs, b), mnesia:table_info(xs, index), mnesia:dirty_index_read(xs, x, b)]
    end),
    t(add_index_errors, fun() ->
        [
            mnesia:add_table_index(xs, b),
            mnesia:add_table_index(xs, k),
            mnesia:add_table_index(xs, zz),
            mnesia:add_table_index(nope, a),
            mnesia:add_table_index(xs, 5),
            mnesia:add_table_index(xs, {b, bag}),
            mnesia:add_table_index(schema, cstruct),
            mnesia:add_table_index(t_remote, key)
        ]
    end),
    t(add_index_pos, fun() -> [mnesia:add_table_index(e, 3), index_info(e)] end),
    t(del_index, fun() -> [mnesia:del_table_index(xs, b), mnesia:table_info(xs, index), index_info(xs)] end),
    t(del_index_errors, fun() ->
        [
            mnesia:del_table_index(xs, b),
            mnesia:del_table_index(nope, b),
            mnesia:del_table_index(xs, zz),
            mnesia:del_table_index(schema, 3)
        ]
    end),
    t(del_last_index, fun() ->
        [
            mnesia:del_table_index(xs, 3),
            mnesia:table_info(xs, index),
            index_info(xs),
            mnesia:table_info(xs, commit_work)
        ]
    end),
    t(no_index_left, fun() -> catch_all(fun() -> mnesia:dirty_index_read(xs, 1, a) end) end),
    t(dup_index, fun() ->
        ok = mnesia:dirty_write({t_ix_dup, 1, v, w}),
        [mnesia:dirty_index_read(t_ix_dup, v, b), mnesia:del_table_index(t_ix_dup, b), index_info(t_ix_dup)]
    end).

% dirty_update_counter/2,3.
counters() ->
    {atomic, ok} = mnesia:create_table(c, []),
    {atomic, ok} = mnesia:create_table(co, [{type, ordered_set}, {index, [val]}]),
    t(counter, fun() ->
        [
            mnesia:dirty_update_counter(c, a, 1),
            mnesia:dirty_update_counter({c, a}, 5),
            mnesia:dirty_update_counter(c, a, -10),
            mnesia:dirty_read(c, a),
            mnesia:dirty_update_counter(c, n, -3),
            mnesia:dirty_read(c, n)
        ]
    end),
    t(counter_index, fun() ->
        [
            mnesia:dirty_update_counter(co, a, 3),
            mnesia:dirty_update_counter(co, a, 2),
            mnesia:dirty_index_read(co, 5, val),
            mnesia:dirty_index_read(co, 3, val)
        ]
    end),
    t(counter_big, fun() -> [mnesia:dirty_update_counter(c, b, 1 bsl 70), mnesia:dirty_update_counter(c, b, 1)] end),
    t(counter_not_integer, fun() ->
        ok = mnesia:dirty_write({c, f, 1.5}),
        ok = mnesia:dirty_write({c, x, x}),
        [
            mnesia:dirty_update_counter(c, f, 1),
            mnesia:dirty_update_counter(c, x, -1),
            mnesia:dirty_read(c, f),
            mnesia:dirty_read(c, x)
        ]
    end),
    t(counter_record_name, fun() -> [mnesia:dirty_update_counter(t_record, n, 4), mnesia:dirty_read(t_record, n)] end),
    t(counter_errors, fun() ->
        [
            catch_all(F)
         || F <- [
                fun() -> mnesia:dirty_update_counter(t_bag, 1, 1) end,
                fun() -> mnesia:dirty_update_counter(s, 1, 1) end,
                fun() -> mnesia:dirty_update_counter(c, a, x) end,
                fun() -> mnesia:dirty_update_counter(c, a, 1.0) end,
                fun() -> mnesia:dirty_update_counter(schema, a, 1) end,
                fun() -> mnesia:dirty_update_counter(foo, 1) end,
                fun() -> mnesia:dirty_update_counter(t_read_only, a, 1) end
            ]
        ]
    end).

% Arguments of the wrong shape.
argument_errors() ->
    t(argument_errors, fun() ->
        [
            catch_all(F)
         || F <- [
                fun() -> mnesia:dirty_write(foo) end,
                fun() -> mnesia:dirty_write({s, 1}) end,
                fun() -> mnesia:dirty_write("s", {s, 1, 2, 3}) end,
                fun() -> mnesia:dirty_write(s, {o, 1, 2, 3}) end,
                fun() -> mnesia:dirty_write(s, {s, 1, 2}) end,
                fun() -> mnesia:dirty_write({schema, x, []}) end,
                fun() -> mnesia:dirty_read(foo) end,
                fun() -> mnesia:dirty_read("s", 1) end,
                fun() -> mnesia:dirty_read(schema, s) end,
                fun() -> mnesia:dirty_delete(foo) end,
                fun() -> mnesia:dirty_delete({s}) end,
                fun() -> mnesia:dirty_delete(schema, s) end,
                fun() -> mnesia:dirty_delete_object(foo) end,
                fun() -> mnesia:dirty_delete_object({s, 1, '$1', x}) end,
                fun() -> mnesia:dirty_delete_object(s, {s, '_', 1, x}) end,
                fun() -> mnesia:dirty_match_object({o, '_'}) end,
                fun() -> mnesia:dirty_match_object(schema, {schema, '_', '_'}) end,
                fun() -> mnesia:dirty_select(o, foo) end,
                fun() -> mnesia:dirty_select(o, [{foo}]) end,
                fun() -> mnesia:dirty_select(schema, []) end,
                fun() -> mnesia:dirty_select_reverse(o, foo) end,
                fun() -> mnesia:dirty_all_keys(schema) end,
                fun() -> mnesia:dirty_all_keys("s") end,
                fun() -> mnesia:dirty_first(schema) end,
                fun() -> mnesia:dirty_last("s") end,
                fun() -> mnesia:dirty_next(schema, 1) end,
                fun() -> mnesia:dirty_prev("s", 1) end,
                fun() -> mnesia:dirty_index_read(schema, 1, 2) end,
                fun() -> mnesia:dirty_index_match_object(schema, {schema, '_', 1}, 3) end
            ]
        ]
    end).

% delete_table/1 and the transaction counters of schema changes.
schema_changes() ->
    t(delete_table, fun() -> [mnesia:delete_table(g), ets:info(g), catch_all(fun() -> mnesia:dirty_read(g, 1) end)] end),
    t(delete_table_errors, fun() -> [mnesia:delete_table(g), mnesia:delete_table(schema), mnesia:delete_table("g")] end),
    t(delete_indexed, fun() -> [mnesia:delete_table(xg), catch_all(fun() -> mnesia:table_info(xg, index) end)] end),
    t(delete_remote, fun() -> [mnesia:delete_table(t_remote), lists:member(t_remote, mnesia:system_info(tables))] end),
    t(recreate, fun() -> [mnesia:create_table(g, []), mnesia:dirty_read(g, 1), mnesia:table_info(g, attributes)] end),
    t(counts, fun() ->
        [mnesia:system_info(I) || I <- [transaction_commits, transaction_failures, transaction_restarts]]
    end).

% schema/1 and schema/0 print a table's properties.
printed() ->
    {atomic, ok} = mnesia:create_table(shown, [
        {cookie, {{1, 2, 3}, node()}}, {attributes, [k, v]}, {user_properties, [{p, 1}]}
    ]),
    ok = mnesia:dirty_write({shown, 1, one}),
    t(schema_table, fun() -> mnesia:schema(shown) end),
    t(schema_none, fun() -> mnesia:schema(nope) end).

% Stopping: the tables go, and mnesia starts again with an empty schema.
stopped() ->
    t(error_descriptions, fun() ->
        [
            mnesia:error_description(E)
         || E <- [
                nested_transaction,
                badarg,
                no_transaction,
                combine_error,
                bad_index,
                already_exists,
                index_exists,
                no_exists,
                system_limit,
                mnesia_down,
                not_a_db_node,
                bad_type,
                node_not_running,
                truncated_binary_file,
                active,
                illegal,
                not_active,
                foo,
                "x",
                {no_exists, x},
                {aborted, {bad_type, person, 3.14}},
                {error, no_transaction},
                {'EXIT', {already_exists, t}},
                {aborted, {foo, bar}},
                {}
            ]
        ]
    end),
    t(stop, fun() -> mnesia:stop() end),
    t(stop_again, fun() -> mnesia:stop() end),
    t(after_stop, fun() ->
        [
            mnesia:system_info(is_running),
            ets:info(person),
            ets:info(mnesia_gvar),
            whereis(mnesia_monitor),
            catch_all(fun() -> mnesia:dirty_read(person, ann) end),
            catch_all(fun() -> mnesia:table_info(person, type) end),
            catch_all(fun() -> mnesia:system_info(tables) end),
            mnesia:create_table(z, [])
        ]
    end),
    t(restart, fun() -> mnesia:start() end),
    t(restarted, fun() ->
        [
            mnesia:system_info(tables),
            mnesia:system_info(transaction_commits),
            mnesia:system_info(transaction_failures),
            catch_all(fun() -> mnesia:dirty_read(person, ann) end)
        ]
    end),
    t(start_options, fun() -> [mnesia:start(foo), mnesia:start([foo]), mnesia:start([{debug, foo}])] end),
    t(stop_end, fun() -> mnesia:stop() end).

% Helpers.

c(Name, Options) ->
    t({create, Name}, fun() -> mnesia:create_table(Name, Options) end).

t(Label, F) ->
    io:format("~p: ~p~n", [Label, catch_all(F)]).

catch_all(F) ->
    try
        F()
    catch
        Class:Reason -> {Class, Reason}
    end.

system_info(Item) ->
    catch_all(fun() -> info_value(Item, mnesia:system_info(Item)) end).

% The information whose values depend on the host or the run: the directory's name and the event subscribers' number.
info_value(directory, Directory) -> filename:basename(Directory);
info_value(subscribers, Pids) -> length(Pids);
info_value(_, Value) -> Value.

ets_info(Tab) ->
    Owner = ets:info(Tab, owner),
    [ets:info(Tab, I) || I <- [type, protection, keypos, named_table]] ++
        [[N || N <- [mnesia_monitor, mnesia_controller], whereis(N) =:= Owner]].

% table_info(Tab, all) without the values of a run: cookies, memory and index tables.
props(Tab) ->
    [{K, prop(K, V)} || {K, V} <- mnesia:table_info(Tab, all)].

prop(cookie, Cookie) -> cookie(Cookie);
prop(cstruct, Cs) -> setelement(19, Cs, cookie(element(19, Cs)));
prop(memory, Words) -> is_integer(Words);
prop(index_info, Info) -> index_shape(Info);
prop(commit_work, Work) -> [index_shape(W) || W <- Work];
prop({index, _}, Table) -> is_reference(Table);
prop(_, Value) -> Value.

cookie({{A, B, 1}, Node}) when is_integer(A), is_integer(B) -> {made, Node};
cookie(Cookie) -> Cookie.

index_info(Tab) ->
    index_shape(mnesia:table_info(Tab, index_info)).

index_shape({index, Type, Positions}) ->
    {index, Type, [{P, Kind, is_reference(Table)} || {P, {Kind, Table}} <- Positions]};
index_shape(Other) ->
    Other.

walk(Tab) ->
    walk(Tab, mnesia:dirty_first(Tab), []).

walk(_, '$end_of_table', Keys) -> lists:sort(Keys);
walk(Tab, Key, Keys) -> walk(Tab, mnesia:dirty_next(Tab, Key), [Key | Keys]).
