%% Two nodes take turns on one mnesia directory (plan 21 step MIM1H1): node a creates a disc schema with tables and
%% records, dumps part of its log, sets master nodes and stops; node b starts on that directory without dumping
%% disc_copies tables at start, checks the records, changes records and tables and stops; then node a starts on it
%% again, backs it up and installs the backup as a fallback, which node b starts from. Each turn shows the directory's
%% files and their terms, so that a run with an OTP node on one side and a Clause node on the other proves both read
%% and write each other's files. The nodes take turns through marker files named after the run's epmd port, which no
%% earlier run shares.
-module(mnesia_dir_exchange).
-export([main/1]).

-define(DIR, "exchange.db").

main(["a"]) ->
    _ = file:del_dir_r(?DIR),
    setup(true),
    create(),
    pass_turn(a1),
    wait_turn(b1),
    take(a2),
    ok = mnesia:backup("exchange.BUP"),
    ok = mnesia:install_fallback("exchange.BUP"),
    stopped = mnesia:stop(),
    files(a2_stopped),
    pass_turn(a3),
    io:format("mnesia_dir_exchange: ok~n");
main(["b"]) ->
    wait_turn(a1),
    setup(false),
    take(b1),
    change(),
    stopped = mnesia:stop(),
    files(b1_stopped),
    pass_turn(b1),
    wait_turn(a3),
    take(b3),
    stopped = mnesia:stop(),
    files(b3_stopped),
    io:format("mnesia_dir_exchange: ok~n").

setup(DumpAtStart) ->
    ok = application:load(mnesia),
    ok = application:set_env(mnesia, dir, ?DIR),
    ok = application:set_env(mnesia, dump_disc_copies_at_startup, DumpAtStart).

%% Node a's first turn: tables of each storage, records written in each way, part of the log dumped into DCL files.
create() ->
    ok = mnesia:create_schema([node()]),
    ok = mnesia:start(),
    Disc = [{disc_copies, [node()]}],
    {atomic, ok} = mnesia:create_table(d, [{index, [val]} | Disc]),
    {atomic, ok} = mnesia:create_table(o, [{disc_only_copies, [node()]}, {type, bag}]),
    {atomic, ok} = mnesia:create_table(s, [{type, ordered_set} | Disc]),
    {atomic, ok} = mnesia:create_table(r, [{ram_copies, [node()]}]),
    names(a1_created),
    {atomic, _} = mnesia:transaction(fun() -> [mnesia:write({d, K, K * 10}) || K <- [1, 2, 3]] end),
    {atomic, _} = mnesia:transaction(fun() -> [mnesia:write(R) || R <- [{o, 1, a}, {o, 1, b}, {s, 2, x}]] end),
    ok = mnesia:dirty_write({s, 1, y}),
    ok = mnesia:dirty_write({r, 1, ram}),
    {atomic, ok} = mnesia:sync_transaction(fun() -> mnesia:delete({d, 2}) end),
    ok = mnesia:dirty_write({d, c, 0}),
    6 = mnesia:dirty_update_counter(d, c, 6),
    show(dump_log, mnesia:dump_log()),
    {atomic, ok} = mnesia:transaction(fun() -> mnesia:write({d, 4, 40}) end),
    {atomic, ok} = mnesia:transaction(fun() -> mnesia:delete_object({o, 1, a}) end),
    ok = mnesia:sync_dirty(fun() -> mnesia:write({s, 3, z}) end),
    ok = mnesia:set_master_nodes(d, [node()]),
    stopped = mnesia:stop(),
    files(a1_stopped).

%% A turn on the other node's directory: the records each table holds, the index, the master nodes and the files.
take(Turn) ->
    ok = mnesia:start(),
    ok = mnesia:wait_for_tables(tabs(), 10000),
    show(tables, lists:sort(mnesia:system_info(tables))),
    show(storage, [{T, mnesia:table_info(T, storage_type)} || T <- lists:sort(mnesia:system_info(tables))]),
    show(records, [{T, lists:sort(mnesia:dirty_match_object(T, mnesia:table_info(T, wild_pattern)))} || T <- tabs()]),
    show(index_read, lists:sort(mnesia:dirty_index_read(d, 30, val))),
    show(properties, [{T, mnesia:table_info(T, load_order), mnesia:table_info(T, user_properties)} || T <- tabs()]),
    show(master_nodes, [mnesia:table_info(d, master_nodes), lists:sort(mnesia:system_info(master_node_tables))]),
    names(Turn).

tabs() -> lists:sort(mnesia:system_info(tables)) -- [schema].

