%% Two nodes' RAM schemas merging (plan 21 step MIM1H5, docs/libraries/mnesia-protocol.md#schema-merge-mim1h5): each
%% node has a table of its own and one of the same name, whose cookies clash, so that node a's first merge fails; once
%% b deletes its copy of the clashing table, a merges with b through change_config/2, then b stops and starts again
%% with a as an extra db node and merges at its start. Each step shows both nodes' db nodes and table information
%% (where tables are read and written, the schema's cookie and version by node) and the system events of a. Only
%% exported functions travel, so either node may be an OTP node.
-module(mnesia_merge).
-export([main/1]).
-export([start/1, stop/0, listen/0, heard/0, state/0, delete/1, finish/0]).

main(["a"]) ->
    B = 'mrmb@localhost',
    wait_registered("mrmb_ready"),
    pong = net_adm:ping(B),
    ok = start([ta, tc]),
    ok = rpc:call(B, ?MODULE, start, [[tb, tc]]),
    show("before", both(B)),
    show("nobody", mnesia:change_config(extra_db_nodes, ['nobody@localhost'])),
    show("clash", clash(mnesia:change_config(extra_db_nodes, [B]))),
    show("clashed", both(B)),
    show("b_delete", rpc:call(B, ?MODULE, delete, [tc])),
    show("merge", mnesia:change_config(extra_db_nodes, [B])),
    show("events", wait_event({mnesia_up, node()})),
    show("merged", both(B)),
    show("again", mnesia:change_config(extra_db_nodes, [B])),
    show("b_stop", rpc:call(B, ?MODULE, stop, [])),
    show("down", wait_event({mnesia_down, B})),
    show("stopped", [{a, state()}]),
    ok = rpc:call(B, application, set_env, [mnesia, extra_db_nodes, [node()]]),
    show("b_start", rpc:call(B, ?MODULE, start, [[]])),
    show("up", wait_event({mnesia_up, B})),
    timer:sleep(500),
    show("restarted", both(B)),
    show("finished", rpc:call(B, ?MODULE, finish, [])),
    io:format("mnesia_merge: ok~n");
main(["b"]) ->
    _Ready = mark("mrmb_ready"),
    receive
    after infinity -> ok
    end.

%% mnesia started with its tables, each holding a record, and a process that keeps its system events.
start(Tabs) ->
    ok = mnesia:start(),
    [{atomic, ok} = mnesia:create_table(T, [{attributes, [k, v]}]) || T <- Tabs],
    [ok = mnesia:dirty_write({T, 1, node()}) || T <- Tabs],
    listen().

stop() ->
    mnesia:stop().

delete(Tab) ->
    mnesia:delete_table(Tab).

listen() ->
    Self = self(),
    Pid = spawn(fun() -> listening(Self) end),
    receive
        {Pid, subscribed} -> ok
    end.

listening(Parent) ->
    is_pid(whereis(mrm_events)) andalso unregister(mrm_events),
    register(mrm_events, self()),
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

%% The system events since the last look.
heard() ->
    mrm_events ! {self(), heard},
    receive
        {heard, Events} -> Events
    after 1000 -> no_listener
    end.

%% The events until Event, sorted, as the nodes' events may come in either order; the events seen so far when it does
%% not come.
wait_event(Event) ->
    wait_event(Event, 50, []).

wait_event(Event, 0, Seen) ->
    {missing, Event, Seen};
wait_event(Event, N, Seen) ->
    All = Seen ++ heard(),
    case lists:member(Event, All) of
        true ->
            lists:sort(All);
        false ->
            timer:sleep(100),
            wait_event(Event, N - 1, All)
    end.

both(B) ->
    [{a, state()}, {b, rpc:call(B, ?MODULE, state, [])}].

%% This node's db nodes and its tables' information.
state() ->
    [
        {db_nodes, mnesia:system_info(db_nodes)},
        {running, mnesia:system_info(running_db_nodes)},
        {extra, mnesia:system_info(extra_db_nodes)},
        {recover_nodes, gvar(recover_nodes)},
        {tables, [table(T) || T <- lists:sort(mnesia:system_info(tables))]}
    ].

table(Tab) ->
    Items = [where_to_read, where_to_write, active_replicas, ram_copies, storage_type, size],
    Info = [{I, mnesia:table_info(Tab, I)} || I <- Items],
    {_, CookieNode} = mnesia:table_info(Tab, cookie),
    Commit = [{where_to_commit, gvar({Tab, where_to_commit})}, {where_to_wlock, gvar({Tab, where_to_wlock})}],
    {Tab, Info ++ Commit ++ [{cookie, CookieNode}, {version, version(mnesia:table_info(Tab, version))}]}.

version({{Major, Minor}, []}) -> {Major, Minor};
version({{Major, Minor}, {Node, {_, _, _}}}) -> {Major, Minor, Node}.

gvar(Name) ->
    try
        ets:lookup_element(mnesia_gvar, Name, 2)
    catch
        error:badarg -> missing
    end.

%% A clash's message without the cookies' numbers, which differ in each run.
clash({error, {merge_schema_failed, Message}}) ->
    Cookie = "\\{\\{-?[0-9]+,-?[0-9]+,-?[0-9]+\\},",
    {error, {merge_schema_failed, re:replace(Message, Cookie, "{cookie,", [global, {return, list}])}};
clash(Other) ->
    Other.

%% b's end, which a sees as {badrpc, nodedown}.
finish() ->
    io:format(user, "mnesia_merge: ok~n", []),
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
