%% Master nodes, majority and copies between two disc nodes (plan 21 step MIM1H10b,
%% docs/libraries/mnesia-protocol.md#master-nodes-majority-and-copies-mim1h10b): master nodes set while mnesia runs
%% and while it is stopped decide where the replicas load from at the next start, force_load_table/1 loads a replica
%% that waits, a majority table refuses writes while half of its replicas are down. Then replicas are added, moved
%% and changed between the nodes, b leaves the schema while stopped, its files go, and it joins again as a RAM node
%% made a disc node. Each step shows where the tables are read and loaded from, the master nodes, and what each
%% node's files hold, without the transaction ids and times that differ between runs. Only exported functions
%% travel, so either node may be an OTP node.
-module(mnesia_disc_masters).
-export([main/1]).
-export([start/0, stop/0, info/1, masters/0, files/0, finish/0]).

-define(TABLES, [td, tm, tr, to, tx]).
-define(COPIES, [td, tn, tp, tq, tu, tr]).

main(["a"]) ->
    B = 'mdmb@localhost',
    wait_registered("mdmb_ready"),
    pong = net_adm:ping(B),
    masters_and_majority(node(), B),
    copies(node(), B),
    rpc:call(B, ?MODULE, finish, []),
    io:format("mnesia_disc_masters: ok~n");
main(["b"]) ->
    _Ready = mark("mdmb_ready"),
    receive
    after infinity -> ok
    end.

masters_and_majority(A, B) ->
    % The runs of this program share their directory: an earlier run's schemas go first.
    ok = mnesia:delete_schema([A, B]),
    show("create_schema", mnesia:create_schema([A, B])),
    show("masters_before_start", [mnesia:set_master_nodes(td, [B]), mnesia:set_master_nodes([A])]),
    show("start", together(B)),
    show("create_tables", [
        mnesia:create_table(td, [{disc_copies, [A, B]}, {attributes, [k, v]}]),
        mnesia:create_table(tm, [{disc_copies, [A, B]}, {attributes, [k, v]}, {majority, true}]),
        mnesia:create_table(tr, [{ram_copies, [A, B]}, {attributes, [k, v]}]),
        mnesia:create_table(to, [{disc_only_copies, [A, B]}, {attributes, [k, v]}]),
        mnesia:create_table(tx, [{disc_copies, [A]}, {ram_copies, [B]}, {attributes, [k, v]}])
    ]),
    show("writes", mnesia:transaction(fun() -> [mnesia:write({T, 1, a}) || T <- ?TABLES] end)),
    show("masters_refused", [
        mnesia:set_master_nodes(td, [c@localhost]),
        mnesia:set_master_nodes(nope, [A]),
        mnesia:set_master_nodes(td, x),
        mnesia:set_master_nodes(x),
        mnesia:set_master_nodes([c@localhost])
    ]),
    show("set_masters", [
        mnesia:set_master_nodes(td, [B]),
        mnesia:set_master_nodes(to, [A, B]),
        rpc:call(B, mnesia, set_master_nodes, [[A]])
    ]),
    show("masters", both(B, masters, [])),
    show("force_loaded", [mnesia:force_load_table(td), mnesia:force_load_table(nope), mnesia:force_load_table(schema)]),
    show("b_stops", rpc:call(B, ?MODULE, stop, [])),
    show("b_masters_stopped", [
        rpc:call(B, mnesia, set_master_nodes, [tr, [B]]),
        rpc:call(B, mnesia, set_master_nodes, [nope, [B]]),
        rpc:call(B, mnesia, set_master_nodes, [tr, [c@localhost]])
    ]),
    show("majority_with_b_down", [
        mnesia:transaction(fun() -> mnesia:write({tm, 2, a}) end),
        mnesia:dirty_write({tm, 3, a}),
        mnesia:sync_transaction(fun() -> mnesia:write({tm, 4, a}) end),
        mnesia:transaction(fun() -> mnesia:read(tm, 1) end),
        mnesia:transaction(fun() -> mnesia:read(tm, 1, write) end),
        mnesia:transaction(fun() -> mnesia:write({td, 2, a}) end),
        mnesia:transaction(fun() ->
            mnesia:write({tm, 5, a}),
            mnesia:write({td, 5, a})
        end),
        mnesia:add_table_index(tm, v),
        mnesia:change_table_majority(td, true)
    ]),
    show("majority_info", [{I, mnesia:table_info(tm, I)} || I <- [where_to_write, where_to_wlock, majority]]),
    show("b_starts", rpc:call(B, ?MODULE, start, [])),
    show("tables_after_b_starts", both(B, info, [{tables, ?TABLES}])),
    show("majority_with_both", [
        mnesia:transaction(fun() -> mnesia:write({tm, 7, a}) end),
        mnesia:change_table_majority(td, true)
    ]),
    show("a_stops", stop()),
    show("b_stops_last", rpc:call(B, ?MODULE, stop, [])),
    show("a_starts_alone", start()),
    show("tables_with_a_alone", info({tables, ?TABLES})),
    show("forced", [mnesia:force_load_table(td), mnesia:force_load_table(tm)]),
    show("tables_forced", info({tables, ?TABLES})),
    show("majority_alone", [
        mnesia:transaction(fun() -> mnesia:write({tm, 8, a}) end),
        mnesia:transaction(fun() -> mnesia:write({td, 8, a}) end)
    ]),
    show("b_starts_again", rpc:call(B, ?MODULE, start, [])),
    show("a_waits", mnesia:wait_for_tables(?TABLES, 5000)),
    show("tables_after_both", both(B, info, [{tables, ?TABLES}])),
    show("masters_cleared", [
        mnesia:set_master_nodes([]),
        rpc:call(B, mnesia, set_master_nodes, [td, []]),
        rpc:call(B, mnesia, set_master_nodes, [[]])
    ]),
    show("masters_after_clear", both(B, masters, [])),
    show("both_stop", [stop(), rpc:call(B, ?MODULE, stop, [])]),
    show("files_with_masters", both(B, files, [])).

copies(A, B) ->
    % b stopped last: it loads its disc replicas and waits for tx, which a holds on disc; a then loads from b.
    show("b_starts_first", rpc:call(B, ?MODULE, start, [])),
    show("a_starts_after", start()),
    % A majority table's lock would refuse b leaving the schema while it is down.
    show("no_majority", [mnesia:change_table_majority(T, false) || T <- [td, tm]]),
    show("create_copies", [
        mnesia:create_table(tn, [{disc_copies, [A]}, {attributes, [k, v]}]),
        mnesia:create_table(tp, [{disc_copies, [A]}, {attributes, [k, v]}]),
        mnesia:create_table(tq, [{disc_only_copies, [A]}, {attributes, [k, v]}]),
        mnesia:create_table(tu, [{disc_only_copies, [A]}, {attributes, [k, v]}])
    ]),
    show("write_copies", mnesia:transaction(fun() -> [mnesia:write({T, 1, a}) || T <- ?COPIES] end)),
    show("add_copies", [
        mnesia:add_table_copy(tn, B, disc_copies),
        mnesia:add_table_copy(tq, B, disc_copies),
        mnesia:add_table_copy(to, B, disc_only_copies)
    ]),
    show("moves", [
        mnesia:move_table_copy(tp, A, B),
        rpc:call(B, mnesia, move_table_copy, [tu, A, B]),
        mnesia:move_table_copy(td, A, B)
    ]),
    show("copy_types", [
        mnesia:change_table_copy_type(td, B, disc_only_copies),
        mnesia:change_table_copy_type(tr, B, disc_copies),
        mnesia:change_table_copy_type(tn, B, ram_copies),
        rpc:call(B, mnesia, change_table_copy_type, [tq, A, ram_copies])
    ]),
    show("tables_after_copies", both(B, info, [{tables, ?COPIES}])),
    show("files_after_copies", both(B, files, [])),
    show("copies_deleted", [mnesia:del_table_copy(td, B), mnesia:del_table_copy(tn, B)]),
    show("b_files_after_delete", file_names(rpc:call(B, ?MODULE, files, []))),
    show("b_running", [mnesia:del_table_copy(schema, B), mnesia:delete_schema([B])]),
    show("b_stops", rpc:call(B, ?MODULE, stop, [])),
    show("b_leaves", mnesia:del_table_copy(schema, B)),
    show("nodes_without_b", info(nodes)),
    show("tables_without_b", info({tables, ?COPIES})),
    show("b_schema_deleted", mnesia:delete_schema([B])),
    show("b_files_deleted", rpc:call(B, ?MODULE, files, [])),
    show("b_starts_fresh", rpc:call(B, ?MODULE, start, [])),
    show("b_nodes_fresh", rpc:call(B, ?MODULE, info, [nodes])),
    show("b_connects", rpc:call(B, mnesia, change_config, [extra_db_nodes, [A]])),
    show("b_nodes_connected", rpc:call(B, ?MODULE, info, [nodes])),
    show("b_on_disc", [mnesia:change_table_copy_type(schema, B, disc_copies), mnesia:add_table_copy(tn, B, disc_copies)]),
    show("nodes_with_b", both(B, info, [nodes])),
    show("tables_with_b", both(B, info, [{tables, ?COPIES}])),
    show("both_stop_again", [stop(), rpc:call(B, ?MODULE, stop, [])]),
    show("files_at_end", [{N, file_names(Files)} || {N, Files} <- both(B, files, [])]).

%% mnesia started; then whether this node's tables loaded within a moment, or which did not.
start() ->
    Started = mnesia:start(),
    Local = mnesia:system_info(local_tables),
    case mnesia:wait_for_tables(Local, 2000) of
        {timeout, Tabs} -> {Started, {timeout, lists:sort(Tabs)}};
        Waited -> {Started, Waited}
    end.

stop() -> mnesia:stop().

%% Both nodes start mnesia at once.
together(B) ->
    Self = self(),
    spawn(fun() -> Self ! {b, rpc:call(B, ?MODULE, start, [])} end),
    A = start(),
    receive
        {b, BStarted} -> {A, BStarted}
    end.

both(B, F, Args) ->
    [{a, apply(?MODULE, F, Args)}, {b, rpc:call(B, ?MODULE, F, Args)}].

%% The nodes mnesia knows, or where each table is read and how its replica here was loaded.
info(nodes) ->
    Schema = [{I, sorted(mnesia:table_info(schema, I))} || I <- [disc_copies, ram_copies]],
    norm([{I, sorted(mnesia:system_info(I))} || I <- [db_nodes, running_db_nodes, use_dir, local_tables]] ++ Schema);
info({tables, Tabs}) ->
    Items = [where_to_read, active_replicas, load_node, load_reason, storage_type, size],
    norm([{T, [{I, sorted(caught(fun() -> mnesia:table_info(T, I) end))} || I <- Items]} || T <- Tabs]).

%% Each table's master nodes, and the tables that have some.
masters() ->
    Tables = [{T, mnesia:table_info(T, master_nodes)} || T <- [schema | ?TABLES]],
    [{master_node_tables, lists:sort(mnesia:system_info(master_node_tables))} | Tables].

sorted(List) when is_list(List) -> lists:sort(List);
sorted(Term) -> Term.

%% What each file of this node's directory holds, without transaction ids and times: the logs' terms in order (a
%% commit as its decision kind, the tables of its records and its operations), the tables' records.
files() ->
    Dir = mnesia:system_info(directory),
    case file:list_dir(Dir) of
        {ok, Names} -> norm([{N, content(filename:join(Dir, N))} || N <- lists:sort(Names)]);
        {error, enoent} -> []
    end.

file_names(Files) -> [N || {N, _} <- Files].

content(File) ->
    case filename:extension(File) of
        ".DAT" -> dat(File);
        _ -> log(filename:basename(File), read_log(File))
    end.

% LATEST.LOG shows once mnesia stopped, as its writes are buffered meanwhile; its decisions are counted, as their
% place depends on when the other node's im_certain message comes, and its master nodes are sorted, as they are
% logged in the order of a table.
log("LATEST.LOG", Terms) ->
    case mnesia:system_info(is_running) of
        yes ->
            running;
        _ ->
            Decisions = [T || T <- Terms, element(1, T) =:= decision],
            Masters = [T || T <- Terms, element(1, T) =:= master_nodes],
            [term(T) || T <- Terms -- (Decisions ++ Masters)] ++
                [{masters, lists:sort(Masters)}, {decisions, length(Decisions)}]
    end;
log(_, [Header | Terms]) ->
    % Records in key order: OTP writes a table's DCD file in its ets order.
    [term(Header) | lists:sort([term(T) || T <- Terms])];
log(_, []) ->
    [].

% A dets file's keys, read through mnesia's dets table of the same name while it is open.
dat(File) ->
    Name = list_to_atom(filename:basename(File, ".DAT")),
    Records =
        case dets:info(Name, filename) of
            undefined ->
                {ok, D} = dets:open_file(make_ref(), [{file, File}, {access, read}, {keypos, 2}]),
                Read = dets:match_object(D, '_'),
                ok = dets:close(D),
                Read;
            _ ->
                dets:match_object(Name, '_')
        end,
    [element(2, R) || R <- lists:sort(Records)].

term({log_header, Kind, Version, _, _, _}) ->
    {log_header, Kind, Version};
term({commit, _, Decision, Ram, Disc, DiscOnly, _, Ops}) ->
    {commit, decided(Decision), tabs(Ram), tabs(Disc), tabs(DiscOnly), [{op, element(2, O)} || O <- Ops]};
term({decision, _, Outcome, _, _}) ->
    {decision, Outcome};
term({Updown, Node, _, _}) when Updown =:= mnesia_up; Updown =:= mnesia_down ->
    {Updown, Node};
term({decision_list, List}) ->
    {decision_list, lists:sort([term(D) || D <- List])};
term({trans_tid, serial, _}) ->
    trans_tid;
term({master_nodes, _, _} = Masters) ->
    Masters;
term({schema, Tab, _}) ->
    {schema, Tab};
term(Record) when is_tuple(Record) ->
    element(2, Record);
term(Other) ->
    Other.

decided({decision, _, Outcome, Disc, Ram}) -> {Outcome, lists:sort(Disc), lists:sort(Ram)};
decided(Decision) -> Decision.

tabs(Items) -> lists:usort([T || {{T, _}, _, _} <- Items]).

read_log(File) ->
    case disk_log:open([{name, make_ref()}, {file, File}, {mode, read_only}, {repair, false}]) of
        {ok, Log} -> chunks(Log, start, []);
        {repaired, Log, _, _} -> chunks(Log, start, [])
    end.

chunks(Log, Cont, Acc) ->
    case disk_log:chunk(Log, Cont) of
        eof ->
            ok = disk_log:close(Log),
            lists:append(lists:reverse(Acc));
        {Cont2, Terms} ->
            chunks(Log, Cont2, [Terms | Acc])
    end.

% Pids as their nodes, transaction ids without their numbers.
norm(Pid) when is_pid(Pid) -> node(Pid);
norm({tid, _, _}) -> tid;
norm(List) when is_list(List) -> [norm(E) || E <- List];
norm(Tuple) when is_tuple(Tuple) -> list_to_tuple(norm(tuple_to_list(Tuple)));
norm(Term) -> Term.

caught(Fun) ->
    try
        Fun()
    catch
        Class:Reason -> {Class, Reason}
    end.

%% b's end, which a sees as {badrpc, nodedown}.
finish() ->
    io:format(user, "mnesia_disc_masters: ok~n", []),
    erlang:halt().

show(Label, Value) -> io:format("~s: ~p~n", [Label, Value]).

wait_registered(Name) ->
    {ok, Names} = net_adm:names(localhost),
    case lists:keymember(Name, 1, Names) of
        true ->
            ok;
        false ->
            timer:sleep(10),
            wait_registered(Name)
    end.

% Register the epmd name Name while the returned socket stays open: this node is ready.
mark(Name) ->
    Port = list_to_integer(os:getenv("ERL_EPMD_PORT", "4369")),
    {ok, Socket} = gen_tcp:connect({127, 0, 0, 1}, Port, [binary, {active, false}]),
    Bytes = list_to_binary(Name),
    Request = <<120, 1:16, 77, 0, 6:16, 5:16, (byte_size(Bytes)):16, Bytes/binary, 0:16>>,
    ok = gen_tcp:send(Socket, [<<(byte_size(Request)):16>>, Request]),
    {ok, <<_, 0, _/binary>>} = gen_tcp:recv(Socket, 0, 5000),
    Socket.
