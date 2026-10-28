%% The ssl engine over libssl (plan 21 step LIB81B): a client and a server engine run TLS 1.3 and 1.2 handshakes
%% against each other in memory, pausing for public_key's path validation, then exchange data, update keys and close.
%% Covers suites, groups, signature schemes, ALPN, SNI, rejected chains, client certificates and failed handshakes.
%% Clause's own: OTP has no such engine, so the golden is authored.
-module(ssl_engine).
-export([main/1]).

main(_) ->
    Server = chain(secp256r1),
    Client = chain(secp384r1),
    tls13(Server),
    tls12(Server),
    rejected(Server, Client),
    mutual(Server, Client),
    no_alpn(Server),
    io:format("ssl_engine: ok~n").

%% A generated root and peer: the peer's certificate, its key and the root that validates it.
chain(Curve) ->
    Config = public_key:pkix_test_data(#{
        root => [{key, {namedCurve, Curve}}], intermediates => [], peer => [{key, {namedCurve, Curve}}]
    }),
    #{
        cert => proplists:get_value(cert, Config),
        key => proplists:get_value(key, Config),
        root => lists:last(proplists:get_value(cacerts, Config))
    }.

%% A context the options of which ssl.erl would have validated.
context(Role, Options) ->
    {ok, Context} = clause_ssl_engine:context(Role, Options),
    Context.

