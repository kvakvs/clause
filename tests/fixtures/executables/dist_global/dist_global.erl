%% global across nodes (plan 30 step DIST8a, docs/distribution.md#global): c joins b first, then a joins b, and global
%% connects a and c too. Names registered in both partitions before a joined meet: a, the smaller node, resolves each
%% conflict with its registration's method. Locks hold on every node, the names of an ended process go everywhere, and
%% when a disconnects c, b removes c to prevent overlapping partitions (c itself runs without that prevention, so the
%% outcome does not depend on which report comes first). Only exported functions travel, so any node may be an OTP node.
-module(dist_global).
-export([main/1]).
-export([holder/1, keep_other/3, bad_method/3, crash_method/3, inboxes/0, try_lock/1, whereis_node/1, finish/0]).

-define(CONFLICTS, [cx_all, cx_bad, cx_crash, cx_exit, cx_notify, cx_user]).

main(["a"]) ->
    B = 'drgb@localhost',
    C = 'drgc@localhost',
    wait_registered("drgc_ready"),
    Holders = register_conflicts([
        {cx_exit, fun global:random_exit_name/3},
        {cx_notify, fun global:random_notify_name/3},
        {cx_all, fun global:notify_all_name/3},
        {cx_user, fun ?MODULE:keep_other/3},
        {cx_bad, fun ?MODULE:bad_method/3},
        {cx_crash, fun ?MODULE:crash_method/3}
    ]),
    yes = global:register_name(ga, spawn(?MODULE, holder, [[]])),
    show("ping", net_adm:ping(B)),
    show("sync", global:sync()),
    wait_until(fun() -> lists:member(C, nodes()) end),
    show("sync_all", global:sync()),
    show("nodes", lists:sort(nodes())),
    show("locker", is_pid(global:get_locker())),
    names(B, C),
    conflicts(B, C, Holders),
    locks(B),
    ended_names(B),
    partition(B, C),
    show("disconnect", global:disconnect()),
    show("halted", rpc:call(B, ?MODULE, finish, [])),
    done();
main(["b"]) ->
    Holders = register_conflicts([{Name, fun global:random_exit_name/3} || Name <- ?CONFLICTS]),
    register(drg_holders, spawn(fun() -> keeper(Holders) end)),
    yes = global:register_name(gb, spawn(?MODULE, holder, [[]])),
    _Ready = mark("drgb_ready"),
    receive
    after infinity -> ok
    end;
main(["c"]) ->
    B = 'drgb@localhost',
    wait_registered("drgb_ready"),
    ok = net_kernel:monitor_nodes(true),
    show("ping", net_adm:ping(B)),
    show("sync", global:sync()),
    yes = global:register_name(gc, spawn(?MODULE, holder, [[]])),
    _Ready = mark("drgc_ready"),
    % a connects through global, then a disconnects c and b removes it.
    wait_down(['drga@localhost', B]),
    show("sync_alone", global:sync()),
    show("isolated", nodes()),
    show("names_alone", lists:sort(global:registered_names())),
    done().

%% The names a and b registered before they met, each held by its own process: a's methods resolve the conflicts.
register_conflicts(Methods) ->
    [
        begin
            Pid = spawn(?MODULE, holder, [[]]),
            yes = global:register_name(Name, Pid, Method),
            {Name, Pid}
        end
     || {Name, Method} <- Methods
    ].

%% The process of a name: it keeps the messages it gets until asked for them.
holder(Inbox) ->
    receive
        {report, From} ->
            From ! {inbox, self(), lists:reverse(Inbox)},
            holder(Inbox);
        Message ->
            holder([Message | Inbox])
    end.

%% b's holders of the conflicting names, kept for a's questions.
keeper(Holders) ->
    receive
        {holders, From} ->
            From ! {holders, Holders},
            keeper(Holders)
    end.

%% Resolve methods: a's registration chooses b's pid, returns no pid, or fails.
keep_other(_Name, Other, _Mine) -> Other.

bad_method(_Name, _Other, _Mine) -> bad.

crash_method(_Name, _Other, _Mine) -> error(crashed).

names(B, C) ->
    show("whereis", [whereis_node(Name) || Name <- [ga, gb, gc, nobody]]),
    show("whereis_b", rpc:call(B, ?MODULE, whereis_node, [ga])),
    show("whereis_c", rpc:call(C, ?MODULE, whereis_node, [gb])),
    show("names", lists:sort(global:registered_names())),
    show("send", node(global:send(gb, hello))),
    show("send_unknown", attempt(fun() -> global:send(nobody, hello) end)).

whereis_node(Name) ->
    case global:whereis_name(Name) of
        undefined -> undefined;
        Pid -> node(Pid)
    end.

%% Each conflict as a, b and c see it, and the messages its holders got (pids as their nodes).
conflicts(B, C, Holders) ->
    View = fun(Node) -> [{Name, rpc:call(Node, ?MODULE, whereis_node, [Name])} || Name <- ?CONFLICTS] end,
    show("conflicts", View(node())),
    show("conflicts_b", View(B) =:= View(node())),
    show("conflicts_c", View(C) =:= View(node())),
    show("inboxes", inboxes(Holders)),
    show("inboxes_b", rpc:call(B, ?MODULE, inboxes, [])).

inboxes() ->
    drg_holders ! {holders, self()},
    receive
        {holders, Holders} -> inboxes(Holders)
    end.

inboxes(Holders) -> [{Name, inbox(Pid)} || {Name, Pid} <- Holders].

inbox(Pid) ->
    Ref = monitor(process, Pid),
    Pid ! {report, self()},
    receive
        {inbox, Pid, Messages} ->
            demonitor(Ref, [flush]),
            [nodes_of(Message) || Message <- Messages];
        {'DOWN', Ref, process, Pid, Reason} ->
            {ended, Reason}
    end.

nodes_of({global_name_conflict, Name, Pid}) -> {global_name_conflict, Name, node(Pid)};
nodes_of(Message) -> Message.

%% A lock a holds on every node: b's requester is refused until a lets go.
locks(B) ->
    Id = {drg_lock, self()},
    show("set_lock", global:set_lock(Id)),
    show("set_lock_again", global:set_lock(Id, [node() | nodes()], 0)),
    show("refused_b", rpc:call(B, ?MODULE, try_lock, [refused])),
    show("trans_b", rpc:call(B, global, trans, [{drg_lock, b}, fun erlang:node/0, [node(), B], 0])),
    show("del_lock", global:del_lock(Id)),
    show("granted_b", rpc:call(B, ?MODULE, try_lock, [granted])),
    show("trans", global:trans({drg_lock, a}, fun() -> nodes() =/= [] end)),
    Self = self(),
    Holder = spawn(fun() -> Self ! {locked, self(), global:set_lock({drg_held, self()})} end),
    show("held", receive_locked(Holder)),
    % The lock goes with its holder on every node.
    show("after_holder", global:set_lock({drg_held, a}, [node() | nodes()], infinity)),
    global:del_lock({drg_held, a}).

try_lock(Tag) -> {Tag, global:set_lock({drg_lock, Tag}, [node() | nodes()], 0)}.

receive_locked(Holder) ->
    receive
        {locked, Holder, Result} -> Result
    after 5000 -> timeout
    end.

%% A name goes on every node when its process ends.
ended_names(B) ->
    Pid = spawn(?MODULE, holder, [[]]),
    yes = global:register_name(gtemp, Pid),
    show("temp_b", rpc:call(B, ?MODULE, whereis_node, [gtemp])),
    exit(Pid, kill),
    wait_until(fun() -> rpc:call(B, ?MODULE, whereis_node, [gtemp]) =:= undefined end),
    show("temp_gone", [whereis_node(gtemp), rpc:call(B, ?MODULE, whereis_node, [gtemp])]).

%% a disconnects c: b hears of the lost connection and removes c, so that a and b form one partition without it.
partition(B, C) ->
    monitor_node(C, true),
    true = erlang:disconnect_node(C),
    receive
        {nodedown, C} -> ok
    end,
    wait_until(fun() -> rpc:call(B, erlang, nodes, []) =:= [node()] end),
    show("partition", [nodes(), rpc:call(B, erlang, nodes, [])]),
    show("sync_partition", global:sync()),
    show("names_partition", lists:sort(global:registered_names())).

%% b's end: its standard output is its own, whatever the caller's group leader is.
finish() ->
    io:format(user, "dist_global: ok~n", []),
    erlang:halt().

show(Label, Value) -> io:format("~s: ~p~n", [Label, Value]).

attempt(Fun) ->
    try
        Fun()
    catch
        Class:Reason -> {Class, Reason}
    end.

wait_down([]) ->
    ok;
wait_down(Nodes) ->
    receive
        {nodedown, Node} -> wait_down(lists:delete(Node, Nodes))
    end.

wait_until(Condition) -> wait_until(Condition, 500).

wait_until(_, 0) ->
    timeout;
wait_until(Condition, Tries) ->
    case Condition() of
        true ->
            ok;
        false ->
            timer:sleep(20),
            wait_until(Condition, Tries - 1)
    end.

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

done() -> io:format("dist_global: ok~n").
