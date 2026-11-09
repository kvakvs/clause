%% Fallbacks and restores over two disc nodes (plan 21 step MIM1H11b,
%% docs/libraries/mnesia-protocol.md#fallbacks-and-restores-over-nodes-mim1h11b): a backup of the whole database is
%% installed as a fallback on both nodes and uninstalled from either, a local fallback stays on its node, restores
%% clear, keep and recreate tables held on either node or both, a deleted table comes back on its node, and both nodes
%% start again from a global fallback. Only exported functions travel, so either node may be an OTP node.
-module(mnesia_remote_backups).
-export([main/1]).
-export([start/0, stop/0, write/3, tables/0, fallback/0, finish/0]).

-define(TABLES, [td, tb, ta, tr]).

main(["a"]) ->
    B = 'mrbb@localhost',
    wait_registered("mrbb_ready"),
    pong = net_adm:ping(B),
    created(node(), B),
    fallbacks(B),
    restores(B),
    started_from_fallback(B),
    rpc:call(B, ?MODULE, finish, []),
    io:format("mnesia_remote_backups: ok~n");
main(["b"]) ->
    _Ready = mark("mrbb_ready"),
    receive
    after infinity -> ok
    end.

%% Tables on a, on b and on both, and a backup of the whole database.
created(A, B) ->
    % The runs of this program share their directory: an earlier run's schemas and backup go first.
    ok = mnesia:delete_schema([A, B]),
    _ = file:delete("all.bup"),
    show("create_schema", mnesia:create_schema([A, B])),
    show("start", [start(), rpc:call(B, ?MODULE, start, [])]),
    show("create_tables", [
        mnesia:create_table(td, [{disc_copies, [A, B]}, {attributes, [k, v]}]),
        mnesia:create_table(tb, [{disc_copies, [B]}, {attributes, [k, v]}]),
        mnesia:create_table(ta, [{disc_copies, [A]}, {attributes, [k, v]}]),
        mnesia:create_table(tr, [{ram_copies, [A, B]}, {attributes, [k, v]}])
    ]),
    show("writes", write([td, tb, ta, tr], [1, 2], a)),
    show("backup", mnesia:backup("all.bup")).

%% A global fallback on both nodes, uninstalled from b; a local one on b alone.
fallbacks(B) ->
    show("global_fallback", mnesia:install_fallback("all.bup")),
    show("fallbacks", both(B, fallback, [])),
    show("uninstalled_on_b", rpc:call(B, mnesia, uninstall_fallback, [])),
    show("fallbacks_uninstalled", both(B, fallback, [])),
    show("local_fallback_on_b", rpc:call(B, mnesia, install_fallback, ["all.bup", [{scope, local}]])),
    show("fallbacks_local", both(B, fallback, [])),
    show("uninstalled_without_one", mnesia:uninstall_fallback()),
    show("fallbacks_left", both(B, fallback, [])),
    show("uninstalled_local_on_b", rpc:call(B, mnesia, uninstall_fallback, [[{scope, local}]])),
    show("fallbacks_none", both(B, fallback, [])).

%% Restores from a and from b, each table cleared, kept or recreated; a deleted table comes back on b.
restores(B) ->
    show("writes_c", write([td, tb, ta, tr], [1, 3], c)),
    show("restore_cleared", sorted(mnesia:restore("all.bup", [{default_op, clear_tables}]))),
    show("tables_cleared", both(B, tables, [])),
    show("writes_d", write([td, tb, ta], [1, 4], d)),
    Kept = [{keep_tables, [td]}, {recreate_tables, [tb, ta]}],
    show("restore_on_b", sorted(rpc:call(B, mnesia, restore, ["all.bup", Kept]))),
    show("tables_restored_on_b", both(B, tables, [])),
    show("delete_tb", mnesia:delete_table(tb)),
    Recreated = [{recreate_tables, [tb]}, {default_op, skip_tables}],
    show("restore_deleted", sorted(mnesia:restore("all.bup", Recreated))),
    show("tables_recreated", both(B, tables, [])).

%% A global fallback from b, records written after it, both nodes stopped and started again with its records.
started_from_fallback(B) ->
    show("global_fallback_from_b", rpc:call(B, mnesia, install_fallback, ["all.bup"])),
    show("writes_e", write([td, tb, ta], [5], e)),
    show("stop", [stop(), rpc:call(B, ?MODULE, stop, [])]),
    show("fallbacks_stopped", both(B, fallback, [])),
    Self = self(),
    spawn(fun() -> Self ! {b, rpc:call(B, ?MODULE, start, [])} end),
    StartedA = start(),
    StartedB =
        receive
            {b, Started} -> Started
        end,
    show("both_start", [StartedA, StartedB]),
    show("fallbacks_taken", both(B, fallback, [])),
    show("tables_started", both(B, tables, [])),
    show("both_stop", [stop(), rpc:call(B, ?MODULE, stop, [])]).

%% Keys written with Value in each of Tabs.
write(Tabs, Keys, Value) ->
    mnesia:transaction(fun() -> [mnesia:write({T, K, V}) || T <- Tabs, K <- Keys, V <- [Value]] end).

%% mnesia started, then whether this node's tables loaded.
start() ->
    Started = mnesia:start(),
    {Started, mnesia:wait_for_tables(mnesia:system_info(local_tables), 3000)}.

stop() -> mnesia:stop().

both(B, F, Args) ->
    [{a, apply(?MODULE, F, Args)}, {b, rpc:call(B, ?MODULE, F, Args)}].

%% Whether a fallback waits in this node's directory.
fallback() ->
    Dir = mnesia:system_info(directory),
    {ok, Files} = file:list_dir(Dir),
    {mnesia:system_info(fallback_activated), lists:sort([F || F <- Files, lists:prefix("FALLBACK", F)])}.

%% Each table's nodes and records, as this node reads them.
tables() ->
    [{T, copies(T), records(T)} || T <- ?TABLES].

copies(Tab) ->
    try
        lists:sort(mnesia:table_info(Tab, disc_copies) ++ mnesia:table_info(Tab, ram_copies))
    catch
        exit:{aborted, Reason} -> Reason
    end.

records(Tab) ->
    try
        lists:sort(mnesia:dirty_match_object({Tab, '_', '_'}))
    catch
        exit:{aborted, Reason} -> Reason
    end.

%% A restore's tables in order: a backup lists them in its node's table order.
sorted({atomic, Tabs}) -> {atomic, lists:sort(Tabs)};
sorted(Other) -> Other.

%% b's end, which a sees as {badrpc, nodedown}.
finish() ->
    io:format(user, "mnesia_remote_backups: ok~n", []),
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
