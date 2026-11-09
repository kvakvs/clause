%% Partitions of two disc nodes and adopted orphans (plan 21 step MIM1H10c,
%% docs/libraries/mnesia-protocol.md#partitions-and-orphans-mim1h10c): the connection between both running nodes breaks,
%% each side writes alone, and when they meet again each reports the partition (running_partitioned_network) and
%% stays apart; b started again reports it too (starting_partitioned_network, on stderr) and takes a's records; master
%% nodes keep both reports away. Then each node's replicas wait for the other at start, and the smallest disc node
%% adopts them from its disc, itself or through their master node. Each step shows where the tables are read and loaded
%% from and what they hold, and at the end each node's files. Only exported functions travel, so either node may be an
%% OTP node.
-module(mnesia_partitions).
-export([main/1]).
-export([start/0, stop/0, write/2, arm/2, armed/0, info/0, heard/0, forget/0, wait_event/1, masters/0, files/0]).
-export([finish/0]).

-define(TABLES, [td, tr, to]).
-define(ORPHANS, [td, to, tr, tx]).

main(["a"]) ->
    B = 'mptb@localhost',
    wait_registered("mptb_ready"),
    pong = net_adm:ping(B),
    partitions(node(), B),
    orphans(node(), B),
    show("both_stop", [stop(), rpc:call(B, ?MODULE, stop, [])]),
    show("files", both(B, files, [])),
    rpc:call(B, ?MODULE, finish, []),
    io:format("mnesia_partitions: ok~n");
main(["b"]) ->
    _Ready = mark("mptb_ready"),
    receive
    after infinity -> ok
    end.

partitions(A, B) ->
    % The runs of this program share their directory: an earlier run's schemas go first.
    ok = mnesia:delete_schema([A, B]),
    show("create_schema", mnesia:create_schema([A, B])),
    show("start", [start(), rpc:call(B, ?MODULE, start, [])]),
    show("create_tables", [
        mnesia:create_table(td, [{disc_copies, [A, B]}, {attributes, [k, v]}]),
        mnesia:create_table(tr, [{ram_copies, [A, B]}, {attributes, [k, v]}]),
        mnesia:create_table(to, [{disc_only_copies, [A, B]}, {attributes, [k, v]}])
    ]),
    show("writes", write(1, a)),
    show("up", wait_event({mnesia_up, B})),
    partition(B, b1),
    show("tables_apart", both(B, info, [])),
    show("writes_apart", [write(3, a), rpc:call(B, ?MODULE, write, [3, b])]),
    show("tables_written_apart", both(B, info, [])),
    b_restarts(B, "b_restarts"),
    partition(B, b2),
    show("b_masters", rpc:call(B, mnesia, set_master_nodes, [[A]])),
    show("masters", both(B, masters, [])),
    b_restarts(B, "b_restarts_with_masters"),
    show("b_masters_cleared", rpc:call(B, mnesia, set_master_nodes, [[]])),
    a_masters_partition(A, B),
    show("b_masters_again", rpc:call(B, mnesia, set_master_nodes, [[A]])),
    b_restarts(B, "b_restarts_again"),
    show("b_masters_cleared_again", rpc:call(B, mnesia, set_master_nodes, [[]])).

%% The connection breaks while both run: each side writes alone, then they meet again and each reports the partition.
partition(B, Tag) ->
    show("armed", rpc:call(B, ?MODULE, arm, [node(), Tag])),
    true = erlang:disconnect_node(B),
    show("down", wait_event({mnesia_down, B})),
    show("write_alone", write(2, a)),
    timer:sleep(700),
    pong = net_adm:ping(B),
    show("a_reports", wait_event({inconsistent_database, running_partitioned_network, B})),
    show("b_reports", rpc:call(B, ?MODULE, wait_event, [{inconsistent_database, running_partitioned_network, node()}])),
    show("b_wrote_alone", rpc:call(B, ?MODULE, armed, [])),
    show("nodes_apart", both(B, info, [])).

%% b starts again: it reports the partition at start unless it has master nodes, and loads a's records.
b_restarts(B, Label) ->
    show(Label, [rpc:call(B, ?MODULE, stop, []), rpc:call(B, ?MODULE, start, [])]),
    show(Label ++ "_up", wait_event({mnesia_up, B})),
    timer:sleep(300),
    ok = rpc:call(B, ?MODULE, forget, []),
    show(Label ++ "_tables", both(B, info, [])).

%% A partition while a has master nodes: b, asking a, reports nothing, a still does.
a_masters_partition(A, B) ->
    show("armed", rpc:call(B, ?MODULE, arm, [A, b3])),
    true = erlang:disconnect_node(B),
    show("down", wait_event({mnesia_down, B})),
    show("a_masters", mnesia:set_master_nodes([A])),
    timer:sleep(700),
    pong = net_adm:ping(B),
    show("a_reports", wait_event({inconsistent_database, running_partitioned_network, B})),
    timer:sleep(500),
    show("b_hears", rpc:call(B, ?MODULE, heard, [])),
    show("b_wrote_alone", rpc:call(B, ?MODULE, armed, [])),
    show("a_masters_cleared", mnesia:set_master_nodes([])).

%% Replicas waiting for each other at start: the smallest disc node, a, adopts them from its disc, or asks their
%% master node to.
orphans(A, B) ->
    show("create_tx", mnesia:create_table(tx, [{disc_copies, [A, B]}, {attributes, [k, v]}])),
    show("orphan_writes", write(5, a)),
    % a stops first, so b loads tx from its disc; td and to wait for their master node a on b.
    show("b_orphan_masters", [rpc:call(B, mnesia, set_master_nodes, [T, [A]]) || T <- [td, to]]),
    show("a_stops", stop()),
    show("b_writes", rpc:call(B, ?MODULE, write, [6, b])),
    show("b_stops", rpc:call(B, ?MODULE, stop, [])),
    show("a_starts_alone", start()),
    show("a_waits", info()),
    show("b_starts", rpc:call(B, ?MODULE, start, [])),
    adopted(B, "adopted_by_a"),
    % b waits for its master node a for every table; a, starting last, adopts them.
    show("a_stops_again", stop()),
    show("b_tx_master", rpc:call(B, mnesia, set_master_nodes, [tx, [A]])),
    show("b_starts_again", [rpc:call(B, ?MODULE, stop, []), rpc:call(B, ?MODULE, start, [])]),
    show("a_starts", start()),
    adopted(B, "adopted_at_start"),
    % a waits for its master node b for td, b for a: a asks b to load td from b's disc.
    show("b_masters_cleared", rpc:call(B, mnesia, set_master_nodes, [[]])),
    show("a_td_master", mnesia:set_master_nodes(td, [B])),
    show("b_stops_first", rpc:call(B, ?MODULE, stop, [])),
    show("a_stops_last", stop()),
    show("b_starts_alone", rpc:call(B, ?MODULE, start, [])),
    show("a_starts_last", start()),
    adopted(B, "adopted_by_master"),
    show("a_masters_cleared", mnesia:set_master_nodes([])).

adopted(B, Label) ->
    timer:sleep(1000),
    show(Label, both(B, info, [])).

%% Key written with Value in each table held here.
write(Key, Value) ->
    mnesia:transaction(fun() ->
        [mnesia:write({T, Key, Value}) || T <- mnesia:system_info(local_tables), T =/= schema]
    end).

%% mnesia started, with a process keeping its system events; then whether this node's tables loaded within a moment.
start() ->
    Started = mnesia:start(),
    listen(),
    Local = mnesia:system_info(local_tables),
    case mnesia:wait_for_tables(Local, 2000) of
        {timeout, Tabs} -> {Started, {timeout, lists:sort(Tabs)}};
        Waited -> {Started, Waited}
    end.

stop() -> mnesia:stop().

%% On b, a process that writes once the connection to A breaks, while the nodes are apart.
arm(A, Tag) ->
    Self = self(),
    spawn(fun() ->
        {ok, _} = mnesia:subscribe(system),
        Self ! armed,
        receive
            {mnesia_system_event, {mnesia_down, A}} -> ok
        end,
        persistent_term:put(mpt_armed, {Tag, write(2, Tag), mnesia:system_info(running_db_nodes)})
    end),
    receive
        armed -> ok
    end.

armed() -> persistent_term:get(mpt_armed, none).

both(B, F, Args) ->
    [{a, apply(?MODULE, F, Args)}, {b, rpc:call(B, ?MODULE, F, Args)}].

%% The running nodes, where each table is read and how its replica here was loaded, and its records.
info() ->
    Items = [where_to_read, active_replicas, load_node, load_reason, storage_type, size],
    Tables = [T || T <- ?ORPHANS, lists:member(T, mnesia:system_info(tables))],
    [{running, lists:sort(mnesia:system_info(running_db_nodes))}, {downs, downs()}] ++
        [
            {T, [{I, sorted(caught(fun() -> mnesia:table_info(T, I) end))} || I <- Items] ++ [{rows, rows(T)}]}
         || T <- Tables
        ].

downs() ->
    lists:sort([N || {mnesia_down, {node, N}, _, _} <- ets:tab2list(mnesia_decision)]).

rows(Tab) ->
    case mnesia:table_info(Tab, where_to_read) of
        nowhere -> nowhere;
        _ -> lists:sort(mnesia:dirty_match_object({Tab, '_', '_'}))
    end.

%% Each table's master nodes.
masters() ->
    [{T, mnesia:table_info(T, master_nodes)} || T <- [schema | ?TABLES]].

sorted(List) when is_list(List) -> lists:sort(List);
sorted(Term) -> Term.

%% The system events since the last look, kept by a process of this node.
listen() ->
    Self = self(),
    Pid = spawn(fun() -> listening(Self) end),
    receive
        {Pid, subscribed} -> ok
    end.

listening(Parent) ->
    is_pid(whereis(mpt_events)) andalso unregister(mpt_events),
    register(mpt_events, self()),
    {ok, _} = mnesia:subscribe(system),
    Parent ! {self(), subscribed},
    events([]).

events(Events) ->
    receive
        {mnesia_system_event, Event} ->
            events([Event | Events]);
        {From, heard} ->
            From ! {heard, lists:reverse(Events)},
            events([])
    end.

heard() ->
    mpt_events ! {self(), heard},
    receive
        {heard, Events} -> Events
    after 1000 -> no_listener
    end.

%% The events since the last look are dropped: a node that just started hears its mnesia_up events at times that
%% differ between runs.
forget() ->
    _ = heard(),
    ok.

%% Waits until Event comes: ok, or the events heard meanwhile.
wait_event(Event) ->
    wait_event(Event, 50, []).

wait_event(Event, 0, Heard) ->
    {missing, Event, Heard};
wait_event(Event, N, Heard) ->
    Events = Heard ++ heard(),
    case lists:member(Event, Events) of
        true ->
            ok;
        false ->
            timer:sleep(100),
            wait_event(Event, N - 1, Events)
    end.

%% What each file of this node's directory holds, without transaction ids and times.
files() ->
    Dir = mnesia:system_info(directory),
    case file:list_dir(Dir) of
        {ok, Names} -> norm([{N, content(filename:join(Dir, N))} || N <- lists:sort(Names)]);
        {error, enoent} -> []
    end.

content(File) ->
    case filename:extension(File) of
        ".DAT" -> dat(File);
        _ -> log(filename:basename(File), read_log(File))
    end.

% LATEST.LOG's decisions are counted, as their place depends on when the other node's im_certain message comes, and
% its master nodes are sorted, as they are logged in the order of a table.
log("LATEST.LOG", Terms) ->
    Decisions = [T || T <- Terms, element(1, T) =:= decision],
    Masters = [T || T <- Terms, element(1, T) =:= master_nodes],
    [term(T) || T <- Terms -- (Decisions ++ Masters)] ++
        [{masters, lists:sort(Masters)}, {decisions, length(Decisions)}];
log(_, [Header | Terms]) ->
    % Records in key order: OTP writes a table's DCD file in its ets order.
    [term(Header) | lists:sort([term(T) || T <- Terms])];
log(_, []) ->
    [].

dat(File) ->
    {ok, D} = dets:open_file(make_ref(), [{file, File}, {access, read}, {keypos, 2}]),
    Records = dets:match_object(D, '_'),
    ok = dets:close(D),
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
    io:format(user, "mnesia_partitions: ok~n", []),
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
