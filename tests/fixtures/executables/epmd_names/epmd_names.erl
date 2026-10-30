%% epmd registrations and names (plan 30 step DIST3a) on the runner's epmd: two nodes that meet only through epmd.
%% Node a registers alpha with erl_epmd and waits for b's beta; b waits for alpha, registers beta, then waits until
%% only beta is left. Meanwhile a asks epmd with erl_epmd and net_adm, then with raw epmd requests: ALIVE2 replies,
%% PORT2 replies, NAMES, DUMP (descriptors masked), refused KILL and STOP and malformed requests. Creations differ
%% between epmds, so only their kind prints. The relaxed run uses an epmd with -relaxed_command_check: STOP and KILL.
-module(epmd_names).
-export([main/1]).

-define(WAIT_MS, 30000).

main(["a"]) ->
    node_a();
main(["b"]) ->
    node_b();
main(["relaxed"]) ->
    relaxed().

node_a() ->
    show("no server", catch_exit(fun() -> erl_epmd:register_node(alpha, 40001) end)),
    {ok, Server} = erl_epmd:start(),
    show("start again", erl_epmd:start_link() =:= {error, {already_started, Server}}),
    {ok, Creation} = erl_epmd:register_node(alpha, 40001),
    show("creation", is_integer(Creation) andalso Creation > 0),
    show("register again", erl_epmd:register_node(alpha2, 40002)),
    wait_names([{"alpha", 40001}, {"beta", 40003}]),
    clients(),
    raw_requests(),
    wait_names([{"alpha", 40001}, {"beta", 40003}]),
    ok = erl_epmd:stop(),
    wait_names([]),
    {ok, _} = erl_epmd:start(),
    {ok, Again} = erl_epmd:register_node(alpha, 40001),
    show("creation again", Again =/= Creation),
    ok = erl_epmd:stop(),
    io:format("epmd_names: ok~n").

node_b() ->
    wait_names([{"alpha", 40001}]),
    {ok, _} = erl_epmd:start_link(),
    {ok, _} = erl_epmd:register_node(beta, 40003, inet_tcp),
    wait_names([{"beta", 40003}]),
    show("names", erl_epmd:names()),
    show("alpha", erl_epmd:port_please(alpha, localhost)),
    io:format("epmd_names: ok~n").

%% erl_epmd's and net_adm's questions while alpha and beta are registered.
clients() ->
    show("names", [erl_epmd:names(), erl_epmd:names("localhost"), erl_epmd:names({127, 0, 0, 1})]),
    show("net_adm", [net_adm:names(), net_adm:names(localhost)]),
    show("no host", erl_epmd:names("nohost.invalid")),
    show("port_please", [
        erl_epmd:port_please(alpha, localhost),
        erl_epmd:port_please("beta", "localhost"),
        erl_epmd:port_please(beta, {127, 0, 0, 1}, 5000),
        erl_epmd:port_please(nosuch, localhost)
    ]),
    show("port_please binary", catch_error(fun() -> erl_epmd:port_please(<<"alpha">>, localhost) end)),
    show("address_please", erl_epmd:address_please(alpha, "localhost", inet)),
    show("listen_port_please", erl_epmd:listen_port_please(alpha, localhost)),
    {ok, Socket} = erl_epmd:open(),
    show("open", inet:getopts(Socket, [active, packet, mode])),
    gen_tcp:close(Socket).

%% epmd's answers to requests on raw connections.
raw_requests() ->
    Gamma = alive("gamma", 40004, $H, 6, 6, <<"xyz">>),
    show("gamma", first(Gamma)),
    Delta = alive("delta", 40005, $M, 5, 5, <<>>),
    show("delta", first(Delta)),
    Epsilon = alive("epsilon", 40006, $M, 7, 5, <<>>),
    show("epsilon", first(Epsilon)),
    show("alpha twice", reply(alive("alpha", 40007, $M, 6, 5, <<>>))),
    show("delta twice", reply(alive("delta", 40007, $M, 5, 5, <<>>))),
    show("no name", reply(alive("", 40008, $M, 6, 5, <<>>))),
    show("short alive", reply(request(packet(<<120, 1, 2>>)))),
    show("long name", reply(request(packet(<<120, 40009:16, $M, 0, 6:16, 5:16, 10:16, "ab", 0:16>>)))),
    show("names", lines(reply(request(packet(<<110>>))))),
    show("names extra", lines(reply(request(packet(<<110, 1>>))))),
    show("port gamma", reply(request(packet(<<122, "gamma">>)))),
    show("port none", reply(request(packet(<<122, "none">>)))),
    show("port no name", reply(request(packet(<<122>>)))),
    show("versions", [erl_epmd:port_please(delta, localhost), erl_epmd:port_please(epsilon, localhost)]),
    Zeta = alive("zeta", 40010, $M, 6, 5, <<>>),
    first(Zeta),
    ok = gen_tcp:send(Zeta, packet(<<110>>)),
    show("zeta after request", reply(Zeta)),
    show("dump", lines(reply(request(packet(<<100>>))))),
    show("kill", reply(request(packet(<<107>>)))),
    show("stop", reply(request(packet(<<115, "gamma">>)))),
    show("unknown", reply(request(packet(<<1, 2, 3>>)))),
    show("empty", reply(request(packet(<<>>)))),
    Split = request(<<0>>),
    timer:sleep(200),
    ok = gen_tcp:send(Split, <<1, 110>>),
    show("split", lines(reply(Split))),
    [gen_tcp:close(Socket) || Socket <- [Gamma, Delta, Epsilon]].

