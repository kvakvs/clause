%% Automatic rekeys over loopback (plan 21 step LIB81D3b): TLS 1.3 key updates once the data sent passes key_update_at
%% (large sends split at it), TLS 1.2 renegotiation once the records sent reach renegotiate_at, and the options'
%% errors.
-module(ssl_rekeys).
-export([main/1]).

-include_lib("public_key/include/public_key.hrl").

main(_) ->
    ok = ssl:start(),
    Server = chain("localhost"),
    key_updates(Server),
    renegotiations(Server),
    show("option errors", [connect_error(Server, Options) || Options <- errors()]),
    io:format("ssl_rekeys: ok~n").

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

%% A listening socket, a passive client and the server's accepted socket.
pair(#{cert := Cert, key := Key, root := Root}, ServerOptions, ClientOptions) ->
    {ok, Listen} = ssl:listen(0, [
        {ip, {127, 0, 0, 1}}, binary, {active, false}, {cert, Cert}, {key, Key}, {log_level, none} | ServerOptions
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
        "localhost", Port, [{cacerts, [Root]}, binary, {active, false}, {log_level, none} | ClientOptions], 5000
    ),
    receive
        {server, Socket} -> {Listen, Client, Socket}
    after 5000 -> exit(server_timeout)
    end.

%% The traffic secret labels of a socket's key log.
traffic(Socket) ->
    {ok, [{keylog, Lines}]} = ssl:connection_information(Socket, [keylog]),
    [Label || Line <- Lines, [Label | _] <- [string:split(lists:flatten(Line), " ")], lists:member(Label, labels())].

labels() ->
    [P ++ integer_to_list(N) || P <- ["CLIENT_TRAFFIC_SECRET_", "SERVER_TRAFFIC_SECRET_"], N <- lists:seq(0, 9)].

%% TLS 1.3: a client with key_update_at 10 sends 5, 5, 1 and 25 bytes.
key_updates(Server) ->
    Options = [{versions, ['tlsv1.3']}, {keep_secrets, true}],
    {Listen, Client, Accepted} = pair(Server, Options, [{key_update_at, 10} | Options]),
    Steps = [
        begin
            Data = binary:copy(<<"k">>, Size),
            ok = ssl:send(Client, Data),
            {ok, Data} = ssl:recv(Accepted, Size, 2000),
            {Size, [L || L <- traffic(Client), lists:prefix("CLIENT_TRAFFIC", L)]}
        end
     || Size <- [5, 5, 1, 25]
    ],
    show("key updates", Steps),
    ok = ssl:send(Accepted, <<"back">>),
    {ok, <<"back">>} = ssl:recv(Client, 4, 2000),
    show("client secrets", traffic(Client)),
    show("server secrets", traffic(Accepted)),
    [ssl:close(S) || S <- [Client, Accepted, Listen]].

%% TLS 1.2: a client with renegotiate_at 3 sends four messages; its client_random changes with the renegotiation
%% before the third. (A second one within 12 seconds is refused by the server and ends the connection.)
renegotiations(Server) ->
    Options = [{versions, ['tlsv1.2']}],
    {Listen, Client, Accepted} = pair(Server, Options, [{renegotiate_at, 3} | Options]),
    {ok, [{client_random, First}]} = ssl:connection_information(Client, [client_random]),
    Steps = [
        begin
            ok = ssl:send(Client, <<N>>),
            {ok, <<N>>} = ssl:recv(Accepted, 1, 2000),
            {ok, [{client_random, Random}]} = ssl:connection_information(Client, [client_random]),
            Random =/= First
        end
     || N <- [1, 2, 3, 4]
    ],
    show("renegotiated", Steps),
    [ssl:close(S) || S <- [Client, Accepted, Listen]].

errors() ->
    [
        [{key_update_at, 0}],
        [{key_update_at, many}],
        [{versions, ['tlsv1.2']}, {key_update_at, 10}],
        [{renegotiate_at, 0}],
        [{renegotiate_at, 1 bsl 40}]
    ].

connect_error(#{root := Root}, Options) ->
    case ssl:connect("localhost", 1, [{cacerts, [Root]} | Options], 1000) of
        {error, {options, _} = Reason} -> {error, Reason};
        {error, {options, _, _} = Reason} -> {error, Reason};
        {error, _} -> accepted
    end.
