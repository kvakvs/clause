%% Schema transactions in mnesia's transaction log (plan 21 step MIM1H2): each schema operation runs between a start and
%% a stop, which does not dump the log, so the log shows the operation's terms as OTP's schema transactions write them:
%% the commit with its {op, ...} tuples under its decision, then the committed decision. The next start replays the log,
%% and the tables' records show that cleared, restored and moved records come back right. Last, the directory's files
%% after a dump. Cookies, the times of table versions, transaction ids and funs are hidden.
-module(mnesia_schema_ops).
-export([main/1]).

-define(DIR, "schema_ops.db").
-define(BACKUP, "schema_ops.BUP").

main(_) ->
    _ = file:del_dir_r(?DIR),
    ok = application:load(mnesia),
    ok = application:set_env(mnesia, dir, ?DIR),
    ok = mnesia:create_schema([node()]),
    Disc = [{disc_copies, [node()]}],
    step(create_table, fun() -> mnesia:create_table(t, [{attributes, [k, v, w]}, {index, [v]} | Disc]) end),
    step(create_ram, fun() -> mnesia:create_table(r, [{ram_copies, [node()]}]) end),
    step(write, fun() -> [mnesia:dirty_write(R) || R <- [{t, 1, 2, 3}, {t, 2, 3, 4}, {r, 1, 2}, {r, 2, 3}]] end),
    step(add_index, fun() -> mnesia:add_table_index(t, w) end),
    step(del_index, fun() -> mnesia:del_table_index(t, w) end),
    step(transform, fun() ->
        mnesia:transform_table(t, fun({t, K, V, W}) -> {t2, K, V, W, x} end, [k, v, w, x], t2)
    end),
    step(copy_type, fun() -> mnesia:change_table_copy_type(r, node(), disc_copies) end),
    step(access_mode, fun() -> mnesia:change_table_access_mode(r, read_only) end),
    step(load_order, fun() -> mnesia:change_table_load_order(r, 5) end),
    step(write_property, fun() -> mnesia:write_table_property(r, {p, 1}) end),
    step(delete_property, fun() -> mnesia:delete_table_property(r, p) end),
    step(access_mode_back, fun() -> mnesia:change_table_access_mode(r, read_write) end),
    step(clear_table, fun() -> mnesia:clear_table(r) end),
    step(write_again, fun() -> [mnesia:dirty_write({r, K, K}) || K <- [1, 2, 3, 4]] end),
    step(snmp_open, fun() -> mnesia:snmp_open_table(r, [{key, integer}]) end),
    step(snmp_close, fun() -> mnesia:snmp_close_table(r) end),
    step(create_m, fun() -> mnesia:create_table(m, [{ram_copies, [node()]}]) end),
    step(dump_tables, fun() ->
        ok = mnesia:dirty_write({m, 1, one}),
        mnesia:dump_tables([m])
    end),
    step(frag_activate, fun() -> mnesia:change_table_frag(r, {activate, []}) end),
    step(frag_add, fun() -> mnesia:change_table_frag(r, {add_frag, [node()]}) end),
    step(frag_del, fun() -> mnesia:change_table_frag(r, del_frag) end),
    step(frag_deactivate, fun() -> mnesia:change_table_frag(r, deactivate) end),
    step(majority, fun() -> mnesia:change_table_majority(r, true) end),
    step(majority_write, fun() -> mnesia:transaction(fun() -> mnesia:write({r, 5, 5}) end) end),
    step(majority_clear, fun() -> mnesia:clear_table(r) end),
    step(majority_back, fun() -> mnesia:change_table_majority(r, false) end),
    step(delete_table, fun() -> mnesia:delete_table(m) end),
    step(backup, fun() ->
        ok = mnesia:dirty_write({r, 7, 7}),
        mnesia:backup(?BACKUP)
    end),
    step(restore_recreate, fun() -> sorted(mnesia:restore(?BACKUP, [{recreate_tables, [r]}])) end),
    step(restore_clear, fun() -> sorted(mnesia:restore(?BACKUP, [{clear_tables, [r]}, {keep_tables, [t]}])) end),
    step(copy_type_only, fun() -> mnesia:change_table_copy_type(r, node(), disc_only_copies) end),
    step(clear_only, fun() -> mnesia:clear_table(r) end),
    step(create_frag, fun() ->
        mnesia:create_table(f, [{frag_properties, [{n_fragments, 2}, {node_pool, [node()]}]} | Disc])
    end),
    step(del_copy, fun() -> mnesia:del_table_copy(r, node()) end),
    ok = mnesia:start(),
    ok = mnesia:wait_for_tables(mnesia:system_info(tables), 10000),
    show(records, records()),
    show(dump_log, mnesia:dump_log()),
    stopped = mnesia:stop(),
    files(),
    io:format("mnesia_schema_ops: ok~n").

