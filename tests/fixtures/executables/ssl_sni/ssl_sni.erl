%% Server name indication on servers over loopback (plan 21 step LIB81D3a): sni_hosts and sni_fun choosing the
%% certificate and options of a connection by the name the client sends, unrecognized names, names with a trailing
%% dot, and the options' errors.
-module(ssl_sni).
-export([main/1]).

-include_lib("public_key/include/public_key.hrl").

main(_) ->
    ok = ssl:start(),
    Default = chain("localhost"),
    Other = chain("other.example"),
    hosts(Default, Other),
    funs(Default, Other),
    show("server option errors", [listen_error(Default, Options) || Options <- server_errors(Other)]),
    show("client option errors", [connect_error(Options) || Options <- client_errors()]),
    io:format("ssl_sni: ok~n").

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

%% A listening socket of the default certificate with the SNI options.
listener(#{cert := Cert, key := Key}, Options) ->
    {ok, Listen} = ssl:listen(0, [
        {ip, {127, 0, 0, 1}}, {active, false}, {cert, Cert}, {key, Key}, {log_level, none} | Options
    ]),
    Listen.

%% A connection that sends the server name Name and trusts Root: what the client and the server see.
connect(Listen, Name, #{root := Root}, Options) ->
    {ok, {_, Port}} = ssl:sockname(Listen),
    Self = self(),
    spawn(fun() ->
        {ok, Accepted} = ssl:transport_accept(Listen),
        Result = ssl:handshake(Accepted, 5000),
        case Result of
            {ok, Socket} -> ok = ssl:controlling_process(Socket, Self);
            _ -> ok
        end,
        Self ! {server, Result}
    end),
    Client = ssl:connect(
        "localhost",
        Port,
        [{cacerts, [Root]}, {server_name_indication, Name}, {active, false}, {log_level, none} | Options],
        5000
    ),
    Server =
        receive
            {server, R} -> R
        after 5000 -> timeout
        end,
    Seen = {client_side(Client), server_side(Server)},
    [ssl:close(S) || {ok, S} <- [Client, Server]],
    Seen.

client_side({ok, Socket}) ->
    {ok, Cert} = ssl:peercert(Socket),
    {ok, [{protocol, Protocol}]} = ssl:connection_information(Socket, [protocol]),
    {Protocol, subject_name(Cert)};
client_side(Error) ->
    alert(Error).

server_side({ok, Socket}) ->
    {ok, Info} = ssl:connection_information(Socket, [sni_hostname]),
    Info;
server_side(Error) ->
    alert(Error).

subject_name(Der) ->
    #'OTPCertificate'{tbsCertificate = #'OTPTBSCertificate'{extensions = Extensions}} =
        public_key:pkix_decode_cert(Der, otp),
    [Names] = [V || #'Extension'{extnID = ?'id-ce-subjectAltName', extnValue = V} <- Extensions],
    Names.

%% An error with its tls_alert text left out: OTP's texts name its own states and source lines.
alert({error, {tls_alert, {Description, _Text}}}) -> {error, {tls_alert, Description}};
alert(Other) -> Other.

%% sni_hosts: the options of a listed name replace the server's; other names keep them.
hosts(Default, #{cert := Cert, key := Key} = Other) ->
    Listen = listener(Default, [{sni_hosts, [{"other.example", [{cert, Cert}, {key, Key}]}]}]),
    show("sni_hosts listed", connect(Listen, "other.example", Other, [])),
    show("sni_hosts default", connect(Listen, "localhost", Default, [])),
    show("sni_hosts unlisted", connect(Listen, "unlisted.example", Default, [{verify, verify_none}])),
    show("sni_hosts trailing dot", connect(Listen, "other.example.", Other, [{verify, verify_none}])),
    show("sni_hosts no name", connect(Listen, disable, Default, [{verify, verify_none}])),
    ssl:close(Listen).

%% sni_fun: options, undefined for the server's own, unrecognized for an unrecognized_name alert.
funs(Default, #{cert := Cert, key := Key} = Other) ->
    Self = self(),
    Fun = fun(Name) ->
        Self ! {sni, Name},
        case Name of
            "other.example" -> [{cert, Cert}, {key, Key}, {versions, ['tlsv1.2']}];
            "localhost" -> undefined;
            _ -> unrecognized
        end
    end,
    Listen = listener(Default, [{sni_fun, Fun}]),
    show("sni_fun options", connect(Listen, "other.example", Other, [])),
    show("sni_fun undefined", connect(Listen, "localhost", Default, [])),
    show("sni_fun unrecognized", connect(Listen, "unknown.example", Default, [])),
    % OTP 29.1.1 calls the fun twice for a name it answers with undefined.
    show("sni_fun names", lists:usort(names())),
    ssl:close(Listen).

names() ->
    receive
        {sni, Name} -> [Name | names()]
    after 0 -> []
    end.

server_errors(_Other) ->
    [
        [{sni_hosts, [{"other", [{sni_hosts, []}]}]}],
        [{sni_hosts, [{"other", [{sni_hosts, [{"x", []}]}]}]}],
        [{sni_hosts, [other]}],
        [{sni_hosts, [{"", []}]}],
        [{sni_hosts, other}],
        [{sni_fun, fun(_, _) -> undefined end}],
        [{sni_fun, fun(_) -> undefined end}, {sni_hosts, [{"other", []}]}],
        [{server_name_indication, "other"}]
    ].

client_errors() ->
    [
        [{sni_fun, fun(_) -> undefined end}],
        [{sni_hosts, []}],
        [{server_name_indication, other}]
    ].

listen_error(#{cert := Cert, key := Key}, Options) ->
    funless(ssl:listen(0, [{cert, Cert}, {key, Key} | Options])).

connect_error(Options) ->
    funless(ssl:connect("localhost", 1, [{verify, verify_none} | Options], 1000)).

%% Results with their funs replaced by their arities, which print differently.
funless({ok, Socket}) ->
    ssl:close(Socket),
    ok;
funless({error, {options, {Key, Fun}}}) when is_function(Fun) ->
    {error, {options, {Key, {'fun', element(2, erlang:fun_info(Fun, arity))}}}};
funless(Other) ->
    Other.
