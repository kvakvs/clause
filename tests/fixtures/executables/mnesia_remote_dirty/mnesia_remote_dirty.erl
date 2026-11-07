%% Dirty reads and writes of tables held only on the other node (plan 21 step MIM1H6,
%% docs/libraries/mnesia-protocol.md#remote-reads-and-dirty-writes-mim1h6): after the two nodes' RAM schemas merge,
%% each node reads and dirty-writes the other's ordered_set table and counter table: reads go through the holder's
%% mnesia_rpc, writes to its mnesia_tm as commits, whose table events the holder reports. Then chunked selects and
%% folds in a dirty activity, failures, a write whose holder goes down while it waits, and reads once it is gone. Only
%% exported functions travel, so either node may be an OTP node.
-module(mnesia_remote_dirty).
-export([main/1]).
-export([start/1, listen/1, heard/0, exercise/2, activity/1, failures/1, hold/0, finish/0]).

main(["a"]) ->
    B = 'mrdb@localhost',
    wait_registered("mrdb_ready"),
    pong = net_adm:ping(B),
    ok = start(ta),
    ok = rpc:call(B, ?MODULE, start, [tb]),
    show("merge", mnesia:change_config(extra_db_nodes, [B])),
    timer:sleep(300),
    ok = listen(ta),
    ok = rpc:call(B, ?MODULE, listen, [tb]),
    show("a_on_tb", exercise(tb, tb_count)),
    show("b_heard", rpc:call(B, ?MODULE, heard, [])),
    show("b_on_ta", rpc:call(B, ?MODULE, exercise, [ta, ta_count])),
    show("a_heard", heard()),
    show("a_activity", activity(tb)),
    show("b_activity", rpc:call(B, ?MODULE, activity, [ta])),
    show("activity_heard", [{a, heard()}, {b, rpc:call(B, ?MODULE, heard, [])}]),
    show("a_failures", failures(tb)),
    show("b_failures", rpc:call(B, ?MODULE, failures, [ta])),
    show("b_hold", rpc:call(B, ?MODULE, hold, [])),
    Self = self(),
    _ = spawn(fun() -> Self ! {written, caught(fun() -> mnesia:dirty_write({tb, 9, a}) end)} end),
    timer:sleep(300),
    show("b_finish", rpc:call(B, ?MODULE, finish, [])),
    show(
        "written",
        receive
            {written, Written} -> Written
        after 10000 -> timeout
        end
    ),
    wait_gone(tb),
    show("gone", gone(tb)),
    io:format("mnesia_remote_dirty: ok~n");
main(["b"]) ->
    _Ready = mark("mrdb_ready"),
    receive
    after infinity -> ok
    end.

%% mnesia started with an ordered_set table indexed on v holding this node's record, and a counter table.
start(Tab) ->
    ok = mnesia:start(),
    Attrs = [{attributes, [k, v]}, {type, ordered_set}, {index, [v]}],
    {atomic, ok} = mnesia:create_table(Tab, Attrs),
    {atomic, ok} = mnesia:create_table(counter(Tab), [{attributes, [k, v]}]),
    mnesia:dirty_write({Tab, 1, node()}).

counter(ta) -> ta_count;
counter(tb) -> tb_count.

%% Another node's dirty writes and reads of Tab and of its counter table Counter.
exercise(Tab, Counter) ->
    [
        {write, mnesia:dirty_write({Tab, 2, a})},
        {sync_write, mnesia:sync_dirty(fun() -> mnesia:write({Tab, 3, a}) end)},
        {async_write, mnesia:async_dirty(fun() -> mnesia:write({Tab, 4, b}) end)},
        {read, mnesia:dirty_read(Tab, 2)},
        {delete, mnesia:dirty_delete(Tab, 3)},
        {delete_object, mnesia:dirty_delete_object({Tab, 4, b})},
        {counter, [mnesia:dirty_update_counter(Counter, 5, I) || I <- [3, -1, -10]]},
        {not_a_counter, mnesia:dirty_update_counter(Tab, 2, 1)},
        {counted, mnesia:dirty_read(Tab, 2)},
        {write_more, [mnesia:dirty_write({Tab, K, a}) || K <- [6, 7]]},
        {match, mnesia:dirty_match_object({Tab, '_', a})},
        {match_key, mnesia:dirty_match_object({Tab, 1, '_'})},
        {index_read, mnesia:dirty_index_read(Tab, a, v)},
        {index_match, mnesia:dirty_index_match_object({Tab, '_', a}, v)},
        {select, mnesia:dirty_select(Tab, [{{Tab, '$1', '_'}, [], ['$1']}])},
        {select_reverse, mnesia:dirty_select_reverse(Tab, [{'_', [], ['$_']}])},
        {all_keys, mnesia:dirty_all_keys(Tab)},
        {walk, [mnesia:dirty_first(Tab), mnesia:dirty_last(Tab), mnesia:dirty_next(Tab, 2), mnesia:dirty_prev(Tab, 2)]},
        {slot, mnesia:dirty_slot(Tab, 0)},
        {info, [{I, mnesia:table_info(Tab, I)} || I <- [size, memory, type, storage_type, arity]]},
        {where, [{I, mnesia:table_info(Tab, I)} || I <- [where_to_read, where_to_write, where_to_commit]]}
    ].

%% Chunked selects, a fold and writes in dirty activities on the other node's table.
activity(Tab) ->
    Async = mnesia:async_dirty(fun() -> chunks(Tab) end),
    Sync = mnesia:sync_dirty(fun() -> {mnesia:write({Tab, 8, c}), mnesia:read(Tab, 8)} end),
    [{async, Async}, {sync, Sync}].

chunks(Tab) ->
    Spec = [{'_', [], ['$_']}],
    {First, Cont} = mnesia:select(Tab, Spec, 2, read),
    {Last, RevCont} = mnesia:select_reverse(Tab, Spec, 2, read),
    [
        {chunks, [First | more(mnesia:select(Cont))]},
        {reverse, [Last | more(mnesia:select(RevCont))]},
        {foldl, mnesia:foldl(fun({_, K, _}, Acc) -> [K | Acc] end, [], Tab)},
        {all_keys, mnesia:all_keys(Tab)},
        {read, mnesia:read(Tab, 1)}
    ].

more('$end_of_table') -> [];
more({Records, Cont}) -> [Records | more(mnesia:select(Cont))].

%% Reads that fail on the other node and writes this node refuses.
failures(Tab) ->
    [
        {bad_record, caught(fun() -> mnesia:dirty_write({Tab, 1}) end)},
        {bad_arity, caught(fun() -> mnesia:dirty_write({Tab, 1, 2, 3}) end)},
        {bad_slot, caught(fun() -> mnesia:dirty_slot(Tab, -1) end)},
        {bad_select, caught(fun() -> mnesia:dirty_select(Tab, [bad]) end)},
        {bad_counter, caught(fun() -> mnesia:dirty_update_counter(Tab, 1, a) end)},
        {ets, caught(fun() -> mnesia:ets(fun() -> mnesia:read(Tab, 1) end) end)}
    ].

caught(F) ->
    try
        F()
    catch
        exit:{aborted, Reason} -> {aborted, Reason};
        exit:{Reason, Stack} when is_list(Stack) -> {exit, Reason};
        Class:Reason -> {Class, Reason}
    end.

%% Keeps this node's mnesia_tm suspended, so that the other node's writes wait for it.
hold() ->
    Self = self(),
    _ = spawn(fun() ->
        true = erlang:suspend_process(whereis(mnesia_tm)),
        Self ! held,
        receive
        after infinity -> ok
        end
    end),
    receive
        held -> ok
    end.

wait_gone(Tab) ->
    case mnesia:table_info(Tab, where_to_read) of
        nowhere ->
            ok;
        _ ->
            timer:sleep(50),
            wait_gone(Tab)
    end.

%% A table whose only holder went: its reads and writes.
gone(Tab) ->
    [
        {read, caught(fun() -> mnesia:dirty_read(Tab, 1) end)},
        {write, caught(fun() -> mnesia:dirty_write({Tab, 9, a}) end)},
        {size, mnesia:table_info(Tab, size)},
        {select, caught(fun() -> mnesia:dirty_select(Tab, [{'_', [], ['$_']}]) end)}
    ].

%% A process that keeps Tab's detailed table events.
listen(Tab) ->
    Self = self(),
    Pid = spawn(fun() -> listening(Self, Tab) end),
    receive
        {Pid, subscribed} -> ok
    end.

listening(Parent, Tab) ->
    register(mrd_events, self()),
    {ok, _} = mnesia:subscribe({table, Tab, detailed}),
    Parent ! {self(), subscribed},
    events([]).

events(Events) ->
    receive
        {mnesia_table_event, Event} ->
            events([event(Event) | Events]);
        {From, heard} ->
            From ! {heard, lists:reverse(Events)},
            events([])
    end.

% An event with the node of the activity's process in place of the process.
event(Event) ->
    case element(tuple_size(Event), Event) of
        {Kind, Pid} when is_pid(Pid) -> setelement(tuple_size(Event), Event, {Kind, node(Pid)});
        _ -> Event
    end.

%% The table events since the last look.
heard() ->
    mrd_events ! {self(), heard},
    receive
        {heard, Events} -> Events
    after 1000 -> no_listener
    end.

%% b's end, which a sees as {badrpc, nodedown}.
finish() ->
    io:format(user, "mnesia_remote_dirty: ok~n", []),
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
