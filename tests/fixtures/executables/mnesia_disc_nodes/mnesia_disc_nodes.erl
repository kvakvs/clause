%% Two disc nodes (plan 21 step MIM1H10a, docs/libraries/mnesia-protocol.md#disc-nodes-mim1h10a): a creates the schema
%% on both nodes, which then start together; tables of each placement are written in transactions, which each node
%% logs; schema changes wait for a disc node that is down; b starts again and loads from a; then both stop, b last,
%% and a starts alone: it loads the tables only it holds on disc and waits for those b saw last, which it loads from b
%% once b is back. Each step shows where both nodes read the tables, where each replica came from, and what each node's
%% files hold, without the transaction ids and times that differ between runs. Only exported functions travel, so
%% either node may be an OTP node.
-module(mnesia_disc_nodes).
-export([main/1]).
-export([start/0, stop/0, info/1, files/0, finish/0]).

-define(TABLES, [td, to, tr, tx, ty, tra, trb]).

main(["a"]) ->
    B = 'mdnb@localhost',
    A = node(),
    wait_registered("mdnb_ready"),
    pong = net_adm:ping(B),
    % The runs of this program share their directory: an earlier run's schemas go first.
    ok = mnesia:delete_schema([A, B]),
    show("create_schema", mnesia:create_schema([A, B])),
    show("create_schema_again", mnesia:create_schema([A, B])),
    show("files_after_create", both(B, files, [])),
    show("start", together(B)),
    show("nodes", both(B, info, [nodes])),
    Defs = [
        {td, [{disc_copies, [A, B]}]},
        {to, [{disc_only_copies, [A, B]}]},
        {tr, [{ram_copies, [A, B]}]},
        {tx, [{disc_copies, [A]}, {ram_copies, [B]}]},
        {ty, [{disc_copies, [B]}, {ram_copies, [A]}]},
        {tra, [{ram_copies, [A]}]},
        {trb, [{ram_copies, [B]}]}
    ],
    show("create_tables", [mnesia:create_table(T, [{attributes, [k, v]} | Opts]) || {T, Opts} <- Defs]),
    show("write_disc", mnesia:transaction(fun() -> mnesia:write({td, 1, a}) end)),
    show(
        "write_mixed",
        mnesia:transaction(fun() ->
            [mnesia:write({T, 1, a}) || T <- ?TABLES -- [td]],
            ok
        end)
    ),
    show("write_sync", mnesia:sync_transaction(fun() -> mnesia:write({to, 2, a}) end)),
    show("write_dirty", mnesia:dirty_write({td, 2, a})),
    timer:sleep(300),
    show("tables", both(B, info, [tables])),
    show("files_after_writes", both(B, files, [])),
    show("b_stops", rpc:call(B, ?MODULE, stop, [])),
    show("changes_with_b_down", [
        mnesia:create_table(tm, [{disc_copies, [A, B]}]),
        mnesia:create_table(tn, [{disc_copies, [A]}]),
        mnesia:create_table(tbr, [{ram_copies, [B]}]),
        mnesia:delete_table(tr),
        mnesia:add_table_index(td, v),
        mnesia:transaction(fun() -> mnesia:write({td, 3, a}) end)
    ]),
    show("b_starts", rpc:call(B, ?MODULE, start, [])),
    show("tables_after_b_starts", both(B, info, [tables])),
    show("a_stops", stop()),
    show("b_stops_last", rpc:call(B, ?MODULE, stop, [])),
    show("files_after_stop", both(B, files, [])),
    show("a_starts_alone", start()),
    show("tables_with_a_alone", info(tables)),
    show("b_starts_again", rpc:call(B, ?MODULE, start, [])),
    show("a_waits", mnesia:wait_for_tables(?TABLES, 5000)),
    show("tables_after_both", both(B, info, [tables])),
    show("dumped", [mnesia:dump_log(), rpc:call(B, mnesia, dump_log, [])]),
    show("files_after_dump", both(B, files, [])),
    rpc:call(B, ?MODULE, finish, []),
    io:format("mnesia_disc_nodes: ok~n");
main(["b"]) ->
    _Ready = mark("mdnb_ready"),
    receive
    after infinity -> ok
    end.

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
    norm([{I, sorted(mnesia:system_info(I))} || I <- [db_nodes, running_db_nodes, use_dir, local_tables]]);
info(tables) ->
    Items = [where_to_read, active_replicas, where_to_write, load_node, load_reason, storage_type, size],
    norm([{T, [{I, sorted(caught(fun() -> mnesia:table_info(T, I) end))} || I <- Items]} || T <- ?TABLES]).

sorted(List) when is_list(List) -> lists:sort(List);
sorted(Term) -> Term.

%% What each file of this node's directory holds, without transaction ids and times: the logs' terms in order (a
%% commit as its decision kind, the tables of its records and its operations), the tables' records.
files() ->
    Dir = mnesia:system_info(directory),
    {ok, Names} = file:list_dir(Dir),
    norm([{N, content(filename:join(Dir, N))} || N <- lists:sort(Names)]).

content(File) ->
    case filename:extension(File) of
        ".DAT" -> dat(File);
        _ -> log(filename:basename(File), read_log(File))
    end.

% LATEST.LOG shows once mnesia stopped, as its writes are buffered meanwhile; its decisions are counted, as their
% place depends on when the other node's im_certain message comes.
log("LATEST.LOG", Terms) ->
    case mnesia:system_info(is_running) of
        yes ->
            running;
        _ ->
            Decisions = [T || T <- Terms, element(1, T) =:= decision],
            [term(T) || T <- Terms -- Decisions] ++ [{decisions, length(Decisions)}]
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

norm(Pid) when is_pid(Pid) -> node(Pid);
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
    io:format(user, "mnesia_disc_nodes: ok~n", []),
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
