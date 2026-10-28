%% CRL checks of ssl connections over loopback (plan 21 step LIB81D4a): crl_check true, peer and best_effort against
%% CRLs in ssl_crl_cache by distribution point and by issuer, revoked and missing CRLs, a custom crl_cache module, a
%% verify fun's view of a revocation, and the cache functions' and options' errors.
-module(ssl_crls).
-export([main/1]).
-behaviour(ssl_crl_cache_api).
-export([lookup/3, select/2, fresh_crl/2]).

-include_lib("public_key/include/public_key.hrl").

-define(URI, "http://localhost:8080/ca.crl").

main(_) ->
    ok = ssl:start(),
    Root = public_key:pkix_test_root_cert("CRL root", [{key, {namedCurve, secp256r1}}, {digest, sha256}]),
    WithDp = peer(Root, [distribution_point()]),
    WithoutDp = peer(Root, []),
    Good = crl(Root, []),
    Revoked = crl(Root, [serial(maps:get(cert, WithDp)), serial(maps:get(cert, WithoutDp))]),
    by_distribution_point(Root, WithDp, Good, Revoked),
    by_issuer(Root, WithoutDp, Revoked),
    custom_cache(Root, WithDp, Revoked),
    verify_fun(Root, WithDp, Revoked),
    show("cache errors", [
        ssl_crl_cache:insert({file, "missing.pem"}),
        ssl_crl_cache:insert("ftp://localhost/ca.crl", {der, [Good]}),
        ssl_crl_cache:delete("ftp://localhost/ca.crl")
    ]),
    show("option errors", [connect_error(Root, Options) || Options <- errors()]),
    io:format("ssl_crls: ok~n").

show(Label, Value) -> io:format("~s ~p~n", [Label, Value]).

distribution_point() ->
    #'Extension'{
        extnID = ?'id-ce-cRLDistributionPoints',
        critical = false,
        extnValue = [
            #'DistributionPoint'{
                distributionPoint = {fullName, [{uniformResourceIdentifier, ?URI}]},
                reasons = asn1_NOVALUE,
                cRLIssuer = asn1_NOVALUE
            }
        ]
    }.

%% A server certificate of the root naming localhost, with Extensions.
peer(Root, Extensions) ->
    AltName = #'Extension'{extnID = ?'id-ce-subjectAltName', extnValue = [{dNSName, "localhost"}], critical = false},
    Config = public_key:pkix_test_data(#{
        root => Root,
        intermediates => [],
        peer => [{key, {namedCurve, secp256r1}}, {digest, sha256}, {extensions, [AltName | Extensions]}]
    }),
    #{cert => proplists:get_value(cert, Config), key => proplists:get_value(key, Config)}.

serial(Der) ->
    #'OTPCertificate'{tbsCertificate = #'OTPTBSCertificate'{serialNumber = Serial}} =
        public_key:pkix_decode_cert(Der, otp),
    Serial.

%% A CRL of the root revoking the serial numbers, valid from yesterday to tomorrow.
crl(#{cert := RootCert, key := RootKey}, Serials) ->
    #'Certificate'{tbsCertificate = #'TBSCertificate'{subject = Issuer}} = public_key:pkix_decode_cert(RootCert, plain),
    Algorithm = #'AlgorithmIdentifier'{algorithm = ?'ecdsa-with-SHA256', parameters = asn1_NOVALUE},
    Revoked =
        case Serials of
            [] ->
                asn1_NOVALUE;
            _ ->
                [
                    #'TBSCertList_revokedCertificates_SEQOF'{
                        userCertificate = S, revocationDate = time(-1), crlEntryExtensions = asn1_NOVALUE
                    }
                 || S <- Serials
                ]
        end,
    TBS = #'TBSCertList'{
        version = v2,
        signature = Algorithm,
        issuer = Issuer,
        thisUpdate = time(-1),
        nextUpdate = time(1),
        revokedCertificates = Revoked,
        crlExtensions = asn1_NOVALUE
    },
    Signature = public_key:sign(public_key:der_encode('TBSCertList', TBS), sha256, RootKey),
    public_key:der_encode('CertificateList', #'CertificateList'{
        tbsCertList = TBS, signatureAlgorithm = Algorithm, signature = Signature
    }).

time(Days) ->
    Seconds = calendar:datetime_to_gregorian_seconds(calendar:universal_time()) + Days * 86400,
    {{Y, Mo, D}, {H, Mi, S}} = calendar:gregorian_seconds_to_datetime(Seconds),
    {utcTime, lists:flatten(io_lib:format("~2..0w~2..0w~2..0w~2..0w~2..0w~2..0wZ", [Y rem 100, Mo, D, H, Mi, S]))}.

