%% Transactions locking tables held on the other node (plan 21 step MIM1H7,
%% docs/libraries/mnesia-protocol.md#remote-locks-mim1h7): after the two nodes' RAM schemas merge, each node's
%% transactions take every kind of lock on the other node's table and read it, abort and nest; then transactions of
%% both nodes contend: an older one waits in the other node's queue, a younger one is refused and restarts until the
%% holder ends. A killed transaction's locks go on the other node, and a holder going down ends the wait of a
%% transaction queued there and releases its own transactions' locks. Transaction ids and processes are shown by node,
%% as their numbers differ between runs. Only exported functions travel, so either node may be an OTP node.
-module(mnesia_remote_locks).
-export([main/1]).
-export([start/1, locks/2, contend/2, refuse/2, killed/1, hold/3, release/1, bump_past/1, looked/0, finish/0]).

main(["a"]) ->
    B = 'mrlb@localhost',
    wait_registered("mrlb_ready"),
    pong = net_adm:ping(B),
    ok = start(ta),
    ok = rpc:call(B, ?MODULE, start, [tb]),
    show("merge", mnesia:change_config(extra_db_nodes, [B])),
    timer:sleep(300),
    show("a_locks_tb", locks(tb, B)),
    show("b_locks_ta", rpc:call(B, ?MODULE, locks, [ta, node()])),
    show("a_waits_for_b", contend(tb, B)),
    show("b_waits_for_a", rpc:call(B, ?MODULE, contend, [ta, node()])),
    show("a_refused_by_b", refuse(tb, B)),
    show("b_refused_by_a", rpc:call(B, ?MODULE, refuse, [ta, node()])),
    show("a_killed", killed(B)),
    show("holder_down", holder_down(B)),
    io:format("mnesia_remote_locks: ok~n");
main(["b"]) ->
    _Ready = mark("mrlb_ready"),
    receive
    after infinity -> ok
    end.

%% mnesia started with a table indexed on v holding this node's records 0 to 3.
start(Tab) ->
    ok = mnesia:start(),
    {atomic, ok} = mnesia:create_table(Tab, [{attributes, [k, v]}, {index, [v]}]),
    [ok = mnesia:dirty_write({Tab, K, node()}) || K <- [0, 1, 2, 3]],
    ok.

%% Each kind of lock and read of Tab, held on Other, in a transaction of its own.
locks(Tab, Other) ->
    Here = node(),
    Run = fun(F) -> norm(mnesia:transaction(F)) end,
    [
        {read, Run(fun() -> mnesia:read(Tab, 1) end)},
        {read_missing, Run(fun() -> mnesia:read(Tab, 9) end)},
        {wread, Run(fun() -> mnesia:read(Tab, 1, write) end)},
        {table_read, Run(fun() -> mnesia:lock({table, Tab}, read) end)},
        {table_write, Run(fun() -> mnesia:lock({table, Tab}, write) end)},
        {record_write, Run(fun() -> mnesia:lock({record, Tab, 1}, write) end)},
        {global_write, Run(fun() -> mnesia:lock({global, item, [Here, Other]}, write) end)},
        {global_read, Run(fun() -> mnesia:lock({global, item, [Other]}, read) end)},
        {index_read, Run(fun() -> lists:sort(mnesia:index_read(Tab, Other, v)) end)},
        {index_held,
            Run(fun() ->
                mnesia:lock({table, Tab}, read),
                lists:sort(mnesia:index_read(Tab, Other, v))
            end)},
        {match, Run(fun() -> lists:sort(mnesia:match_object({Tab, '_', '_'})) end)},
        {select, Run(fun() -> lists:sort(mnesia:select(Tab, [{{Tab, '$1', '_'}, [], ['$1']}])) end)},
        {walk, Run(fun() -> walk(Tab) end)},
        {all_keys, Run(fun() -> lists:sort(mnesia:all_keys(Tab)) end)},
        {foldl, Run(fun() -> lists:sort(mnesia:foldl(fun({_, K, _}, Acc) -> [K | Acc] end, [], Tab)) end)},
        {chunks, Run(fun() -> chunks(Tab) end)},
        {held,
            Run(fun() ->
                mnesia:read(Tab, 1),
                mnesia:lock({table, Tab}, write),
                rpc:call(Other, ?MODULE, looked, [])
            end)},
        {abort,
            Run(fun() ->
                mnesia:read(Tab, 2, write),
                mnesia:abort(stop)
            end)},
        {after_abort, wait_unlocked(Other)},
        {nested, Run(fun() -> mnesia:transaction(fun() -> mnesia:read(Tab, 2) end) end)},
        {nested_abort,
            Run(fun() ->
                {
                    mnesia:transaction(fun() ->
                        mnesia:read(Tab, 3),
                        mnesia:abort(inner)
                    end),
                    rpc:call(Other, ?MODULE, looked, [])
                }
            end)},
        {after_nested, wait_unlocked(Other)},
        {sticky, Run(fun() -> mnesia:write(Tab, {Tab, 5, x}, sticky_write) end)},
        {locks_after, rpc:call(Other, ?MODULE, looked, [])}
    ].

walk(Tab) ->
    First = mnesia:first(Tab),
    Keys = walk(Tab, First, []),
    lists:sort(Keys).

walk(_, '$end_of_table', Keys) -> Keys;
walk(Tab, Key, Keys) -> walk(Tab, mnesia:next(Tab, Key), [Key | Keys]).

chunks(Tab) ->
    {First, Cont} = mnesia:select(Tab, [{'_', [], ['$_']}], 2, read),
    lists:sort(First ++ more(mnesia:select(Cont))).

more('$end_of_table') -> [];
more({Records, Cont}) -> Records ++ more(mnesia:select(Cont)).

%% An older transaction here asks for a record that a younger one holds on Other: it waits in Other's queue and gets
%% the lock once the holder ends.
contend(Tab, Other) ->
    Self = self(),
    Waiter = spawn(fun() ->
        R = mnesia:transaction(fun() ->
            Self ! {tid, tid()},
            receive
                go -> ok
            end,
            mnesia:read(Tab, 1)
        end),
        Self ! {waited, R}
    end),
    Tid =
        receive
            {tid, T} -> T
        end,
    ok = rpc:call(Other, ?MODULE, bump_past, [Tid]),
    _ = rpc:call(Other, ?MODULE, hold, [younger, Tab, 1]),
    Waiter ! go,
    Queued = wait_queued(Other, 50),
    Released = rpc:call(Other, ?MODULE, release, [younger]),
    Result =
        receive
            {waited, W} -> W
        after 10000 -> timeout
        end,
    [{queued, Queued}, {holder, Released}, {waiter, norm(Result)}, {after_wait, wait_unlocked(Other)}].

wait_queued(Other, 0) ->
    {not_queued, rpc:call(Other, ?MODULE, looked, [])};
wait_queued(Other, N) ->
    case rpc:call(Other, ?MODULE, looked, []) of
        {_, [_ | _]} = Locks ->
            Locks;
        _ ->
            timer:sleep(20),
            wait_queued(Other, N - 1)
    end.

%% A younger transaction here asks for a record that an older one holds on Other: it is refused and restarts until the
%% holder ends, without waiting in Other's queue.
refuse(Tab, Other) ->
    Older = rpc:call(Other, ?MODULE, hold, [older, Tab, 2]),
    ok = bump_past(Older),
    Restarts = mnesia:system_info(transaction_restarts),
    Self = self(),
    Counter = ets:new(attempts, [public]),
    ets:insert(Counter, {n, 0}),
    _ = spawn(fun() ->
        R = mnesia:transaction(fun() ->
            ets:update_counter(Counter, n, 1),
            mnesia:read(Tab, 2)
        end),
        Self ! {refused, R}
    end),
    timer:sleep(300),
    Held = rpc:call(Other, ?MODULE, looked, []),
    Released = rpc:call(Other, ?MODULE, release, [older]),
    Result =
        receive
            {refused, W} -> W
        after 10000 -> timeout
        end,
    [
        {held, Held},
        {holder, Released},
        {refused, norm(Result)},
        {retried, ets:lookup_element(Counter, n, 2) > 1},
        {restarts_counted, mnesia:system_info(transaction_restarts) > Restarts},
        {after_refusal, wait_unlocked(Other)}
    ].

%% A transaction holding a record on Other is killed: its lock there goes.
killed(Other) ->
    Self = self(),
    Pid = spawn(fun() ->
        mnesia:transaction(fun() ->
            mnesia:lock({record, tb, 3}, write),
            Self ! locked,
            receive
            after infinity -> ok
            end
        end)
    end),
    receive
        locked -> ok
    end,
    Held = rpc:call(Other, ?MODULE, looked, []),
    exit(Pid, kill),
    [{held, Held}, {after_kill, wait_unlocked(Other)}].

%% A transaction here waits in Other's queue while Other's transaction holds a lock here; Other goes down: the waiter
%% restarts and finds the table gone, and the lock here goes.
holder_down(Other) ->
    Self = self(),
    Waiter = spawn(fun() ->
        R = mnesia:transaction(fun() ->
            first_run(Self),
            mnesia:read(tb, 3, write)
        end),
        Self ! {waited, R}
    end),
    Tid =
        receive
            {tid, T} -> T
        end,
    ok = rpc:call(Other, ?MODULE, bump_past, [Tid]),
    _ = rpc:call(Other, ?MODULE, hold, [younger, tb, 3]),
    _ = rpc:call(Other, ?MODULE, hold, [remote_holder, ta, 3]),
    Here = looked(),
    Waiter ! go,
    Queued = wait_queued(Other, 50),
    Finished = rpc:call(Other, ?MODULE, finish, []),
    Result =
        receive
            {waited, W} -> W
        after 20000 -> timeout
        end,
    [{here, Here}, {queued, Queued}, {finished, Finished}, {waiter, norm(Result)}, {after_down, wait_unlocked(node())}].

% The first run of a transaction tells its tid and waits for go; a restarted run goes on at once.
first_run(Parent) ->
    case get(started) of
        undefined ->
            put(started, true),
            Parent ! {tid, tid()},
            receive
                go -> ok
            end;
        true ->
            ok
    end.

%% A transaction of a process registered as Name holding a write lock on Tab's record Key until release/1; its tid.
hold(Name, Tab, Key) ->
    Self = self(),
    _ = spawn(fun() ->
        register(Name, self()),
        R = mnesia:transaction(fun() ->
            mnesia:lock({record, Tab, Key}, write),
            Self ! {held, tid()},
            receive
                {release, From} -> From
            end
        end),
        {atomic, From} = R,
        From ! {released, Name}
    end),
    receive
        {held, Tid} -> Tid
    end.

release(Name) ->
    Name ! {release, self()},
    receive
        {released, Name} -> released
    after 10000 -> timeout
    end.

%% Transactions here until this node's next transaction is younger than Tid.
bump_past(Tid) ->
    {atomic, Mine} = mnesia:transaction(fun tid/0),
    case Mine > Tid of
        true -> ok;
        false -> bump_past(Tid)
    end.

tid() ->
    {_, Tid, _} = get(mnesia_activity_state),
    Tid.

%% This node's held locks and lock queue, with each transaction and process shown by its node.
looked() ->
    norm({mnesia:system_info(held_locks), mnesia:system_info(lock_queue)}).

%% Node's locks once the transactions of the other node have none there.
wait_unlocked(Node) ->
    wait_unlocked(Node, 100).

wait_unlocked(Node, 0) ->
    {locked, rpc:call(Node, ?MODULE, looked, [])};
wait_unlocked(Node, N) ->
    case rpc:call(Node, ?MODULE, looked, []) of
        {[], []} ->
            unlocked;
        _ ->
            timer:sleep(20),
            wait_unlocked(Node, N - 1)
    end.

norm({tid, _, Pid}) when is_pid(Pid) -> {tid, node(Pid)};
norm(Pid) when is_pid(Pid) -> node(Pid);
norm(List) when is_list(List) -> [norm(E) || E <- List];
norm(Tuple) when is_tuple(Tuple) -> list_to_tuple(norm(tuple_to_list(Tuple)));
norm(Term) -> Term.

%% b's end, which a sees as {badrpc, nodedown}.
finish() ->
    io:format(user, "mnesia_remote_locks: ok~n", []),
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
