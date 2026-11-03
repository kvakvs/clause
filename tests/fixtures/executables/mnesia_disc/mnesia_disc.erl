%% mnesia's disc storage (plan 21 step MIM1D): creating and deleting the disc schema, disc_copies and disc_only_copies
%% tables with their indexes, the transaction log and its dumps, dumps of RAM tables, storage type changes of tables and
%% of the schema, and a program that stops and starts mnesia and finds its data, each with OTP's results.
-module(mnesia_disc).
-export([main/1]).

-define(OTHER, 'other@nohost').

main(_) ->
    ok = application:load(mnesia),
    ok = mnesia:delete_schema([node()]),
    no_schema(),
    create_schema(),
    ok = mnesia:start(),
    started(),
    tables(),
    writes(),
    log_writes(),
    dumps(),
    restart(),
    copy_types(),
    disc_only(),
    deleted_tables(),
    stopped = mnesia:stop(),
    ok = mnesia:delete_schema([node()]),
    ram_schema(),
    schema_location(),
    io:format("mnesia_disc: ok~n").

% The functions without a schema on disc.
no_schema() ->
    t(no_schema, fun() ->
        [
            mnesia:system_info(use_dir),
            mnesia:system_info(fallback_activated),
            mnesia:dump_log(),
            mnesia:sync_log(),
            mnesia:dump_tables([t])
        ]
    end),
    t(create_schema_errors, fun() ->
        [
            catch_all(fun() -> mnesia:create_schema(foo) end),
            catch_all(fun() -> mnesia:create_schema([a, a]) end),
            catch_all(fun() -> mnesia:create_schema([1]) end),
            catch_all(fun() -> mnesia:create_schema([?OTHER]) end),
            catch_all(fun() -> mnesia:create_schema([node(), ?OTHER]) end),
            catch_all(fun() -> mnesia:create_schema([node()], foo) end),
            catch_all(fun() -> mnesia:create_schema([node()], [bogus]) end),
            catch_all(fun() -> mnesia:create_schema([node()], [{bogus, 1}]) end),
            files()
        ]
    end).

% create_schema/1 leaves a fallback for the next start.
create_schema() ->
    t(create_schema, fun() ->
        [
            mnesia:create_schema([]),
            files(),
            mnesia:system_info(use_dir),
            mnesia:system_info(fallback_activated),
            mnesia:create_schema([node()])
        ]
    end),
    t(delete_schema_errors, fun() ->
        [mnesia:delete_schema(foo), mnesia:delete_schema([]), mnesia:delete_schema([?OTHER])]
    end).

started() ->
    t(started, fun() ->
        [
            files(),
            mnesia:system_info(use_dir),
            mnesia:system_info(fallback_activated),
            mnesia:system_info(transaction_log_writes),
            [mnesia:table_info(schema, I) || I <- [storage_type, disc_copies, ram_copies, load_reason]],
            mnesia:create_schema([node()]),
            mnesia:delete_schema([node()])
        ]
    end).

tables() ->
    t(create_tables, fun() ->
        [
            mnesia:create_table(dc, [{disc_copies, [node()]}, {attributes, [k, a]}, {index, [a]}]),
            mnesia:create_table(dco, [{disc_only_copies, [node()]}, {attributes, [k, a]}, {index, [a]}]),
            mnesia:create_table(rc, []),
            mnesia:create_table(dcb, [{disc_copies, [node()]}, {type, bag}]),
            mnesia:create_table(dcob, [{disc_only_copies, [node()]}, {type, bag}]),
            mnesia:create_table(dcord, [{disc_copies, [node()]}, {type, ordered_set}]),
            mnesia:create_table(dco2, [{disc_only_copies, [node()]}, {type, ordered_set}]),
            mnesia:create_table(cnt, [{disc_only_copies, [node()]}]),
            mnesia:create_table(dcsp, [{disc_only_copies, [node()]}, {storage_properties, [{dets, [{file, "x"}]}]}]),
            files()
        ]
    end),
    t(table_info, fun() -> [info(T) || T <- [dc, dco, rc, dcob]] end),
    t(cstruct_index, fun() -> element(11, mnesia:table_info(dco, cstruct)) end),
    t(schema_info, fun() ->
        [
            mnesia:system_info(use_dir),
            lists:sort(mnesia:system_info(tables)),
            lists:sort(mnesia:system_info(local_tables)),
            mnesia:system_info(db_nodes)
        ]
    end).

