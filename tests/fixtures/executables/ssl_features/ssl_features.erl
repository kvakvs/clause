%% TLS connection features over loopback (plan 21 step LIB81D1): TLS 1.3 key updates, keying material exporters and
%% the deprecated prf/5, TLS 1.2 renegotiation by either side and its refusals, the secrets connection_information/2
%% gives, the keep_secrets key log funs, and close/2 downgrades after the peer's close_notify.
-module(ssl_features).
-export([main/1]).
-compile({nowarn_deprecated_function, [{ssl, prf, 5}]}).

-include_lib("public_key/include/public_key.hrl").

main(_) ->
    ok = ssl:start(),
    Server = chain("localhost"),
    tls13(Server),
    exporters13(Server),
    tls12(Server),
    renegotiation(Server),
    keylog_funs(Server),
    keylog_handshakes(Server),
    [late_downgrade(Server, Active) || Active <- [false, true]],
    show("bad keep_secrets", [funless(Connect) || Connect <- bad_keep_secrets()]),
    io:format("ssl_features: ok~n").

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

id(X) -> X.

%% A listening socket, a passive client and the server's accepted socket, owned by this process; ClientOptions
%% replace the trusted root of a verifying client.
pair(#{cert := Cert, key := Key, root := Root}, ServerOptions, ClientOptions) ->
    {ok, Listen} = ssl:listen(0, [
        {ip, {127, 0, 0, 1}}, {active, false}, {cert, Cert}, {key, Key}, {log_level, none} | ServerOptions
    ]),
    {ok, {_, Port}} = ssl:sockname(Listen),
    Self = self(),
    spawn(fun() ->
        {ok, Accepted} = ssl:transport_accept(Listen),
        Result = ssl:handshake(Accepted, 5000),
        case Result of
            {ok, Socket} -> ssl:controlling_process(Socket, Self);
            _ -> ok
        end,
        Self ! {server, Result}
    end),
    Options = [{cacerts, [Root]}, {active, false}, {log_level, none} | ClientOptions],
    Client = ssl:connect("localhost", Port, Options, 5000),
    Accepted =
        receive
            {server, {ok, Socket}} -> Socket;
            {server, Error} -> alert(Error)
        after 5000 -> timeout
        end,
    {Listen, alert(Client), Accepted}.

%% An error with its tls_alert text left out: OTP's texts name its own states and source lines.
alert({error, {tls_alert, {Description, _Text}}}) -> {error, {tls_alert, Description}};
alert(Other) -> Other.

close_all(Sockets) -> [ssl:close(S) || S <- Sockets, element(1, S) == sslsocket].

echo(From, To, Data) ->
    ok = ssl:send(From, Data),
    ssl:recv(To, byte_size(Data), 2000).

%% The labels of key log lines, and whether each line is LABEL, client random and secret in upper-case hexadecimal.
labels(Lines) -> [hd(string:split(Line, " ")) || Line <- Lines].

well_formed(Line) ->
    case string:split(Line, " ", all) of
        [_, Random, Secret] -> length(Random) == 64 andalso hex(Random) andalso hex(Secret);
        _ -> false
    end.

hex(Text) -> lists:all(fun(C) -> (C >= $0 andalso C =< $9) orelse (C >= $A andalso C =< $F) end, Text).

keylog(Socket) ->
    {ok, [{keylog, Lines}]} = ssl:connection_information(Socket, [keylog]),
    [lists:flatten(Line) || Line <- Lines].

%% TLS 1.3: renegotiation and key updates, and the key log of keep_secrets after each update.
tls13(Server) ->
    Options = [{keep_secrets, true}, {versions, ['tlsv1.3']}],
    {Listen, {ok, Client}, Accepted} = pair(Server, Options, Options),
    show("renegotiate tlsv1.3", ssl:renegotiate(Client)),
    show("renegotiate listen", ssl:renegotiate(Listen)),
    show("update_keys listen", ssl:update_keys(Listen, write)),
    show("update_keys bad", ssl:update_keys(Client, bad)),
    Keylog0 = keylog(Client),
    show("keylog", labels(Keylog0)),
    show("keylog well formed", lists:all(fun well_formed/1, Keylog0)),
    show("keylog same on both sides", Keylog0 == keylog(Accepted)),
    show("update_keys write", ssl:update_keys(Client, write)),
    show("echo to server", echo(Client, Accepted, <<"one">>)),
    show("echo to client", echo(Accepted, Client, <<"two">>)),
    show("client keylog after write", labels(keylog(Client))),
    show("server keylog after write", labels(keylog(Accepted))),
    show("update_keys read_write", ssl:update_keys(Client, read_write)),
    show("echo to server", echo(Client, Accepted, <<"three">>)),
    show("echo to client", echo(Accepted, Client, <<"four">>)),
    Keylog2 = keylog(Client),
    show("client keylog after read_write", labels(Keylog2)),
    show("server keylog after read_write", labels(keylog(Accepted))),
    show("traffic secrets agree", Keylog2 == keylog(Accepted)),
    {ok, Secrets} = ssl:connection_information(Client, [client_random, server_random, master_secret, keylog]),
    % Clause has no TLS 1.3 master_secret (docs/differences.md#ssl).
    show("secret items", [K || {K, _} <- Secrets, K =/= master_secret]),
    close_all([Client, Accepted, Listen]).

%% TLS 1.3 exporters: materials agree, no_context is an empty context, and a consumed exporter secret is gone.
exporters13(Server) ->
    Options = [{versions, ['tlsv1.3']}],
    {Listen, {ok, Client}, Accepted} = pair(Server, Options, Options),
    Labels = [<<"EXPERIMENTAL a">>, <<"EXPERIMENTAL b">>],
    {ok, Client1} = ssl:export_key_materials(Client, Labels, [no_context, <<>>], [16, 32], false),
    {ok, Server1} = ssl:export_key_materials(Accepted, Labels, [<<>>, no_context], [16, 32], false),
    show("export sizes", [byte_size(M) || M <- Client1]),
    show("export agrees", Client1 == Server1),
    {ok, [Empty]} = ssl:export_key_materials(Client, [<<"EXPERIMENTAL a">>], [<<>>], [16], false),
    show("no_context is empty", Empty == hd(Client1)),
    show(
        "contexts differ",
        Client1 =/= element(2, ssl:export_key_materials(Client, Labels, [<<"x">>, <<>>], [16, 32], false))
    ),
    show("export unequal lists", ssl:export_key_materials(Client, [<<"a">>], [no_context, <<>>], [16], false)),
    show("export bad context", ssl:export_key_materials(Client, [<<"a">>], [id(foo)], [16], false)),
    show("export none", ssl:export_key_materials(Client, [], [], [], false)),
    show("export consumes", element(1, ssl:export_key_materials(Client, [<<"a">>], [<<>>], [8]))),
    show("export consumed", ssl:export_key_materials(Client, [<<"a">>], [<<>>], [8])),
    show("export consumed, keep", ssl:export_key_materials(Client, [<<"a">>], [<<>>], [8], false)),
    {ok, Prf} = ssl:prf(Accepted, master_secret, <<"EXPERIMENTAL a">>, [client_random, server_random], 16),
    show("prf is export", Prf == hd(Client1)),
    show("prf consumed", ssl:prf(Accepted, master_secret, <<"EXPERIMENTAL a">>, [client_random, server_random], 16)),
    show("export listen", ssl:export_key_materials(Listen, [<<"a">>], [<<>>], [8])),
    show("export/5 listen", reason(fun() -> ssl:export_key_materials(Listen, [<<"a">>], [<<>>], [8], true) end)),
    show("prf listen", ssl:prf(Listen, master_secret, <<"a">>, [client_random, server_random], 8)),
    close_all([Client, Accepted, Listen]).

%% The class and reason of a call's exception, without its stack.
reason(Fun) ->
    try Fun() of
        Value -> Value
    catch
        Class:Reason -> {Class, Reason}
    end.

%% TLS 1.2: the security items without keep_secrets, exporters and the forms of prf/5.
tls12(Server) ->
    Options = [{versions, ['tlsv1.2']}],
    {Listen, {ok, Client}, Accepted} = pair(Server, Options, Options),
    Items = [client_random, server_random, master_secret, keylog],
    {ok, Secrets} = ssl:connection_information(Client, Items),
    show("secret items", [{K, byte_size(V)} || {K, V} <- Secrets]),
    show("master secret agrees", Secrets == element(2, ssl:connection_information(Accepted, Items))),
    Label = <<"EXPERIMENTAL a">>,
    {ok, Plain} = ssl:export_key_materials(Client, [Label], [no_context], [16]),
    {ok, Empty} = ssl:export_key_materials(Client, [Label], [<<>>], [16]),
    show("export", {is_binary(Plain), byte_size(Plain)}),
    show("export agrees", {ok, Plain} == ssl:export_key_materials(Accepted, [Label], [no_context], [16], true)),
    show("no_context differs", Plain =/= Empty),
    show("export again", element(1, ssl:export_key_materials(Client, [Label], [<<>>], [8]))),
    show("prf", ssl:prf(Client, master_secret, Label, [client_random, server_random], 16) == {ok, Plain}),
    show("prf context", ssl:prf(Client, master_secret, Label, [client_random, server_random, <<>>], 16) == {ok, Empty}),
    {ok, Legacy} = ssl:prf(Client, <<"secret">>, <<"label">>, [<<"seed">>], 8),
    show("prf legacy", {
        byte_size(Legacy), {ok, Legacy} == ssl:prf(Accepted, <<"secret">>, <<"label">>, [<<"seed">>], 8)
    }),
    show("prf legacy, bad seed", reason(fun() -> ssl:prf(Client, master_secret, <<"l">>, [server_random], 8) end)),
    close_all([Client, Accepted, Listen]).

%% TLS 1.2 renegotiation: by the client (refused for a while after one), by the server, and refused by a server
%% without client_renegotiation. After a refusal OTP's connection goes on but Clause's ends (docs/differences.md#ssl),
%% so the refused connections only close.
renegotiation(Server) ->
    Options = [{versions, ['tlsv1.2']}],
    {Listen, {ok, Client}, Accepted} = pair(Server, Options, Options),
    show("client renegotiates", ssl:renegotiate(Client)),
    show("echo to server", echo(Client, Accepted, <<"x">>)),
    show("server renegotiates", ssl:renegotiate(Accepted)),
    show("echo to client", echo(Accepted, Client, <<"y">>)),
    show("server renegotiates again", ssl:renegotiate(Accepted)),
    show("echo to server", echo(Client, Accepted, <<"z">>)),
    show("client renegotiates again", ssl:renegotiate(Client)),
    close_all([Client, Accepted, Listen]),
    {Listen2, {ok, Client2}, Accepted2} = pair(Server, [{client_renegotiation, false} | Options], Options),
    show("echo to server", echo(Client2, Accepted2, <<"w">>)),
    show("refused renegotiation", ssl:renegotiate(Client2)),
    close_all([Client2, Accepted2, Listen2]).

%% {keylog, Fun} calls of both sides: TLS 1.3 handshake and traffic secrets, key updates, TLS 1.2 handshakes.
collector(Side) ->
    Self = self(),
    fun(#{items := Items, client_random := Random} = Info) ->
        Self ! {keylog, Side, lists:sort(maps:keys(Info)), byte_size(Random), labels([lists:flatten(I) || I <- Items])}
    end.

calls() ->
    receive
        {keylog, Side, Keys, RandomSize, Labels} -> [{Side, Keys, RandomSize, Labels} | calls()]
    after 300 -> []
    end.

keylog_funs(#{root := Root} = Server) ->
    Keep = fun(Side, Version) -> [{keep_secrets, {keylog, collector(Side)}}, {versions, [Version]}] end,
    {Listen, {ok, Client}, Accepted} = pair(Server, Keep(server, 'tlsv1.3'), Keep(client, 'tlsv1.3')),
    show("tlsv1.3 handshake", lists:sort(calls())),
    show("secrets of a keylog fun", funless(fun() -> ssl:connection_information(Client, [keylog]) end)),
    ok = ssl:update_keys(Client, write),
    show("echo to server", echo(Client, Accepted, <<"a">>)),
    show("update write", lists:sort(calls())),
    ok = ssl:update_keys(Client, read_write),
    show("echo to server", echo(Client, Accepted, <<"b">>)),
    show("echo to client", echo(Accepted, Client, <<"c">>)),
    show("update read_write", lists:sort(calls())),
    close_all([Client, Accepted, Listen]),
    {Listen2, {ok, Client2}, Accepted2} = pair(Server, Keep(server, 'tlsv1.2'), [
        {cacerts, [Root]} | Keep(client, 'tlsv1.2')
    ]),
    show("tlsv1.2 handshake", lists:sort(calls())),
    show("renegotiate", ssl:renegotiate(Client2)),
    show("tlsv1.2 renegotiation", lists:sort(calls())),
    close_all([Client2, Accepted2, Listen2]).

%% The reason of a failure whose term holds a fun, with the fun left out.
funless(Fun) ->
    try Fun() of
        {error, {options, {keep_secrets, {Type, F}}}} when is_function(F) -> {error, {options, {keep_secrets, Type}}};
        Value -> Value
    catch
        error:{case_clause, {keep_secrets, {Type, F}}} when is_function(F) -> {error, {case_clause, keep_secrets, Type}}
    end.

%% {keylog_hs, Fun}: the secrets of TLS 1.3 handshakes that fail with a fatal alert, nothing for TLS 1.2 or success.
keylog_handshakes(Server) ->
    Keep = fun(Side, Version) -> [{keep_secrets, {keylog_hs, collector(Side)}}, {versions, [Version]}] end,
    Untrusting = fun(Version) -> [{cacerts, []}, {verify, verify_peer} | Keep(client, Version)] end,
    {Listen, Client, Accepted} = pair(Server, Keep(server, 'tlsv1.3'), Untrusting('tlsv1.3')),
    show("tlsv1.3 failure", {Client, Accepted}),
    % A libssl client has no handshake secret of its own before the server's Finished (docs/differences.md#ssl).
    show("tlsv1.3 failure secrets", [
        {Side, Keys, Size, Labels -- ["CLIENT_HANDSHAKE_TRAFFIC_SECRET" || Side == client]}
     || {Side, Keys, Size, Labels} <- lists:sort(calls())
    ]),
    ssl:close(Listen),
    {Listen2, {ok, Client2}, Accepted2} = pair(Server, Keep(server, 'tlsv1.3'), Keep(client, 'tlsv1.3')),
    show("tlsv1.3 success secrets", calls()),
    close_all([Client2, Accepted2, Listen2]),
    {Listen3, Client3, Accepted3} = pair(Server, Keep(server, 'tlsv1.2'), Untrusting('tlsv1.2')),
    show("tlsv1.2 failure", {Client3, Accepted3}),
    show("tlsv1.2 failure secrets", calls()),
    ssl:close(Listen3).

bad_keep_secrets() ->
    Connect = fun(Keep) -> fun() -> ssl:connect("localhost", 1, [{verify, verify_none}, {keep_secrets, Keep}]) end end,
    [Connect({keylog, foo}), Connect({other, fun() -> ok end}), Connect('maybe')].

%% A close/2 downgrade after the peer's close_notify: a passive connection has not acted on it and hands its transport
%% over, an active one has closed.
late_downgrade(Server, Active) ->
    {Listen, {ok, Client}, Accepted} = pair(Server, [], []),
    ok = ssl:setopts(Accepted, [{active, Active}]),
    ok = ssl:close(Client),
    timer:sleep(200),
    Result =
        case ssl:close(Accepted, {self(), 500}) of
            {ok, Port} -> {ok, is_port(Port)};
            Other -> Other
        end,
    Messages =
        receive
            {ssl_closed, Accepted} -> [ssl_closed]
        after 100 -> []
        end,
    show("late downgrade", {Active, Result, Messages}),
    ssl:close(Listen).
