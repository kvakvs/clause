%% mnesia's schema changes after creating tables (plan 21 step MIM1C): clearing and transforming tables, table copies
%% and their storage types on this node and others, user properties, access mode, load order and majority, master
%% nodes, forced loads, configuration changes and the debug level, each with OTP's results and abort terms.
-module(mnesia_schema_changes).
-export([main/1]).

-define(OTHER, 'other@nohost').

main(_) ->
    ok = application:load(mnesia),
    not_running(),
    ok = mnesia:start(),
    tables(),
    clearing(),
    transforming(),
    transform_failures(),
    copies(),
    copy_types(),
    properties(),
    modes(),
    masters(),
    loading(),
    config(),
    debug_level(),
    counters(),
    stopped = mnesia:stop(),
    io:format("mnesia_schema_changes: ok~n").

% The functions before mnesia starts.
not_running() ->
    t(not_running, fun() ->
        [
            mnesia:clear_table(t),
            mnesia:transform_table(t, ignore, [k, v], t),
            mnesia:add_table_copy(t, node(), ram_copies),
            mnesia:del_table_copy(t, node()),
            mnesia:move_table_copy(t, node(), ?OTHER),
            mnesia:change_table_copy_type(t, node(), disc_copies),
            mnesia:write_table_property(t, {p, 1}),
            mnesia:delete_table_property(t, p),
            catch_all(fun() -> mnesia:read_table_property(t, p) end),
            mnesia:change_table_access_mode(t, read_only),
            mnesia:change_table_load_order(t, 1),
            mnesia:change_table_majority(t, true),
            mnesia:set_master_nodes([node()]),
            mnesia:set_master_nodes(t, [node()]),
            mnesia:force_load_table(t),
            mnesia:change_config(dc_dump_limit, 8),
            mnesia:change_config(extra_db_nodes, []),
            mnesia:set_debug_level(none),
            mnesia:set_debug_level(verbose),
            mnesia:set_debug_level(none),
            mnesia:set_debug_level(bogus)
        ]
    end).

tables() ->
    {atomic, ok} = mnesia:create_table(c, [{attributes, [k, a, b]}, {index, [a]}]),
    {atomic, ok} = mnesia:create_table(cbag, [{type, bag}, {attributes, [k, a]}, {index, [a]}]),
    {atomic, ok} = mnesia:create_table(cord, [{type, ordered_set}]),
    {atomic, ok} = mnesia:create_table(ro, [{access_mode, read_only}]),
    {atomic, ok} = mnesia:create_table(far, [{ram_copies, [?OTHER]}]),
    fill(),
    ok.

fill() ->
    [ok = mnesia:dirty_write({c, K, A, K * 10}) || {K, A} <- [{1, x}, {2, y}, {3, x}]],
    [ok = mnesia:dirty_write({cbag, K, A}) || {K, A} <- [{1, x}, {1, y}, {2, x}]],
    [ok = mnesia:dirty_write({cord, K, K}) || K <- [3, 1, 2]],
    ok.

% clear_table/1: a transaction of its own, never inside one.
clearing() ->
    t(clear, fun() ->
        [
            [mnesia:clear_table(T) || T <- [c, cbag, cord]],
            [mnesia:table_info(T, size) || T <- [c, cbag, cord]],
            mnesia:dirty_index_read(c, x, a),
            mnesia:dirty_index_read(cbag, x, a),
            mnesia:dirty_first(cord)
        ]
    end),
    fill(),
    t(clear_errors, fun() ->
        [
            mnesia:clear_table(nope),
            mnesia:clear_table(ro),
            mnesia:clear_table(far),
            mnesia:clear_table(3),
            tx(fun() -> mnesia:clear_table(c) end),
            catch_all(fun() -> mnesia:async_dirty(fun() -> mnesia:clear_table(cord) end) end),
            catch_all(fun() -> mnesia:sync_dirty(fun() -> mnesia:clear_table(nope) end) end),
            mnesia:table_info(cord, size),
            catch_all(fun() -> mnesia:ets(fun() -> mnesia:clear_table(c) end) end)
        ]
    end),
    fill(),
    t(clear_locked, fun() ->
        Parent = self(),
        Holder = spawn(fun() ->
            Parent !
                {result, self(),
                    tx(fun() ->
                        mnesia:read(c, 1, write),
                        Parent ! {locked, self()},
                        timer:sleep(100),
                        held
                    end)}
        end),
        receive
            {locked, Holder} -> ok
        end,
        [mnesia:clear_table(c), result(Holder), mnesia:table_info(c, size)]
    end),
    fill().

