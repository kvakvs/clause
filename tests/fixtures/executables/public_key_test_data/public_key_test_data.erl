%% public_key:pkix_test_data/1 and pkix_test_root_cert/2 (plan 21 step LIB80D5): generated roots and chains shown
%% without their random parts (keys, serial numbers, signatures, today's dates): subjects, signature algorithms, key
%% types, extensions and validity, then checked with pkix_path_validation/3. Covers one chain and a server and client
%% pair, given roots and keys, digests, RSA padding, RSASSA-PSS, Ed25519, validity and extensions.
-module(public_key_test_data).
-export([main/1]).

-include_lib("public_key/include/public_key.hrl").

main(_) ->
    Rsa = public_key:generate_key({rsa, 1024, 65537}),
    #{cert := RootDer, key := RootKey} =
        Root = public_key:pkix_test_root_cert("Test root", [{key, Rsa}, {digest, sha256}]),
    io:format("root ~p ~p~n", [lists:sort(maps:keys(Root)), RootKey =:= Rsa]),
    show(RootDer),
    io:format("self signed ~p~n", [public_key:pkix_is_self_signed(RootDer)]),
    Chain = public_key:pkix_test_data(#{
        root => [{key, {rsa, 1024, 65537}}],
        intermediates => [[{key, {namedCurve, secp256r1}}, {digest, sha384}], [{key, {namedCurve, secp384r1}}]],
        peer => [{key, {namedCurve, secp256r1}}, {digest, sha256}]
    }),
    io:format("chain ~p~n", [[Key || {Key, _} <- Chain]]),
    node_config(Chain),
    Pair = public_key:pkix_test_data(#{
        server_chain => #{root => Root, intermediates => [], peer => [{key, {namedCurve, ed25519}}]},
        client_chain => #{
            root => [{key, {namedCurve, secp256r1}}],
            intermediates => [[{key, {rsa, 1024, 65537}}, {rsa_padding, rsa_pss_rsae}]],
            peer => [{validity, {{2048, 12, 31}, {2050, 1, 2}}}]
        }
    }),
    io:format("pair ~p~n", [lists:sort(maps:keys(Pair))]),
    #{server_config := Server, client_config := Client} = Pair,
    [show_config(Name, Config) || {Name, Config} <- [{server, Server}, {client, Client}]],
    Pss = #'RSASSA-PSS-params'{
        hashAlgorithm = #'HashAlgorithm'{algorithm = ?'id-sha256'},
        maskGenAlgorithm = #'MaskGenAlgorithm'{
            algorithm = ?'id-mgf1', parameters = #'HashAlgorithm'{algorithm = ?'id-sha256'}
        },
        saltLength = 32,
        trailerField = 1
    },
    #{cert := PssDer} = public_key:pkix_test_root_cert("PSS root", [{key, {Rsa, Pss}}]),
    show(PssDer),
    Usage = #'Extension'{extnID = ?'id-ce-keyUsage', extnValue = [keyCertSign], critical = true},
    Policies = #'Extension'{
        extnID = ?'id-ce-certificatePolicies',
        extnValue = [#'PolicyInformation'{policyIdentifier = ?anyPolicy}],
        critical = false
    },
    #{cert := ExtensionsDer} = public_key:pkix_test_root_cert("Extensions root", [
        {key, {namedCurve, secp256r1}}, {extensions, [Usage, Policies]}
    ]),
    show(ExtensionsDer),
    #{cert := DefaultDer} = public_key:pkix_test_root_cert("Default root", []),
    show(DefaultDer),
    io:format("public_key_test_data: ok~n").

%% A node configuration: the peer, its key type, the CA certificates and the validated path.
node_config(Config) ->
    Cert = proplists:get_value(cert, Config),
    {KeyType, KeyDer} = proplists:get_value(key, Config),
    CAs = proplists:get_value(cacerts, Config),
    io:format("key ~p ~p~n", [KeyType, element(1, public_key:der_decode(KeyType, KeyDer))]),
    show(Cert),
    [show(CA) || CA <- CAs],
    [RootDer | Issuers] = CAs,
    Path = lists:reverse([Cert | lists:droplast(Issuers)]),
    io:format("serials increase ~p~n", [increasing([serial(C) || C <- [RootDer | Path]])]),
    io:format("path ~p~n", [element(1, public_key:pkix_path_validation(RootDer, Path, []))]).