writes() ->
    [ok = mnesia:dirty_write({T, K, K * 10}) || T <- [dc, dco, rc, dcb, dcob, dcord], K <- [1, 2, 3]],
    t(transaction, fun() ->
        mnesia:transaction(fun() ->
            [mnesia:write({T, 4, 40}) || T <- [dc, dco, rc, dcb, dcob]],
            mnesia:delete({dc, 1})
        end)
    end),
    t(counter, fun() ->
        [mnesia:dirty_update_counter(cnt, a, 5), mnesia:dirty_update_counter(cnt, a, -2), mnesia:dirty_read(cnt, a)]
    end),
    t(disc_only_reads, fun() ->
        [
            lists:sort(mnesia:dirty_match_object({dco, '_', '_'})),
            mnesia:dirty_read(dco, 2),
            mnesia:dirty_index_read(dco, 20, a),
            lists:sort(mnesia:dirty_all_keys(dco)),
            mnesia:table_info(dco, size),
            mnesia:table_info(dc, size),
            lists:sort(mnesia:dirty_select(dco, [{{dco, '$1', '_'}, [], ['$1']}])),
            tx(fun() -> lists:sort(mnesia:all_keys(dco)) end),
            tx(fun() -> lists:sort(mnesia:foldl(fun(R, A) -> [R | A] end, [], dcob)) end),
            tx(fun() -> lists:sort(mnesia:index_read(dco, 30, a)) end)
        ]
    end).

% Each commit that writes a disc table is one write of the transaction log, a schema transaction two.
log_writes() ->
    d(start, fun() -> ok end),
    d(create_disc_table, fun() -> mnesia:create_table(lw, [{disc_copies, [node()]}]) end),
    d(create_ram_table, fun() -> mnesia:create_table(lwr, []) end),
    d(failed_create, fun() -> mnesia:create_table(lwr, []) end),
    d(dirty_disc, fun() -> mnesia:dirty_write({lw, 1, 1}) end),
    d(dirty_ram, fun() -> mnesia:dirty_write({lwr, 1, 1}) end),
    d(transaction_ram, fun() -> tx(fun() -> mnesia:write({lwr, 1, 2}) end) end),
    d(transaction_disc, fun() ->
        tx(fun() ->
            mnesia:write({lw, 1, 2}),
            mnesia:write({lwr, 2, 2})
        end)
    end),
    d(transaction_read, fun() -> tx(fun() -> mnesia:read({lw, 1}) end) end),
    d(sync_dirty, fun() -> mnesia:sync_dirty(fun() -> mnesia:write({lw, 1, 3}) end) end),
    d(ets, fun() -> mnesia:ets(fun() -> mnesia:write({lw, 1, 4}) end) end),
    d(counter, fun() -> mnesia:dirty_update_counter(lw, c, 1) end),
    d(add_index, fun() -> mnesia:add_table_index(lw, val) end),
    d(clear_ram, fun() -> mnesia:clear_table(lwr) end),
    d(clear_disc, fun() -> mnesia:clear_table(lw) end),
    d(property, fun() -> mnesia:write_table_property(lwr, {a, 1}) end),
    d(delete_tables, fun() -> [mnesia:delete_table(lwr), mnesia:delete_table(lw)] end),
    d(dump_log, fun() -> mnesia:dump_log() end).

dumps() ->
    t(dump_log, fun() -> [mnesia:dump_log(), mnesia:sync_log()] end),
    t(dump_tables, fun() ->
        [
            mnesia:dump_tables([rc]),
            mnesia:dump_tables([schema]),
            mnesia:dump_tables([dc]),
            mnesia:dump_tables(foo),
            mnesia:dump_tables([nope]),
            mnesia:dump_tables([rc, nope]),
            mnesia:dump_tables([]),
            mnesia:dump_tables([rc, rc]),
            files()
        ]
    end).

% The data is back after a restart: disc tables, the dumped RAM table, indexes and counters.
restart() ->
    t(delete_while_running, fun() -> mnesia:delete_schema([node()]) end),
    stopped = mnesia:stop(),
    t(stopped, fun() ->
        [
            mnesia:system_info(use_dir),
            mnesia:system_info(fallback_activated),
            mnesia:dump_log(),
            mnesia:sync_log(),
            mnesia:dump_tables([rc])
        ]
    end),
    ok = mnesia:start(),
    t(restarted, fun() ->
        [
            mnesia:wait_for_tables([dc, dco, rc, dcb, dcob, cnt, dcord], 5000),
            [{T, contents(T)} || T <- [dc, dco, rc, dcb, dcob, cnt, dcord]],
            mnesia:dirty_index_read(dc, 20, a),
            mnesia:dirty_index_read(dco, 30, a),
            [{T, mnesia:table_info(T, I)} || T <- [schema, dc, dco, rc], I <- [storage_type, load_reason, load_node]],
            mnesia:system_info(transaction_log_writes)
        ]
    end).

