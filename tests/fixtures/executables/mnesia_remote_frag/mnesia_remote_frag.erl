%% Events and fragmented tables over two disc nodes (plan 21 step MIM1H12,
%% docs/libraries/mnesia-protocol.md#events-and-fragments-over-nodes-mim1h12): subscriptions to tables held here and
%% elsewhere, the table, activity and system events of transactions and dirty writes from either node; fragmented
%% tables spread over a node pool of both nodes, written and read from either node through mnesia_frag, fragments
%% added and deleted with their records moving between nodes, pool nodes added and deleted, and the schema copy of b
%% deleted while b is down. Only exported functions travel, so either node may be an OTP node.
-module(mnesia_remote_frag).
-export([main/1]).
-export([start/0, stop/0, write/3, subscribe/1, heard/0, frag_info/1, frag_write/3, frag_read/1, finish/0]).

main(["a"]) ->
    B = 'mrfb@localhost',
    wait_registered("mrfb_ready"),
    pong = net_adm:ping(B),
    created(node(), B),
    events(B),
    fragments(node(), B),
    pools(B),
    b_removed(B),
    rpc:call(B, ?MODULE, finish, []),
    io:format("mnesia_remote_frag: ok~n");
main(["b"]) ->
    _Ready = mark("mrfb_ready"),
    receive
    after infinity -> ok
    end.

created(A, B) ->
    % The runs of this program share their directory: an earlier run's schemas go first.
    ok = mnesia:delete_schema([A, B]),
    show("create_schema", mnesia:create_schema([A, B])),
    show("start", [start(), rpc:call(B, ?MODULE, start, [])]),
    show("create_tables", [
        mnesia:create_table(tb, [{disc_copies, [B]}, {attributes, [k, v]}]),
        mnesia:create_table(td, [{disc_copies, [A, B]}, {attributes, [k, v]}])
    ]).

%% Subscriptions on both nodes, then writes from each node and what each subscriber hears.
events(B) ->
    Subscriptions = [{table, tb, simple}, {table, tb, detailed}, {table, td, simple}, {table, td, detailed}, activity],
    show("subscribe", subscribe(Subscriptions)),
    show("subscribe_on_b", rpc:call(B, ?MODULE, subscribe, [[{table, td, simple}, {table, tb, simple}, activity]])),
    show("writes", write([tb, td], [1], a)),
    timer:sleep(300),
    % One record a transaction: the order of a transaction's events on other nodes differs (docs/differences.md).
    show("writes_on_b", rpc:call(B, ?MODULE, write, [[td], [2], b])),
    show("writes_tb_on_b", rpc:call(B, ?MODULE, write, [[tb], [1], b])),
    timer:sleep(300),
    show("dirty_write_on_b", rpc:call(B, mnesia, dirty_write, [{td, 3, b}])),
    show("dirty_delete", mnesia:dirty_delete(td, 2)),
    timer:sleep(300),
    show("heard", heard()),
    show("heard_on_b", rpc:call(B, ?MODULE, heard, [])).

%% A fragmented table over both nodes, written and read from either node; a fragment added on b and deleted again.
fragments(A, B) ->
    Props = [{n_fragments, 4}, {node_pool, [A, B]}, {n_disc_copies, 1}],
    show("create_frag", mnesia:create_table(tf, [{frag_properties, Props}, {attributes, [k, v]}])),
    show("frag_info", frag_info(tf)),
    show("frag_info_on_b", rpc:call(B, ?MODULE, frag_info, [tf])),
    show("frag_writes", frag_write(transaction, lists:seq(1, 12), x)),
    show("frag_writes_on_b", rpc:call(B, ?MODULE, frag_write, [sync_dirty, lists:seq(13, 16), y])),
    show("frag_read", frag_read(transaction)),
    show("frag_read_on_b", rpc:call(B, ?MODULE, frag_read, [async_dirty])),
    show("add_frag", mnesia:change_table_frag(tf, {add_frag, [B]})),
    show("frag_info_added", frag_info(tf)),
    show("frag_read_added", rpc:call(B, ?MODULE, frag_read, [transaction])),
    show("del_frag", mnesia:change_table_frag(tf, del_frag)),
    show("frag_info_deleted", frag_info(tf)),
    show("frag_read_deleted", frag_read(async_dirty)).

%% Pool nodes added and deleted, and a table fragmented over a pool that begins with a.
pools(B) ->
    show("add_node", mnesia:change_table_frag(tf, {add_node, B})),
    show("del_node", mnesia:change_table_frag(tf, {del_node, B})),
    show("add_node_again", rpc:call(B, mnesia, change_table_frag, [tf, {add_node, B}])),
    show("frag_info_pool", frag_info(tf)),
    Props = [{n_fragments, 3}, {node_pool, [node(), B]}, {n_disc_copies, 2}],
    show("create_frag_both", mnesia:create_table(tg, [{frag_properties, Props}, {attributes, [k, v]}])),
    show("frag_info_both", frag_info(tg)).

%% b goes down and its schema copy is deleted: its fragments go, and the pools lose it.
b_removed(B) ->
    show("b_stops", rpc:call(B, ?MODULE, stop, [])),
    show("del_schema_copy", mnesia:del_table_copy(schema, B)),
    show("tables", lists:sort(mnesia:system_info(tables))),
    show("frag_info_tf", frag_info(tf)),
    show("frag_info_tg", frag_info(tg)),
    show("stop", stop()).

%% Keys written with Value in each of Tabs.
write(Tabs, Keys, Value) ->
    mnesia:transaction(fun() -> [mnesia:write({T, K, V}) || T <- Tabs, K <- Keys, V <- [Value]] end).

%% mnesia started, then whether this node's tables loaded.
start() ->
    Started = mnesia:start(),
    {Started, mnesia:wait_for_tables(mnesia:system_info(local_tables), 3000)}.

stop() -> mnesia:stop().

%% A process of this node subscribed to each of What, keeping what it hears; each subscription's answer.
subscribe(What) ->
    Self = self(),
    Pid = spawn(fun() ->
        Self ! {subscribed, [mnesia:subscribe(W) || W <- What]},
        listening([])
    end),
    is_pid(whereis(mrf_events)) andalso unregister(mrf_events),
    register(mrf_events, Pid),
    receive
        {subscribed, Answers} -> Answers
    end.

listening(Events) ->
    receive
        {From, heard} ->
            From ! {heard, lists:reverse(Events)},
            listening([]);
        Event ->
            listening([event(Event) | Events])
    end.

%% An event without its transaction's identity: the node of the process that made it.
event({mnesia_activity_event, {What, {tid, _, Pid}}}) ->
    {activity, What, node(Pid)};
event({mnesia_table_event, {Op, Record, {tid, _, Pid}}}) ->
    {table, Op, Record, node(Pid)};
event({mnesia_table_event, {Op, Record, {dirty, Pid}}}) ->
    {table, Op, Record, dirty, node(Pid)};
event({mnesia_table_event, {Op, Tab, Record, Old, {tid, _, Pid}}}) ->
    {detailed, Op, Tab, Record, Old, node(Pid)};
event({mnesia_table_event, {Op, Tab, Record, Old, {dirty, Pid}}}) ->
    {detailed, Op, Tab, Record, Old, dirty, node(Pid)};
event(Event) ->
    Event.

heard() ->
    mrf_events ! {self(), heard},
    receive
        {heard, Events} -> Events
    after 1000 -> no_listener
    end.

%% A fragmented table's properties, its fragments' nodes and sizes as mnesia_frag tells them.
frag_info(Tab) ->
    try
        mnesia:activity(
            async_dirty,
            fun() ->
                Items = [n_fragments, node_pool, frag_names, frag_dist, frag_size],
                % In order: the operands of ++ may be evaluated either way first.
                Info = [{I, mnesia:table_info(Tab, I)} || I <- Items],
                Info ++ [{F, lists:sort(mnesia:table_info(F, disc_copies))} || F <- mnesia:table_info(Tab, frag_names)]
            end,
            [],
            mnesia_frag
        )
    catch
        exit:Reason -> Reason
    end.

frag_write(Kind, Keys, Value) ->
    mnesia:activity(Kind, fun() -> [mnesia:write({tf, K, Value}) || K <- Keys] end, [], mnesia_frag).

frag_read(Kind) ->
    mnesia:activity(Kind, fun() -> lists:sort(mnesia:match_object({tf, '_', '_'})) end, [], mnesia_frag).

%% b's end, which a sees as {badrpc, nodedown}.
finish() ->
    io:format(user, "mnesia_remote_frag: ok~n", []),
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
