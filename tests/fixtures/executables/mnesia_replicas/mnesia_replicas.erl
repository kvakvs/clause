%% Table replicas over two nodes (plan 21 step MIM1H9, docs/libraries/mnesia-protocol.md#replicas-and-loading-mim1h9):
%% after the two nodes' RAM schemas merge, copies are added on the other node and on this one, by each node, and loaded
%% from the node holding the table, a large table in several chunks; copies are deleted and moved both ways; then b
%% stops and starts again, loading its replica of a replicated table from a, while its own table comes back empty.
%% Each step shows where both nodes read and write the table, where its replica came from and whether both hold the
%% same records. Transaction ids are shown by node, as their numbers differ between runs. Only exported functions
%% travel, so either node may be an OTP node.
-module(mnesia_replicas).
-export([main/1]).
-export([start/1, info/1, digest/1, restart/1, write_one/1, finish/0]).

main(["a"]) ->
    B = 'mrpb@localhost',
    A = node(),
    wait_registered("mrpb_ready"),
    pong = net_adm:ping(B),
    ok = start([ta, tl, tc, te, tg]),
    ok = rpc:call(B, ?MODULE, start, [[tb, td, tf, th]]),
    show("merge", mnesia:change_config(extra_db_nodes, [B])),
    timer:sleep(300),
    [ok = mnesia:dirty_write({tl, K, lists:duplicate(100, $x)}) || K <- lists:seq(10, 3000)],
    step("a_copies_ta_to_b", B, ta, fun() -> mnesia:add_table_copy(ta, B, ram_copies) end),
    step("a_copies_tb_to_a", B, tb, fun() -> mnesia:add_table_copy(tb, A, ram_copies) end),
    step("a_copies_large_tl_to_b", B, tl, fun() -> mnesia:add_table_copy(tl, B, ram_copies) end),
    step("b_copies_tc_to_b", B, tc, fun() -> rpc:call(B, mnesia, add_table_copy, [tc, B, ram_copies]) end),
    step("b_copies_td_to_a", B, td, fun() -> rpc:call(B, mnesia, add_table_copy, [td, A, ram_copies]) end),
    step("copy_errors", B, ta, fun() ->
        [
            mnesia:add_table_copy(ta, B, ram_copies),
            mnesia:add_table_copy(nothing, B, ram_copies),
            mnesia:add_table_copy(ta, B, disc_copies)
        ]
    end),
    step("b_writes_ta", B, ta, fun() -> rpc:call(B, ?MODULE, write_one, [{ta, 10, b}]) end),
    step("a_deletes_b_copy_of_ta", B, ta, fun() -> mnesia:del_table_copy(ta, B) end),
    step("b_deletes_a_copy_of_tb", B, tb, fun() -> rpc:call(B, mnesia, del_table_copy, [tb, A]) end),
    step("a_moves_te_to_b", B, te, fun() -> mnesia:move_table_copy(te, A, B) end),
    step("a_moves_tf_to_a", B, tf, fun() -> mnesia:move_table_copy(tf, B, A) end),
    step("b_moves_tg_to_b", B, tg, fun() -> rpc:call(B, mnesia, move_table_copy, [tg, A, B]) end),
    step("b_moves_th_to_a", B, th, fun() -> rpc:call(B, mnesia, move_table_copy, [th, B, A]) end),
    step("move_errors", B, te, fun() ->
        [mnesia:move_table_copy(te, A, B), mnesia:move_table_copy(te, B, B), mnesia:move_table_copy(schema, A, B)]
    end),
    {atomic, ok} = mnesia:create_table(tr, [{ram_copies, [A, B]}, {attributes, [k, v]}]),
    [ok = mnesia:dirty_write({tr, K, x}) || K <- [1, 2, 3]],
    {atomic, ok} = rpc:call(B, mnesia, create_table, [tq, [{attributes, [k, v]}]]),
    ok = rpc:call(B, mnesia, dirty_write, [{tq, 1, x}]),
    show("b_restarts", rpc:call(B, ?MODULE, restart, [[A]])),
    show("a_sees_b", active_on(B, [tr, tq], 500)),
    show("tr_after_restart", both(B, tr)),
    show("tq_after_restart", both(B, tq)),
    step("b_writes_tr", B, tr, fun() -> rpc:call(B, ?MODULE, write_one, [{tr, 4, b}]) end),
    show("b_tables", lists:sort(rpc:call(B, mnesia, system_info, [local_tables]))),
    rpc:call(B, ?MODULE, finish, []),
    io:format("mnesia_replicas: ok~n");
main(["b"]) ->
    _Ready = mark("mrpb_ready"),
    receive
    after infinity -> ok
    end.

%% mnesia started with tables of this node only, each holding records 1 to 3.
start(Tabs) ->
    ok = mnesia:start(),
    [{atomic, ok} = mnesia:create_table(T, [{attributes, [k, v]}]) || T <- Tabs],
    [ok = mnesia:dirty_write({T, K, node()}) || T <- Tabs, K <- [1, 2, 3]],
    ok.

%% A change's result, then the table as both nodes see it.
step(Label, B, Tab, Fun) ->
    show(Label, norm(Fun())),
    show(Label ++ "_table", both(B, Tab)).

both(B, Tab) ->
    [
        {a, info(Tab)},
        {b, rpc:call(B, ?MODULE, info, [Tab])},
        {records, compared(digest(Tab), rpc:call(B, ?MODULE, digest, [Tab]))}
    ].

compared(Same, Same) -> {same, Same};
compared(A, B) -> {a, A, b, B}.

%% Where this node reads and writes Tab, and where its replica came from.
info(Tab) ->
    Items = [where_to_commit, where_to_write, active_replicas, where_to_read, load_node, load_reason, ram_copies],
    norm([{I, caught(fun() -> mnesia:table_info(Tab, I) end)} || I <- Items]).

%% The number of records of this node's replica and a digest of them, or no_replica.
digest(Tab) ->
    case ets:info(Tab) of
        undefined ->
            no_replica;
        _ ->
            Records = lists:sort(ets:tab2list(Tab)),
            {length(Records), erlang:phash2(Records), lists:sublist(Records, 3)}
    end.

%% mnesia stops and starts again with Nodes as extra_db_nodes; then every replica of this node has loaded.
restart(Nodes) ->
    stopped = mnesia:stop(),
    ok = mnesia:start([{extra_db_nodes, Nodes}]),
    Wait = mnesia:wait_for_tables([tr, tq], 10000),
    {Wait, loaded(1500)}.

% Whether each table with a replica here is among the local tables, which a replica joins at the end of its load.
loaded(0) ->
    {not_loaded, mine() -- mnesia:system_info(local_tables)};
loaded(Tries) ->
    case mine() -- mnesia:system_info(local_tables) of
        [] ->
            loaded;
        _ ->
            timer:sleep(20),
            loaded(Tries - 1)
    end.

% Whether this node sees Node's replicas of Tabs active, which Node announces after its load.
active_on(Node, Tabs, 0) ->
    {not_active, [T || T <- Tabs, not lists:member(Node, mnesia:table_info(T, active_replicas))]};
active_on(Node, Tabs, Tries) ->
    case lists:all(fun(T) -> lists:member(Node, mnesia:table_info(T, active_replicas)) end, Tabs) of
        true ->
            active;
        false ->
            timer:sleep(20),
            active_on(Node, Tabs, Tries - 1)
    end.

mine() ->
    [T || T <- mnesia:system_info(tables), lists:member(node(), mnesia:table_info(T, all_nodes))].

%% A transaction of this node writing Record.
write_one(Record) ->
    mnesia:transaction(fun() -> mnesia:write(Record) end).

norm({tid, _, Pid}) when is_pid(Pid) -> {tid, node(Pid)};
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
    io:format(user, "mnesia_replicas: ok~n", []),
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