% transform_table/3,4: new attributes and record names, the records transformed and the indexes kept.
transforming() ->
    {atomic, ok} = mnesia:create_table(tr, [{attributes, [k, a]}, {index, [a]}]),
    [ok = mnesia:dirty_write({tr, K, A}) || {K, A} <- [{1, x}, {2, y}, {3, x}]],
    t(widen, fun() ->
        [
            mnesia:transform_table(tr, fun({tr, K, A}) -> {tr, K, A, K * 2} end, [k, a, d]),
            lists:sort(mnesia:dirty_match_object({tr, '_', '_', '_'})),
            lists:sort(mnesia:dirty_index_read(tr, x, a)),
            info(tr)
        ]
    end),
    t(rename, fun() ->
        [
            mnesia:transform_table(tr, fun({tr, K, A, D}) -> {rec, K, A, D + 1} end, [k, a, d], rec),
            lists:sort(mnesia:dirty_select(tr, [{'_', [], ['$_']}])),
            mnesia:dirty_read(tr, 2),
            lists:sort(mnesia:dirty_index_read(tr, x, a)),
            tx(fun() -> mnesia:write(tr, {rec, 4, z, 0}, write) end),
            tx(fun() -> mnesia:write(tr, {tr, 5, z, 0}, write) end),
            info(tr)
        ]
    end),
    t(ignore, fun() ->
        [
            mnesia:transform_table(tr, ignore, [k, a, d, e], rec),
            lists:sort(mnesia:dirty_select(tr, [{'_', [], ['$_']}])),
            info(tr)
        ]
    end),
    t(unchanged_and_bag, fun() ->
        {atomic, ok} = mnesia:create_table(trb, [{type, bag}, {attributes, [k, a]}]),
        [ok = mnesia:dirty_write({trb, K, A}) || {K, A} <- [{1, x}, {1, y}, {2, z}]],
        [
            mnesia:transform_table(trb, fun(R) -> R end, [k, a]),
            mnesia:transform_table(trb, fun({trb, K, A}) -> {trb, K, {A}} end, [k, b]),
            lists:sort(mnesia:dirty_match_object({trb, '_', '_'})),
            mnesia:transform_table(cord, fun({cord, K, V}) -> {cord, K, V, -V} end, [key, val, neg]),
            mnesia:dirty_match_object({cord, '_', '_', '_'}),
            info(trb)
        ]
    end),
    t(index_dropped_by_arity, fun() ->
        {atomic, ok} = mnesia:create_table(tri, [{attributes, [k, a, b]}, {index, [b]}]),
        [mnesia:transform_table(tri, ignore, [k, a]), mnesia:table_info(tri, attributes)]
    end).

