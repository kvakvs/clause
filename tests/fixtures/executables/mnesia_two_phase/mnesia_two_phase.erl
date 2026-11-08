%% Transactions committed over two nodes (plan 21 step MIM1H8, docs/libraries/mnesia-protocol.md#two-phase-commit-mim1h8):
%% after the two nodes' RAM schemas merge, schema changes reach both nodes' schemas and replicated tables are created
%% on both; each node's transactions write tables replicated on both nodes (sym_trans), tables of different nodes
%% (asym_trans), only the other node's tables, in sync transactions, nested and aborted, and each node sees the other's
%% commits with their table events. The other node refuses a table its ets name clashes with. Each node answers
%% what_happened from the outcomes it keeps. Run "a" ends with a participant going down before it votes: the
%% coordinator restarts and commits alone. Run "a_coordinator" ends with the coordinator going down while this node has
%% not voted: this node aborts its transactions. Transaction ids and processes are shown by node, as their numbers
%% differ between runs. Only exported functions travel, so either node may be an OTP node.
-module(mnesia_two_phase).
-export([main/1]).
-export([start/1, writes/3, contents/1, info/1, sub/1, events/0, counts/0, ask/2, make_ets/1, upgrade/1]).
-export([suspend/1, resume/0, coordinate/2, looked/0, finish/0, write_one/1]).

main(["a"]) ->
    B = connect(),
    A = node(),
    show("create_tr", mnesia:create_table(tr, [{ram_copies, [A, B]}, {attributes, [k, v]}])),
    show("tr_info_a", info(tr)),
    show("tr_info_b", rpc:call(B, ?MODULE, info, [tr])),
    show("b_creates_tq", rpc:call(B, mnesia, create_table, [tq, [{ram_copies, [A, B]}, {attributes, [k, v]}]])),
    show("tq_info_a", info(tq)),
    show("create_tm", mnesia:create_table(tm, [{ram_copies, [A, B]}, {majority, true}, {attributes, [k, v]}])),
    ok = rpc:call(B, ?MODULE, sub, [tr]),
    Before = counts(),
    BBefore = rpc:call(B, ?MODULE, counts, []),
    show("a_writes", writes(ta, tb, 1)),
    show("contents_after_a", both(B, [tr, tm, ta, tb])),
    show("b_events", rpc:call(B, ?MODULE, events, [])),
    show("b_writes", rpc:call(B, ?MODULE, writes, [tb, ta, 2])),
    show("contents_after_b", both(B, [tr, tm, ta, tb])),
    show("counts_a", delta(Before, counts())),
    show("counts_b", delta(BBefore, rpc:call(B, ?MODULE, counts, []))),
    schema_changes(A, B),
    show("clash", clash(B)),
    show("what_happened", what_happened(B)),
    show("participant_down", participant_down(B)),
    io:format("mnesia_two_phase: ok~n");
main(["a_coordinator"]) ->
    B = connect(),
    A = node(),
    {atomic, ok} = mnesia:create_table(tr, [{ram_copies, [A, B]}, {attributes, [k, v]}]),
    show("coordinator_down", coordinator_down(B)),
    io:format("mnesia_two_phase: ok~n");
main([_]) ->
    _Ready = mark("mtpb_ready"),
    receive
    after infinity -> ok
    end.

%% Both nodes run mnesia, a with table ta and b with tb, and their schemas merged; b's name.
connect() ->
    B = 'mtpb@localhost',
    wait_registered("mtpb_ready"),
    pong = net_adm:ping(B),
    ok = start(ta),
    ok = rpc:call(B, ?MODULE, start, [tb]),
    show("merge", mnesia:change_config(extra_db_nodes, [B])),
    timer:sleep(300),
    B.

%% mnesia started with a table of this node only, holding record 0.
start(Tab) ->
    ok = mnesia:start(),
    {atomic, ok} = mnesia:create_table(Tab, [{attributes, [k, v]}]),
    ok = mnesia:dirty_write({Tab, 0, node()}),
    ok.

%% Transactions of this node: on the replicated tables, its own table with a replicated one, only the other node's
%% table, a sync transaction, a delete and a delete_object, a nested and an aborted transaction, clear_table and dirty
%% writes. Keys start
%% at N * 10, so that each node writes its own records.
writes(Mine, Theirs, N) ->
    K = N * 10,
    Run = fun(F) -> norm(mnesia:transaction(F)) end,
    [
        {replicated, Run(fun() -> mnesia:write({tr, K, node()}) end)},
        {two_tables,
            Run(fun() ->
                mnesia:write({tr, K + 1, x}),
                mnesia:write({tm, K + 1, x})
            end)},
        {read_only, Run(fun() -> mnesia:read(tr, K) end)},
        {mine_and_replicated,
            Run(fun() ->
                mnesia:write({Mine, K + 2, x}),
                mnesia:write({tr, K + 2, x})
            end)},
        {theirs_only,
            Run(fun() ->
                mnesia:write({Theirs, K + 3, x}),
                mnesia:delete({Theirs, 0})
            end)},
        {majority, Run(fun() -> mnesia:write({tm, K + 4, x}) end)},
        {sync, norm(mnesia:sync_transaction(fun() -> mnesia:write({tr, K + 5, x}) end))},
        {sync_two_nodes,
            norm(
                mnesia:sync_transaction(fun() ->
                    mnesia:write({tr, K + 6, x}),
                    mnesia:write({Mine, K + 6, x})
                end)
            )},
        {delete, Run(fun() -> mnesia:delete({tr, K + 5}) end)},
        {delete_object, Run(fun() -> mnesia:delete_object({tr, K + 6, x}) end)},
        {nested, Run(fun() -> mnesia:transaction(fun() -> mnesia:write({tr, K + 7, x}) end) end)},
        {aborted,
            Run(fun() ->
                mnesia:write({tr, K + 8, x}),
                mnesia:abort(stop)
            end)},
        {clear, mnesia:clear_table(tm)},
        {sync_dirty, mnesia:sync_dirty(fun() -> mnesia:write({tr, K + 9, x}) end)},
        {dirty, mnesia:dirty_write({tr, K + 10, x})},
        {replicas, wait_for(fun() -> [rpc:call(N2, mnesia, dirty_read, [tr, K + 10]) || N2 <- nodes_of(tr)] end)}
    ].

nodes_of(Tab) -> mnesia:table_info(Tab, active_replicas).

%% Fun's result once every replica answers the same non-empty records.
wait_for(Fun) ->
    wait_for(Fun, 100).

wait_for(Fun, 0) ->
    {differ, Fun()};
wait_for(Fun, N) ->
    case lists:usort(Fun()) of
        [[_ | _] = Records] ->
            Records;
        _ ->
            timer:sleep(20),
            wait_for(Fun, N - 1)
    end.

%% The records of each table on both nodes, sorted; same when both nodes hold the same.
both(B, Tabs) ->
    [{Tab, compared(contents(Tab), rpc:call(B, ?MODULE, contents, [Tab]))} || Tab <- Tabs].

compared(Same, Same) -> {same, Same};
compared(A, B) -> {a, A, b, B}.

contents(Tab) ->
    case ets:info(Tab) of
        undefined -> no_replica;
        _ -> lists:sort(ets:tab2list(Tab))
    end.

%% Where a table is read and written from this node.
info(Tab) ->
    [
        {I, caught(fun() -> mnesia:table_info(Tab, I) end)}
     || I <- [where_to_commit, where_to_write, active_replicas, where_to_read]
    ].

%% Schema changes made on one node reach the other's schema.
schema_changes(A, B) ->
    show("create_a_only", mnesia:create_table(tx, [{attributes, [k, v]}])),
    show("tx_on_b", rpc:call(B, ?MODULE, info, [tx])),
    show("tx_b_writes", rpc:call(B, ?MODULE, write_one, [{tx, 1, b}])),
    show("tx_a", contents(tx)),
    show("add_index", mnesia:add_table_index(tr, v)),
    show("b_index_read", rpc:call(B, mnesia, dirty_index_read, [tr, A, v])),
    show("b_index_info", rpc:call(B, mnesia, table_info, [tr, index])),
    show("transform", mnesia:transform_table(tr, fun ?MODULE:upgrade/1, [k, v, w])),
    show("b_transformed", rpc:call(B, ?MODULE, contents, [tr])),
    show("b_attributes", rpc:call(B, mnesia, table_info, [tr, attributes])),
    show("del_index", mnesia:del_table_index(tr, v)),
    show("b_index_after", rpc:call(B, mnesia, table_info, [tr, index])),
    show("b_property", rpc:call(B, mnesia, write_table_property, [tq, {owner, b}])),
    show("a_property", mnesia:read_table_property(tq, owner)),
    show("b_load_order", rpc:call(B, mnesia, change_table_load_order, [tq, 5])),
    show("a_load_order", mnesia:table_info(tq, load_order)),
    show("access_mode", mnesia:change_table_access_mode(tq, read_only)),
    show("b_tq_info", rpc:call(B, ?MODULE, info, [tq])),
    show("b_tq_write", rpc:call(B, ?MODULE, write_one, [{tq, 1, b}])),
    show("b_access_mode", rpc:call(B, mnesia, change_table_access_mode, [tq, read_write])),
    show("a_tq_info", info(tq)),
    show("delete_tx", mnesia:delete_table(tx)),
    show("b_tx_after", rpc:call(B, mnesia, table_info, [tx, where_to_commit])),
    show("b_deletes_tq", rpc:call(B, mnesia, delete_table, [tq])),
    show("tq_after", [
        caught(fun() -> mnesia:table_info(tq, type) end), contents(tq), rpc:call(B, ?MODULE, contents, [tq])
    ]),
    show("tables_b", lists:sort(rpc:call(B, mnesia, system_info, [tables]))).

upgrade({tr, K, V}) -> {tr, K, V, new}.

%% A transaction of this node writing Record.
write_one(Record) ->
    mnesia:transaction(fun() -> mnesia:write(Record) end).

%% An ets table named ty on the other node makes it refuse the replicated table ty.
clash(B) ->
    ok = rpc:call(B, ?MODULE, make_ets, [ty]),
    A = node(),
    [
        {create, mnesia:create_table(ty, [{ram_copies, [A, B]}, {attributes, [k, v]}])},
        {a, caught(fun() -> mnesia:table_info(ty, where_to_commit) end)},
        {b, rpc:call(B, mnesia, system_info, [tables])}
    ].

make_ets(Name) ->
    Self = self(),
    _ = spawn(fun() ->
        _ = ets:new(Name, [named_table, public]),
        Self ! made,
        receive
        after infinity -> ok
        end
    end),
    receive
        made -> ok
    end.

%% What each node answers about transactions committed on both nodes and one it never saw, for each default.
what_happened(B) ->
    {atomic, Sym} = mnesia:transaction(fun() ->
        mnesia:write({tr, 100, x, y}),
        tid()
    end),
    {atomic, Asym} = mnesia:transaction(fun() ->
        mnesia:write({tr, 101, x, y}),
        mnesia:write({ta, 101, x}),
        tid()
    end),
    Unknown = {tid, 999999, self()},
    Tids = [{sym, Sym}, {asym, Asym}, {unknown, Unknown}],
    [
        {Name, Node, [ask(Node, Default, Tid) || Default <- [unclear, aborted]]}
     || {Name, Tid} <- Tids, Node <- [node(), B]
    ].

ask(Node, Default, Tid) when Node =/= node() ->
    rpc:call(Node, ?MODULE, ask, [Default, Tid]);
ask(_, Default, Tid) ->
    ask(Default, Tid).

ask(Default, Tid) ->
    gen_server:call(mnesia_recover, {what_happened, Default, Tid}, 5000).

%% b's transaction manager stops answering, then b goes down while this node's transaction waits for b's vote: the
%% transaction restarts and commits on this node alone.
participant_down(B) ->
    ok = rpc:call(B, ?MODULE, suspend, [mnesia_tm]),
    Before = counts(),
    _ = spawn(fun() ->
        timer:sleep(500),
        rpc:cast(B, ?MODULE, finish, [])
    end),
    Result = mnesia:transaction(fun() -> mnesia:write({tr, 200, x, y}) end),
    timer:sleep(300),
    [
        {result, Result},
        {counts, delta(Before, counts())},
        {tr_200, mnesia:dirty_read(tr, 200)},
        {running, mnesia:system_info(running_db_nodes)},
        {where_to_commit, mnesia:table_info(tr, where_to_commit)},
        {locks, looked()}
    ].

%% This node's transaction manager stops answering while b coordinates two transactions writing tr, one on both nodes'
%% replicas and one also on b's table; b goes down before this node votes, and this node aborts both.
coordinator_down(B) ->
    ok = suspend(mnesia_tm),
    ok = rpc:call(B, ?MODULE, coordinate, [tr, node()]),
    Queued = wait_queued(2, 200),
    erlang:monitor_node(B, true),
    rpc:cast(B, ?MODULE, finish, []),
    receive
        {nodedown, B} -> ok
    end,
    resume(),
    timer:sleep(500),
    [
        {queued, Queued},
        {tr, contents(tr)},
        {locks, wait_unlocked()},
        {write_after, mnesia:transaction(fun() -> mnesia:write({tr, 3, a}) end)},
        {tr_after, contents(tr)}
    ].

%% Two transactions of this node, started without waiting for them: one writing Tab only, one also this node's table.
coordinate(Tab, Other) ->
    _ = spawn(fun() -> mnesia:transaction(fun() -> mnesia:write({Tab, 1, b}) end) end),
    _ = spawn(fun() ->
        mnesia:transaction(fun() ->
            mnesia:write({Tab, 2, b}),
            mnesia:write({tb, 2, b})
        end)
    end),
    _ = Other,
    ok.

%% Whether mnesia_tm got N messages while it was suspended.
wait_queued(_, 0) ->
    timeout;
wait_queued(N, Tries) ->
    case process_info(whereis(mnesia_tm), message_queue_len) of
        {message_queue_len, Len} when Len >= N ->
            ok;
        _ ->
            timer:sleep(10),
            wait_queued(N, Tries - 1)
    end.

%% A process suspends the registered process Name until resume/0.
suspend(Name) ->
    Self = self(),
    _ = spawn(fun() ->
        register(mtp_suspender, self()),
        Pid = whereis(Name),
        true = erlang:suspend_process(Pid),
        Self ! suspended,
        receive
            resume -> true = erlang:resume_process(Pid)
        end
    end),
    receive
        suspended -> ok
    end.

resume() ->
    mtp_suspender ! resume,
    ok.

%% b subscribes to Tab's simple events and to activities; events/0 returns those received so far.
sub(Tab) ->
    Self = self(),
    _ = spawn(fun() ->
        register(mtp_events, self()),
        {ok, _} = mnesia:subscribe({table, Tab, simple}),
        {ok, _} = mnesia:subscribe(activity),
        Self ! subscribed,
        collect([])
    end),
    receive
        subscribed -> ok
    end.

collect(Events) ->
    receive
        {From, get} ->
            From ! {events, lists:reverse(Events)},
            collect([]);
        Event ->
            collect([norm(Event) | Events])
    end.

events() ->
    mtp_events ! {self(), get},
    receive
        {events, Events} -> Events
    after 2000 -> timeout
    end.

counts() ->
    [{I, mnesia:system_info(I)} || I <- [transaction_commits, transaction_failures, transaction_restarts]].

delta(Before, After) ->
    [{I, A - B} || {{I, B}, {I, A}} <- lists:zip(Before, After)].

tid() ->
    {_, Tid, _} = get(mnesia_activity_state),
    Tid.

%% This node's held locks and lock queue, with each transaction and process shown by its node.
looked() ->
    norm({mnesia:system_info(held_locks), mnesia:system_info(lock_queue)}).

wait_unlocked() ->
    wait_unlocked(100).

wait_unlocked(0) ->
    {locked, looked()};
wait_unlocked(N) ->
    case looked() of
        {[], []} ->
            unlocked;
        _ ->
            timer:sleep(20),
            wait_unlocked(N - 1)
    end.

norm({tid, _, Pid}) when is_pid(Pid) -> {tid, node(Pid)};
norm(Pid) when is_pid(Pid) -> node(Pid);
norm(List) when is_list(List) -> [norm(E) || E <- List];
norm(Tuple) when is_tuple(Tuple) -> list_to_tuple(norm(tuple_to_list(Tuple)));
norm(Term) -> Term.

%% b's end, which a sees as {badrpc, nodedown}.
finish() ->
    io:format(user, "mnesia_two_phase: ok~n", []),
    erlang:halt().

caught(Fun) ->
    try
        Fun()
    catch
        Class:Reason -> {Class, Reason}
    end.

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
