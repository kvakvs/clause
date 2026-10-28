%% TLS 1.3 session tickets over loopback (plan 21 step LIB81D2b): automatic and manual client tickets against
%% stateless and stateful servers, single-use stateful tickets, the peer certificates of resumed sessions with and
%% without the _with_cert modes, and the ticket options' errors. A client's automatic tickets are kept by server name,
%% so each scenario names the server differently (and does not verify it).
-module(ssl_tickets).
-export([main/1]).

-include_lib("public_key/include/public_key.hrl").

main(_) ->
    ok = ssl:start(),
    Server = chain("localhost"),
    [automatic(Server, Mode) || Mode <- [stateless, stateful, disabled]],
    client_disabled(Server),
    [manual(Server, Mode) || Mode <- [stateless, stateful]],
    [peer_certificates(Server, Mode) || Mode <- [stateless, stateless_with_cert, stateful, stateful_with_cert]],
    tls12(Server),
    show("client option errors", [connect_error(Server, Options) || Options <- client_errors()]),
    show("server option errors", [listen_error(Server, Options) || Options <- server_errors()]),
    io:format("ssl_tickets: ok~n").

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

%% A TLS 1.3 listening socket of the server certificate.
listener(#{cert := Cert, key := Key}, Options) ->
    {ok, Listen} = ssl:listen(0, [
        {ip, {127, 0, 0, 1}},
        {active, false},
        {cert, Cert},
        {key, Key},
        {log_level, none},
        {versions, ['tlsv1.3']}
        | Options
    ]),
    Listen.

%% One connection to the listener: {Client, Accepted}, both owned by this process.
connect(#{root := Root}, Listen, Options) ->
    {ok, {_, Port}} = ssl:sockname(Listen),
    Self = self(),
    spawn(fun() ->
        {ok, Accepted} = ssl:transport_accept(Listen),
        {ok, Socket} = ssl:handshake(Accepted, 5000),
        ok = ssl:controlling_process(Socket, Self),
        Self ! {server, Socket}
    end),
    {ok, Client} = ssl:connect(
        "localhost",
        Port,
        [{cacerts, [Root]}, {active, false}, {log_level, none}, {versions, ['tlsv1.3']} | Options],
        5000
    ),
    receive
        {server, Socket} -> {Client, Socket}
    after 5000 -> exit(server_timeout)
    end.

resumed(Socket) ->
    {ok, [{session_resumption, Resumed}]} = ssl:connection_information(Socket, [session_resumption]),
    Resumed.

%% Connections one after another, a little apart so that the client has the tickets of the last one: the
%% resumptions seen by the client and by the server.
resumptions(Server, Listen, OptionLists) ->
    lists:map(
        fun(Options) ->
            {Client, Accepted} = connect(Server, Listen, Options),
            timer:sleep(200),
            Pair = {resumed(Client), resumed(Accepted)},
            [ssl:close(S) || S <- [Client, Accepted]],
            Pair
        end,
        OptionLists
    ).

%% Client options naming the server Name, unverified, so that automatic tickets of other scenarios do not apply.
named(Name) -> [{verify, verify_none}, {server_name_indication, Name}].

%% session_tickets auto keeps the server's tickets and resumes with them.
automatic(Server, Mode) ->
    Listen = listener(Server, [{session_tickets, Mode}]),
    Auto = [{session_tickets, auto} | named(atom_to_list(Mode) ++ ".auto")],
    show(io_lib:format("auto, server ~p", [Mode]), resumptions(Server, Listen, [Auto, Auto, Auto])),
    ssl:close(Listen).

%% A client without session_tickets never resumes.
client_disabled(Server) ->
    Listen = listener(Server, [{session_tickets, stateless}]),
    show("client disabled", resumptions(Server, Listen, [named("disabled"), named("disabled")])),
    ssl:close(Listen).

%% session_tickets manual sends each ticket to the owner; use_ticket offers one. A stateful server takes each ticket
%% once.
manual(Server, Mode) ->
    Listen = listener(Server, [{session_tickets, Mode}]),
    Name = named(atom_to_list(Mode) ++ ".manual"),
    {Client, Accepted} = connect(Server, Listen, [{session_tickets, manual} | Name]),
    Tickets = tickets(),
    show(io_lib:format("manual tickets, server ~p", [Mode]), {length(Tickets), [maps:get(sni, T) || T <- Tickets]}),
    [ssl:close(S) || S <- [Client, Accepted]],
    [Ticket | _] = Tickets,
    Use = [{session_tickets, manual}, {use_ticket, [Ticket]} | Name],
    show(io_lib:format("use_ticket, server ~p", [Mode]), resumptions(Server, Listen, [Use, Use])),
    tickets(),
    ssl:close(Listen).

tickets() ->
    receive
        {ssl, session_ticket, Ticket} -> [Ticket | tickets()]
    after 500 -> []
    end.

%% The peer certificates of resumed connections: a server keeps the client's only in the _with_cert modes.
peer_certificates(#{cert := Cert, key := Key, root := Root} = Server, Mode) ->
    Listen = listener(Server, [{session_tickets, Mode}, {verify, verify_peer}, {cacerts, [Root]}]),
    Options = [{session_tickets, auto}, {cert, Cert}, {key, Key} | named(atom_to_list(Mode) ++ ".peer")],
    {Client1, Accepted1} = connect(Server, Listen, Options),
    timer:sleep(200),
    {Client2, Accepted2} = connect(Server, Listen, Options),
    show(
        io_lib:format("peercert, server ~p", [Mode]),
        {resumed(Client2), certificate(ssl:peercert(Client2), Cert), certificate(ssl:peercert(Accepted2), Cert)}
    ),
    [ssl:close(S) || S <- [Client1, Accepted1, Client2, Accepted2, Listen]].

certificate({ok, Cert}, Cert) -> same;
certificate(Other, _) -> Other.

%% A ticket server that negotiates TLS 1.2 resumes nothing with tickets.
tls12(#{cert := Cert, key := Key} = Server) ->
    {ok, Listen} = ssl:listen(0, [
        {ip, {127, 0, 0, 1}},
        {active, false},
        {cert, Cert},
        {key, Key},
        {log_level, none},
        {versions, ['tlsv1.3', 'tlsv1.2']},
        {session_tickets, stateless},
        {reuse_sessions, false}
    ]),
    Options = [{versions, ['tlsv1.2']}, {reuse_sessions, false}],
    {ok, {_, Port}} = ssl:sockname(Listen),
    Self = self(),
    spawn(fun() ->
        [
            begin
                {ok, A} = ssl:transport_accept(Listen),
                {ok, S} = ssl:handshake(A, 5000),
                ok = ssl:controlling_process(S, Self),
                Self ! {server, S}
            end
         || _ <- [1, 2]
        ]
    end),
    #{root := Root} = Server,
    Pairs = [
        begin
            {ok, C} = ssl:connect("localhost", Port, [{cacerts, [Root]}, {active, false} | Options], 5000),
            S =
                receive
                    {server, S0} -> S0
                end,
            timer:sleep(100),
            {C, S}
        end
     || _ <- [1, 2]
    ],
    show("tlsv1.2 against a ticket server", [{resumed(C), resumed(S)} || {C, S} <- Pairs]),
    [ssl:close(S) || {C, A} <- Pairs, S <- [C, A]],
    ssl:close(Listen).

client_errors() ->
    [
        [{session_tickets, sometimes}],
        [{session_tickets, stateless}],
        [{versions, ['tlsv1.2']}, {session_tickets, auto}],
        [{session_tickets, manual}, {use_ticket, []}],
        [{session_tickets, manual}, {use_ticket, ticket}],
        [{session_tickets, auto}, {use_ticket, [#{sni => "localhost"}]}],
        [{session_tickets, disabled}, {early_data, <<"x">>}],
        [{session_tickets, manual}, {early_data, <<"x">>}],
        [{session_tickets, auto}, {early_data, data}],
        [{anti_replay, '10k'}],
        [{stateless_tickets_seed, <<"seed">>}]
    ].

server_errors() ->
    [
        [{session_tickets, auto}],
        [{versions, ['tlsv1.2']}, {session_tickets, stateless}],
        [{early_data, enabled}],
        [{session_tickets, stateless}, {early_data, sometimes}],
        [{session_tickets, stateful}, {anti_replay, '10k'}],
        [{session_tickets, stateless}, {anti_replay, '1m'}],
        [{session_tickets, stateful}, {stateless_tickets_seed, <<"seed">>}],
        [{session_tickets, stateless}, {stateless_tickets_seed, seed}],
        [{use_ticket, [#{sni => "localhost"}]}]
    ].

connect_error(#{root := Root}, Options) ->
    funless(ssl:connect("localhost", 1, [{cacerts, [Root]} | Options], 1000)).

listen_error(#{cert := Cert, key := Key}, Options) ->
    funless(ssl:listen(0, [{cert, Cert}, {key, Key} | Options])).

funless({ok, Socket}) ->
    ssl:close(Socket),
    ok;
funless(Other) ->
    Other.