% transform_table/3,4's aborts, before and while the records are transformed.
transform_failures() ->
    Capture = capture_event_output(),
    t(transform_errors, fun() ->
        [
            catch_all(fun() -> mnesia:transform_table(nope, ignore, [k, v]) end),
            mnesia:transform_table(nope, ignore, [k, v], nope),
            mnesia:transform_table(schema, ignore, [k, v], schema),
            mnesia:transform_table(ro, ignore, [k, v, w], ro),
            mnesia:transform_table(c, ignore, [k], c),
            mnesia:transform_table(c, ignore, [k, a, a], c),
            mnesia:transform_table(c, ignore, [k, a, 1], c),
            mnesia:transform_table(c, ignore, [k, a, b], "c"),
            mnesia:transform_table(c, ignore, k, c),
            funless(mnesia:transform_table(c, fun(R) -> R end, [k, a, b], "c")),
            mnesia:transform_table(c, not_fun, [k, a, b], c),
            tx(fun() -> mnesia:transform_table(c, ignore, [k, a, b]) end),
            mnesia:transform_table(far, ignore, [k, v, w]),
            mnesia:table_info(far, attributes)
        ]
    end),
    t(bad_transforms, fun() ->
        [
            funless(mnesia:transform_table(c, fun({c, K, A, _}) -> {c, K, A} end, [k, a, b])),
            funless(mnesia:transform_table(c, fun({c, K, A, B}) -> {c, K + 100, A, B} end, [k, a, b])),
            funless(mnesia:transform_table(c, fun({c, K, A, B}) -> {other, K, A, B} end, [k, a, b])),
            funless(
                mnesia:transform_table(
                    c,
                    fun
                        ({c, K, _, _}) when K > 2 -> error(boom);
                        (R) -> R
                    end,
                    [k, a, b]
                )
            ),
            lists:sort(mnesia:dirty_match_object({c, '_', '_', '_'})),
            mnesia:table_info(c, attributes)
        ]
    end),
    t(transform_reports, fun() -> captured(Capture) end).

% Table copies on this node and on a node outside the schema.
copies() ->
    {atomic, ok} = mnesia:create_table(cp, []),
    ok = mnesia:dirty_write({cp, 1, one}),
    t(add_copy, fun() ->
        [
            mnesia:add_table_copy(cp, node(), ram_copies),
            mnesia:add_table_copy(cp, ?OTHER, disc_copies),
            mnesia:add_table_copy(cp, ?OTHER, disc_only_copies),
            mnesia:add_table_copy(cp, ?OTHER, bogus),
            mnesia:add_table_copy(nope, node(), ram_copies),
            mnesia:add_table_copy(cp, "node", ram_copies),
            mnesia:add_table_copy("cp", node(), ram_copies),
            mnesia:add_table_copy(schema, node(), ram_copies),
            mnesia:add_table_copy(schema, ?OTHER, disc_copies),
            tx(fun() -> mnesia:add_table_copy(cp, ?OTHER, ram_copies) end)
        ]
    end),
    t(add_remote_ram_copy, fun() ->
        [
            mnesia:add_table_copy(cp, ?OTHER, ram_copies),
            mnesia:add_table_copy(cp, ?OTHER, ram_copies),
            info(cp),
            mnesia:dirty_read(cp, 1),
            tx(fun() -> mnesia:write({cp, 2, two}) end),
            mnesia:del_table_copy(cp, ?OTHER),
            info(cp)
        ]
    end),
    t(del_copy, fun() ->
        [
            mnesia:del_table_copy(cp, 'third@nohost'),
            mnesia:del_table_copy(cp, "node"),
            mnesia:del_table_copy(nope, node()),
            mnesia:del_table_copy(schema, node()),
            mnesia:del_table_copy(far, ?OTHER),
            lists:member(far, mnesia:system_info(tables)),
            mnesia:del_table_copy(schema, ?OTHER),
            tx(fun() -> mnesia:del_table_copy(cp, node()) end),
            mnesia:del_table_copy(cp, node()),
            lists:member(cp, mnesia:system_info(tables)),
            catch_all(fun() -> mnesia:dirty_read(cp, 1) end)
        ]
    end),
    {atomic, ok} = mnesia:create_table(mv, []),
    {atomic, ok} = mnesia:create_table(far, [{ram_copies, [?OTHER]}]),
    t(remote_schema_copy, fun() ->
        [mnesia:del_table_copy(schema, ?OTHER), catch_all(fun() -> mnesia:table_info(far, all) end)]
    end),
    {atomic, ok} = mnesia:create_table(far2, [{ram_copies, [?OTHER]}]),
    {atomic, ok} = mnesia:create_table(loc, [{local_content, true}]),
    t(move_copy, fun() ->
        [
            mnesia:move_table_copy(mv, node(), ?OTHER),
            mnesia:move_table_copy(mv, node(), node()),
            mnesia:move_table_copy(mv, ?OTHER, node()),
            mnesia:move_table_copy(mv, ?OTHER, 'third@nohost'),
            mnesia:move_table_copy(far2, ?OTHER, node()),
            mnesia:move_table_copy(far2, ?OTHER, 'third@nohost'),
            mnesia:move_table_copy(loc, node(), ?OTHER),
            mnesia:move_table_copy(ro, node(), ?OTHER),
            mnesia:move_table_copy(schema, node(), ?OTHER),
            mnesia:move_table_copy(nope, node(), ?OTHER),
            mnesia:move_table_copy(mv, "a", node()),
            tx(fun() -> mnesia:move_table_copy(mv, node(), ?OTHER) end)
        ]
    end).

