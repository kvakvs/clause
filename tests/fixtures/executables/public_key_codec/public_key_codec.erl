%% public_key's DER and PEM codec (plan 21 step LIB80B): entries encoded, decoded and encoded again, public and
%% private keys of each kind in SubjectPublicKeyInfo and PKCS #8, certificates in the plain, otp and relaxed forms,
%% extension values, PEM encryption (legacy and PBES2) and the errors of bad input. The DER data was made once with
%% OTP's public_key:pkix_test_data/1, the encrypted PEM entries with OpenSSL (password "secret").
-module(public_key_codec).
-export([main/1]).

-include_lib("public_key/include/public_key.hrl").

main(_) ->
    Data = data(),
    pem(Data),
    public_keys(Data),
    private_keys(Data),
    certificates(Data),
    extensions(Data),
    values(),
    encrypted(Data),
    errors(),
    io:format("public_key_codec: ok~n").

%% A call's result, or its exception with the stacks of ASN.1 errors left out.
safe(Fun) ->
    try
        {ok, Fun()}
    catch
        Class:Reason -> {Class, reduced(Reason)}
    end.

reduced({badmatch, {error, {asn1, {Reason, _}}}}) -> {badmatch, {error, {asn1, Reason}}};
reduced(Reason) -> Reason.

pem(Data) ->
    Entries = [
        {'Certificate', cert},
        {'RSAPrivateKey', rsa},
        {'RSAPublicKey', rsa_public},
        {'ECPrivateKey', ec},
        {'ECPrivateKey', peer_key},
        {'PrivateKeyInfo', pkcs8},
        {'DHParameter', dh}
    ],
    [pem_entry(Type, proplists:get_value(Key, Data)) || {Type, Key} <- Entries],
    CaCerts = [{'Certificate', Der, not_encrypted} || Der <- proplists:get_value(cacerts, Data)],
    Pem = public_key:pem_encode(CaCerts),
    io:format("cacerts~n~s", [Pem]),
    Noisy = iolist_to_binary([
        "junk before\r\n",
        binary:replace(Pem, <<"\n">>, <<"\r\n">>, [global]),
        "-----BEGIN UNKNOWN-----   \r\n"
    ]),
    io:format("  crlf ~p~n", [public_key:pem_decode(Noisy) =:= CaCerts]),
    io:format("  trailing space ~p~n", [
        public_key:pem_decode(
            <<"-----BEGIN DH PARAMETERS----- \t\nMA4CCQD/////////xQIBAg==\n-----END DH PARAMETERS-----">>
        )
    ]).

pem_entry(Type, Der) ->
    Value = public_key:der_decode(Type, Der),
    Entry = public_key:pem_entry_encode(Type, Value),
    Pem = public_key:pem_encode([Entry]),
    [Back] = public_key:pem_decode(Pem),
    io:format("~p~n  <= ~w~n~s  same ~p ~p ~p~n", [
        Type,
        Value,
        Pem,
        Back =:= Entry,
        public_key:pem_entry_decode(Back) =:= Value,
        public_key:der_encode(Type, Value) =:= Der
    ]).

public_keys(Data) ->
    RsaPublic = public_key:der_decode('RSAPublicKey', proplists:get_value(rsa_public, Data)),
    Otp = public_key:pkix_decode_cert(proplists:get_value(cert, Data), otp),
    #'OTPSubjectPublicKeyInfo'{subjectPublicKey = Point, algorithm = #'PublicKeyAlgorithm'{parameters = Curve}} =
        (Otp#'OTPCertificate'.tbsCertificate)#'OTPTBSCertificate'.subjectPublicKeyInfo,
    Keys = [
        RsaPublic,
        {Point, Curve},
        {123456789, #'Dss-Parms'{p = 23, q = 11, g = 4}},
        {#'ECPoint'{point = binary:copy(<<7>>, 32)}, {namedCurve, ?'id-Ed25519'}},
        {#'ECPoint'{point = binary:copy(<<8>>, 57)}, {namedCurve, ?'id-Ed448'}},
        #'ML-DSAPublicKey'{algorithm = mldsa44, key = binary:copy(<<1>>, 1312)},
        #'ML-KEMPublicKey'{algorithm = mlkem768, key = binary:copy(<<2>>, 1184)},
        #'SLH-DSAPublicKey'{algorithm = slh_dsa_shake_128f, key = binary:copy(<<3>>, 32)}
    ],
    [public_key_entry(Key) || Key <- Keys].

public_key_entry(Key) ->
    {'SubjectPublicKeyInfo', Der, not_encrypted} = Entry = public_key:pem_entry_encode('SubjectPublicKeyInfo', Key),
    Back = public_key:pem_entry_decode(Entry),
    io:format("SubjectPublicKeyInfo ~w~n  same ~p~n", [Der, Back =:= Key]).

private_keys(Data) ->
    Rsa = public_key:der_decode('RSAPrivateKey', proplists:get_value(rsa, Data)),
    Ec = public_key:der_decode('ECPrivateKey', proplists:get_value(ec, Data)),
    #'RSASSA-AlgorithmIdentifier'{parameters = DefaultPss} = ?'rSASSA-PSS-Default-Identifier',
    Sha256 = #'HashAlgorithm'{algorithm = ?'id-sha256', parameters = 'NULL'},
    Pss = DefaultPss#'RSASSA-PSS-params'{
        hashAlgorithm = Sha256,
        maskGenAlgorithm = #'MaskGenAlgorithm'{algorithm = ?'id-mgf1', parameters = Sha256},
        saltLength = 32
    },
    Keys = [
        Rsa,
        Ec,
        {Rsa, DefaultPss},
        {Rsa, Pss},
        #'ECPrivateKey'{version = 1, privateKey = binary:copy(<<5>>, 32), parameters = {namedCurve, ?'id-Ed25519'}},
        #'DSAPrivateKey'{version = 0, p = 23, q = 11, g = 4, y = 8, x = 3},
        #'ML-DSAPrivateKey'{algorithm = mldsa65, seed = binary:copy(<<1>>, 32), expandedkey = <<>>},
        #'ML-KEMPrivateKey'{
            algorithm = mlkem512, seed = binary:copy(<<2>>, 64), expandedkey = binary:copy(<<3>>, 1632)
        },
        #'ML-KEMPrivateKey'{algorithm = mlkem1024, seed = <<>>, expandedkey = binary:copy(<<4>>, 3168)},
        #'SLH-DSAPrivateKey'{algorithm = slh_dsa_sha2_128s, key = binary:copy(<<6>>, 64)}
    ],
    [private_key(Key) || Key <- Keys],
    Ed = #'ECPrivateKey'{
        version = 2,
        privateKey = binary:copy(<<9>>, 32),
        parameters = {namedCurve, ?'id-Ed25519'},
        publicKey = binary:copy(<<10>>, 32)
    },
    OneAsymmetric = public_key:der_encode('OneAsymmetricKey', Ed),
    io:format("OneAsymmetricKey ~w~n  <= ~p~n", [
        OneAsymmetric, safe(fun() -> public_key:der_decode('PrivateKeyInfo', OneAsymmetric) end)
    ]),
    Raw = #'OneAsymmetricKey'{
        version = v1,
        privateKeyAlgorithm = {'PrivateKeyAlgorithmIdentifier', ?'id-dsa', asn1_NOVALUE},
        privateKey = <<2, 1, 3>>
    },
    Unknown = public_key:der_encode('OneAsymmetricKey', Raw),
    io:format("unknown algorithm ~w~n  <= ~p~n", [Unknown, public_key:der_decode('PrivateKeyInfo', Unknown)]).

