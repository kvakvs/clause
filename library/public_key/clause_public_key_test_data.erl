%% clause_public_key_test_data: public_key:pkix_test_data/1 and pkix_test_root_cert/2 (plan 21 step LIB80D5,
%% docs/libraries/public_key.md#test-data): certificate chains for testing as OTP's pubkey_cert makes them: a root,
%% intermediate CAs and a peer per chain, keys generated or given, OTP's subjects, validity and default extensions.
-module(clause_public_key_test_data).
-export([pkix_test_data/1, pkix_test_root_cert/2]).

-include("include/public_key.hrl").

%% public_key:pkix_test_data/1: a server and client configuration, or one node's for a single chain.
pkix_test_data(#{client_chain := ClientChain, server_chain := ServerChain}) ->
    #{root := ClientRoot, intermediates := ClientCAs, peer := ClientPeer} = ClientChain,
    #{root := ServerRoot, intermediates := ServerCAs, peer := ServerPeer} = ServerChain,
    #{cert := ServerRootCert, key := ServerRootKey} = root(ServerRoot),
    #{cert := ClientRootCert, key := ClientRootKey} = root(ClientRoot, "CLIENT ROOT CA"),
    [{ServerDERCert, ServerDERKey} | ServerCAsKeys] =
        cert_chain(server, ServerRootCert, ServerRootKey, ServerCAs ++ [ServerPeer]),
    [{ClientDERCert, ClientDERKey} | ClientCAsKeys] =
        cert_chain(client, ClientRootCert, ClientRootKey, ClientCAs ++ [ClientPeer]),
    #{
        server_config => [
            {cert, ServerDERCert}, {key, ServerDERKey}, {cacerts, ca_config(ClientRootCert, ServerCAsKeys)}
        ],
        client_config => [
            {cert, ClientDERCert}, {key, ClientDERKey}, {cacerts, ca_config(ServerRootCert, ClientCAsKeys)}
        ]
    };
pkix_test_data(#{root := Root, intermediates := CAs, peer := Peer}) ->
    #{cert := RootCert, key := RootKey} = root(Root),
    [{DERCert, DERKey} | CAsKeys] = cert_chain(server, RootCert, RootKey, CAs ++ [Peer]),
    [{cert, DERCert}, {key, DERKey}, {cacerts, ca_config(RootCert, CAsKeys)}].

root(Root) -> root(Root, "SERVER ROOT CA").

%% A root given as a certificate and key, or made from its options.
root(#{} = Root, _) -> Root;
root(RootConf, Name) when is_list(RootConf) -> pkix_test_root_cert(Name, RootConf).

%% public_key:pkix_test_root_cert/2: a self-signed root certificate and its key.
pkix_test_root_cert(Name, Opts) ->
    {SPubkeyInfo, PrivKey} = key_info(Opts),
    Issuer = subject("root", Name),
    OTPTBS = (cert_template())#'OTPTBSCertificate'{
        signature = sign_algorithm(PrivKey, Opts),
        issuer = Issuer,
        validity = validity(Opts),
        subject = Issuer,
        subjectPublicKeyInfo = SPubkeyInfo,
        extensions = extensions(undefined, ca, Opts)
    },
    #{cert => public_key:pkix_sign(OTPTBS, PrivKey), key => PrivKey}.

%% The peer and its CAs as {DerCert, {KeyType, DerKey}}, peer first and the root last.
cert_chain(Role, Root, RootKey, Opts) ->
    cert_chain(Role, Root, RootKey, Opts, 0, []).

cert_chain(Role, IssuerCert, IssuerKey, [PeerOpts], _, Acc) ->
    {SPubKeyInfo, PrivKey} = key_info(PeerOpts),
    Cert = cert(Role, IssuerCert, IssuerKey, SPubKeyInfo, {"admin", " Peer cert"}, PeerOpts, peer),
    [{Cert, encode_key(PrivKey)}, {IssuerCert, encode_key(IssuerKey)} | Acc];
cert_chain(Role, IssuerCert, IssuerKey, [CAOpts | Rest], N, Acc) ->
    {SPubKeyInfo, PrivKey} = key_info(CAOpts),
    Name = {"webadmin", " Intermediate CA " ++ integer_to_list(N)},
    Cert = cert(Role, IssuerCert, IssuerKey, SPubKeyInfo, Name, CAOpts, ca),
    cert_chain(Role, Cert, PrivKey, Rest, N + 1, [{IssuerCert, encode_key(IssuerKey)} | Acc]).

cert(Role, IssuerDer, IssuerKey, SPubKeyInfo, {Contact, Name}, Opts, Type) ->
    #'OTPCertificate'{tbsCertificate = #'OTPTBSCertificate'{subject = Issuer}} =
        public_key:pkix_decode_cert(IssuerDer, otp),
    OTPTBS = (cert_template())#'OTPTBSCertificate'{
        signature = sign_algorithm(IssuerKey, Opts),
        issuer = Issuer,
        validity = validity(Opts),
        subject = subject(Contact, atom_to_list(Role) ++ Name),
        subjectPublicKeyInfo = SPubKeyInfo,
        extensions = extensions(Role, Type, Opts)
    },
    public_key:pkix_sign(OTPTBS, IssuerKey).

ca_config(Root, CAsKeys) -> [Root | [CA || {CA, _} <- CAsKeys]].

cert_template() ->
    #'OTPTBSCertificate'{
        version = v3,
        serialNumber = erlang:unique_integer([positive, monotonic]),
        issuerUniqueID = asn1_NOVALUE,
        subjectUniqueID = asn1_NOVALUE
    }.

