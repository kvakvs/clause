%% TLS distribution (plan 30 step DIST12a): nodes a, b and c run inet_tls_dist with an options file each. a and b hold
%% certificates for localhost from one root; c holds one for otherhost from the same root, so its chain is valid but
%% b, which allows only a@localhost, refuses it. a checks inet_tls_dist's functions, connects to b, probes b's server
%% with raw TLS clients (no certificate, another root's certificate), and connects to a fake node whose certificate
%% names otherhost; a's server option verify_fun is inet_tls_dist:verify_client/3. Nodes 6a and 6b connect over
%% inet6_tls_dist; nodes oa and ob take their ssl options from --ssl-dist-opt. Epmd names Node_ready tell the nodes
%% that the others are ready.
-module(dist_tls).
-export([main/1]).

-define(PROBE, [{versions, ['tlsv1.2']}, {verify, verify_none}, {log_level, none}, {active, false}]).

main(["a"]) ->
    ok = net_kernel:monitor_nodes(true),
    functions(),
    show("children", [Id || {Id, _, _, _} <- supervisor:which_children(net_sup)]),
    _Ready = mark("dta_ready"),
    B = peer("dtb_ready", "dtb"),
    connection(B),
    probes(),
    fake_node(),
    C = node_of("dtc"),
    show(
        "c up",
        receive
            {nodeup, C} -> C
        after 60000 -> timeout
        end
    ),
    show(
        "c down",
        receive
            {nodedown, C} -> C
        after 60000 -> timeout
        end
    ),
    stop(B);
main(["b"]) ->
    ok = net_kernel:allow([node_of("dta")]),
    show("allowed", net_kernel:allowed()),
    serve("dtb_ready");
main(["c"]) ->
    B = peer("dtb_ready", "dtb"),
    A = peer("dta_ready", "dta"),
    show("ping b", net_adm:ping(B)),
    show("ping a", net_adm:ping(A)),
    show("nodes", nodes()),
    done();
main(["6a"]) ->
    B = peer("dt6b_ready", "dt6b"),
    show("select", [inet6_tls_dist:select(B), inet6_tls_dist:address()]),
    show("ping", net_adm:ping(B)),
    {ok, Address} = net_kernel:node_info(B, address),
    show("address", [element(N, Address) || N <- [3, 4, 5]]),
    show("rpc", rpc:call(B, erlang, node, [])),
    stop(B);
main(["6b"]) ->
    serve("dt6b_ready");
main(["oa"]) ->
    show("client options", inet_tls_dist:get_ssl_client_options()),
    show("server options", inet_tls_dist:get_ssl_server_options({127, 0, 0, 1})),
    B = peer("dtob_ready", "dtob"),
    show("ping", net_adm:ping(B)),
    stop(B);
main(["ob"]) ->
    serve("dtob_ready").

%% The carrier's own functions: names, addresses, certificate names and the options of the file.
functions() ->
    show("select", [inet_tls_dist:select(node_of("dtb")), inet_tls_dist:select(x)]),
    show("address", inet_tls_dist:address()),
    show("is_node_name", [inet_tls_dist:is_node_name("x@h"), inet_tls_dist:is_node_name("x")]),
    show("childspecs", inet_tls_dist:childspecs()),
    [Node, Other, Root] = [certificate(File) || File <- ["node.pem", "otherhost.pem", "ca.pem"]],
    show("cert_nodes", [inet_tls_dist:cert_nodes(Cert) || Cert <- [Node, Other, Root]]),
    show("allowed_hosts", inet_tls_dist:allowed_hosts(['n@h1', h2, 'n@', 'x@h1', "s@h3"])),
    show("verify_client", [
        inet_tls_dist:verify_client(Node, {bad_cert, unknown_ca}, s),
        inet_tls_dist:verify_client(Node, {extension, x}, s),
        inet_tls_dist:verify_client(Node, valid, s),
        inet_tls_dist:verify_client(Node, valid_peer, {[], {10, 0, 0, 1}}),
        inet_tls_dist:verify_client(Node, valid_peer, {["localhost"], {10, 0, 0, 1}}),
        inet_tls_dist:verify_client(Node, valid_peer, {["otherhost"], {127, 0, 0, 1}}),
        inet_tls_dist:verify_client(Other, valid_peer, {["localhost"], {127, 0, 0, 1}})
    ]),
    show("client options", inet_tls_dist:get_ssl_client_options()),
    show("server options", inet_tls_dist:get_ssl_server_options({127, 0, 0, 1})).

certificate(File) ->
    {ok, Pem} = file:read_file(File),
    [{'Certificate', Der, not_encrypted} | _] = public_key:pem_decode(Pem),
    public_key:pkix_decode_cert(Der, otp).

%% A connection to b: up over TLS, with its address, options and messages.
connection(B) ->
    show("ping b", net_adm:ping(B)),
    {ok, Address} = net_kernel:node_info(B, address),
    show("address", [element(N, Address) || N <- [3, 4, 5]]),
    show("getopts", net_kernel:getopts(B, [nodelay])),
    show("setopts", net_kernel:setopts(B, [{packet, 2}, {nodelay, true}])),
    show("rpc", rpc:call(B, erlang, node, [])),
    Big = binary:copy(<<"tls">>, 100000),
    show("big", rpc:call(B, erlang, iolist_to_binary, [[Big, Big]]) =:= <<Big/binary, Big/binary>>).

%% Raw TLS 1.2 clients at b's distribution port: b's server wants a certificate of its root. Unlike distribution,
%% they need the ssl application.
probes() ->
    {ok, _} = application:ensure_all_started(ssl),
    {port, Port, _} = erl_epmd:port_please("dtb", localhost),
    show("no certificate", alert(ssl:connect({127, 0, 0, 1}, Port, ?PROBE, 10000))),
    Other = [{certfile, "other_node.pem"} | ?PROBE],
    show("other root", refused(ssl:connect({127, 0, 0, 1}, Port, Other, 10000))).

alert({error, {tls_alert, {Description, _}}}) -> {tls_alert, Description};
alert(Other) -> Other.

% Whether a server refused a client: during the handshake, or right after it, where a server over libssl before 4.1
% verifies client certificates (docs/differences.md#ssl).
refused({error, _}) ->
    refused;
refused({ok, Socket}) ->
    case ssl:recv(Socket, 0, 10000) of
        {error, _} -> refused;
        Other -> Other
    end.

%% A fake node dtfake whose server certificate names otherhost: a's client refuses it.
fake_node() ->
    Options = [{certfile, "otherhost.pem"}, {ip, {127, 0, 0, 1}}, {log_level, none}, {active, false}],
    {ok, Listen} = ssl:listen(0, Options),
    {ok, {_, Port}} = ssl:sockname(Listen),
    Self = self(),
    spawn(fun() -> fake_server(Self, Listen) end),
    Fake = mark("dtfake", Port),
    show("fake node", net_adm:ping(node_of("dtfake"))),
    show(
        "fake server",
        receive
            {fake, Result} -> Result
        after 30000 -> timeout
        end
    ),
    gen_tcp:close(Fake),
    ssl:close(Listen).

fake_server(Parent, Listen) ->
    {ok, Socket} = ssl:transport_accept(Listen, 30000),
    Parent ! {fake, alert(ssl:handshake(Socket, 30000))}.

%% A served node: registers dist_tls_b, marks itself ready and ends when stopped.
serve(Marker) ->
    register(dist_tls_b, self()),
    _Ready = mark(Marker),
    receive
        stop -> done()
    end.

% Stop node B's served process and wait until it ends: a program's end loses queued packets.
stop(B) ->
    Monitor = erlang:monitor(process, {dist_tls_b, B}),
    {dist_tls_b, B} ! stop,
    receive
        {'DOWN', Monitor, process, _, _} -> done()
    end.

% Register Name with epmd, at Port, for as long as the socket stays open.
mark(Name) -> mark(Name, 1).

mark(Marker, Port) ->
    Name = list_to_binary(Marker),
    EpmdPort = list_to_integer(os:getenv("ERL_EPMD_PORT", "4369")),
    {ok, Socket} = gen_tcp:connect({127, 0, 0, 1}, EpmdPort, [binary, {active, false}]),
    Request = <<120, Port:16, 77, 0, 6:16, 5:16, (byte_size(Name)):16, Name/binary, 0:16>>,
    ok = gen_tcp:send(Socket, [<<(byte_size(Request)):16>>, Request]),
    {ok, <<_, 0, _/binary>>} = gen_tcp:recv(Socket, 0, 5000),
    Socket.

% Node Name once Marker is registered with epmd.
peer(Marker, Name) ->
    wait_registered(Marker),
    node_of(Name).

% Test nodes are named @localhost: they listen on loopback only.
node_of(Name) -> list_to_atom(Name ++ "@localhost").

wait_registered(Name) ->
    {ok, Names} = net_adm:names(localhost),
    case lists:keymember(Name, 1, Names) of
        true ->
            ok;
        false ->
            timer:sleep(10),
            wait_registered(Name)
    end.

done() -> io:format("dist_tls: ok~n").

% The host name (in carrier addresses) is HOST in the output.
show(Label, Term) ->
    {ok, Host} = inet:gethostname(),
    Text = lists:flatten(io_lib:format("~p", [Term])),
    io:format("~s: ~s~n", [Label, string:replace(Text, Host, "HOST", all)]).