private_key(Key) ->
    case safe(fun() -> public_key:der_encode('PrivateKeyInfo', Key) end) of
        {ok, Der} -> private_key(Key, Der);
        Error -> io:format("PrivateKeyInfo ~p~n", [Error])
    end.

private_key(Key, Der) ->
    Back = public_key:der_decode('PrivateKeyInfo', Der),
    {_, Pem, not_encrypted} = public_key:pem_entry_encode('PrivateKeyInfo', Key),
    io:format("PrivateKeyInfo ~w~n  same ~p ~p~n", [Der, Pem =:= Der, Back =:= Key]),
    Back =/= Key andalso io:format("  <= ~w~n", [Back]).

certificates(Data) ->
    [certificate(Der) || Der <- [proplists:get_value(cert, Data) | proplists:get_value(cacerts, Data)]].

certificate(Der) ->
    Plain = public_key:pkix_decode_cert(Der, plain),
    Otp = public_key:pkix_decode_cert(Der, otp),
    Tbs = Otp#'OTPCertificate'.tbsCertificate,
    PlainTbs = public_key:der_encode('TBSCertificate', Plain#'Certificate'.tbsCertificate),
    Spki = public_key:pkix_encode('OTPSubjectPublicKeyInfo', Tbs#'OTPTBSCertificate'.subjectPublicKeyInfo, otp),
    PlainSpki = (Plain#'Certificate'.tbsCertificate)#'TBSCertificate'.subjectPublicKeyInfo,
    io:format("certificate~n  plain ~w~n  otp ~w~n  same ~p ~p ~p ~p ~p ~p~n", [
        Plain,
        Otp,
        public_key:pkix_decode_cert(Der, relaxed) =:= Otp,
        public_key:der_decode('Certificate', Der) =:= Plain,
        public_key:pkix_encode('Certificate', Plain, plain) =:= Der,
        public_key:pkix_encode('OTPCertificate', Otp, otp) =:= Der,
        public_key:pkix_encode('OTPTBSCertificate', Tbs, otp) =:= PlainTbs,
        Spki =:= public_key:der_encode('SubjectPublicKeyInfo', PlainSpki)
    ]).

%% The extensions of the peer certificate, decoded by their types and encoded again.
extensions(Data) ->
    Plain = public_key:pkix_decode_cert(proplists:get_value(cert, Data), plain),
    Extensions = (Plain#'Certificate'.tbsCertificate)#'TBSCertificate'.extensions,
    [extension(Extension) || Extension <- Extensions].

extension(#'Extension'{extnID = Id, extnValue = Der}) ->
    Types = [
        {?'id-ce-subjectAltName', 'SubjectAltName'},
        {?'id-ce-keyUsage', 'KeyUsage'},
        {?'id-ce-basicConstraints', 'BasicConstraints'},
        {?'id-ce-subjectKeyIdentifier', 'SubjectKeyIdentifier'},
        {?'id-ce-authorityKeyIdentifier', 'AuthorityKeyIdentifier'},
        {?'id-ce-extKeyUsage', 'ExtKeyUsageSyntax'}
    ],
    case lists:keyfind(Id, 1, Types) of
        {_, Type} ->
            Value = public_key:der_decode(Type, Der),
            io:format("~p ~w~n  same ~p~n", [Type, Value, public_key:der_encode(Type, Value) =:= Der]);
        false ->
            io:format("extension ~w~n", [Id])
    end.

%% Values of the types public_key codes itself and of extensions without a module type.
values() ->
    Notice = #'UserNotice'{noticeRef = asn1_NOVALUE, explicitText = {utf8String, lists:duplicate(300, $n)}},
    Policies = [
        #'PolicyInformation'{
            policyIdentifier = ?anyPolicy,
            policyQualifiers = [#'PolicyQualifierInfo'{policyQualifierId = ?'id-qt-unotice', qualifier = Notice}]
        }
    ],
    %% A visible string notice past 200 characters, which fails its table constraint before OTP-PKIX reads it.
    Visible = #'UserNotice'{noticeRef = asn1_NOVALUE, explicitText = {visibleString, lists:duplicate(400, $v)}},
    VisiblePolicies = [
        #'PolicyInformation'{
            policyIdentifier = ?anyPolicy,
            policyQualifiers = [#'PolicyQualifierInfo'{policyQualifierId = ?'id-qt-unotice', qualifier = Visible}]
        }
    ],
    Point = #'DistributionPoint'{
        distributionPoint = {fullName, [{uniformResourceIdentifier, "http://crl.example/ca.crl"}]},
        reasons = asn1_NOVALUE,
        cRLIssuer = asn1_NOVALUE
    },
    Values = [
        {'InvalidityDate', "20260101120000Z"},
        {'HoldInstructionCode', ?'id-holdinstruction-callissuer'},
        {'CRLReason', keyCompromise},
        {'BaseCRLNumber', 7},
        {'CRLNumber', 8},
        {'InhibitAnyPolicy', 2},
        {'CertificateIssuer', [{dNSName, "issuer.example"}]},
        {'IssuerAltName', [{rfc822Name, "ca@example.org"}]},
        {'FreshestCRL', [Point]},
        {'CRLDistributionPoints', [Point]},
        {'CertificatePolicies', Policies},
        {'CertificatePolicies', VisiblePolicies},
        {'PolicyConstraints', #'PolicyConstraints'{requireExplicitPolicy = 1, inhibitPolicyMapping = asn1_NOVALUE}},
        {'AttributeTypeAndValue', #'AttributeTypeAndValue'{type = ?'id-at-countryName', value = "SE"}},
        {'AttributeTypeAndValue', #'AttributeTypeAndValue'{
            type = ?'id-at-commonName', value = {printableString, "common"}
        }},
        {'EcpkParameters', {namedCurve, ?secp256r1}},
        {'Dss-Sig-Value', #'Dss-Sig-Value'{r = 12345, s = 67890}},
        {'PBEParameter', #'PBEParameter'{salt = <<1, 2, 3, 4, 5, 6, 7, 8>>, iterationCount = 2048}},
        {'ExtensionRequest', [#'Extension'{extnID = ?'id-ce-basicConstraints', critical = true, extnValue = <<48, 0>>}]}
    ],
    [value(Type, Value) || {Type, Value} <- Values].

value(Type, Value) ->
    case safe(fun() -> public_key:der_encode(Type, Value) end) of
        {ok, Der} ->
            io:format("~p ~w~n  <= ~p~n", [Type, Der, safe(fun() -> public_key:der_decode(Type, Der) end)]);
        Error ->
            io:format("~p ~p~n", [Type, Error])
    end.

encrypted(Data) ->
    Rsa = public_key:der_decode('RSAPrivateKey', proplists:get_value(rsa, Data)),
    Ec = public_key:der_decode('ECPrivateKey', proplists:get_value(ec, Data)),
    Entries = [{Name, Entry} || {Name, Pem} <- pems(), Entry <- public_key:pem_decode(Pem)],
    [
        io:format("~p ~p~n  ~p ~p~n", [
            Name,
            element(3, Entry),
            lists:member(public_key:pem_entry_decode(Entry, "secret"), [Rsa, Ec]),
            public_key:pem_entry_decode(Entry, fun() -> "secret" end) =:= public_key:pem_entry_decode(Entry, "secret")
        ])
     || {Name, Entry} <- Entries
    ],
    Salts = [
        {"DES-EDE3-CBC", <<1, 2, 3, 4, 5, 6, 7, 8>>},
        {"AES-128-CBC", <<1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16>>},
        {"AES-192-CBC", <<16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1>>},
        {"AES-256-CBC", <<0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1>>}
    ],
    [encrypt('ECPrivateKey', Ec, Cipher) || Cipher <- Salts],
    {_, {_, _, Pbes2}} = lists:keyfind(rsa_pbes2_aes, 1, Entries),
    {_, {_, _, Des3}} = lists:keyfind(ec_pbes2_des3, 1, Entries),
    encrypt('PrivateKeyInfo', Ec, Des3),
    encrypt('PrivateKeyInfo', Rsa, Pbes2),
    {_, Legacy} = lists:keyfind(rsa_legacy_aes, 1, Entries),
    io:format("wrong password ~p~n", [safe(fun() -> public_key:pem_entry_decode(Legacy, "wrong") end)]).

encrypt(Type, Value, Cipher) ->
    Entry = public_key:pem_entry_encode(Type, Value, {Cipher, "secret"}),
    Pem = public_key:pem_encode([Entry]),
    [Back] = public_key:pem_decode(Pem),
    Decoded = public_key:pem_entry_decode(Back, "secret"),
    io:format("~s  same ~p ~p~n", [Pem, Back =:= Entry, Decoded =:= Value orelse Decoded]).

errors() ->
    Calls = [
        fun() -> public_key:der_decode('Certificate', <<1, 2, 3>>) end,
        fun() -> public_key:der_decode('PrivateKeyInfo', <<48, 0>>) end,
        fun() -> public_key:der_decode(nonsense, <<48, 0>>) end,
        fun() -> public_key:der_encode(nonsense, value) end,
        fun() -> public_key:der_encode('RSAPublicKey', value) end,
        fun() -> public_key:pkix_decode_cert(<<48, 3, 1, 1, 0>>, otp) end,
        fun() -> public_key:pkix_decode_cert(<<48, 3, 1, 1, 0>>, plain) end,
        fun() -> public_key:pem_decode(<<"-----BEGIN CERTIFICATE-----\nMAA=\n">>) end,
        fun() -> public_key:pem_encode([{nonsense, <<>>, not_encrypted}]) end,
        fun() -> public_key:pem_entry_decode({'Certificate', <<48, 0>>, {"AES-128-CBC", <<1, 2, 3>>}}, "x") end,
        fun() -> public_key:pem_entry_encode('SubjectPublicKeyInfo', {not_a_key}) end,
        fun() -> public_key:pem_entry_decode({'SubjectPublicKeyInfo', <<48, 0>>, not_encrypted}) end
    ],
    [io:format("error ~p~n", [safe(Call)]) || Call <- Calls].

data() ->
    [
        {cert,
            <<48, 130, 2, 95, 48, 130, 2, 4, 160, 3, 2, 1, 2, 2, 1, 3, 48, 11, 6, 7, 42, 134, 72, 206, 61, 4, 1, 5, 0,
                48, 129, 150, 49, 35, 48, 33, 6, 9, 42, 134, 72, 134, 247, 13, 1, 9, 1, 22, 20, 119, 101, 98, 97, 100,
                109, 105, 110, 64, 101, 120, 97, 109, 112, 108, 101, 46, 111, 114, 103, 49, 33, 48, 31, 6, 3, 85, 4, 3,
                19, 24, 115, 101, 114, 118, 101, 114, 32, 73, 110, 116, 101, 114, 109, 101, 100, 105, 97, 116, 101, 32,
                67, 65, 32, 48, 49, 18, 48, 16, 6, 3, 85, 4, 7, 19, 9, 83, 116, 111, 99, 107, 104, 111, 108, 109, 49,
                11, 48, 9, 6, 3, 85, 4, 6, 19, 2, 83, 69, 49, 15, 48, 13, 6, 3, 85, 4, 10, 19, 6, 101, 114, 108, 97,
                110, 103, 49, 26, 48, 24, 6, 3, 85, 4, 11, 19, 17, 97, 117, 116, 111, 109, 97, 116, 101, 100, 32, 116,
                101, 115, 116, 105, 110, 103, 48, 30, 23, 13, 50, 54, 49, 48, 50, 53, 49, 51, 48, 48, 48, 48, 90, 23,
                13, 50, 54, 49, 49, 48, 50, 49, 51, 48, 48, 48, 48, 90, 48, 129, 139, 49, 32, 48, 30, 6, 9, 42, 134, 72,
                134, 247, 13, 1, 9, 1, 22, 17, 97, 100, 109, 105, 110, 64, 101, 120, 97, 109, 112, 108, 101, 46, 111,
                114, 103, 49, 25, 48, 23, 6, 3, 85, 4, 3, 19, 16, 115, 101, 114, 118, 101, 114, 32, 80, 101, 101, 114,
                32, 99, 101, 114, 116, 49, 18, 48, 16, 6, 3, 85, 4, 7, 19, 9, 83, 116, 111, 99, 107, 104, 111, 108, 109,
                49, 11, 48, 9, 6, 3, 85, 4, 6, 19, 2, 83, 69, 49, 15, 48, 13, 6, 3, 85, 4, 10, 19, 6, 101, 114, 108, 97,
                110, 103, 49, 26, 48, 24, 6, 3, 85, 4, 11, 19, 17, 97, 117, 116, 111, 109, 97, 116, 101, 100, 32, 116,
                101, 115, 116, 105, 110, 103, 48, 118, 48, 16, 6, 7, 42, 134, 72, 206, 61, 2, 1, 6, 5, 43, 129, 4, 0,
                34, 3, 98, 0, 4, 177, 248, 146, 155, 217, 187, 83, 77, 8, 96, 86, 67, 179, 105, 167, 200, 114, 141, 219,
                160, 236, 181, 232, 62, 139, 195, 167, 27, 164, 64, 238, 225, 249, 115, 225, 80, 10, 0, 46, 47, 47, 75,
                217, 123, 239, 218, 0, 179, 130, 47, 130, 38, 184, 233, 49, 188, 252, 207, 38, 155, 46, 139, 60, 224,
                81, 160, 139, 26, 125, 247, 92, 245, 247, 122, 126, 77, 43, 143, 54, 32, 171, 38, 114, 157, 202, 231,
                108, 1, 152, 161, 214, 167, 31, 161, 118, 108, 163, 46, 48, 44, 48, 29, 6, 3, 85, 29, 17, 4, 22, 48, 20,
                130, 12, 112, 101, 101, 114, 46, 101, 120, 97, 109, 112, 108, 101, 135, 4, 127, 0, 0, 1, 48, 11, 6, 3,
                85, 29, 15, 4, 4, 3, 2, 3, 136, 48, 11, 6, 7, 42, 134, 72, 206, 61, 4, 1, 5, 0, 3, 72, 0, 48, 69, 2, 32,
                4, 89, 250, 45, 146, 116, 210, 5, 250, 153, 189, 234, 122, 179, 235, 56, 38, 225, 236, 135, 240, 70,
                240, 156, 45, 100, 240, 149, 106, 145, 52, 186, 2, 33, 0, 156, 255, 226, 81, 177, 34, 187, 68, 247, 149,
                141, 137, 194, 17, 15, 217, 101, 192, 3, 28, 86, 15, 137, 75, 214, 148, 254, 199, 8, 129, 66, 45>>},
        {cacerts, [
            <<48, 130, 2, 162, 48, 130, 2, 13, 160, 3, 2, 1, 2, 2, 1, 1, 48, 11, 6, 9, 42, 134, 72, 134, 247, 13, 1, 1,
                11, 48, 129, 136, 49, 31, 48, 29, 6, 9, 42, 134, 72, 134, 247, 13, 1, 9, 1, 22, 16, 114, 111, 111, 116,
                64, 101, 120, 97, 109, 112, 108, 101, 46, 111, 114, 103, 49, 23, 48, 21, 6, 3, 85, 4, 3, 19, 14, 83, 69,
                82, 86, 69, 82, 32, 82, 79, 79, 84, 32, 67, 65, 49, 18, 48, 16, 6, 3, 85, 4, 7, 19, 9, 83, 116, 111, 99,
                107, 104, 111, 108, 109, 49, 11, 48, 9, 6, 3, 85, 4, 6, 19, 2, 83, 69, 49, 15, 48, 13, 6, 3, 85, 4, 10,
                19, 6, 101, 114, 108, 97, 110, 103, 49, 26, 48, 24, 6, 3, 85, 4, 11, 19, 17, 97, 117, 116, 111, 109, 97,
                116, 101, 100, 32, 116, 101, 115, 116, 105, 110, 103, 48, 30, 23, 13, 50, 54, 49, 48, 50, 53, 49, 51,
                48, 48, 48, 48, 90, 23, 13, 50, 54, 49, 49, 48, 50, 49, 51, 48, 48, 48, 48, 90, 48, 129, 136, 49, 31,
                48, 29, 6, 9, 42, 134, 72, 134, 247, 13, 1, 9, 1, 22, 16, 114, 111, 111, 116, 64, 101, 120, 97, 109,
                112, 108, 101, 46, 111, 114, 103, 49, 23, 48, 21, 6, 3, 85, 4, 3, 19, 14, 83, 69, 82, 86, 69, 82, 32,
                82, 79, 79, 84, 32, 67, 65, 49, 18, 48, 16, 6, 3, 85, 4, 7, 19, 9, 83, 116, 111, 99, 107, 104, 111, 108,
                109, 49, 11, 48, 9, 6, 3, 85, 4, 6, 19, 2, 83, 69, 49, 15, 48, 13, 6, 3, 85, 4, 10, 19, 6, 101, 114,
                108, 97, 110, 103, 49, 26, 48, 24, 6, 3, 85, 4, 11, 19, 17, 97, 117, 116, 111, 109, 97, 116, 101, 100,
                32, 116, 101, 115, 116, 105, 110, 103, 48, 129, 157, 48, 11, 6, 9, 42, 134, 72, 134, 247, 13, 1, 1, 1,
                3, 129, 141, 0, 48, 129, 137, 2, 129, 129, 0, 248, 193, 26, 237, 199, 102, 202, 142, 101, 211, 125, 152,
                181, 168, 18, 102, 109, 66, 116, 186, 247, 232, 110, 73, 240, 27, 110, 132, 99, 27, 184, 117, 117, 105,
                99, 50, 71, 27, 117, 66, 225, 211, 100, 205, 215, 172, 215, 95, 67, 50, 225, 34, 217, 197, 160, 38, 195,
                209, 119, 65, 214, 211, 61, 1, 205, 66, 210, 212, 129, 158, 16, 1, 26, 32, 245, 8, 10, 72, 108, 105, 9,
                94, 86, 56, 200, 166, 145, 60, 59, 105, 155, 108, 105, 65, 168, 73, 43, 140, 207, 172, 184, 33, 220, 15,
                16, 63, 14, 168, 59, 242, 227, 77, 117, 85, 196, 188, 252, 166, 56, 153, 36, 94, 136, 20, 225, 25, 13,
                77, 2, 3, 1, 0, 1, 163, 32, 48, 30, 48, 11, 6, 3, 85, 29, 15, 4, 4, 3, 2, 1, 134, 48, 15, 6, 3, 85, 29,
                19, 1, 1, 255, 4, 5, 48, 3, 1, 1, 255, 48, 11, 6, 9, 42, 134, 72, 134, 247, 13, 1, 1, 11, 3, 129, 129,
                0, 90, 239, 72, 182, 196, 175, 151, 110, 35, 101, 4, 63, 236, 195, 192, 119, 101, 218, 26, 197, 119, 82,
                156, 139, 59, 63, 79, 41, 81, 215, 22, 48, 156, 75, 10, 82, 125, 57, 56, 150, 172, 108, 225, 145, 146,
                200, 105, 244, 199, 198, 66, 173, 181, 128, 224, 245, 220, 178, 231, 101, 253, 111, 119, 65, 139, 5, 74,
                157, 85, 213, 216, 165, 148, 199, 50, 34, 110, 254, 150, 178, 207, 78, 66, 27, 116, 71, 33, 101, 209,
                11, 250, 97, 109, 35, 28, 34, 142, 209, 244, 133, 110, 234, 93, 47, 23, 162, 82, 238, 124, 169, 178, 80,
                210, 228, 229, 190, 37, 76, 13, 19, 142, 0, 225, 170, 11, 72, 202, 72>>,
            <<48, 130, 2, 107, 48, 130, 1, 214, 160, 3, 2, 1, 2, 2, 1, 2, 48, 11, 6, 9, 42, 134, 72, 134, 247, 13, 1, 1,
                5, 48, 129, 136, 49, 31, 48, 29, 6, 9, 42, 134, 72, 134, 247, 13, 1, 9, 1, 22, 16, 114, 111, 111, 116,
                64, 101, 120, 97, 109, 112, 108, 101, 46, 111, 114, 103, 49, 23, 48, 21, 6, 3, 85, 4, 3, 19, 14, 83, 69,
                82, 86, 69, 82, 32, 82, 79, 79, 84, 32, 67, 65, 49, 18, 48, 16, 6, 3, 85, 4, 7, 19, 9, 83, 116, 111, 99,
                107, 104, 111, 108, 109, 49, 11, 48, 9, 6, 3, 85, 4, 6, 19, 2, 83, 69, 49, 15, 48, 13, 6, 3, 85, 4, 10,
                19, 6, 101, 114, 108, 97, 110, 103, 49, 26, 48, 24, 6, 3, 85, 4, 11, 19, 17, 97, 117, 116, 111, 109, 97,
                116, 101, 100, 32, 116, 101, 115, 116, 105, 110, 103, 48, 30, 23, 13, 50, 54, 49, 48, 50, 53, 49, 51,
                48, 48, 48, 48, 90, 23, 13, 50, 54, 49, 49, 48, 50, 49, 51, 48, 48, 48, 48, 90, 48, 129, 150, 49, 35,
                48, 33, 6, 9, 42, 134, 72, 134, 247, 13, 1, 9, 1, 22, 20, 119, 101, 98, 97, 100, 109, 105, 110, 64, 101,
                120, 97, 109, 112, 108, 101, 46, 111, 114, 103, 49, 33, 48, 31, 6, 3, 85, 4, 3, 19, 24, 115, 101, 114,
                118, 101, 114, 32, 73, 110, 116, 101, 114, 109, 101, 100, 105, 97, 116, 101, 32, 67, 65, 32, 48, 49, 18,
                48, 16, 6, 3, 85, 4, 7, 19, 9, 83, 116, 111, 99, 107, 104, 111, 108, 109, 49, 11, 48, 9, 6, 3, 85, 4, 6,
                19, 2, 83, 69, 49, 15, 48, 13, 6, 3, 85, 4, 10, 19, 6, 101, 114, 108, 97, 110, 103, 49, 26, 48, 24, 6,
                3, 85, 4, 11, 19, 17, 97, 117, 116, 111, 109, 97, 116, 101, 100, 32, 116, 101, 115, 116, 105, 110, 103,
                48, 89, 48, 19, 6, 7, 42, 134, 72, 206, 61, 2, 1, 6, 8, 42, 134, 72, 206, 61, 3, 1, 7, 3, 66, 0, 4, 86,
                113, 252, 89, 75, 228, 150, 191, 65, 19, 228, 146, 245, 169, 113, 124, 151, 96, 244, 230, 98, 213, 104,
                181, 61, 196, 64, 207, 83, 171, 103, 1, 57, 250, 149, 111, 220, 237, 147, 92, 43, 224, 241, 72, 172, 95,
                122, 103, 112, 255, 171, 129, 82, 147, 155, 81, 140, 199, 191, 214, 86, 72, 184, 117, 163, 32, 48, 30,
                48, 11, 6, 3, 85, 29, 15, 4, 4, 3, 2, 1, 134, 48, 15, 6, 3, 85, 29, 19, 1, 1, 255, 4, 5, 48, 3, 1, 1,
                255, 48, 11, 6, 9, 42, 134, 72, 134, 247, 13, 1, 1, 5, 3, 129, 129, 0, 15, 238, 226, 91, 71, 116, 168,
                230, 103, 10, 39, 212, 74, 196, 233, 225, 118, 185, 3, 83, 24, 186, 206, 43, 97, 150, 115, 153, 87, 82,
                240, 32, 153, 84, 192, 46, 88, 224, 144, 194, 64, 2, 1, 247, 88, 205, 0, 214, 228, 191, 155, 127, 45,
                244, 244, 33, 59, 193, 221, 162, 54, 41, 52, 145, 93, 6, 177, 22, 209, 124, 118, 0, 208, 124, 45, 52,
                125, 34, 222, 175, 30, 218, 12, 145, 8, 77, 60, 20, 185, 189, 164, 83, 143, 52, 12, 117, 95, 218, 65,
                26, 201, 207, 175, 238, 4, 116, 191, 103, 249, 193, 130, 9, 52, 67, 220, 41, 204, 108, 11, 205, 156,
                208, 66, 207, 60, 44, 128, 59>>,
            <<48, 130, 2, 162, 48, 130, 2, 13, 160, 3, 2, 1, 2, 2, 1, 1, 48, 11, 6, 9, 42, 134, 72, 134, 247, 13, 1, 1,
                11, 48, 129, 136, 49, 31, 48, 29, 6, 9, 42, 134, 72, 134, 247, 13, 1, 9, 1, 22, 16, 114, 111, 111, 116,
                64, 101, 120, 97, 109, 112, 108, 101, 46, 111, 114, 103, 49, 23, 48, 21, 6, 3, 85, 4, 3, 19, 14, 83, 69,
                82, 86, 69, 82, 32, 82, 79, 79, 84, 32, 67, 65, 49, 18, 48, 16, 6, 3, 85, 4, 7, 19, 9, 83, 116, 111, 99,
                107, 104, 111, 108, 109, 49, 11, 48, 9, 6, 3, 85, 4, 6, 19, 2, 83, 69, 49, 15, 48, 13, 6, 3, 85, 4, 10,
                19, 6, 101, 114, 108, 97, 110, 103, 49, 26, 48, 24, 6, 3, 85, 4, 11, 19, 17, 97, 117, 116, 111, 109, 97,
                116, 101, 100, 32, 116, 101, 115, 116, 105, 110, 103, 48, 30, 23, 13, 50, 54, 49, 48, 50, 53, 49, 51,
                48, 48, 48, 48, 90, 23, 13, 50, 54, 49, 49, 48, 50, 49, 51, 48, 48, 48, 48, 90, 48, 129, 136, 49, 31,
                48, 29, 6, 9, 42, 134, 72, 134, 247, 13, 1, 9, 1, 22, 16, 114, 111, 111, 116, 64, 101, 120, 97, 109,
                112, 108, 101, 46, 111, 114, 103, 49, 23, 48, 21, 6, 3, 85, 4, 3, 19, 14, 83, 69, 82, 86, 69, 82, 32,
                82, 79, 79, 84, 32, 67, 65, 49, 18, 48, 16, 6, 3, 85, 4, 7, 19, 9, 83, 116, 111, 99, 107, 104, 111, 108,
                109, 49, 11, 48, 9, 6, 3, 85, 4, 6, 19, 2, 83, 69, 49, 15, 48, 13, 6, 3, 85, 4, 10, 19, 6, 101, 114,
                108, 97, 110, 103, 49, 26, 48, 24, 6, 3, 85, 4, 11, 19, 17, 97, 117, 116, 111, 109, 97, 116, 101, 100,
                32, 116, 101, 115, 116, 105, 110, 103, 48, 129, 157, 48, 11, 6, 9, 42, 134, 72, 134, 247, 13, 1, 1, 1,
                3, 129, 141, 0, 48, 129, 137, 2, 129, 129, 0, 248, 193, 26, 237, 199, 102, 202, 142, 101, 211, 125, 152,
                181, 168, 18, 102, 109, 66, 116, 186, 247, 232, 110, 73, 240, 27, 110, 132, 99, 27, 184, 117, 117, 105,
                99, 50, 71, 27, 117, 66, 225, 211, 100, 205, 215, 172, 215, 95, 67, 50, 225, 34, 217, 197, 160, 38, 195,
                209, 119, 65, 214, 211, 61, 1, 205, 66, 210, 212, 129, 158, 16, 1, 26, 32, 245, 8, 10, 72, 108, 105, 9,
                94, 86, 56, 200, 166, 145, 60, 59, 105, 155, 108, 105, 65, 168, 73, 43, 140, 207, 172, 184, 33, 220, 15,
                16, 63, 14, 168, 59, 242, 227, 77, 117, 85, 196, 188, 252, 166, 56, 153, 36, 94, 136, 20, 225, 25, 13,
                77, 2, 3, 1, 0, 1, 163, 32, 48, 30, 48, 11, 6, 3, 85, 29, 15, 4, 4, 3, 2, 1, 134, 48, 15, 6, 3, 85, 29,
                19, 1, 1, 255, 4, 5, 48, 3, 1, 1, 255, 48, 11, 6, 9, 42, 134, 72, 134, 247, 13, 1, 1, 11, 3, 129, 129,
                0, 90, 239, 72, 182, 196, 175, 151, 110, 35, 101, 4, 63, 236, 195, 192, 119, 101, 218, 26, 197, 119, 82,
                156, 139, 59, 63, 79, 41, 81, 215, 22, 48, 156, 75, 10, 82, 125, 57, 56, 150, 172, 108, 225, 145, 146,
                200, 105, 244, 199, 198, 66, 173, 181, 128, 224, 245, 220, 178, 231, 101, 253, 111, 119, 65, 139, 5, 74,
                157, 85, 213, 216, 165, 148, 199, 50, 34, 110, 254, 150, 178, 207, 78, 66, 27, 116, 71, 33, 101, 209,
                11, 250, 97, 109, 35, 28, 34, 142, 209, 244, 133, 110, 234, 93, 47, 23, 162, 82, 238, 124, 169, 178, 80,
                210, 228, 229, 190, 37, 76, 13, 19, 142, 0, 225, 170, 11, 72, 202, 72>>
        ]},
        {peer_key,
            <<48, 129, 164, 2, 1, 1, 4, 48, 225, 158, 49, 195, 167, 5, 5, 79, 119, 190, 196, 66, 59, 15, 5, 194, 11,
                209, 73, 180, 115, 243, 127, 167, 45, 181, 45, 127, 17, 240, 149, 25, 180, 182, 48, 130, 245, 45, 41,
                176, 122, 80, 141, 69, 72, 151, 96, 29, 160, 7, 6, 5, 43, 129, 4, 0, 34, 161, 100, 3, 98, 0, 4, 177,
                248, 146, 155, 217, 187, 83, 77, 8, 96, 86, 67, 179, 105, 167, 200, 114, 141, 219, 160, 236, 181, 232,
                62, 139, 195, 167, 27, 164, 64, 238, 225, 249, 115, 225, 80, 10, 0, 46, 47, 47, 75, 217, 123, 239, 218,
                0, 179, 130, 47, 130, 38, 184, 233, 49, 188, 252, 207, 38, 155, 46, 139, 60, 224, 81, 160, 139, 26, 125,
                247, 92, 245, 247, 122, 126, 77, 43, 143, 54, 32, 171, 38, 114, 157, 202, 231, 108, 1, 152, 161, 214,
                167, 31, 161, 118, 108>>},
        {rsa,
            <<48, 130, 2, 93, 2, 1, 0, 2, 129, 129, 0, 152, 85, 127, 45, 188, 7, 172, 95, 49, 148, 5, 38, 226, 23, 188,
                57, 88, 1, 83, 228, 250, 2, 223, 121, 142, 155, 214, 112, 118, 171, 147, 225, 139, 38, 10, 212, 127,
                144, 245, 155, 203, 190, 106, 160, 232, 69, 29, 74, 234, 105, 251, 13, 171, 236, 6, 92, 75, 82, 254, 28,
                236, 98, 127, 47, 206, 189, 42, 37, 176, 45, 227, 134, 50, 172, 60, 223, 224, 220, 170, 79, 232, 157,
                248, 39, 193, 34, 247, 183, 177, 186, 24, 86, 192, 81, 76, 16, 94, 74, 14, 47, 224, 22, 36, 56, 209,
                141, 161, 211, 80, 97, 196, 186, 108, 255, 145, 131, 159, 7, 2, 196, 173, 9, 0, 157, 30, 34, 160, 115,
                2, 3, 1, 0, 1, 2, 129, 129, 0, 129, 152, 42, 171, 243, 204, 235, 93, 200, 197, 206, 100, 55, 240, 77,
                86, 211, 129, 189, 167, 140, 242, 40, 192, 55, 12, 104, 176, 32, 164, 243, 23, 235, 211, 31, 95, 140,
                110, 252, 35, 224, 127, 76, 35, 59, 182, 160, 166, 253, 41, 208, 68, 223, 63, 218, 97, 102, 75, 98, 151,
                164, 154, 94, 236, 50, 255, 130, 4, 75, 135, 170, 2, 130, 69, 87, 202, 141, 204, 163, 72, 105, 19, 38,
                143, 73, 19, 41, 115, 56, 249, 137, 83, 72, 59, 108, 176, 205, 241, 169, 24, 100, 176, 88, 39, 196, 224,
                206, 69, 121, 33, 180, 131, 39, 191, 133, 234, 128, 10, 44, 66, 150, 103, 135, 116, 231, 243, 50, 161,
                2, 65, 0, 200, 159, 166, 47, 60, 236, 29, 72, 166, 211, 218, 231, 250, 216, 140, 191, 9, 96, 47, 185,
                243, 201, 249, 214, 80, 188, 119, 82, 150, 157, 8, 234, 62, 195, 57, 3, 104, 46, 179, 77, 97, 209, 51,
                60, 156, 75, 44, 209, 40, 4, 34, 86, 10, 246, 139, 255, 86, 64, 104, 150, 126, 168, 10, 59, 2, 65, 0,
                194, 97, 161, 188, 75, 140, 137, 114, 100, 239, 204, 175, 207, 241, 129, 95, 142, 126, 216, 249, 155,
                219, 121, 46, 113, 59, 121, 247, 46, 13, 193, 236, 2, 75, 96, 60, 15, 251, 206, 56, 110, 206, 80, 201,
                181, 174, 67, 197, 83, 66, 72, 193, 100, 46, 22, 106, 209, 49, 239, 88, 206, 94, 39, 41, 2, 64, 78, 21,
                44, 213, 231, 205, 49, 220, 139, 233, 154, 162, 125, 19, 249, 59, 133, 7, 165, 171, 167, 1, 174, 54, 70,
                120, 3, 172, 230, 39, 186, 71, 137, 35, 236, 53, 178, 63, 178, 211, 100, 69, 202, 190, 69, 74, 80, 232,
                26, 189, 249, 43, 41, 219, 41, 67, 184, 171, 26, 201, 176, 3, 240, 239, 2, 65, 0, 158, 51, 11, 136, 206,
                11, 241, 9, 83, 48, 255, 107, 207, 87, 132, 173, 154, 177, 142, 49, 136, 49, 149, 108, 192, 231, 97, 26,
                85, 55, 9, 46, 229, 167, 39, 9, 123, 16, 187, 3, 158, 218, 247, 81, 83, 54, 35, 30, 3, 231, 28, 131,
                152, 223, 17, 139, 153, 250, 132, 11, 200, 217, 68, 17, 2, 64, 119, 35, 96, 138, 248, 184, 204, 176,
                252, 134, 75, 204, 61, 193, 253, 202, 55, 247, 197, 43, 210, 54, 203, 54, 1, 23, 138, 73, 15, 233, 149,
                30, 82, 157, 80, 200, 193, 123, 2, 42, 143, 80, 91, 183, 165, 92, 91, 109, 162, 235, 153, 161, 87, 237,
                45, 80, 130, 251, 217, 44, 192, 99, 57, 229>>},
        {rsa_public,
            <<48, 129, 137, 2, 129, 129, 0, 152, 85, 127, 45, 188, 7, 172, 95, 49, 148, 5, 38, 226, 23, 188, 57, 88, 1,
                83, 228, 250, 2, 223, 121, 142, 155, 214, 112, 118, 171, 147, 225, 139, 38, 10, 212, 127, 144, 245, 155,
                203, 190, 106, 160, 232, 69, 29, 74, 234, 105, 251, 13, 171, 236, 6, 92, 75, 82, 254, 28, 236, 98, 127,
                47, 206, 189, 42, 37, 176, 45, 227, 134, 50, 172, 60, 223, 224, 220, 170, 79, 232, 157, 248, 39, 193,
                34, 247, 183, 177, 186, 24, 86, 192, 81, 76, 16, 94, 74, 14, 47, 224, 22, 36, 56, 209, 141, 161, 211,
                80, 97, 196, 186, 108, 255, 145, 131, 159, 7, 2, 196, 173, 9, 0, 157, 30, 34, 160, 115, 2, 3, 1, 0,
                1>>},
        {ec,
            <<48, 119, 2, 1, 1, 4, 32, 61, 23, 22, 26, 241, 208, 98, 17, 36, 89, 61, 116, 22, 186, 229, 29, 114, 35,
                123, 151, 18, 129, 239, 103, 148, 9, 15, 55, 36, 59, 15, 37, 160, 10, 6, 8, 42, 134, 72, 206, 61, 3, 1,
                7, 161, 68, 3, 66, 0, 4, 197, 3, 118, 45, 226, 81, 162, 144, 221, 29, 26, 224, 125, 18, 177, 245, 83,
                108, 253, 40, 113, 170, 87, 223, 9, 245, 99, 44, 39, 35, 119, 20, 72, 129, 136, 220, 187, 2, 149, 240,
                245, 62, 142, 1, 86, 127, 103, 110, 38, 197, 87, 241, 10, 181, 110, 80, 90, 250, 3, 176, 22, 88, 194,
                113>>},
        {pkcs8,
            <<48, 130, 2, 119, 2, 1, 0, 48, 13, 6, 9, 42, 134, 72, 134, 247, 13, 1, 1, 1, 5, 0, 4, 130, 2, 97, 48, 130,
                2, 93, 2, 1, 0, 2, 129, 129, 0, 152, 85, 127, 45, 188, 7, 172, 95, 49, 148, 5, 38, 226, 23, 188, 57, 88,
                1, 83, 228, 250, 2, 223, 121, 142, 155, 214, 112, 118, 171, 147, 225, 139, 38, 10, 212, 127, 144, 245,
                155, 203, 190, 106, 160, 232, 69, 29, 74, 234, 105, 251, 13, 171, 236, 6, 92, 75, 82, 254, 28, 236, 98,
                127, 47, 206, 189, 42, 37, 176, 45, 227, 134, 50, 172, 60, 223, 224, 220, 170, 79, 232, 157, 248, 39,
                193, 34, 247, 183, 177, 186, 24, 86, 192, 81, 76, 16, 94, 74, 14, 47, 224, 22, 36, 56, 209, 141, 161,
                211, 80, 97, 196, 186, 108, 255, 145, 131, 159, 7, 2, 196, 173, 9, 0, 157, 30, 34, 160, 115, 2, 3, 1, 0,
                1, 2, 129, 129, 0, 129, 152, 42, 171, 243, 204, 235, 93, 200, 197, 206, 100, 55, 240, 77, 86, 211, 129,
                189, 167, 140, 242, 40, 192, 55, 12, 104, 176, 32, 164, 243, 23, 235, 211, 31, 95, 140, 110, 252, 35,
                224, 127, 76, 35, 59, 182, 160, 166, 253, 41, 208, 68, 223, 63, 218, 97, 102, 75, 98, 151, 164, 154, 94,
                236, 50, 255, 130, 4, 75, 135, 170, 2, 130, 69, 87, 202, 141, 204, 163, 72, 105, 19, 38, 143, 73, 19,
                41, 115, 56, 249, 137, 83, 72, 59, 108, 176, 205, 241, 169, 24, 100, 176, 88, 39, 196, 224, 206, 69,
                121, 33, 180, 131, 39, 191, 133, 234, 128, 10, 44, 66, 150, 103, 135, 116, 231, 243, 50, 161, 2, 65, 0,
                200, 159, 166, 47, 60, 236, 29, 72, 166, 211, 218, 231, 250, 216, 140, 191, 9, 96, 47, 185, 243, 201,
                249, 214, 80, 188, 119, 82, 150, 157, 8, 234, 62, 195, 57, 3, 104, 46, 179, 77, 97, 209, 51, 60, 156,
                75, 44, 209, 40, 4, 34, 86, 10, 246, 139, 255, 86, 64, 104, 150, 126, 168, 10, 59, 2, 65, 0, 194, 97,
                161, 188, 75, 140, 137, 114, 100, 239, 204, 175, 207, 241, 129, 95, 142, 126, 216, 249, 155, 219, 121,
                46, 113, 59, 121, 247, 46, 13, 193, 236, 2, 75, 96, 60, 15, 251, 206, 56, 110, 206, 80, 201, 181, 174,
                67, 197, 83, 66, 72, 193, 100, 46, 22, 106, 209, 49, 239, 88, 206, 94, 39, 41, 2, 64, 78, 21, 44, 213,
                231, 205, 49, 220, 139, 233, 154, 162, 125, 19, 249, 59, 133, 7, 165, 171, 167, 1, 174, 54, 70, 120, 3,
                172, 230, 39, 186, 71, 137, 35, 236, 53, 178, 63, 178, 211, 100, 69, 202, 190, 69, 74, 80, 232, 26, 189,
                249, 43, 41, 219, 41, 67, 184, 171, 26, 201, 176, 3, 240, 239, 2, 65, 0, 158, 51, 11, 136, 206, 11, 241,
                9, 83, 48, 255, 107, 207, 87, 132, 173, 154, 177, 142, 49, 136, 49, 149, 108, 192, 231, 97, 26, 85, 55,
                9, 46, 229, 167, 39, 9, 123, 16, 187, 3, 158, 218, 247, 81, 83, 54, 35, 30, 3, 231, 28, 131, 152, 223,
                17, 139, 153, 250, 132, 11, 200, 217, 68, 17, 2, 64, 119, 35, 96, 138, 248, 184, 204, 176, 252, 134, 75,
                204, 61, 193, 253, 202, 55, 247, 197, 43, 210, 54, 203, 54, 1, 23, 138, 73, 15, 233, 149, 30, 82, 157,
                80, 200, 193, 123, 2, 42, 143, 80, 91, 183, 165, 92, 91, 109, 162, 235, 153, 161, 87, 237, 45, 80, 130,
                251, 217, 44, 192, 99, 57, 229>>},
        {dh, <<48, 14, 2, 9, 0, 255, 255, 255, 255, 255, 255, 255, 197, 2, 1, 2>>}
    ].

%% Encrypted PEM entries of the rsa and ec keys, made with OpenSSL 3.5.
pems() ->
    [
        {rsa_pbes2_aes, <<
            "-----BEGIN ENCRYPTED PRIVATE KEY-----\n"
            "MIIC5TBfBgkqhkiG9w0BBQ0wUjAxBgkqhkiG9w0BBQwwJAQQzT9FxJ0EcBOXETqE\n"
            "IS+TlAICA+gwDAYIKoZIhvcNAgkFADAdBglghkgBZQMEASoEED7kYn+aboPzCibq\n"
            "LSqGo5sEggKATX6CzjZIdSE4fRfbLGV3aX1fZkn8vt5/zb7UcHrayzjmXl5t1fkM\n"
            "JtzgUTBhZAxUm6c69YZdJK+1QbK8zZxylps5EOwT2nJDJL6lsIFIqk8AJ6MHQ5MO\n"
            "ANjIZqScBBO/WINM2WwtJSk94Ef8T7o0D90bBDs317Nn/Y5kwwFMdxPD4c4EON7d\n"
            "zsMExJAUbZiMMinieOUA6mR/dd3tJzAVw1nSrhOifc+KQEdaS2NKRsyymgqAu7H/\n"
            "s/wZxDi8mhtFvMBgYjYQp0s2XCCjLDpGjU46W6jH2Z+u1yxvFoDSeCeBdNKQxVCH\n"
            "kWEvaKYDntR1YRhKj0mvmsp51DHA5aBvnmOY96h0Np71gtzs5luZoI+Rvoygrl9K\n"
            "12ojM7BT9aRfkVWp2Qb4TXcG+Iqb6fE7rlra/XusIFxRMvxYt6aIdu9U/uHyIPr+\n"
            "XDrrqPIizKvvcRRfFUrh2yOZtrYst4dWIUI2f5c1mdt2R39JQF5vRT/2jy5vgrKe\n"
            "9eVgWIcf/N+YliOfhNV/re+eGTONGV3/HkBgqZRQ2W1+LUym2SzAKmlR8/Yw7O1P\n"
            "oP89ybunaLYnYmJJCi+cbMlLAZEaKZ7nDL9xoZkbYm9LEb6hgiSjgOKZei3q5+/F\n"
            "Kt10SfXkMye+/XJMkk8Isfl+jk3DGiZ7AAs44CRYSV26JjlZxEW1mwOIaC5FNuwN\n"
            "oBBC7J1OpcD2Q6xw90j0/vKVehSjPZTcWM5CqKizaZeZTAxxTKnJ3RBaiprlAqBN\n"
            "9eXrXLsJYU6D9oc4Kov7ZVd+CYa4PwNb8XbdIM1eQPyYJBhFllHTVlDDBVc0Q89Y\n"
            "fEJCHx9E1NPIkfEUVW7+N4MHcoa0ADqncw==\n"
            "-----END ENCRYPTED PRIVATE KEY-----\n"
        >>},
        {ec_pbes2_des3, <<
            "-----BEGIN ENCRYPTED PRIVATE KEY-----\n"
            "MIHdMEgGCSqGSIb3DQEFDTA7MCMGCSqGSIb3DQEFDDAWBBDm36UUzC0yKlp5zDTn\n"
            "AG9wAgIB9DAUBggqhkiG9w0DBwQIiiNMGitj9+4EgZDDR7Y/+YyRz4Prpu8P1+2v\n"
            "qu5TYI6236RlS/xPwh9n8r404KsyV/y3uOFyt/gbUFSA9ocho7XQ4+pcjH/xVaKe\n"
            "syZZI29j0kmvVisuDAboGE54WYi/faqE3ug5DRq8nxQaSOO4tkrdpYAArFEkb4vK\n"
            "+bTj1J2Qaszs3QjtDE6F8XSdrJsJ9dI9TmeM/3JPsi0=\n"
            "-----END ENCRYPTED PRIVATE KEY-----\n"
        >>},
        {rsa_legacy_aes, <<
            "-----BEGIN RSA PRIVATE KEY-----\n"
            "Proc-Type: 4,ENCRYPTED\n"
            "DEK-Info: AES-128-CBC,63F2AF6945B1B7DE26E4444968994376\n"
            "\n"
            "fsuu4mQ5LQfZ50TpCpVe0H56QGS3EPXdnBPc+Fh8ZWlovk0OcF7zKgE0wGvsIFAu\n"
            "kE4q+a8BDtzYDhijvxf1Fu9xovxwnpfbdiOhclmp7WOEDReJXnxy95XotPkcQqPZ\n"
            "VQksXOTztWStqUCzN9uQZ2IHKO3TJuajYgGIzduMGoLSJIxauI8HWObgoq5gkuBf\n"
            "c0Frs3jlTBOL8EyhEa1+Zau0aaPXVgvzmwI7CZXuk7S7aGfTc57eDvZ95k6nIHB1\n"
            "UGjd1MCnA4xXw/zp33LkIg07cvcyMzUxfAzD6gjdMaWwYFrDlMHeRMDU7S4WoShR\n"
            "Qxu0ykKlEpBaZ2zs3Kd80gj6J68yUcYMBfWMj4ZuW4qi99EX5MNNHLl/CdNFsCOH\n"
            "0hSHHOej5zz/53FIPJeMifBTk+S/dmUPXFjf8v0wY5B8NNgt/rDBtXu/WnVGJRxt\n"
            "BO8NEI5v21kwdB/PvAaOalmG+gO7BsBKO3oDN/0q6d1xvYBC/A11YZ34fkYKAtZe\n"
            "Fh+71cvx/afvZkD5UKt1x4ZLoVGc0KPGETZxgaR8jEmIYA+sgImsRVwuvG2WYlwE\n"
            "rNh0T5233ogu3IXEJaEAiSl7aNg9Oae9qZQdQHBakqQjT572FcGcALgFJux1+ROX\n"
            "M6M9PGyem9gm0Dy6XAgBQ77sgVMoju0Bx4T9YAzRT1FD/E4mnD4zt9KzzawRP+gQ\n"
            "gp84oXb3pOR9gqVa9yrM+LJhS1xEbIa2ljjsYrZ5d4f0BEU3CNMJg9AmSYL3b1kv\n"
            "FVDZBxM8BXTo0DpmBv6LbGElyDsklgAjHGC2CQk+A3jfsn+wl/iRdqPJ5UduSeJR\n"
            "-----END RSA PRIVATE KEY-----\n"
        >>},
        {ec_legacy_des3, <<
            "-----BEGIN EC PRIVATE KEY-----\n"
            "Proc-Type: 4,ENCRYPTED\n"
            "DEK-Info: DES-EDE3-CBC,E22C9AB92BB25D33\n"
            "\n"
            "zs7viEn69tcImIGGhNnKp4up9ki6PdUo6SmCr9l6tEU+IPXTJVjUO3VkaGta0lkC\n"
            "vnIAmnJmS+/hAbPoZtjuCo5O5Xk+MMaDhLj9zGRh3MmrYQJWbsn/6+6IMFdAX1Zz\n"
            "u9JtqPJqB1Rb39Me55q6GD1of2eTCpOODEi+OV7xWYs=\n"
            "-----END EC PRIVATE KEY-----\n"
        >>}
    ].