copy_types() ->
    t(copy_types, fun() ->
        [
            mnesia:change_table_copy_type(rc, node(), disc_copies),
            mnesia:change_table_copy_type(rc, node(), disc_only_copies),
            mnesia:change_table_copy_type(dc, node(), disc_only_copies),
            mnesia:change_table_copy_type(dco, node(), ram_copies),
            mnesia:change_table_copy_type(dcb, node(), disc_only_copies),
            mnesia:change_table_copy_type(dcord, node(), disc_only_copies),
            sorted_tables(mnesia:change_table_copy_type(schema, node(), ram_copies)),
            files()
        ]
    end),
    t(after_copy_types, fun() ->
        [
            [{T, mnesia:table_info(T, storage_type), contents(T)} || T <- [dc, dco, rc, dcb]],
            mnesia:dirty_index_read(dc, 20, a),
            mnesia:dirty_index_read(dco, 30, a),
            index_info(dc)
        ]
    end),
    t(back_to_disc, fun() ->
        [
            mnesia:change_table_copy_type(dco, node(), disc_copies),
            mnesia:change_table_copy_type(dc, node(), disc_copies),
            [{T, mnesia:table_info(T, storage_type), contents(T)} || T <- [dc, dco]],
            mnesia:dirty_index_read(dc, 30, a)
        ]
    end).

% A disc_only_copies table walks, selects and transforms as dets allows.
disc_only() ->
    t(disc_only_indexes, fun() ->
        [
            mnesia:create_table(d, [{disc_only_copies, [node()]}, {attributes, [k, a, b]}, {index, [a, {b, ordered}]}]),
            index_info(d),
            mnesia:add_table_index(d, b),
            mnesia:del_table_index(d, a),
            index_info(d),
            mnesia:dirty_write({d, 1, x, y}),
            mnesia:dirty_write({d, 2, x, z}),
            catch_all(fun() -> mnesia:dirty_index_read(d, y, b) end),
            catch_all(fun() -> mnesia:dirty_index_match_object({d, '_', '_', z}, b) end),
            tx(fun() -> mnesia:index_read(d, z, b) end),
            lists:sort(files())
        ]
    end),
    [ok = mnesia:dirty_write({d, K, K, K}) || K <- lists:seq(3, 12)],
    t(disc_only_walks, fun() ->
        First = mnesia:dirty_first(d),
        [
            mnesia:dirty_last(d) =:= First,
            mnesia:dirty_prev(d, First) =:= mnesia:dirty_next(d, First),
            lists:sort(mnesia:dirty_select_reverse(d, [{{d, '$1', '_', '_'}, [], ['$1']}])),
            tx(fun() ->
                {R1, C1} = mnesia:select(d, [{{d, '$1', '_', '_'}, [], ['$1']}], 5, read),
                {R2, C2} = mnesia:select(C1),
                {R3, _} = mnesia:select(C2),
                {length(R1), length(R2), lists:sort(R1 ++ R2 ++ R3)}
            end),
            lists:sort(mnesia:dirty_match_object({d, '_', x, '_'}))
        ]
    end),
    t(disc_only_transform, fun() ->
        [
            mnesia:transform_table(d, fun({d, K, A, B}) -> {d, K, B, A} end, [k, a, b]),
            lists:sort(mnesia:dirty_index_read(d, x, b)),
            mnesia:dirty_read(d, 1)
        ]
    end),
    t(disc_transform, fun() ->
        [
            mnesia:create_table(td, [{disc_copies, [node()]}]),
            mnesia:dirty_write({td, 1, a}),
            mnesia:transform_table(td, fun({td, K, V}) -> {td, K, {V}} end, [key, val]),
            mnesia:create_table(cl, [{disc_copies, [node()]}]),
            mnesia:dirty_write({cl, 1, a}),
            mnesia:clear_table(cl),
            mnesia:dirty_write({cl, 2, b})
        ]
    end).

deleted_tables() ->
    t(delete_tables, fun() -> [mnesia:delete_table(dcb), mnesia:delete_table(rc), lists:member("rc.DCD", files())] end),
    stopped = mnesia:stop(),
    ok = mnesia:start(),
    t(restarted_again, fun() ->
        [
            mnesia:wait_for_tables([dc, dco, dcob, d, td, cl], 5000),
            [{T, mnesia:table_info(T, storage_type), contents(T)} || T <- [dc, dco, dcob, td, cl]],
            lists:sort(mnesia:dirty_index_read(d, x, b)),
            lists:sort(mnesia:system_info(tables))
        ]
    end).