% change_table_copy_type/3 on a node without a disc schema.
copy_types() ->
    {atomic, ok} = mnesia:create_table(ct, []),
    t(copy_type, fun() ->
        [
            mnesia:change_table_copy_type(ct, node(), ram_copies),
            mnesia:change_table_copy_type(ct, node(), disc_copies),
            mnesia:change_table_copy_type(ct, node(), disc_only_copies),
            mnesia:change_table_copy_type(ct, node(), bogus),
            mnesia:change_table_copy_type(ct, ?OTHER, ram_copies),
            mnesia:change_table_copy_type(ct, ?OTHER, disc_copies),
            mnesia:change_table_copy_type(far2, ?OTHER, ram_copies),
            mnesia:change_table_copy_type(far2, ?OTHER, disc_copies),
            mnesia:change_table_copy_type(nope, node(), disc_copies),
            mnesia:change_table_copy_type(ct, "node", disc_copies),
            mnesia:change_table_copy_type(schema, node(), ram_copies),
            tx(fun() -> mnesia:change_table_copy_type(ct, node(), disc_copies) end),
            mnesia:change_table_copy_type(ct, node(), unknown),
            lists:member(ct, mnesia:system_info(tables))
        ]
    end).

% User properties: written, read and deleted, the definition's own properties among them.
properties() ->
    {atomic, ok} = mnesia:create_table(pr, [{user_properties, [{owner, ann}, {size, 3, extra}]}]),
    t(properties, fun() ->
        [
            mnesia:read_table_property(pr, owner),
            mnesia:read_table_property(pr, size),
            catch_all(fun() -> mnesia:read_table_property(pr, missing) end),
            catch_all(fun() -> mnesia:read_table_property(nope, owner) end),
            mnesia:write_table_property(pr, {owner, bob}),
            mnesia:write_table_property(pr, {added}),
            mnesia:write_table_property(pr, {added}),
            mnesia:read_table_property(pr, owner),
            mnesia:read_table_property(pr, added),
            mnesia:table_info(pr, user_properties),
            mnesia:delete_table_property(pr, size),
            mnesia:delete_table_property(pr, size),
            mnesia:delete_table_property(pr, never),
            catch_all(fun() -> mnesia:read_table_property(pr, size) end),
            mnesia:table_info(pr, user_properties),
            mnesia:write_table_property(schema, {sp, 1}),
            mnesia:read_table_property(schema, sp),
            mnesia:delete_table_property(schema, sp),
            mnesia:write_table_property(ro, {rp, 1}),
            mnesia:write_table_property(far2, {fp, 1})
        ]
    end),
    t(property_errors, fun() ->
        [
            mnesia:write_table_property(pr, {}),
            mnesia:write_table_property(pr, owner),
            mnesia:write_table_property(nope, {p, 1}),
            mnesia:delete_table_property(nope, p),
            tx(fun() -> mnesia:write_table_property(pr, {p, 1}) end),
            tx(fun() -> mnesia:delete_table_property(pr, owner) end),
            catch_all(fun() -> tx(fun() -> mnesia:read_table_property(pr, owner) end) end)
        ]
    end),
    t(properties_after_delete, fun() ->
        {atomic, ok} = mnesia:delete_table(pr),
        {atomic, ok} = mnesia:create_table(pr, []),
        [catch_all(fun() -> mnesia:read_table_property(pr, owner) end), mnesia:table_info(pr, user_properties)]
    end).