%% A server or client configuration: the peer and the CAs, whose first is the other side's root.
show_config(Name, Config) ->
    Cert = proplists:get_value(cert, Config),
    {KeyType, _} = proplists:get_value(key, Config),
    CAs = proplists:get_value(cacerts, Config),
    io:format("~p key ~p cacerts ~p~n", [Name, KeyType, [common_name(CA) || CA <- CAs]]),
    show(Cert),
    [show(CA) || CA <- tl(CAs)].

%% A certificate without its random parts; validity dates are shown relative to today.
show(Der) ->
    #'OTPCertificate'{tbsCertificate = TBS, signatureAlgorithm = SigAlg} = public_key:pkix_decode_cert(Der, otp),
    #'OTPTBSCertificate'{
        version = Version,
        subject = Subject,
        issuer = Issuer,
        validity = #'Validity'{notBefore = NotBefore, notAfter = NotAfter},
        subjectPublicKeyInfo = #'OTPSubjectPublicKeyInfo'{algorithm = KeyAlg, subjectPublicKey = Key},
        extensions = Extensions
    } = TBS,
    io:format("cert ~p~n  subject ~p~n  issuer ~p~n  signature ~p~n  key ~p ~p~n  validity ~p ~p~n  extensions ~p~n", [
        Version,
        names(Subject),
        names(Issuer),
        {SigAlg#'SignatureAlgorithm'.algorithm, params(SigAlg#'SignatureAlgorithm'.parameters)},
        KeyAlg#'PublicKeyAlgorithm'.algorithm,
        key_type(Key),
        relative(NotBefore),
        relative(NotAfter),
        [
            {ID, Critical, value(ID, Value)}
         || #'Extension'{extnID = ID, critical = Critical, extnValue = Value} <- Extensions
        ]
    ]).

names({rdnSequence, RDNs}) -> [{Type, Value} || [#'AttributeTypeAndValue'{type = Type, value = Value}] <- RDNs].

common_name(Der) ->
    #'OTPCertificate'{tbsCertificate = #'OTPTBSCertificate'{subject = Subject}} = public_key:pkix_decode_cert(Der, otp),
    proplists:get_value(?'id-at-commonName', names(Subject)).

params(#'RSASSA-PSS-params'{saltLength = SaltLength}) -> {pss, SaltLength};
params({namedCurve, Curve}) -> Curve;
params(Other) -> Other.

key_type(#'ECPoint'{point = Point}) -> {ec_point, byte_size(Point)};
key_type(#'RSAPublicKey'{publicExponent = E}) -> {rsa, E};
key_type(Other) -> element(1, Other).

%% The local hostname of a server peer's subjectAltName shown as localhost.
value(?'id-ce-subjectAltName', [{dNSName, Host}]) ->
    [{dNSName, Host =:= net_adm:localhost()}];
value(_, Value) ->
    Value.

%% A default validity time as its type and days from today, others as they are.
relative({Type, Time}) ->
    case days(Type, lists:sublist(Time, length(Time) - 7)) of
        Days when Days >= -1, Days =< 7 -> {Type, Days};
        _ -> {Type, Time}
    end.

days(utcTime, [Y1, Y2 | MonthDay]) ->
    days(generalTime, [$2, $0, Y1, Y2 | MonthDay]);
days(generalTime, [Y1, Y2, Y3, Y4, M1, M2, D1, D2]) ->
    Date = {list_to_integer([Y1, Y2, Y3, Y4]), list_to_integer([M1, M2]), list_to_integer([D1, D2])},
    calendar:date_to_gregorian_days(Date) - calendar:date_to_gregorian_days(date()).

serial(Der) ->
    #'OTPCertificate'{tbsCertificate = #'OTPTBSCertificate'{serialNumber = Serial}} = public_key:pkix_decode_cert(
        Der, otp
    ),
    Serial.

increasing([A, B | Rest]) -> A < B andalso increasing([B | Rest]);
increasing(_) -> true.