%% Node b's changes: records, then tables created, changed and deleted, a dump, then writes, schema changes (MIM1H2) and
%% a cleared table left in the log for the other node's start.
change() ->
    {atomic, ok} = mnesia:transaction(fun() ->
        mnesia:write({d, 10, 100}),
        mnesia:delete({d, 1})
    end),
    ok = mnesia:dirty_write({o, 2, c}),
    ok = mnesia:sync_dirty(fun() -> mnesia:write({s, 4, w}) end),
    {atomic, ok} = mnesia:create_table(n, [{disc_copies, [node()]}, {attributes, [k, v, w]}]),
    {atomic, ok} = mnesia:transaction(fun() -> mnesia:write({n, 1, 2, 3}) end),
    {atomic, ok} = mnesia:add_table_index(s, val),
    {atomic, ok} = mnesia:delete_table(r),
    show(dump_log, mnesia:dump_log()),
    {atomic, ok} = mnesia:sync_transaction(fun() -> mnesia:write({n, 2, 3, 4}) end),
    ok = mnesia:dirty_delete(s, 1),
    ok = mnesia:set_master_nodes(s, [node()]),
    ok = mnesia:set_master_nodes(d, []),
    {atomic, ok} = mnesia:create_table(q, [{disc_copies, [node()]}, {type, bag}]),
    {atomic, _} = mnesia:transaction(fun() -> [mnesia:write({q, K, V}) || {K, V} <- [{1, a}, {1, b}, {2, c}]] end),
    {atomic, ok} = mnesia:write_table_property(q, {owner, b}),
    {atomic, ok} = mnesia:clear_table(o),
    ok = mnesia:dirty_write({o, 3, d}),
    {atomic, ok} = mnesia:change_table_load_order(n, 2).

%% The directory's files while mnesia runs.
names(Turn) ->
    {ok, Names} = file:list_dir(?DIR),
    show(Turn, lists:sort(Names)).

%% The directory's files and their terms once mnesia stopped: a log's header kind and entries, a dets table's records,
%% sorted where the order is the table's (DCL items by key, the order of each key's items kept, as the order of one
%% transaction's writes is OTP's ets order); cookies, the times of table versions and transaction serials hidden.
files(Turn) ->
    {ok, Names} = file:list_dir(?DIR),
    show(Turn, lists:sort(Names)),
    [show(Name, contents(Name, filename:join(?DIR, Name))) || Name <- lists:sort(Names)],
    ok.

contents("schema.DAT", Path) ->
    [{Tab, hide_cookie(List)} || {schema, Tab, List} <- dets_records(Path)];
contents("FALLBACK.BUP", Path) ->
    lists:sort([hide_schema(T) || T <- log_terms(Path)]);
contents("LATEST.LOG", Path) ->
    [hide_entry(T) || T <- log_terms(Path)];
contents("DECISION_TAB.LOG", Path) ->
    [{decision_list, lists:sort([hide_serial(D) || D <- List])} || {decision_list, List} <- log_terms(Path)];
contents(Name, Path) ->
    case filename:extension(Name) of
        ".DAT" -> dets_records(Path);
        ".DCD" -> lists:sort(log_terms(Path));
        ".DCL" -> lists:keysort(1, log_terms(Path));
        _ -> log_terms(Path)
    end.

% A log entry with its items sorted by key and its transaction id, operations' cookies and version times hidden.
hide_entry({commit, Node, Decision, Ram, Disc, DiscOnly, Ext, Ops}) ->
    [Ram2, Disc2, DiscOnly2] = [lists:keysort(1, I) || I <- [Ram, Disc, DiscOnly]],
    {commit, Node, hide_tid(Decision), Ram2, Disc2, DiscOnly2, Ext, [hide_op(Op) || Op <- Ops]};
hide_entry(Entry) ->
    hide_tid(Entry).

hide_tid({decision, {tid, _, _}, Outcome, Disc, Ram}) -> {decision, {tid, n, pid}, Outcome, Disc, Ram};
hide_tid(Term) -> Term.

hide_op(Op) -> list_to_tuple([hide_list(E) || E <- tuple_to_list(Op)]).

hide_list([{name, _} | _] = List) -> hide_cookie(List);
hide_list(Term) -> Term.

hide_schema({schema, Tab, List}) -> {schema, Tab, hide_cookie(List)};
hide_schema(Term) -> Term.

hide_cookie(List) -> [hide_time(P) || P <- List, element(1, P) =/= cookie].

hide_time({version, {Version, {Node, _Time}}}) -> {version, {Version, {Node, time}}};
hide_time(Property) -> Property.

hide_serial({trans_tid, serial, _}) -> {trans_tid, serial, n};
hide_serial(Decision) -> Decision.

dets_records(Path) ->
    {ok, Name} = dets:open_file(Path),
    Records = lists:sort(dets:match_object(Name, '_')),
    ok = dets:close(Name),
    Records.

% A log's terms, its header as {header, Kind, Version}.
log_terms(Path) ->
    Name = make_ref(),
    {ok, Name} = disk_log:open([{name, Name}, {file, Path}, {mode, read_only}]),
    Terms = chunks(Name, start),
    ok = disk_log:close(Name),
    [term(T) || T <- Terms].

chunks(Name, Continuation) ->
    case disk_log:chunk(Name, Continuation) of
        eof -> [];
        {Next, Terms} -> Terms ++ chunks(Name, Next)
    end.

term({log_header, Kind, Version, _, _, _}) -> {header, Kind, Version};
term(Term) -> Term.

show(Label, Value) -> io:format("~s: ~p~n", [Label, Value]).

%% A turn ends with its marker file, which the other node waits for and removes.
pass_turn(Turn) ->
    ok = file:write_file(marker(Turn), <<>>).

wait_turn(Turn) ->
    case file:delete(marker(Turn)) of
        ok ->
            ok;
        {error, _} ->
            timer:sleep(10),
            wait_turn(Turn)
    end.

marker(Turn) -> lists:concat(["turn_", os:getenv("ERL_EPMD_PORT", "4369"), "_", Turn]).