server_context(#{cert := Cert, key := Key}, Options) ->
    context(server, Options#{certs => [{[Cert], Key}]}).

%% TLS 1.3: the client's suite preference, SNI, a group and a scheme, then data, key updates and close_notify.
tls13(Server) ->
    S = clause_ssl_engine:connection(
        server_context(Server, #{versions => ['tlsv1.3'], ciphers => [16#1301, 16#1302, 16#1303]}), #{}
    ),
    ClientContext = context(client, #{
        versions => ['tlsv1.3', 'tlsv1.2'],
        ciphers => [16#1302, 16#1301],
        groups => [secp256r1, no_such_group],
        signature_algs => [ecdsa_secp256r1_sha256, eddsa_ed25519],
        verify => verify_peer
    }),
    C = clause_ssl_engine:connection(ClientContext, #{server_name => <<"example.test">>}),
    {done, done} = run({C, maps:get(root, Server)}, {S, none}),
    show(tls13, C, S),
    [Peer] = clause_ssl_engine:peer_chain(C),
    io:format("tls13 peer is server ~p~n", [Peer =:= maps:get(cert, Server)]),
    exchange(tls13, C, S),
    {ok, Update} = clause_ssl_engine:key_update(C, read_write),
    {ok, <<>>, Answer} = clause_ssl_engine:read(S, Update),
    {ok, <<>>, <<>>} = clause_ssl_engine:read(C, Answer),
    io:format("tls13 key update answered ~p~n", [Answer =/= <<>>]),
    exchange(tls13_updated, C, S),
    close(tls13, C, S).

%% TLS 1.2: an ECDHE-ECDSA suite, the server's ALPN preference; no key updates before TLS 1.3.
tls12(Server) ->
    S = clause_ssl_engine:connection(
        server_context(Server, #{versions => ['tlsv1.2'], alpn => [<<"http/1.1">>, <<"h2">>]}), #{}
    ),
    ClientContext = context(client, #{
        versions => ['tlsv1.2'],
        ciphers => [16#1301, 16#C02B, 16#C02C],
        signature_algs => [{sha256, ecdsa}, ecdsa_secp256r1_sha256, {sha384, ecdsa}],
        alpn => [<<"h2">>, <<"http/1.1">>],
        verify => verify_peer
    }),
    C = clause_ssl_engine:connection(ClientContext, #{}),
    {done, done} = run({C, maps:get(root, Server)}, {S, none}),
    show(tls12, C, S),
    exchange(tls12, C, S),
    {{error, _}, <<>>} = clause_ssl_engine:key_update(C, write),
    io:format("tls12 key_update refused~n"),
    close(tls12, C, S).

%% A server chain the client's root does not validate: the client sends unknown_ca, the server receives it.
rejected(Server, Client) ->
    S = clause_ssl_engine:connection(server_context(Server, #{}), #{}),
    C = clause_ssl_engine:connection(context(client, #{verify => verify_peer}), #{}),
    {ClientStatus, ServerStatus} = run({C, maps:get(root, Client)}, {S, none}),
    io:format("rejected client ~w server ~w~n", [ClientStatus, ServerStatus]).

%% A server asking for a client certificate validates it like the client validates the server's.
mutual(Server, Client) ->
    ServerContext = server_context(Server, #{
        verify => verify_peer, fail_if_no_peer_cert => true, client_cas => [maps:get(root, Client)]
    }),
    #{cert := Cert, key := Key} = Client,
    ClientContext = context(client, #{verify => verify_peer, certs => [{[Cert], Key}]}),
    S = clause_ssl_engine:connection(ServerContext, #{}),
    C = clause_ssl_engine:connection(ClientContext, #{}),
    {done, done} = run({C, maps:get(root, Server)}, {S, maps:get(root, Client)}),
    [Peer] = clause_ssl_engine:peer_chain(S),
    io:format("mutual peer is client ~p~n", [Peer =:= Cert]),
    exchange(mutual, C, S),
    Anonymous = clause_ssl_engine:connection(context(client, #{verify => verify_peer}), #{}),
    Strict = clause_ssl_engine:connection(ServerContext, #{}),
    {AnonymousStatus, StrictStatus} = run({Anonymous, maps:get(root, Server)}, {Strict, maps:get(root, Client)}),
    io:format("mutual without certificate client ~w server ~w~n", [AnonymousStatus, StrictStatus]).

%% A server whose ALPN preferences the client does not offer sends no_application_protocol.
no_alpn(Server) ->
    S = clause_ssl_engine:connection(server_context(Server, #{alpn => [<<"x">>]}), #{}),
    C = clause_ssl_engine:connection(context(client, #{alpn => [<<"y">>]}), #{}),
    {ClientStatus, ServerStatus} = run({C, none}, {S, none}),
    io:format("no_alpn client ~w server ~w~n", [ClientStatus, ServerStatus]).

%% Run both handshakes to an end, each side's output going to the other; {ClientStatus, ServerStatus}.
run(Client, Server) ->
    run(Client, Server, <<>>, pending, pending, 20).

run(_Client, _Server, <<>>, ClientStatus, ServerStatus, _) when ClientStatus =/= pending, ServerStatus =/= pending ->
    {ClientStatus, ServerStatus};
run(Client, Server, ToClient, ClientStatus0, ServerStatus0, Rounds) when Rounds > 0 ->
    {ClientStatus, ToServer} = step(Client, ToClient, ClientStatus0),
    {ServerStatus, ToClient1} = step(Server, ToServer, ServerStatus0),
    run(Client, Server, ToClient1, ClientStatus, ServerStatus, Rounds - 1).

%% One handshake step of a pending side; a side that ended reads what arrives (a late alert).
step({Engine, _Root}, Received, done) ->
    case clause_ssl_engine:read(Engine, Received) of
        {ok, <<>>, Output} -> {done, Output};
        {Failure, <<>>, Output} -> {{after_done, Failure}, Output}
    end;
step(_Side, _Received, Status) when Status =/= pending ->
    {Status, <<>>};
step({Engine, Root} = Side, Received, pending) ->
    case clause_ssl_engine:handshake(Engine, Received) of
        {want_read, Output} ->
            {pending, Output};
        {verify, Output} ->
            ok = clause_ssl_engine:verify(Engine, verdict(Engine, Root)),
            {Status, More} = step(Side, <<>>, pending),
            {Status, <<Output/binary, More/binary>>};
        {Status, Output} ->
            {Status, Output}
    end.

%% public_key's verdict on the peer's chain: ok, or the unknown_ca alert.
verdict(Engine, Root) ->
    [Peer | _] = clause_ssl_engine:peer_chain(Engine),
    case public_key:pkix_path_validation(Root, [Peer], []) of
        {ok, _} -> ok;
        {error, {bad_cert, _}} -> 48
    end.

%% The negotiated items of both sides.
show(Name, Client, Server) ->
    #{protocol := Protocol, cipher_suite := Suite, alpn := Alpn, group := Group, resumed := Resumed} =
        clause_ssl_engine:info(Client),
    #{server_name := ServerName, cipher_suite := Suite, protocol := Protocol} = clause_ssl_engine:info(Server),
    io:format("~p ~p suite ~.16B alpn ~p group ~p resumed ~p sni ~p~n", [
        Name, Protocol, Suite, Alpn, Group, Resumed, ServerName
    ]).

%% Application data both ways.
exchange(Name, Client, Server) ->
    {ok, Ping} = clause_ssl_engine:write(Client, [<<"pi">>, "ng"]),
    {ok, <<"ping">>, <<>>} = clause_ssl_engine:read(Server, Ping),
    Large = binary:copy(<<"x">>, 40000),
    {ok, Pong} = clause_ssl_engine:write(Server, Large),
    {ok, Large, <<>>} = clause_ssl_engine:read(Client, Pong),
    io:format("~p data ok~n", [Name]).

%% close_notify both ways: each side reads closed.
close(Name, Client, Server) ->
    {ok, Notify} = clause_ssl_engine:shutdown(Client),
    {closed, <<>>, <<>>} = clause_ssl_engine:read(Server, Notify),
    {ok, Answer} = clause_ssl_engine:shutdown(Server),
    {closed, <<>>, <<>>} = clause_ssl_engine:read(Client, Answer),
    io:format("~p closed~n", [Name]).
