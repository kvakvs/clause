%% global_group across nodes (plan 30 step DIST8b, docs/distribution.md#global_group): a and b form group g1, c group
%% g2 (kernel's global_groups). a and b check each other's group before global syncs them; a's connection to c stays,
%% but global keeps c's names apart, and global_group finds and reaches them by searching the other group. Only
%% exported functions travel, so any node may be an OTP node.
-module(dist_global_group).
-export([main/1]).
-export([holder/1, inbox/1, finish/0]).

main(["a"]) ->
    B = 'ggb@localhost',
    C = 'ggc@localhost',
    wait_registered("ggb_ready"),
    wait_registered("ggc_ready"),
    yes = global:register_name(na, spawn(?MODULE, holder, [[]])),
    show("groups", global_group:global_groups()),
    show("own_nodes", global_group:own_nodes()),
    show("monitor_nodes", global_group:monitor_nodes(true)),
    % Each node asks its group's nodes for their groups at its start, so b may be connected already.
    show("ping_b", net_adm:ping(B)),
    show("synced", wait_synced(B, 250)),
    show("sync", global:sync()),
    show("ping_c", net_adm:ping(C)),
    show("connected", lists:sort(nodes())),
    show("global_names", lists:sort(global:registered_names())),
    show("global_whereis", [node_of(global:whereis_name(Name)) || Name <- [nb, nc]]),
    searches(B, C),
    show("sync_group", global_group:sync()),
    show("info", info_view()),
    show("halted_c", rpc:call(C, ?MODULE, finish, [])),
    show("halted_b", rpc:call(B, ?MODULE, finish, [])),
    % Only the group's nodes are reported.
    show("nodedown", receive_nodedown()),
    show("unexpected", [Message || Message <- receive_all(), Message =/= {nodeup, B}]),
    done();
main(["b"]) ->
    yes = global:register_name(nb, spawn(?MODULE, holder, [[]])),
    _Ready = mark("ggb_ready"),
    receive
    after infinity -> ok
    end;
main(["c"]) ->
    yes = global:register_name(nc, spawn(?MODULE, holder, [[]])),
    _Ready = mark("ggc_ready"),
    receive
    after infinity -> ok
    end.

%% Names, sends and searches in this node's group, the other group and on nodes.
searches(B, C) ->
    show("whereis", [
        node_of(global_group:whereis_name(nc)),
        node_of(global_group:whereis_name({group, g2}, nc)),
        node_of(global_group:whereis_name({node, C}, nc)),
        node_of(global_group:whereis_name({group, g1}, nb)),
        global_group:whereis_name({group, g3}, nc),
        global_group:whereis_name(nobody)
    ]),
    show("names", [
        global_group:registered_names({node, C}),
        lists:sort(global_group:registered_names({group, g1})),
        global_group:registered_names({group, g2}),
        global_group:registered_names({group, g3})
    ]),
    show("send", [
        node_of(global_group:send(nc, hello)),
        node_of(global_group:send({group, g2}, nc, hi)),
        global_group:send({group, g2}, nobody, lost),
        node_of(global_group:send({node, B}, nb, hey))
    ]),
    show("told_c", rpc:call(C, ?MODULE, inbox, [nc])),
    show("told_b", rpc:call(B, ?MODULE, inbox, [nb])).

%% info/0 with the subscribers counted: their pids differ between runs.
info_view() ->
    [
        case Item of
            {monitoring, Pids} -> {monitoring, length(Pids)};
            _ -> Item
        end
     || Item <- global_group:info()
    ].

node_of(Pid) when is_pid(Pid) -> node(Pid);
node_of(Other) -> Other.

%% The process of a name: it keeps the messages it gets until asked for them.
holder(Inbox) ->
    receive
        {report, From} ->
            From ! {inbox, self(), lists:reverse(Inbox)},
            holder(Inbox);
        Message ->
            holder([Message | Inbox])
    end.

inbox(Name) ->
    Pid = global:whereis_name(Name),
    Pid ! {report, self()},
    receive
        {inbox, Pid, Messages} -> Messages
    after 5000 -> timeout
    end.

%% A node's end: its standard output is its own, whatever the caller's group leader is.
finish() ->
    io:format(user, "dist_global_group: ok~n", []),
    erlang:halt().

show(Label, Value) -> io:format("~s: ~p~n", [Label, Value]).

% The group's nodedown; a nodeup of b's connection may come first, depending on when b connected.
receive_nodedown() ->
    receive
        {nodedown, _} = Message -> Message
    after 5000 -> none
    end.

wait_synced(_, 0) ->
    timeout;
wait_synced(Node, Tries) ->
    {synced_nodes, Synced} = lists:keyfind(synced_nodes, 1, global_group:info()),
    case lists:member(Node, Synced) of
        true ->
            true;
        false ->
            timer:sleep(20),
            wait_synced(Node, Tries - 1)
    end.

% Messages that arrive within 400 ms.
receive_all() ->
    receive
        Message -> [Message | receive_all()]
    after 400 -> []
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

done() -> io:format("dist_global_group: ok~n").
