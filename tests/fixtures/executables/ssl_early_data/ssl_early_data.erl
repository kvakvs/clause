%% TLS 1.3 early data over loopback (plan 21 step LIB81D2c): a resuming client's early_data accepted or rejected by
%% the server, its {ssl, S, {early_data, Result}} message, the data the server reads, single-use stateful tickets and
%% the reuse of a ticket under anti_replay. (OTP 29.1.1's servers fail early data above the tickets' limit with an
%% unexpected_message alert, so that case is left out.)
-module(ssl_early_data).
-export([main/1]).

-include_lib("public_key/include/public_key.hrl").

main(_) ->
    ok = ssl:start(),
    Server = chain("localhost"),
    accepted(Server, stateful, []),
    accepted(Server, stateless, [{anti_replay, '10k'}]),
    rejected(Server),
    replayed(Server, stateless, [{anti_replay, '10k'}]),
    io:format("ssl_early_data: ok~n").

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

%% A TLS 1.3 listening socket of the server certificate, with tickets of Mode.
listener(#{cert := Cert, key := Key}, Mode, Options) ->
    {ok, Listen} = ssl:listen(0, [
        {ip, {127, 0, 0, 1}},
        {active, false},
        {cert, Cert},
        {key, Key},
        {log_level, none},
        {versions, ['tlsv1.3']},
        {session_tickets, Mode}
        | Options
    ]),
    Listen.

%% One connection to the listener named Name (unverified): {Client, Accepted}, both owned by this process.
connect(Listen, Name, Options) ->
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
        [
            {verify, verify_none},
            {server_name_indication, Name},
            {active, false},
            {log_level, none},
            {versions, ['tlsv1.3']}
            | Options
        ],
        5000
    ),
    receive
        {server, Socket} -> {Client, Socket}
    after 5000 -> exit(server_timeout)
    end.

resumed(Socket) ->
    {ok, [{session_resumption, Resumed}]} = ssl:connection_information(Socket, [session_resumption]),
    Resumed.

%% The early_data messages of a client socket.
early_results(Client) ->
    receive
        {ssl, Client, {early_data, Result}} -> [Result | early_results(Client)]
    after 300 -> []
    end.

%% What the server reads first, or timeout.
first_read(Accepted) -> ssl:recv(Accepted, 0, 500).

%% A full handshake for tickets, then a resuming connection that sends early data.
accepted(Server, Mode, ServerOptions) ->
    Name = atom_to_list(Mode) ++ ".accepted",
    Listen = listener(Server, Mode, [{early_data, enabled} | ServerOptions]),
    {C1, A1} = connect(Listen, Name, [{session_tickets, auto}]),
    timer:sleep(200),
    {C2, A2} = connect(Listen, Name, [{session_tickets, auto}, {early_data, <<"early">>}]),
    show(
        io_lib:format("~p early data", [Mode]),
        {resumed(C2), early_results(C2), first_read(A2)}
    ),
    ok = ssl:send(C2, <<"later">>),
    show("then", ssl:recv(A2, 0, 1000)),
    [ssl:close(S) || S <- [C1, A1, C2, A2, Listen]].

%% A server without early_data rejects it; the client resumes and resends.
rejected(Server) ->
    Listen = listener(Server, stateless, []),
    {C1, A1} = connect(Listen, "rejected", [{session_tickets, auto}]),
    timer:sleep(200),
    {C2, A2} = connect(Listen, "rejected", [{session_tickets, auto}, {early_data, <<"early">>}]),
    show("rejected", {resumed(C2), early_results(C2), first_read(A2)}),
    [ssl:close(S) || S <- [C1, A1, C2, A2, Listen]].

%% The same ticket twice with early data: anti_replay sees the replay. (OTP 29.1.1's stateful servers fail a reused
%% ticket that comes with early data with an unexpected_message alert, so they are left out.)
replayed(Server, Mode, ServerOptions) ->
    Name = atom_to_list(Mode) ++ ".replayed",
    Listen = listener(Server, Mode, [{early_data, enabled} | ServerOptions]),
    {C1, A1} = connect(Listen, Name, [{session_tickets, manual}]),
    Ticket =
        receive
            {ssl, session_ticket, T} -> T
        after 1000 -> exit(no_ticket)
        end,
    Use = [{session_tickets, manual}, {use_ticket, [Ticket]}, {early_data, <<"again">>}],
    Results = [
        begin
            {C, A} = connect(Listen, Name, Use),
            R = {resumed(C), early_results(C), first_read(A)},
            [ssl:close(S) || S <- [C, A]],
            R
        end
     || _ <- [1, 2]
    ],
    show(io_lib:format("~p replayed", [Mode]), Results),
    flush(),
    [ssl:close(S) || S <- [C1, A1, Listen]].

flush() ->
    receive
        _ -> flush()
    after 0 -> ok
    end.