%% One operation between a start and a stop: the records the start found, the operation's result and the log after.
step(Name, Fun) ->
    ok = mnesia:start(),
    ok = mnesia:wait_for_tables(mnesia:system_info(tables), 10000),
    Records = records(),
    Result = Fun(),
    stopped = mnesia:stop(),
    io:format("~n== ~p: ~p~n", [Name, Result]),
    show(records, Records),
    show(log, [hide(T) || T <- log_terms(filename:join(?DIR, "LATEST.LOG")), not is_header(T)]).

records() ->
    [{T, lists:sort(mnesia:dirty_match_object(T, mnesia:table_info(T, wild_pattern)))} || T <- tables()].

tables() -> lists:sort(mnesia:system_info(tables)) -- [schema].

sorted({atomic, Tabs}) -> {atomic, lists:sort(Tabs)};
sorted(Other) -> Other.

%% The directory's files after the last dump and their terms, sorted where the order is a table's.
files() ->
    {ok, Names} = file:list_dir(?DIR),
    show(files, lists:sort(Names)),
    [
        show(Name, contents(Name, filename:join(?DIR, Name)))
     || Name <- lists:sort(Names), filename:extension(Name) =/= ".DAT"
    ],
    ok.

contents("DECISION_TAB.LOG", Path) ->
    [{decision_list, lists:sort([hide(D) || D <- List])} || {decision_list, List} <- log_terms(Path)];
contents(Name, Path) ->
    case filename:extension(Name) of
        ".DCD" -> lists:sort([hide(T) || T <- log_terms(Path)]);
        ".DCL" -> lists:keysort(1, [hide(T) || T <- log_terms(Path)]);
        _ -> [hide(T) || T <- log_terms(Path)]
    end.

is_header(T) -> element(1, T) =:= log_header.

%% A term with cookies, version times, transaction ids, funs and log headers' details hidden. A commit's items, its
%% moved records and its restored tables are sorted, as they come in the order of OTP's ets tables.
hide({commit, Node, Decision, Ram, Disc, DiscOnly, Ext, Ops}) ->
    Sorted = [Op || Op <- Ops, lists:member(element(2, Op), [rec, clear_table, restore_recreate])],
    [Ram2, Disc2, DiscOnly2] = [lists:keysort(1, I) || I <- [Ram, Disc, DiscOnly]],
    {commit, Node, hide(Decision), Ram2, Disc2, DiscOnly2, Ext, hide((Ops -- Sorted) ++ lists:sort(Sorted))};
hide({log_header, Kind, Version, _, _, _}) ->
    {header, Kind, Version};
hide({cookie, _}) ->
    {cookie, cookie};
hide({version, {Version, {Node, _}}}) ->
    {version, {Version, {Node, time}}};
hide({tid, _, _}) ->
    {tid, n, pid};
hide({trans_tid, serial, _}) ->
    {trans_tid, serial, n};
hide(Fun) when is_function(Fun) -> 'fun';
hide(Tuple) when is_tuple(Tuple) -> list_to_tuple([hide(E) || E <- tuple_to_list(Tuple)]);
hide(List) when is_list(List) -> [hide(E) || E <- List];
hide(Term) ->
    Term.

log_terms(Path) ->
    Name = make_ref(),
    {ok, Name} = disk_log:open([{name, Name}, {file, Path}, {mode, read_only}]),
    Terms = chunks(Name, start),
    ok = disk_log:close(Name),
    Terms.

chunks(Name, Continuation) ->
    case disk_log:chunk(Name, Continuation) of
        eof -> [];
        {Next, Terms} -> Terms ++ chunks(Name, Next)
    end.

show(Label, Value) -> io:format("~s: ~p~n", [Label, Value]).
