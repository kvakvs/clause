%% Checkpoints over two disc nodes and backups of them (plan 21 step MIM1H11a,
%% docs/libraries/mnesia-protocol.md#checkpoints-over-nodes-mim1h11a): checkpoints activated on either node keep
%% retainers where the tables' replicas are, with most redundancy (max) or least (min), and each node's bookkeeping
%% shows them; backups read the retainers of the other node; b stopping ends the checkpoints it alone wrote (reported on
%% stderr), b started again and a new copy join the checkpoints kept with most redundancy, with the records retained
%% so far; a deactivation from b ends a checkpoint on both nodes, and a copy deleted ends the checkpoints it alone
%% wrote. Only exported functions travel, so either node may be an OTP node.
-module(mnesia_remote_checkpoints).
-export([main/1]).
-export([start/0, stop/0, write/3, checkpoints/0, stores/0, backup/2, heard/0, finish/0]).

-define(TABLES, [td, tb, tr, te]).
-define(NAMES, [cp1, cp2, cp3, cp5, cp6, cp7, cp8]).

main(["a"]) ->
    B = 'mrcb@localhost',
    wait_registered("mrcb_ready"),
    pong = net_adm:ping(B),
    activated(node(), B),
    backups(B),
    b_restarts(B),
    copies(node(), B),
    show("both_stop", [stop(), rpc:call(B, ?MODULE, stop, [])]),
    rpc:call(B, ?MODULE, finish, []),
    io:format("mnesia_remote_checkpoints: ok~n");
main(["b"]) ->
    _Ready = mark("mrcb_ready"),
    receive
    after infinity -> ok
    end.

%% Checkpoints activated on a and b, with their retainers and each node's bookkeeping.
activated(A, B) ->
    % The runs of this program share their directory: an earlier run's schemas go first.
    ok = mnesia:delete_schema([A, B]),
    show("create_schema", mnesia:create_schema([A, B])),
    show("start", [start(), rpc:call(B, ?MODULE, start, [])]),
    show("create_tables", [
        mnesia:create_table(td, [{disc_copies, [A, B]}, {attributes, [k, v]}]),
        mnesia:create_table(tb, [{disc_copies, [B]}, {attributes, [k, v]}]),
        mnesia:create_table(tr, [{ram_copies, [A, B]}, {attributes, [k, v]}]),
        mnesia:create_table(te, [{disc_copies, [A]}, {attributes, [k, v]}])
    ]),
    show("writes", write(?TABLES, [1, 2], a)),
    show("cp1_max", mnesia:activate_checkpoint([{name, cp1}, {max, [td]}])),
    show("cp2_min", mnesia:activate_checkpoint([{name, cp2}, {min, [td]}])),
    show("cp3_min_remote", mnesia:activate_checkpoint([{name, cp3}, {min, [tb]}])),
    show("cp4_not_remote", mnesia:activate_checkpoint([{name, cp4}, {min, [tb]}, {allow_remote, false}])),
    show("cp5_max", mnesia:activate_checkpoint([{name, cp5}, {max, [td, tb, tr]}])),
    show("cp6_min_on_b", rpc:call(B, mnesia, activate_checkpoint, [[{name, cp6}, {min, [td, te]}]])),
    show("checkpoints", both(B, checkpoints, [])).

%% Records written on both nodes, then backups of the checkpoints from either node.
backups(B) ->
    show("b_writes", rpc:call(B, ?MODULE, write, [[td, tb, tr], [1, 2], b])),
    show("a_writes", write([td, te], [1, 3], c)),
    % b applies a's commit after a's transaction returns: the stores wait for it.
    timer:sleep(500),
    show("stores", both(B, stores, [])),
    show("cp5_backup", backup(cp5, "cp5.bup")),
    show("cp3_backup", backup(cp3, "cp3.bup")),
    show("cp6_backup_on_b", rpc:call(B, ?MODULE, backup, [cp6, "cp6b.bup"])),
    show("cp6_backup", backup(cp6, "cp6.bup")).

%% b stops: the checkpoints whose retainers only b wrote end; b started again joins cp1 with a's retained records.
b_restarts(B) ->
    _ = heard(),
    show("b_stops", rpc:call(B, ?MODULE, stop, [])),
    show("events", events_until({mnesia_down, B}, 50)),
    show("checkpoints_alone", checkpoints()),
    show("a_writes_alone", write([td], [2, 4], d)),
    show("b_starts", rpc:call(B, ?MODULE, start, [])),
    timer:sleep(300),
    show("checkpoints_again", both(B, checkpoints, [])),
    show("stores_again", both(B, stores, [])).

%% A new copy of te joins cp7 (max), not cp8 (min), and is backed up on b before b writes: OTP's joined node keeps
%% none of its own later updates (docs/differences.md). Deactivations and a deleted copy end checkpoints.
copies(A, B) ->
    show("cp7_max", mnesia:activate_checkpoint([{name, cp7}, {max, [te]}])),
    show("cp8_min", mnesia:activate_checkpoint([{name, cp8}, {min, [te]}])),
    show("a_writes_te", write([te], [1, 5], e)),
    show("add_copy", mnesia:add_table_copy(te, B, disc_copies)),
    show("checkpoints_copied", both(B, checkpoints, [])),
    show("stores_copied", both(B, stores, [])),
    show("cp7_backup_on_b", rpc:call(B, ?MODULE, backup, [cp7, "cp7b.bup"])),
    show("b_writes_te", rpc:call(B, ?MODULE, write, [[te], [2, 5], f])),
    show("cp7_backup", backup(cp7, "cp7.bup")),
    show("cp1_deactivated_on_b", rpc:call(B, mnesia, deactivate_checkpoint, [cp1])),
    show("cp2_deactivated_on_b", rpc:call(B, mnesia, deactivate_checkpoint, [cp2])),
    show("checkpoints_deactivated", both(B, checkpoints, [])),
    _ = heard(),
    show("del_copy", mnesia:del_table_copy(te, A)),
    show("events_deleted", events_until({mnesia_checkpoint_deactivated, cp8}, 20)),
    show("checkpoints_deleted", both(B, checkpoints, [])).

%% Keys written with Value in each of Tabs.
write(Tabs, Keys, Value) ->
    mnesia:transaction(fun() -> [mnesia:write({T, K, Value}) || T <- Tabs, K <- Keys] end).

%% mnesia started, with a process keeping its system events; then whether this node's tables loaded.
start() ->
    Started = mnesia:start(),
    listen(),
    {Started, mnesia:wait_for_tables(mnesia:system_info(local_tables), 3000)}.

stop() -> mnesia:stop().

both(B, F, Args) ->
    [{a, apply(?MODULE, F, Args)}, {b, rpc:call(B, ?MODULE, F, Args)}].

%% This node's checkpoints, each table's checkpoints and first commit work, and the retainers it knows: their store
%% here, if any, writers and whether they retain updates.
checkpoints() ->
    Tables = [T || T <- ?TABLES, lists:member(T, mnesia:system_info(tables))],
    [{checkpoints, mnesia:system_info(checkpoints)}] ++
        [{T, mnesia:table_info(T, checkpoints), work(mnesia:table_info(T, commit_work))} || T <- Tables] ++
        [
            {T, N, store(Store), Writers, Really}
         || T <- Tables, N <- ?NAMES, {_, {retainer, _, _, Store, Writers, Really}} <- retainer(T, N)
        ].

work([{checkpoints, Names} | _]) -> Names;
work(_) -> none.

retainer(Tab, Name) -> ets:lookup(mnesia_gvar, {Tab, {retainer, Name}}).

store({ets, _}) -> ets;
store(Store) -> Store.

%% The records each retainer written here kept.
stores() ->
    [
        {T, N, lists:sort(ets:tab2list(S))}
     || T <- ?TABLES,
        N <- ?NAMES,
        {_, {retainer, _, _, {ets, S}, Writers, _}} <- retainer(T, N),
        lists:member(node(), Writers)
    ].

%% A checkpoint backed up to File here, and the backup's items sorted: each table's definition (its disc and RAM
%% nodes) and its records and deletes.
backup(Name, File) ->
    _ = file:delete(File),
    case mnesia:backup_checkpoint(Name, File) of
        ok ->
            Fun = fun(Item, Acc) -> {[Item], [item(Item) | Acc]} end,
            {ok, Items} = mnesia:traverse_backup(File, mnesia_backup, dummy, read_only, Fun, []),
            {ok, lists:sort(Items)};
        Error ->
            Error
    end.

item({schema, Tab, Def}) ->
    {schema, Tab, lists:sort(proplists:get_value(disc_copies, Def)), lists:sort(proplists:get_value(ram_copies, Def))};
item(Item) ->
    Item.

%% The system events since the last look, kept by a process of this node.
listen() ->
    Self = self(),
    Pid = spawn(fun() -> listening(Self) end),
    receive
        {Pid, subscribed} -> ok
    end.

listening(Parent) ->
    is_pid(whereis(mrc_events)) andalso unregister(mrc_events),
    register(mrc_events, self()),
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
    mrc_events ! {self(), heard},
    receive
        {heard, Events} -> Events
    after 1000 -> no_listener
    end.

%% The events heard until Event comes.
events_until(Event, N) ->
    events_until(Event, N, []).

events_until(Event, 0, Heard) ->
    {missing, Event, Heard};
events_until(Event, N, Heard) ->
    Events = Heard ++ heard(),
    case lists:member(Event, Events) of
        true ->
            Events;
        false ->
            timer:sleep(100),
            events_until(Event, N - 1, Events)
    end.

%% b's end, which a sees as {badrpc, nodedown}.
finish() ->
    io:format(user, "mnesia_remote_checkpoints: ok~n", []),
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