subject(Contact, Name) ->
    Opts = [
        {email, Contact ++ "@example.org"},
        {name, Name},
        {city, "Stockholm"},
        {country, "SE"},
        {org, "erlang"},
        {org_unit, "automated testing"}
    ],
    {rdnSequence, [[attribute(Opt)] || Opt <- Opts]}.

attribute({name, Name}) -> attribute(?'id-at-commonName', {printableString, Name});
attribute({email, Email}) -> attribute(?'id-emailAddress', Email);
attribute({city, City}) -> attribute(?'id-at-localityName', {printableString, City});
attribute({org, Org}) -> attribute(?'id-at-organizationName', {printableString, Org});
attribute({org_unit, OrgUnit}) -> attribute(?'id-at-organizationalUnitName', {printableString, OrgUnit});
attribute({country, Country}) -> attribute(?'id-at-countryName', Country).

attribute(Type, Value) -> #'AttributeTypeAndValue'{type = Type, value = Value}.

%% From yesterday to a week from today at 13:00, or the validity option's dates; UTCTime before 2049.
validity(Opts) ->
    Today = calendar:date_to_gregorian_days(date()),
    Default = {calendar:gregorian_days_to_date(Today - 1), calendar:gregorian_days_to_date(Today + 7)},
    {From, To} = proplists:get_value(validity, Opts, Default),
    #'Validity'{notBefore = validity_time(From), notAfter = validity_time(To)}.

validity_time({Year, Month, Day}) when Year >= 2049 ->
    {generalTime, lists:flatten(io_lib:format("~4..0w~2..0w~2..0w130000Z", [Year, Month, Day]))};
validity_time({Year, Month, Day}) ->
    [_, _, Y3, Y4] = integer_to_list(Year),
    {utcTime, lists:flatten(io_lib:format("~s~2..0w~2..0w130000Z", [[Y3, Y4], Month, Day]))}.