% Access mode, load order and majority.
modes() ->
    {atomic, ok} = mnesia:create_table(md, []),
    ok = mnesia:dirty_write({md, 1, one}),
    t(access_mode, fun() ->
        [
            mnesia:change_table_access_mode(md, read_only),
            mnesia:change_table_access_mode(md, read_only),
            mnesia:table_info(md, access_mode),
            mnesia:table_info(md, where_to_write),
            mnesia:table_info(md, where_to_commit),
            tx(fun() -> mnesia:write({md, 2, two}) end),
            catch_all(fun() -> mnesia:dirty_write({md, 2, two}) end),
            mnesia:dirty_read(md, 1),
            mnesia:clear_table(md),
            mnesia:change_table_access_mode(md, read_write),
            tx(fun() -> mnesia:write({md, 2, two}) end),
            mnesia:table_info(md, where_to_write),
            mnesia:change_table_access_mode(md, bogus),
            mnesia:change_table_access_mode(nope, read_only),
            mnesia:change_table_access_mode(far2, read_only),
            mnesia:table_info(far2, access_mode),
            tx(fun() -> mnesia:change_table_access_mode(md, read_only) end)
        ]
    end),
    t(load_order, fun() ->
        [
            mnesia:change_table_load_order(md, 5),
            mnesia:table_info(md, load_order),
            mnesia:change_table_load_order(md, 5),
            mnesia:change_table_load_order(md, -1),
            mnesia:change_table_load_order(md, x),
            mnesia:change_table_load_order(schema, 1),
            mnesia:change_table_load_order(nope, 1),
            tx(fun() -> mnesia:change_table_load_order(md, 1) end)
        ]
    end),
    {atomic, ok} = mnesia:create_table(lc, [{local_content, true}]),
    t(majority, fun() ->
        [
            mnesia:change_table_majority(md, true),
            mnesia:table_info(md, majority),
            mnesia:table_info(md, where_to_wlock),
            tx(fun() -> mnesia:write({md, 3, three}) end),
            mnesia:change_table_majority(md, true),
            mnesia:change_table_majority(md, false),
            mnesia:table_info(md, where_to_wlock),
            catch_all(fun() -> mnesia:change_table_majority(md, perhaps) end),
            mnesia:change_table_majority(lc, true),
            mnesia:change_table_majority(schema, true),
            mnesia:change_table_majority(nope, true),
            tx(fun() -> mnesia:change_table_majority(md, true) end)
        ]
    end),
    t(mode_info, fun() -> info(md) end).

% Master nodes of every table or one.
masters() ->
    t(masters, fun() ->
        [
            mnesia:table_info(md, master_nodes),
            mnesia:system_info(master_node_tables),
            mnesia:set_master_nodes(md, [node()]),
            mnesia:table_info(md, master_nodes),
            mnesia:system_info(master_node_tables),
            mnesia:set_master_nodes(md, []),
            mnesia:system_info(master_node_tables),
            mnesia:set_master_nodes(md, [?OTHER]),
            mnesia:set_master_nodes(far2, [?OTHER]),
            mnesia:table_info(far2, master_nodes),
            mnesia:set_master_nodes(nope, [node()]),
            mnesia:set_master_nodes(md, node()),
            mnesia:set_master_nodes([node(), ?OTHER]),
            lists:sort(mnesia:system_info(master_node_tables)),
            mnesia:table_info(far2, master_nodes),
            mnesia:table_info(schema, master_nodes),
            lists:keyfind(master_nodes, 1, mnesia:table_info(md, all)),
            mnesia:set_master_nodes([]),
            mnesia:system_info(master_node_tables),
            mnesia:set_master_nodes(node()),
            tx(fun() -> mnesia:set_master_nodes(md, [node()]) end),
            mnesia:set_master_nodes(md, [])
        ]
    end).