%% STOP and KILL of an epmd started with -relaxed_command_check, and how the linked erl_epmd server stops.
relaxed() ->
    process_flag(trap_exit, true),
    {ok, Server} = erl_epmd:start_link(),
    {ok, _} = erl_epmd:register_node(alpha, 40001),
    Beta = alive("beta", 40002, $M, 6, 5, <<>>),
    first(Beta),
    show("stop beta", reply(request(packet(<<115, "beta">>)))),
    show("beta connection", reply(Beta)),
    show("stop none", reply(request(packet(<<115, "none">>)))),
    show("names", erl_epmd:names()),
    show("kill", reply(request(packet(<<107>>)))),
    wait_names({error, address}),
    show("after kill", [erl_epmd:port_please(alpha, localhost), erl_epmd:names()]),
    ok = erl_epmd:stop(),
    show(
        "stopped",
        receive
            {'EXIT', Server, Reason} -> Reason
        after 5000 -> none
        end
    ),
    {ok, _} = erl_epmd:start(),
    show("register without epmd", erl_epmd:register_node(alpha, 40001)),
    ok = erl_epmd:stop(),
    io:format("epmd_names: ok~n").

show(Label, Value) -> io:format("~s: ~tp~n", [Label, Value]).

catch_exit(Fun) ->
    try Fun() of
        Value -> Value
    catch
        exit:{Reason, _} -> {exit, Reason}
    end.

catch_error(Fun) ->
    try Fun() of
        Value -> Value
    catch
        error:Reason -> {error, Reason}
    end.

%% Wait until erl_epmd:names/0 answers Names (or the error), as the other node changes epmd.
wait_names(Names) when is_list(Names) -> wait_names({ok, Names});
wait_names(Answer) -> wait_names(Answer, erlang:monotonic_time(millisecond) + ?WAIT_MS).

wait_names(Answer, Deadline) ->
    case erl_epmd:names() of
        Answer ->
            ok;
        Other ->
            case erlang:monotonic_time(millisecond) > Deadline of
                true ->
                    io:format("timeout waiting for ~tp, names: ~tp~n", [Answer, Other]),
                    halt(1);
                false ->
                    timer:sleep(50),
                    wait_names(Answer, Deadline)
            end
    end.

epmd_port() -> list_to_integer(os:getenv("ERL_EPMD_PORT")).

%% A raw connection to the epmd that has sent Bytes.
request(Bytes) ->
    {ok, Socket} = gen_tcp:connect({127, 0, 0, 1}, epmd_port(), [binary, {active, false}, {packet, 0}]),
    ok = gen_tcp:send(Socket, Bytes),
    Socket.

packet(Body) -> <<(byte_size(Body)):16, Body/binary>>.

%% A connection that has sent ALIVE2_REQ.
alive(Name, Port, Type, Highest, Lowest, Extra) ->
    Bytes = list_to_binary(Name),
    request(
        packet(
            <<120, Port:16, Type, 0, Highest:16, Lowest:16, (byte_size(Bytes)):16, Bytes/binary, (byte_size(Extra)):16,
                Extra/binary>>
        )
    ).

%% The first reply of a registration.
first(Socket) ->
    {ok, Bytes} = gen_tcp:recv(Socket, 0, ?WAIT_MS),
    creation(Bytes).

%% Everything epmd sends within a second, and whether it closed the connection by then.
reply(Socket) -> reply(Socket, <<>>).

reply(Socket, Acc) ->
    case gen_tcp:recv(Socket, 0, 1000) of
        {ok, Bytes} -> reply(Socket, <<Acc/binary, Bytes/binary>>);
        {error, closed} -> {closed, creation(Acc)};
        {error, timeout} -> {open, creation(Acc)}
    end.

%% ALIVE2 replies with the creation's kind instead of its value: a 32-bit one is positive, a 16-bit one 1 to 3, and a
%% refused registration's is 99.
creation(<<118, 0, Creation:32>>) -> {alive2_x, 0, Creation > 0};
creation(<<121, 0, Creation:16>>) -> {alive2, 0, lists:member(Creation, [1, 2, 3])};
creation(<<Tag, 1, Creation/binary>>) when Tag =:= 118; Tag =:= 121 -> {Tag, 1, binary:decode_unsigned(Creation)};
creation(Bytes) -> Bytes.

%% A NAMES or DUMP reply: the port's presence, then its lines with descriptors masked and NUL bytes shown.
lines({Closed, <<_Port:32, Text/binary>>}) ->
    Masked = re:replace(Text, "fd = [0-9]+", "fd = N", [global, {return, binary}]),
    Lines = binary:split(Masked, [<<"\n">>], [global]),
    {Closed, [binary:replace(Line, <<0>>, <<"<NUL>">>, [global]) || Line <- Lines]};
lines(Reply) ->
    Reply.