%% The signature algorithm a key signs certificates with (SHA-1 unless the digest option says otherwise).
sign_algorithm(#'RSAPrivateKey'{}, Opts) ->
    case proplists:get_value(rsa_padding, Opts, rsa_pkcs1_pss_padding) of
        rsa_pkcs1_pss_padding -> signature(rsa_digest_oid(proplists:get_value(digest, Opts, sha1)), asn1_NOVALUE);
        rsa_pss_rsae -> signature(rsa_digest_oid(proplists:get_value(digest, Opts, sha256)), asn1_NOVALUE)
    end;
sign_algorithm({#'RSAPrivateKey'{}, #'RSASSA-PSS-params'{} = Params}, _Opts) ->
    signature(?'id-RSASSA-PSS', Params);
sign_algorithm(#'DSAPrivateKey'{p = P, q = Q, g = G}, _Opts) ->
    signature(?'id-dsa-with-sha1', {params, #'Dss-Parms'{p = P, q = Q, g = G}});
sign_algorithm(#'ECPrivateKey'{parameters = {namedCurve, EDCurve}}, _Opts) when
    EDCurve == ?'id-Ed25519'; EDCurve == ?'id-Ed448'
->
    signature(EDCurve, asn1_NOVALUE);
sign_algorithm(#'ECPrivateKey'{parameters = Parms}, Opts) ->
    signature(ecdsa_digest_oid(proplists:get_value(digest, Opts, sha1)), Parms);
sign_algorithm(#'ML-DSAPrivateKey'{algorithm = Algo}, _) ->
    signature(clause_public_key:name_oid(ml_dsa, Algo), asn1_NOVALUE);
sign_algorithm(#'SLH-DSAPrivateKey'{algorithm = Algo}, _) ->
    signature(clause_public_key:name_oid(slh_dsa, Algo), asn1_NOVALUE).

signature(Algorithm, Parameters) -> #'SignatureAlgorithm'{algorithm = Algorithm, parameters = Parameters}.

rsa_digest_oid(Oid) when is_tuple(Oid) -> Oid;
rsa_digest_oid(sha1) -> ?'sha1WithRSAEncryption';
rsa_digest_oid(sha) -> ?'sha1WithRSAEncryption';
rsa_digest_oid(sha512) -> ?'sha512WithRSAEncryption';
rsa_digest_oid(sha384) -> ?'sha384WithRSAEncryption';
rsa_digest_oid(sha256) -> ?'sha256WithRSAEncryption';
rsa_digest_oid(md5) -> ?'md5WithRSAEncryption'.

ecdsa_digest_oid(Oid) when is_tuple(Oid) -> Oid;
ecdsa_digest_oid(sha1) -> ?'ecdsa-with-SHA1';
ecdsa_digest_oid(sha) -> ?'ecdsa-with-SHA1';
ecdsa_digest_oid(sha512) -> ?'ecdsa-with-SHA512';
ecdsa_digest_oid(sha384) -> ?'ecdsa-with-SHA384';
ecdsa_digest_oid(sha256) -> ?'ecdsa-with-SHA256'.

%% The subject public key info and private key of the key option: {both, Public, Private}, a key, or what to generate
%% (the first EC curve crypto has, else RSA).
key_info(Opts) ->
    case proplists:get_value(key, Opts, default_key_gen()) of
        {both, PubKey, PrivKey} ->
            {public_key({pub, PubKey}, sign_algorithm(PrivKey, Opts)), PrivKey};
        KeyInfo ->
            PrivKey = gen_key(KeyInfo),
            {public_key(PrivKey, sign_algorithm(PrivKey, Opts)), PrivKey}
    end.

default_key_gen() ->
    case crypto:ec_curves() of
        [] -> {rsa, 2048, 17};
        [Curve | _] -> {namedCurve, clause_public_key:name_oid(named_curve, Curve)}
    end.

gen_key(KeyGen) ->
    case is_key(KeyGen) of
        true -> KeyGen;
        false -> public_key:generate_key(KeyGen)
    end.

is_key(#'DSAPrivateKey'{}) -> true;
is_key(#'RSAPrivateKey'{}) -> true;
is_key({#'RSAPrivateKey'{}, _}) -> true;
is_key(#'ECPrivateKey'{}) -> true;
is_key(_) -> false.

public_key({pub, PubKey}, #'SignatureAlgorithm'{algorithm = SignAlgoId}) ->
    key_info_record(SignAlgoId, asn1_NOVALUE, PubKey);
public_key(#'RSAPrivateKey'{} = Key, #'SignatureAlgorithm'{
    algorithm = ?rsaEncryption, parameters = #'RSASSA-PSS-params'{} = Params
}) ->
    key_info_record(?rsaEncryption, Params, rsa_public_key(Key));
public_key({#'RSAPrivateKey'{} = Key, #'RSASSA-PSS-params'{} = Params}, #'SignatureAlgorithm'{
    algorithm = ?'id-RSASSA-PSS', parameters = #'RSASSA-PSS-params'{} = Params
}) ->
    key_info_record(?'id-RSASSA-PSS', Params, rsa_public_key(Key));
public_key(#'RSAPrivateKey'{} = Key, _) ->
    key_info_record(?rsaEncryption, asn1_NOVALUE, rsa_public_key(Key));
public_key(#'DSAPrivateKey'{p = P, q = Q, g = G, y = Y}, _) ->
    key_info_record(?'id-dsa', {params, #'Dss-Parms'{p = P, q = Q, g = G}}, Y);
public_key(#'ECPrivateKey'{parameters = {namedCurve, ID}, publicKey = PubKey}, _) when
    ID == ?'id-Ed25519'; ID == ?'id-Ed448'
->
    key_info_record(ID, asn1_NOVALUE, #'ECPoint'{point = PubKey});
public_key(#'ECPrivateKey'{parameters = Params, publicKey = PubKey}, _) ->
    key_info_record(?'id-ecPublicKey', Params, #'ECPoint'{point = PubKey}).

rsa_public_key(#'RSAPrivateKey'{modulus = N, publicExponent = E}) -> #'RSAPublicKey'{modulus = N, publicExponent = E}.

key_info_record(Algorithm, Parameters, Key) ->
    #'OTPSubjectPublicKeyInfo'{
        algorithm = #'PublicKeyAlgorithm'{algorithm = Algorithm, parameters = Parameters}, subjectPublicKey = Key
    }.

%% The extensions option followed by the defaults of the certificate's type it does not replace.
extensions(Role, Type, Opts) ->
    Exts = proplists:get_value(extensions, Opts, []),
    Exts ++
        [Ext || #'Extension'{extnID = ID} = Ext <- default_extensions(Role, Type), not lists:keymember(ID, 2, Exts)].

default_extensions(_, ca) ->
    [
        extension(?'id-ce-keyUsage', [keyCertSign, digitalSignature, cRLSign], false),
        extension(?'id-ce-basicConstraints', #'BasicConstraints'{cA = true}, true)
    ];
default_extensions(server, peer) ->
    [
        extension(?'id-ce-keyUsage', [digitalSignature, keyAgreement], false),
        extension(?'id-ce-subjectAltName', [{dNSName, net_adm:localhost()}], false)
    ];
default_extensions(client, peer) ->
    [extension(?'id-ce-keyUsage', [digitalSignature], false)].

extension(ID, Value, Critical) -> #'Extension'{extnID = ID, extnValue = Value, critical = Critical}.

%% A private key as {Asn1Type, Der}: PKCS #8 for ML-DSA, SLH-DSA and RSASSA-PSS keys.
encode_key(#'ML-DSAPrivateKey'{} = Key) -> private_key_info(Key);
encode_key(#'SLH-DSAPrivateKey'{} = Key) -> private_key_info(Key);
encode_key({#'RSAPrivateKey'{}, #'RSASSA-PSS-params'{}} = Key) -> private_key_info(Key);
encode_key(#'RSAPrivateKey'{} = Key) -> {'RSAPrivateKey', public_key:der_encode('RSAPrivateKey', Key)};
encode_key(#'ECPrivateKey'{} = Key) -> {'ECPrivateKey', public_key:der_encode('ECPrivateKey', Key)};
encode_key(#'DSAPrivateKey'{} = Key) -> {'DSAPrivateKey', public_key:der_encode('DSAPrivateKey', Key)}.

private_key_info(Key) ->
    {Asn1Type, DER, _} = public_key:pem_entry_encode('PrivateKeyInfo', Key),
    {Asn1Type, DER}.
