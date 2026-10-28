%% Custom transports over loopback (plan 21 step LIB81D3b): the cb_info option names a gen_tcp-like module, here this
%% one, which passes every call to gen_tcp and inet and records it. Connections over it work as over gen_tcp, in
%% passive and active modes, with the module's message tags.
-module(ssl_transports).
-export([main/1]).
-export([connect/3, connect/4, listen/2, accept/1, accept/2, send/2, recv/2, recv/3, close/1, shutdown/2]).
-export([controlling_process/2, setopts/2, getopts/2, peername/1, sockname/1, port/1, getstat/1, getstat/2]).

-include_lib("public_key/include/public_key.hrl").

main(_) ->
    ok = ssl:start(),
    ets:new(transport_calls, [named_table, public, bag]),
    Server = chain("localhost"),
    [connection(Server, CbInfo) || CbInfo <- cb_infos()],
    show("transport calls", [{F, ets:member(transport_calls, F)} || F <- [listen, accept, connect, send, setopts]]),
    show("option errors", [
        error_of(Server, CbInfo)
     || CbInfo <- [{?MODULE, tcp}, ?MODULE]
    ]),
    io:format("ssl_transports: ok~n").

cb_infos() ->
    [{?MODULE, tcp, tcp_closed, tcp_error}, {?MODULE, tcp, tcp_closed, tcp_error, tcp_passive}].

%% A generated root and a peer whose certificate names Host.
chain(Host) ->
    AltName = #'Extension'{extnID = ?'id-ce-subjectAltName', extnValue = [{dNSName, Host}], critical = false},
    Config = public_key:pkix_test_data(#{
        root => [{key, {namedCurve, secp256r1}}, {digest, sha256}],
        intermediates => [],
        peer => [{key, {namedCurve, secp256r1}}, {digest, sha256}, {extensions, [AltName]}]
    }),
    #{
        cert => proplists:get_value(cert, Config),
        key => proplists:get_value(key, Config),
        root => lists:last(proplists:get_value(cacerts, Config))
    }.

show(Label, Value) -> io:format("~s ~p~n", [Label, Value]).

%% A connection over the transport: data both ways, passive and active, information and closing.
connection(#{cert := Cert, key := Key, root := Root}, CbInfo) ->
    {ok, Listen} = ssl:listen(0, [
        {ip, {127, 0, 0, 1}}, binary, {active, false}, {cert, Cert}, {key, Key}, {cb_info, CbInfo}
    ]),
    {ok, {_, Port}} = ssl:sockname(Listen),
    Self = self(),
    spawn(fun() ->
        {ok, Accepted} = ssl:transport_accept(Listen),
        {ok, Socket} = ssl:handshake(Accepted, 5000),
        ok = ssl:controlling_process(Socket, Self),
        Self ! {server, Socket}
    end),
    {ok, Client} = ssl:connect(
        "localhost", Port, [{cacerts, [Root]}, binary, {active, false}, {cb_info, CbInfo}], 5000
    ),
    Accepted =
        receive
            {server, S} -> S
        after 5000 -> exit(server_timeout)
        end,
    ok = ssl:send(Client, <<"ping">>),
    Passive = ssl:recv(Accepted, 4, 2000),
    ok = ssl:setopts(Client, [{active, true}]),
    ok = ssl:send(Accepted, <<"pong">>),
    Active =
        receive
            {ssl, Client, Data} -> Data
        after 2000 -> timeout
        end,
    {ok, {PeerAddress, _}} = ssl:peername(Client),
    {ok, [{protocol, Protocol}]} = ssl:connection_information(Client, [protocol]),
    ssl:close(Accepted),
    Closed =
        receive
            {ssl_closed, Client} -> closed
        after 2000 -> timeout
        end,
    show(io_lib:format("~p", [tuple_size(CbInfo)]), {Protocol, Passive, Active, PeerAddress, Closed}),
    ssl:close(Listen).

error_of(#{root := Root}, CbInfo) ->
    ssl:connect("localhost", 1, [{cacerts, [Root]}, {cb_info, CbInfo}], 1000).

%% The transport: gen_tcp's and inet's functions, each call recorded by name.
record(Name) -> ets:insert(transport_calls, {Name}).

connect(Host, Port, Options) -> connect(Host, Port, Options, infinity).
connect(Host, Port, Options, Timeout) ->
    record(connect),
    gen_tcp:connect(Host, Port, Options, Timeout).
listen(Port, Options) ->
    record(listen),
    gen_tcp:listen(Port, Options).
accept(Listen) -> accept(Listen, infinity).
accept(Listen, Timeout) ->
    record(accept),
    gen_tcp:accept(Listen, Timeout).
send(Socket, Data) ->
    record(send),
    gen_tcp:send(Socket, Data).
recv(Socket, Length) -> recv(Socket, Length, infinity).
recv(Socket, Length, Timeout) ->
    record(recv),
    gen_tcp:recv(Socket, Length, Timeout).
close(Socket) ->
    record(close),
    gen_tcp:close(Socket).
shutdown(Socket, How) ->
    record(shutdown),
    gen_tcp:shutdown(Socket, How).
controlling_process(Socket, Pid) ->
    record(controlling_process),
    gen_tcp:controlling_process(Socket, Pid).
setopts(Socket, Options) ->
    record(setopts),
    inet:setopts(Socket, Options).
getopts(Socket, Options) ->
    record(getopts),
    inet:getopts(Socket, Options).
peername(Socket) ->
    record(peername),
    inet:peername(Socket).
sockname(Socket) ->
    record(sockname),
    inet:sockname(Socket).
port(Socket) ->
    record(port),
    inet:port(Socket).
getstat(Socket) ->
    record(getstat),
    inet:getstat(Socket).
getstat(Socket, Options) ->
    record(getstat),
    inet:getstat(Socket, Options).