%% One connection to a server of Peer, the client trusting the root with ClientOptions: what both sides see.
connect(#{cert := RootCert}, #{cert := Cert, key := Key}, ClientOptions) ->
    {ok, Listen} = ssl:listen(0, [{ip, {127, 0, 0, 1}}, {active, false}, {cert, Cert}, {key, Key}, {log_level, none}]),
    {ok, {_, Port}} = ssl:sockname(Listen),
    Self = self(),
    spawn(fun() ->
        {ok, Accepted} = ssl:transport_accept(Listen),
        Self ! {server, ssl:handshake(Accepted, 5000)}
    end),
    Client = ssl:connect(
        "localhost", Port, [{cacerts, [RootCert]}, {active, false}, {log_level, none} | ClientOptions], 5000
    ),
    Server =
        receive
            {server, R} -> R
        after 5000 -> timeout
        end,
    [ssl:close(S) || {ok, S} <- [Client, Server]],
    ssl:close(Listen),
    {alert(Client), alert(Server)}.

%% A result without its socket or tls_alert text.
alert({ok, _}) -> ok;
alert({error, {tls_alert, {Description, _Text}}}) -> {error, Description};
alert(Other) -> Other.

%% CRLs by the certificate's distribution point.
by_distribution_point(Root, Peer, Good, Revoked) ->
    ok = ssl_crl_cache:insert(?URI, {der, [Good]}),
    show("good crl", [connect(Root, Peer, [{crl_check, C}]) || C <- [true, peer, best_effort, false]]),
    ok = ssl_crl_cache:delete(?URI),
    ok = ssl_crl_cache:insert(?URI, {der, [Revoked]}),
    show("revoked", [connect(Root, Peer, [{crl_check, C}]) || C <- [true, peer, best_effort, false]]),
    ok = ssl_crl_cache:delete(?URI),
    show("no crl", [connect(Root, Peer, [{crl_check, C}]) || C <- [true, peer, best_effort]]).

%% CRLs by their issuer, for a certificate without a distribution point.
by_issuer(Root, Peer, Revoked) ->
    show("no crl by issuer", [connect(Root, Peer, [{crl_check, C}]) || C <- [true, best_effort]]),
    ok = ssl_crl_cache:insert({der, [Revoked]}),
    timer:sleep(100),
    show("revoked by issuer", [connect(Root, Peer, [{crl_check, C}]) || C <- [true, best_effort]]),
    ok = ssl_crl_cache:delete({der, [Revoked]}),
    timer:sleep(100),
    show("deleted by issuer", connect(Root, Peer, [{crl_check, true}])).

%% A crl_cache module of its own: this one, whose handle holds the CRLs and whose calls are reported.
custom_cache(Root, Peer, Revoked) ->
    Options = [{crl_check, true}, {crl_cache, {?MODULE, {{self(), [Revoked]}, [extra]}}}],
    show("custom cache", connect(Root, Peer, Options)),
    show("custom calls", lists:usort(calls())).

calls() ->
    receive
        {crl_call, Call} -> [Call | calls()]
    after 0 -> []
    end.

lookup(#'DistributionPoint'{}, _Issuer, {{Pid, CRLs}, _Args}) ->
    Pid ! {crl_call, lookup},
    CRLs.

select(_Issuer, {{Pid, CRLs}, _Args}) ->
    Pid ! {crl_call, select},
    CRLs.

fresh_crl(#'DistributionPoint'{}, CRL) ->
    CRL.

%% A verify fun sees the revocation as the peer's failure.
verify_fun(Root, Peer, Revoked) ->
    ok = ssl_crl_cache:insert(?URI, {der, [Revoked]}),
    Self = self(),
    Fun = fun
        (_, {bad_cert, _} = Reason, State) ->
            Self ! {event, Reason},
            {valid, State};
        (_, {extension, _}, State) ->
            {unknown, State};
        (_, Event, State) ->
            Self ! {event, Event},
            {valid, State}
    end,
    show("verify fun", connect(Root, Peer, [{crl_check, true}, {verify_fun, {Fun, []}}])),
    show("verify fun events", events()),
    ok = ssl_crl_cache:delete(?URI).

events() ->
    receive
        {event, Event} -> [Event | events()]
    after 0 -> []
    end.

errors() ->
    [
        [{crl_check, sometimes}],
        [{crl_cache, cache}],
        [{crl_cache, {ssl_crl_cache, {internal, nolist}}}]
    ].

connect_error(#{cert := RootCert}, Options) ->
    case ssl:connect("localhost", 1, [{cacerts, [RootCert]} | Options], 1000) of
        {error, {options, _} = Reason} -> {error, Reason};
        {error, _} -> accepted
    end.