% force_load_table/1 of loaded tables.
loading() ->
    t(force_load, fun() ->
        [
            mnesia:force_load_table(md),
            mnesia:force_load_table(schema),
            mnesia:force_load_table(nope),
            mnesia:force_load_table("md"),
            mnesia:table_info(md, load_by_force)
        ]
    end).

% change_config/2 of a running mnesia.
config() ->
    t(config, fun() ->
        [
            mnesia:change_config(dc_dump_limit, 8),
            mnesia:system_info(dc_dump_limit),
            mnesia:change_config(dc_dump_limit, 2.5),
            mnesia:system_info(dc_dump_limit),
            mnesia:change_config(dc_dump_limit, 0),
            mnesia:change_config(dc_dump_limit, x),
            mnesia:change_config(extra_db_nodes, []),
            mnesia:change_config(extra_db_nodes, [node()]),
            mnesia:change_config(extra_db_nodes, [?OTHER]),
            mnesia:change_config(extra_db_nodes, x),
            mnesia:change_config(debug, verbose),
            mnesia:system_info(extra_db_nodes),
            tx(fun() -> mnesia:change_config(dc_dump_limit, 4) end)
        ]
    end).

% set_debug_level/1 of a running mnesia: the previous level back.
debug_level() ->
    t(debug_level, fun() ->
        [
            mnesia:set_debug_level(verbose),
            mnesia:system_info(debug),
            mnesia:set_debug_level(false),
            mnesia:set_debug_level(true),
            mnesia:set_debug_level(none),
            mnesia:set_debug_level(7),
            mnesia:system_info(debug),
            application:get_env(mnesia, debug)
        ]
    end).

% The transactions the schema changes counted.
counters() ->
    Before = counts(),
    {atomic, ok} = mnesia:clear_table(md),
    {aborted, _} = mnesia:clear_table(nope),
    {atomic, ok} = mnesia:write_table_property(md, {p, 1}),
    {aborted, _} = mnesia:change_table_load_order(nope, 1),
    _ = mnesia:change_config(extra_db_nodes, []),
    _ = mnesia:set_master_nodes(md, [node()]),
    t(counted, fun() -> deltas(Before) end).

% Helpers.

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

% A table's properties that schema changes set, without its cookie, version and the time.
info(Tab) ->
    Items = [attributes, arity, record_name, index, wild_pattern, type, ram_copies, access_mode, size],
    [{I, mnesia:table_info(Tab, I)} || I <- Items].

% A term with its funs as the atom 'fun': OTP prints a fun with its module's numbering.
funless(F) when is_function(F) -> 'fun';
funless(T) when is_tuple(T) -> list_to_tuple(funless(tuple_to_list(T)));
funless([H | T]) -> [funless(H) | funless(T)];
funless(X) -> X.

result(Pid) ->
    receive
        {result, Pid, R} -> R
    after 5000 -> timeout
    end.

counts() ->
    [mnesia:system_info(I) || I <- [transaction_commits, transaction_failures, transaction_restarts]].

deltas(Before) ->
    lists:zipwith(fun(A, B) -> A - B end, counts(), Before).

% mnesia_event's output goes to a process that keeps it: its reports name the failing function's stack trace.
capture_event_output() ->
    Capture = spawn(fun() -> capture([]) end),
    true = group_leader(Capture, whereis(mnesia_event)),
    Capture.

capture(Texts) ->
    receive
        {io_request, From, Ref, Request} ->
            From ! {io_reply, Ref, ok},
            capture([text(Request) | Texts]);
        {texts, From} ->
            From ! {texts, lists:reverse(Texts)}
    end.

text({put_chars, _, Chars}) -> unicode:characters_to_list(Chars);
text({put_chars, _, M, F, A}) -> lists:flatten(apply(M, F, A));
text({put_chars, Chars}) -> unicode:characters_to_list(Chars);
text(Other) -> Other.

% Each captured report up to its stack trace.
captured(Capture) ->
    timer:sleep(100),
    Capture ! {texts, self()},
    receive
        {texts, Texts} -> [hd(string:split(T, "' in '")) || T <- Texts]
    after 5000 -> timeout
    end.
