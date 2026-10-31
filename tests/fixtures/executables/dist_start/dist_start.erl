%% Starting and stopping distribution in a running program with net_kernel:start/1,2 and stop/0, as OTP's: node names,
%% the epmd registration, the start options, start errors and OTP's log lines (plan 30 step DIST4b1a).
-module(dist_start).
-export([main/1, log/2]).

main(_) ->
    % Log events are printed briefly, without times and pids.
    ok = logger:add_handler(dist_start, ?MODULE, #{}),
    ok = logger:remove_handler(default),
    show("before", {node(), is_alive(), net_kernel:get_state()}),
    show("stop before", net_kernel:stop()),
    show("requests before", requests()),
    {ok, Sup} = net_kernel:start([alpha, shortnames]),
    show("started", {is_pid(Sup), Sup =:= whereis(net_sup), is_pid(whereis(net_kernel)), is_pid(whereis(erl_epmd))}),
    show("node", {node(), is_alive(), node(self()), is_integer(erts_internal:get_creation())}),
    show("names", names()),
    show("state", net_kernel:get_state()),
    show("requests", requests()),
    show("again", already(net_kernel:start([alpha, shortnames]), Sup)),
    show("other", already(net_kernel:start(gamma, #{}), Sup)),
    show("connect", net_kernel:connect_node(nobody@localhost)),
    show("monitor", monitor_down(nobody@localhost)),
    show("stop", net_kernel:stop()),
    show("stopped", {node(), is_alive(), net_kernel:get_state(), whereis(net_sup), whereis(net_kernel)}),
    show("names after stop", unregistered("alpha")),
    show("stop again", net_kernel:stop()),
    options(),
    node_names(),
    refused(),
    io:format("dist_start: ok~n").

%% start/2's options and start/1's tick time, then the kernel parameter net_ticktime.
options() ->
    started("options", beta, #{name_domain => shortnames, net_ticktime => 30, hidden => true}, fun() ->
        {net_kernel:get_net_ticktime(), names()}
    end),
    started("tick time", [t3, shortnames, 1000], fun net_kernel:get_net_ticktime/0),
    started("no listen", lf, #{name_domain => shortnames, dist_listen => false}, fun() ->
        {node(), is_alive(), names()}
    end),
    started("dynamic name", undefined, #{name_domain => shortnames}, fun() ->
        {node(), is_alive(), erts_internal:get_creation(), net_kernel:get_state(), names()}
    end),
    show("dynamic name stopped", is_alive()),
    ok = application:set_env(kernel, net_ticktime, 17),
    started("kernel tick time", tk, #{name_domain => shortnames}, fun net_kernel:get_net_ticktime/0),
    ok = application:unset_env(kernel, net_ticktime).

%% Names as OTP derives them: a host part is taken as given, a missing one is this host's name.
node_names() ->
    started("localhost", [a@localhost, shortnames], fun node/0),
    started("long", ['a@127.0.0.1', longnames], fun node/0),
    started("letters", ['A_b-1', shortnames], fun node/0),
    started("empty host", ['a@', shortnames], fun node/0).

%% Starts OTP refuses: bad arguments raise, bad names and taken names or ports fail with OTP's log line.
refused() ->
    show("bad domain", raised(fun() -> net_kernel:start([foo, bad]) end)),
    show("bad option", raised(fun() -> net_kernel:start(foo, #{bad => 1}) end)),
    show("bad tick", raised(fun() -> net_kernel:start(foo, #{net_ticktime => -1}) end)),
    show("bad intensity", raised(fun() -> net_kernel:start(foo, #{net_tickintensity => 3}) end)),
    show("string name", raised(fun() -> net_kernel:start("foo", #{}) end)),
    show("not a map", raised(fun() -> net_kernel:start(foo, []) end)),
    show("empty list", raised(fun() -> net_kernel:start([]) end)),
    show("zero tick", raised(fun() -> net_kernel:start([t5, shortnames, 0]) end)),
    failed("dotted name", ['a.b', shortnames]),
    failed("dotted host", ['a@x.y', shortnames]),
    failed("empty name", ['', shortnames]),
    failed("space", ['a b', shortnames]),
    failed("two hosts", ['a@b@c', shortnames]),
    % A name epmd holds for another node, on a connection kept open.
    Taken = register_name(<<"taken">>),
    failed("taken", [taken, shortnames]),
    gen_tcp:close(Taken),
    {ok, Listen} = gen_tcp:listen(0, []),
    {ok, Port} = inet:port(Listen),
    ok = application:set_env(kernel, inet_dist_listen_min, Port),
    ok = application:set_env(kernel, inet_dist_listen_max, Port),
    failed("port taken", [pt, shortnames]),
    gen_tcp:close(Listen),
    started("port range", [pr, shortnames], fun() -> names() =:= [{"pr", Port}] orelse names() end),
    ok = application:unset_env(kernel, inet_dist_listen_min),
    ok = application:unset_env(kernel, inet_dist_listen_max),
    % The carrier takes no address family from inet_dist_listen_options (DIST4b2c).
    ok = application:set_env(kernel, inet_dist_listen_options, [inet]),
    failed("listen family", [lfam, shortnames]),
    ok = application:unset_env(kernel, inet_dist_listen_options),
    show("after refused", {node(), is_alive(), whereis(net_sup), whereis(net_kernel)}).

% Start, show what Fun answers while started, then stop.
started(Label, Options, Fun) -> started(Label, Options, none, Fun).

started(Label, Name, Options, Fun) ->
    Started =
        case Options of
            none -> net_kernel:start(Name);
            _ -> net_kernel:start(Name, Options)
        end,
    case Started of
        {ok, _} ->
            show(Label, Fun()),
            show(Label ++ " stop", net_kernel:stop());
        Error ->
            show(Label, Error)
    end.

% A start that fails: the reason net_sup gives, and the shape of OTP's net_sup_dynamic child.
failed(Label, Arguments) ->
    case net_kernel:start(Arguments) of
        {error,
            {Reason,
                {child, undefined, net_sup_dynamic, {erl_distribution, start_link, [Map]}, permanent, false, 1000,
                    supervisor, [erl_distribution]}}} ->
            show(Label, {Reason, lists:sort(maps:keys(Map))});
        Other ->
            show(Label, Other)
    end.

raised(Fun) ->
    try Fun() of
        Result -> {returned, Result}
    catch
        Class:Reason -> {Class, Reason}
    end.

already({error, {already_started, Pid}}, Sup) -> {already_started, Pid =:= Sup};
already(Other, _) -> Other.

requests() ->
    [net_kernel:nodename(), net_kernel:longnames(), net_kernel:get_net_ticktime(), net_kernel:connect_node(x@y)].

% A node monitor of a node that cannot be reached gets nodedown.
monitor_down(Node) ->
    true = monitor_node(Node, true),
    receive
        {nodedown, Node} -> nodedown
    after 10000 -> timeout
    end.

% The names epmd holds, with their ports.
names() ->
    {ok, Names} = erl_epmd:names(),
    lists:sort(Names).

% Whether epmd let go of Name; it may see the closed registration a little later.
unregistered(Name) -> unregistered(Name, 100).

unregistered(Name, Attempts) ->
    case lists:keymember(Name, 1, names()) of
        false ->
            true;
        true when Attempts > 1 ->
            timer:sleep(20),
            unregistered(Name, Attempts - 1);
        true ->
            false
    end.

% Register Name with epmd on a connection of its own, as another node would.
register_name(Name) ->
    {ok, Socket} = gen_tcp:connect({127, 0, 0, 1}, list_to_integer(os:getenv("ERL_EPMD_PORT")), [
        binary, {active, false}
    ]),
    Request = <<120, 5000:16, $M, 0, 6:16, 5:16, (byte_size(Name)):16, Name/binary, 0:16>>,
    ok = gen_tcp:send(Socket, [<<(byte_size(Request)):16>>, Request]),
    {ok, <<_, 0, _/binary>>} = gen_tcp:recv(Socket, 0),
    Socket.

% Maps print in key order (~kp), as OTP prints atom keys in an order that varies.
show(Label, Term) -> io:format("~s: ~s~n", [Label, masked(io_lib:format("~kp", [Term]))]).

% The host name is HOST and ports are PORT in the output.
masked(Text) ->
    {ok, Host} = inet:gethostname(),
    Masked = string:replace(lists:flatten(Text), Host, "HOST", all),
    re:replace(Masked, "\"(alpha|beta|pr)\",[0-9]+", "\"\\1\",PORT", [global, {return, list}]).

% The logger handler: info texts in full, reports by their label.
log(#{level := Level, msg := Msg}, _) ->
    Text =
        case Msg of
            {report, #{label := {error_logger, _}, format := Format, args := Args}} -> io_lib:format(Format, Args);
            {report, #{label := Label}} -> io_lib:format("report ~p", [Label]);
            {report, Report} -> io_lib:format("report ~p", [Report]);
            {string, String} -> String;
            {Format, Args} -> io_lib:format(Format, Args)
        end,
    io:format("log ~p: ~s~n", [Level, masked(Text)]).
