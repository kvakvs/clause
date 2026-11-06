%% The queries another node's mnesia makes into a node through rpc and mnesia_rpc (plan 21 step MIM1H3,
%% docs/libraries/mnesia-protocol.md#remote-queries-mim1h3): node a asks node b whether mnesia runs, for its node
%% number, its schema's source, its table definitions and records, before b creates a disc schema, with a fallback,
%% while b runs and after b stops; a also asks through b's mnesia_rpc server, lists b's mnesia processes, and checks b's
%% schema with create_schema/1 and delete_schema/1. Only exported functions travel, so either node may be an OTP node.
-module(mnesia_remote_queries).
-export([main/1]).
-export([setup/0, children/1, checkpoint_children/0, finish/0]).

-define(NOBODY, 'nobody@localhost').

main(["a"]) ->
    B = 'mrqb@localhost',
    wait_registered("mrqb_ready"),
    pong = net_adm:ping(B),
    ok = application:load(mnesia),
    queries(fresh, B),
    show("create_schema", rpc:call(B, mnesia, create_schema, [[B]])),
    show("fallback_source", hide(rpc:call(B, mnesia_schema, remote_read_schema, []))),
    show("schema_exists", mnesia:create_schema([B])),
    show("schema_unknown_node", mnesia:create_schema([?NOBODY])),
    ok = rpc:call(B, ?MODULE, setup, []),
    queries(running, B),
    records(B),
    rpc_server(B),
    processes(B),
    show("delete_running", mnesia:delete_schema([B])),
    show("stop", rpc:call(B, mnesia, stop, [])),
    queries(stopped, B),
    show("delete_schema", mnesia:delete_schema([B])),
    show("deleted_source", hide(rpc:call(B, mnesia_schema, remote_read_schema, []))),
    show("finished", rpc:call(B, ?MODULE, finish, [])),
    io:format("mnesia_remote_queries: ok~n");
main(["b"]) ->
    ok = application:load(mnesia),
    _ = file:del_dir_r("remote_b.db"),
    ok = application:set_env(mnesia, dir, "remote_b.db"),
    _Ready = mark("mrqb_ready"),
    receive
    after infinity -> ok
    end.

%% Node b's tables of each storage, with records: r in RAM, s ordered on disc, o a disc-only bag.
setup() ->
    ok = mnesia:start(),
    {atomic, ok} = mnesia:create_table(r, [{ram_copies, [node()]}, {index, [val]}]),
    {atomic, ok} = mnesia:create_table(s, [{disc_copies, [node()]}, {type, ordered_set}]),
    {atomic, ok} = mnesia:create_table(o, [{disc_only_copies, [node()]}, {type, bag}]),
    [ok = mnesia:dirty_write(R) || R <- [{r, 1, one}, {s, 1, a}, {s, 2, b}, {s, 3, c}, {o, 1, x}, {o, 1, y}]],
    ok.

%% What a peer asks a node in each state of its mnesia.
queries(State, B) ->
    io:format("-- ~p~n", [State]),
    show("is_running", rpc:call(B, mnesia_lib, is_running, [])),
    show("is_running_remote", rpc:call(B, mnesia_lib, is_running_remote, [])),
    show("node_number", node_number(rpc:call(B, mnesia_lib, get_node_number, []))),
    show("schema_source", hide(rpc:call(B, mnesia_schema, remote_read_schema, []))),
    show("cstructs", hide(rpc:call(B, mnesia_controller, get_remote_cstructs, []))),
    show("db_get", rpc:call(B, mnesia_lib, db_get, [r, 1])).

node_number({Node, Pid}) when is_pid(Pid) -> {Node, node(Pid)};
node_number(Other) -> Other.

%% Records read with mnesia_lib:db_get/2, as a dirty read of a table held elsewhere asks for them.
records(B) ->
    Gets = [[r, 1], [r, 2], [s, 2], [o, 1], [nosuch, 1]],
    show("db_gets", [rpc:call(B, mnesia_lib, db_get, Args) || Args <- Gets]).

%% Requests to b's mnesia_rpc server: table reads answered at once or by a process of their own, and other applies.
rpc_server(B) ->
    Applies = [
        {mnesia_lib, db_get, [r, 1]},
        {mnesia_lib, db_get, [o, 1]},
        {mnesia_lib, db_get, [nosuch, 1]},
        {mnesia_lib, db_first, [s]},
        {mnesia_lib, db_next_key, [s, 1]},
        {mnesia_lib, db_next_key, [s, 3]},
        {mnesia_lib, db_last, [s]},
        {mnesia_lib, db_prev_key, [s, 3]},
        {mnesia_lib, db_first, [o]},
        {erlang, node, []},
        {erlang, error, [boom]},
        {erlang, exit, [bye]},
        {erlang, throw, [ball]}
    ],
    show("applies", [gen_server:call({mnesia_rpc, B}, {apply, M, F, A}) || {M, F, A} <- Applies]),
    show("rpc_call", mnesia_rpc:call(B, erlang, node, [])).

%% b's mnesia processes as OTP names them: registered names and supervisor children.
processes(B) ->
    Names = [N || N <- rpc:call(B, erlang, registered, []), lists:prefix("mnesia", atom_to_list(N))],
    show("registered", lists:sort(Names)),
    show("mnesia_sup", rpc:call(B, ?MODULE, children, [mnesia_sup])),
    show("mnesia_kernel_sup", rpc:call(B, ?MODULE, children, [mnesia_kernel_sup])),
    show("checkpoint_sup", rpc:call(B, ?MODULE, checkpoint_children, [])).

children(Sup) -> [{Id, Type, Modules} || {Id, _, Type, Modules} <- supervisor:which_children(Sup)].

checkpoint_children() ->
    {ok, Name, _} = mnesia:activate_checkpoint([{max, [s]}]),
    Children = children(mnesia_checkpoint_sup),
    ok = mnesia:deactivate_checkpoint(Name),
    Children.

%% Cookies and creation times differ per run, and so do the frames of a failure's stack between OTP and Clause.
hide({ok, Source, List}) when is_list(List) -> {ok, Source, hide_list(List)};
hide({cstructs, Cstructs, Running}) -> {cstructs, [hide_cs(Cs) || Cs <- Cstructs], Running};
hide({badrpc, {'EXIT', {Reason, Stack}}}) when is_list(Stack) -> {badrpc, {'EXIT', {Reason, stack}}};
hide(Other) -> Other.

hide_list(List) -> [hide_property(P) || P <- List].

hide_property({cookie, _}) -> {cookie, cookie};
hide_property({version, Version}) -> {version, hide_version(Version)};
hide_property(Property) -> Property.

hide_cs(Cs) when element(1, Cs) =:= cstruct ->
    setelement(20, setelement(19, Cs, cookie), hide_version(element(20, Cs))).

hide_version({Version, {Node, _Time}}) -> {Version, {Node, time}};
hide_version(Version) -> Version.

%% b's end, which a sees as {badrpc, nodedown}.
finish() ->
    io:format(user, "mnesia_remote_queries: ok~n", []),
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
