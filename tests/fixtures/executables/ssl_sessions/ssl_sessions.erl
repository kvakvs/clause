%% TLS 1.2 session reuse over loopback (plan 21 step LIB81D2a): the client's automatic reuse of verified sessions,
%% reuse_sessions save and false, explicit reuse_session by id and by {Id, Data}, the server's reuse_sessions and
%% reuse_session policy, session options' errors, and ssl_session_cache_api callback modules for both caches.
-module(ssl_sessions).
-export([main/1]).
-behaviour(ssl_session_cache_api).
-export([init/1, terminate/1, lookup/2, update/3, delete/2, foldl/3, select_session/2, size/1]).

-include_lib("public_key/include/public_key.hrl").

main(_) ->
    ok = ssl:start(),
    Server = chain("localhost"),
    automatic(Server),
    client_verify_none(Server),
    reuse_false(Server),
    save(Server),
    explicit(Server),
    server_reuse_false(Server),
    server_policy(Server),
    server_verify_peer(Server),
    tls13(Server),
    show("client option errors", [connect_error(Server, Options) || Options <- client_errors()]),
    show("server option errors", [listen_error(Server, Options) || Options <- server_errors()]),
    callbacks(Server),
    io:format("ssl_sessions: ok~n").

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

%% A TLS 1.2 listening socket of the server certificate.
listener(#{cert := Cert, key := Key}, Options) ->
    {ok, Listen} = ssl:listen(0, [
        {ip, {127, 0, 0, 1}},
        {active, false},
        {cert, Cert},
        {key, Key},
        {log_level, none},
        {versions, ['tlsv1.2']}
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
        "localhost", Port, [{cacerts, [Root]}, {active, false}, {log_level, none} | Options], 5000
    ),
    receive
        {server, Socket} -> {Client, Socket}
    after 5000 -> exit(server_timeout)
    end.

%% Whether a connection resumed a session, and its session id.
session(Socket) ->
    {ok, [{session_id, Id}, {session_resumption, Resumed}]} =
        ssl:connection_information(Socket, [session_id, session_resumption]),
    {Resumed, Id}.

%% Connections made one after another with each option list, a little apart so that the client cache registers
%% each session (OTP registers them asynchronously): [{Resumed, Id}] of the client sides.
sessions(Server, Listen, OptionLists) ->
    Pairs = [
        begin
            Pair = connect(Server, Listen, Options),
            timer:sleep(100),
            Pair
        end
     || Options <- OptionLists
    ],
    Sessions = [session(Client) || {Client, _} <- Pairs],
    [ssl:close(S) || {Client, Accepted} <- Pairs, S <- [Client, Accepted]],
    Sessions.

%% The resumptions of sessions, and for each later session whether it has the first one's id.
summary([{Resumed, Id} | Rest]) ->
    {[Resumed | [R || {R, _} <- Rest]], [Other == Id || {_, Other} <- Rest]}.

%% A verifying client reuses its last session to the same server.
automatic(Server) ->
    Listen = listener(Server, []),
    show("automatic", summary(sessions(Server, Listen, [[], [], []]))),
    ssl:close(Listen).

%% A client that does not verify the server keeps no sessions.
client_verify_none(Server) ->
    Listen = listener(Server, []),
    show("client verify_none", summary(sessions(Server, Listen, [[{verify, verify_none}], [{verify, verify_none}]]))),
    ssl:close(Listen).

%% reuse_sessions false neither saves nor reuses a session.
reuse_false(Server) ->
    Listen = listener(Server, []),
    Sessions = sessions(Server, Listen, [[{reuse_sessions, false}], [{reuse_sessions, false}], [], []]),
    [_, _, {_, Id2}, {Resumed3, Id3}] = Sessions,
    show("reuse_sessions false", {[R || {R, _} <- Sessions], Resumed3 andalso Id2 == Id3}),
    ssl:close(Listen).

%% save makes a new session and saves it; reuse_session names a saved session, an unknown one gives a new session.
save(Server) ->
    Listen = listener(Server, []),
    [{false, Id1}, {Resumed2, Id2}] = sessions(Server, Listen, [[], [{reuse_sessions, save}]]),
    show("save", {Resumed2, Id1 == Id2}),
    [{Resumed3, Id3}, {Resumed4, Id4}] =
        sessions(Server, Listen, [[{reuse_session, Id2}], [{reuse_session, <<1:256>>}]]),
    show("reuse_session saved", {Resumed3, Id3 == Id2}),
    show("reuse_session unknown", {Resumed4, lists:member(Id4, [Id1, Id2, <<1:256>>])}),
    ssl:close(Listen).

%% {Id, Data} reuses a session the client cache does not have; bad data gives a new session.
explicit(Server) ->
    Listen = listener(Server, []),
    {Client, Accepted} = connect(Server, Listen, [{reuse_sessions, false}, {verify, verify_none}]),
    {ok, Items} = ssl:connection_information(Client, [session_id, session_data]),
    show("session items", [{K, is_binary(V)} || {K, V} <- Items]),
    [{session_id, Id}, {session_data, Data}] = Items,
    [ssl:close(S) || S <- [Client, Accepted]],
    [{Resumed1, Id1}, {Resumed2, Id2}] =
        sessions(Server, Listen, [[{reuse_session, {Id, Data}}], [{reuse_session, {Id, <<"junk">>}}]]),
    show("reuse_session data", {Resumed1, Id1 == Id}),
    show("reuse_session bad data", {Resumed2, Id2 == Id}),
    ssl:close(Listen).

%% A server with reuse_sessions false never resumes.
server_reuse_false(Server) ->
    Listen = listener(Server, [{reuse_sessions, false}]),
    show("server reuse_sessions false", summary(sessions(Server, Listen, [[], []]))),
    ssl:close(Listen).

%% The server's reuse_session fun decides, given the suggested id, the peer certificate, the compression and the
%% session's cipher suite.
server_policy(Server) ->
    Self = self(),
    lists:foreach(
        fun(Answer) ->
            Policy = fun(Id, PeerCert, Compression, Suite) ->
                Self ! {policy, {byte_size(Id), PeerCert, Compression, suite_shape(Suite)}},
                Answer
            end,
            Listen = listener(Server, [{reuse_session, Policy}]),
            show(io_lib:format("reuse_session ~p", [Answer]), summary(sessions(Server, Listen, [[], []]))),
            show("policy arguments", [Args || {policy, Args} <- flush()]),
            ssl:close(Listen)
        end,
        [false, true]
    ).

%% OTP passes the suite's two-byte code, not the map its documentation names.
suite_shape(Suite) when is_binary(Suite) -> {binary, byte_size(Suite)};
suite_shape(Suite) when is_map(Suite) -> map.

flush() ->
    receive
        Message -> [Message | flush()]
    after 0 -> []
    end.

%% A server that verifies clients reuses sessions only when reuse_sessions is set; a resumed session keeps both
%% peers' certificates.
server_verify_peer(#{cert := Cert, key := Key, root := Root} = Server) ->
    ServerOptions = [{verify, verify_peer}, {cacerts, [Root]}, {fail_if_no_peer_cert, true}],
    ClientOptions = [{cert, Cert}, {key, Key}],
    Listen1 = listener(Server, ServerOptions),
    show("server verify_peer", summary(sessions(Server, Listen1, [ClientOptions, ClientOptions]))),
    ssl:close(Listen1),
    Listen2 = listener(Server, [{reuse_sessions, true} | ServerOptions]),
    {Client1, Accepted1} = connect(Server, Listen2, ClientOptions),
    timer:sleep(100),
    {Client2, Accepted2} = connect(Server, Listen2, ClientOptions),
    show("server verify_peer, reuse_sessions true", element(1, summary([session(Client1), session(Client2)]))),
    show("peer certificates kept", {ssl:peercert(Client2) == {ok, Cert}, ssl:peercert(Accepted2) == {ok, Cert}}),
    [ssl:close(S) || S <- [Client1, Accepted1, Client2, Accepted2, Listen2]].

%% TLS 1.3 without session tickets resumes nothing.
tls13(#{cert := Cert, key := Key, root := Root} = Server) ->
    {ok, Listen} = ssl:listen(0, [
        {ip, {127, 0, 0, 1}}, {active, false}, {cert, Cert}, {key, Key}, {log_level, none}, {versions, ['tlsv1.3']}
    ]),
    Sessions = sessions(Server, Listen, [[{versions, ['tlsv1.3']}], [{versions, ['tlsv1.3']}]]),
    show("tlsv1.3", [R || {R, _} <- Sessions]),
    _ = Root,
    ssl:close(Listen).

client_errors() ->
    [
        [{reuse_sessions, sometimes}],
        [{reuse_session, 42}],
        [{reuse_session, {<<1>>, 2}}],
        [{reuse_session, fun(_, _, _, _) -> true end}],
        [{versions, ['tlsv1.3']}, {reuse_sessions, true}],
        [{versions, ['tlsv1.3']}, {reuse_session, <<1>>}]
    ].

server_errors() ->
    [
        [{reuse_sessions, save}],
        [{reuse_session, <<1>>}],
        [{reuse_session, fun(_, _) -> true end}],
        [{versions, ['tlsv1.3']}, {reuse_sessions, true}]
    ].

connect_error(#{root := Root}, Options) ->
    funless(ssl:connect("localhost", 1, [{cacerts, [Root]} | Options], 1000)).

listen_error(#{cert := Cert, key := Key}, Options) ->
    funless(ssl:listen(0, [{cert, Cert}, {key, Key} | Options])).

%% An error with its funs replaced by their arities, which print differently.
funless({error, {options, {Key, Fun}}}) when is_function(Fun) -> {error, {options, {Key, fun_arity(Fun)}}};
funless({ok, Socket}) ->
    ssl:close(Socket),
    ok;
funless(Other) ->
    Other.

fun_arity(Fun) -> {'fun', element(2, erlang:fun_info(Fun, arity))}.

%% ssl_session_cache_api modules of both roles, from the application environment: each cache initializes with its
%% role and arguments, and its sessions go through the module.
callbacks(Server) ->
    % No reports of the stopped application.
    ok = logger:set_primary_config(level, warning),
    ok = ssl:stop(),
    ets:new(ssl_sessions_calls, [named_table, public, bag]),
    application:load(ssl),
    ok = application:set_env(ssl, client_session_cb, ?MODULE),
    ok = application:set_env(ssl, client_session_cb_init_args, [{tag, client}]),
    ok = application:set_env(ssl, server_session_cb, ?MODULE),
    ok = application:set_env(ssl, server_session_cb_init_args, [{tag, server}]),
    ok = ssl:start(),
    Listen = listener(Server, []),
    show("callbacks", summary(sessions(Server, Listen, [[], []]))),
    ssl:close(Listen),
    Calls = ets:tab2list(ssl_sessions_calls),
    [
        show(
            io_lib:format("~p callbacks", [Role]),
            {
                [Args || {R, {init, Args}} <- Calls, R == Role],
                lists:usort([F || {R, F} <- Calls, R == Role, is_atom(F)])
            }
        )
     || Role <- [client, server]
    ],
    ok = ssl:stop().

%% The cache callbacks: an ETS table per cache, and each call recorded with the cache's role.
init(Args) ->
    Role = proplists:get_value(role, Args),
    ets:insert(ssl_sessions_calls, {Role, {init, Args}}),
    {Role, ets:new(cache, [public, set])}.

terminate({Role, Table}) ->
    record(Role, terminate),
    ets:delete(Table).

lookup({Role, Table}, Key) ->
    record(Role, lookup),
    case ets:lookup(Table, Key) of
        [{_, Session}] -> Session;
        [] -> undefined
    end.

update({Role, Table}, Key, Session) ->
    record(Role, update),
    ets:insert(Table, {Key, Session}).

delete({Role, Table}, Key) ->
    record(Role, delete),
    ets:delete(Table, Key).

foldl(Fun, Acc, {Role, Table}) ->
    record(Role, foldl),
    ets:foldl(fun({_, Session}, A) -> Fun(Session, A) end, Acc, Table).

select_session({Role, Table}, Server) ->
    record(Role, select_session),
    [Session || {{S, _}, Session} <- ets:tab2list(Table), S == Server].

size({Role, Table}) ->
    record(Role, size),
    ets:info(Table, size).

record(Role, Name) -> ets:insert(ssl_sessions_calls, {Role, Name}).