% A RAM schema has no log; its schema goes to disc while mnesia runs, and back.
ram_schema() ->
    ok = mnesia:start(),
    t(ram_schema, fun() ->
        [
            mnesia:system_info(use_dir),
            mnesia:dump_log(),
            mnesia:sync_log(),
            mnesia:create_table(r, []),
            mnesia:dump_tables([r]),
            mnesia:create_table(x, [{disc_copies, [node()]}]),
            mnesia:change_table_copy_type(r, node(), disc_copies),
            mnesia:change_table_copy_type(schema, node(), ram_copies),
            mnesia:system_info(transaction_log_writes)
        ]
    end),
    t(to_disc_schema, fun() ->
        [
            mnesia:dirty_write({r, 1, 1}),
            mnesia:change_table_copy_type(schema, node(), disc_copies),
            mnesia:system_info(use_dir),
            files(),
            mnesia:table_info(schema, storage_type),
            mnesia:table_info(schema, load_reason),
            mnesia:table_info(r, load_reason),
            mnesia:system_info(transaction_log_writes),
            mnesia:create_table(x, [{disc_copies, [node()]}]),
            mnesia:dirty_write({x, 1, 2})
        ]
    end),
    t(back_to_ram_schema, fun() ->
        [
            mnesia:change_table_copy_type(schema, node(), ram_copies),
            mnesia:delete_table(x),
            mnesia:change_table_copy_type(schema, node(), ram_copies),
            mnesia:system_info(use_dir),
            files(),
            mnesia:table_info(schema, storage_type),
            mnesia:change_table_copy_type(schema, node(), disc_copies),
            mnesia:dump_tables([r]),
            files()
        ]
    end),
    stopped = mnesia:stop(),
    ok = mnesia:start(),
    t(dumped_ram_table, fun() ->
        [
            mnesia:wait_for_tables([r], 5000),
            mnesia:dirty_read(r, 1),
            [mnesia:table_info(r, I) || I <- [storage_type, load_reason]],
            mnesia:change_table_copy_type(r, node(), disc_copies),
            mnesia:change_table_copy_type(r, node(), disc_only_copies)
        ]
    end),
    stopped = mnesia:stop(),
    ok = mnesia:delete_schema([node()]).

% schema_location ram never uses the directory, and the schema stays in RAM.
schema_location() ->
    ok = application:set_env(mnesia, schema_location, ram),
    t(schema_location_ram, fun() -> [mnesia:system_info(use_dir), mnesia:create_schema([node()]), files()] end),
    ok = mnesia:start(),
    t(ram_location_running, fun() ->
        [
            mnesia:system_info(use_dir),
            mnesia:table_info(schema, storage_type),
            mnesia:change_table_copy_type(schema, node(), disc_copies)
        ]
    end),
    stopped = mnesia:stop(),
    ok = application:set_env(mnesia, schema_location, opt_disc).

%% Helpers.

t(Label, F) ->
    io:format("~p: ~p~n", [Label, catch_all(F)]).

catch_all(F) ->
    try
        F()
    catch
        Class:Reason -> {Class, Reason}
    end.

tx(F) ->
    mnesia:transaction(F).

% The change of transaction_log_writes over F, and F's result.
d(Label, F) ->
    Before = mnesia:system_info(transaction_log_writes),
    Result = catch_all(F),
    io:format("~p: ~p ~p~n", [Label, mnesia:system_info(transaction_log_writes) - Before, Result]).

% mnesia's files in its directory, without the decision log and the DCL files, which the dumps' timing decides.
files() ->
    case file:list_dir(mnesia:system_info(directory)) of
        {ok, Files} -> lists:sort([F || F <- Files, F =/= "DECISION_TAB.LOG", filename:extension(F) =/= ".DCL"]);
        Error -> Error
    end.

contents(Tab) ->
    lists:sort(mnesia:dirty_match_object(mnesia:table_info(Tab, wild_pattern))).

% A table's storage properties.
info(Tab) ->
    Items = [storage_type, load_reason, where_to_commit, where_to_read, index, disc_copies, disc_only_copies],
    [{I, mnesia:table_info(Tab, I)} || I <- Items] ++ [index_info(Tab)].

% index_info with the references of RAM index tables left out.
index_info(Tab) ->
    {index, Type, Positions} = mnesia:table_info(Tab, index_info),
    {index, Type, [{Entry, hidden(Ix)} || {Entry, Ix} <- Positions]}.

hidden({ram, _}) -> ram;
hidden(Ix) -> Ix.

sorted_tables({aborted, {Reason, Tabs, Node}}) when is_list(Tabs) -> {aborted, {Reason, lists:sort(Tabs), Node}};
sorted_tables(Other) -> Other.
