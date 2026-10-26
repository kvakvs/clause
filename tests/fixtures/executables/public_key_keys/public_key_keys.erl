%% public_key's keys and signatures (plan 21 step LIB80C): signing and verification with every key type, RSA
%% encryption and its padding options, key generation and agreement, certificates signed and verified, signature and
%% digest types, OpenSSL's short name hashes, SSH curve names and DH group selection. Randomised results (ECDSA,
%% ML-DSA, generated keys) are checked by verification and agreement only. The DER data was made once with OTP's
%% public_key:pkix_test_data/1.
-module(public_key_keys).
-export([main/1]).

-include_lib("public_key/include/public_key.hrl").

main(_) ->
    Data = data(),
    Rsa = public_key:der_decode('RSAPrivateKey', proplists:get_value(rsa, Data)),
    RsaPublic = public_key:der_decode('RSAPublicKey', proplists:get_value(rsa_public, Data)),
    Ec = public_key:der_decode('ECPrivateKey', proplists:get_value(ec, Data)),
    types(),
    rsa(Rsa, RsaPublic),
    curves(Ec),
    post_quantum(),
    generated(),
    certificates(Data, Rsa, RsaPublic, Ec),
    names(Data),
    groups(),
    traces(),
    io:format("public_key_keys: ok~n").

%% A call's result, or its exception; crypto's errors without their source location, which differs by design.
safe(Fun) ->
    try
        {ok, Fun()}
    catch
        error:{Class, {File, Line}, Description} when is_list(File), is_integer(Line) -> {error, {Class, Description}};
        Class:Reason -> {Class, Reason}
    end.

hex(Binary) -> binary:encode_hex(Binary).

%% public_key through a variable: its legacy RSA functions without their lint warnings.
legacy() -> public_key.

types() ->
    Oids = [
        ?sha1WithRSAEncryption,
        ?'sha-1WithRSAEncryption',
        ?sha224WithRSAEncryption,
        ?sha256WithRSAEncryption,
        ?sha384WithRSAEncryption,
        ?sha512WithRSAEncryption,
        ?md2WithRSAEncryption,
        ?md5WithRSAEncryption,
        ?'id-dsa-with-sha1',
        {2, 16, 840, 1, 101, 3, 4, 3, 1},
        {2, 16, 840, 1, 101, 3, 4, 3, 2},
        ?'ecdsa-with-SHA1',
        ?'ecdsa-with-SHA256',
        ?'ecdsa-with-SHA384',
        ?'ecdsa-with-SHA512',
        ?'id-Ed25519',
        ?'id-Ed448',
        ?'id-ml-dsa-44',
        ?'id-ml-dsa-65',
        ?'id-ml-dsa-87',
        ?'id-slh-dsa-sha2-128s',
        ?'id-slh-dsa-shake-256f',
        ?'id-RSASSA-PSS',
        {1, 2, 3}
    ],
    [io:format("sign types ~w ~p~n", [Oid, safe(fun() -> public_key:pkix_sign_types(Oid) end)]) || Oid <- Oids],
    Hashes = [
        ?'id-sha1', ?'id-sha224', ?'id-sha256', ?'id-sha384', ?'id-sha512', ?'id-md5', {2, 16, 840, 1, 101, 3, 4, 2, 8}
    ],
    [io:format("hash type ~w ~p~n", [Oid, safe(fun() -> public_key:pkix_hash_type(Oid) end)]) || Oid <- Hashes],
    [
        io:format("ssh ~p ~p~n", [Name, safe(fun() -> public_key:ssh_curvename2oid(Name) end)])
     || Name <- [<<"nistp256">>, <<"nistp384">>, <<"nistp521">>, <<"curve25519">>]
    ],
    [
        io:format("ssh ~w ~p~n", [Oid, safe(fun() -> public_key:oid2ssh_curvename(Oid) end)])
     || Oid <- [?secp256r1, ?secp384r1, ?secp521r1, ?secp256k1]
    ].

rsa(Rsa, RsaPublic) ->
    Msg = <<"the message to sign">>,
    Signature = public_key:sign(Msg, sha256, Rsa),
    Digest = crypto:hash(sha256, Msg),
    io:format("rsa sign ~s~n  ~p ~p ~p ~p~n", [
        hex(Signature),
        public_key:verify(Msg, sha256, Signature, RsaPublic),
        public_key:verify(<<"other">>, sha256, Signature, RsaPublic),
        public_key:sign({digest, Digest}, sha256, Rsa) =:= Signature,
        public_key:verify({digest, Digest}, sha256, Signature, Rsa)
    ]),
    Pss = [{rsa_padding, rsa_pkcs1_pss_padding}, {rsa_pss_saltlen, 32}],
    PssSignature = public_key:sign(Msg, sha256, Rsa, Pss),
    io:format("rsa pss ~p ~p~n", [
        public_key:verify(Msg, sha256, PssSignature, RsaPublic, Pss),
        public_key:verify(Msg, sha256, PssSignature, RsaPublic)
    ]),
    io:format("rsa with params ~p~n", [public_key:sign(Msg, sha256, {Rsa, params}) =:= Signature]),
    Private = (legacy()):encrypt_private(<<"secret">>, Rsa),
    io:format("encrypt private ~s~n  ~p~n", [hex(Private), (legacy()):decrypt_public(Private, RsaPublic)]),
    Paddings = [
        [],
        [rsa_pkcs1_oaep_padding],
        [{rsa_padding, rsa_pkcs1_oaep_padding}, {rsa_oaep_md, sha256}],
        [{rsa_pad, rsa_pkcs1_padding}, {rsa_padding, rsa_no_padding}]
    ],
    [
        io:format("encrypt public ~p ~p~n", [
            Options,
            safe(fun() ->
                public_key:decrypt_private(public_key:encrypt_public(<<"secret">>, RsaPublic, Options), Rsa, Options)
            end)
        ])
     || Options <- Paddings
    ],
    Block = binary:copy(<<1>>, 128),
    io:format("no padding ~p~n", [
        public_key:decrypt_public(public_key:encrypt_private(Block, Rsa, [rsa_no_padding]), RsaPublic, [rsa_no_padding]) =:=
            Block
    ]),
    Short = Rsa#'RSAPrivateKey'{prime1 = undefined},
    io:format("short key ~p~n", [public_key:sign(Msg, sha256, Short) =:= Signature]),
    Fun = fun(PlainText, Options) -> {extern, PlainText, Options} end,
    io:format("extern ~p~n  ~p~n", [
        public_key:encrypt_private(<<"x">>, #{encrypt_fun => Fun}, [rsa_pkcs1_padding]),
        public_key:sign(<<"x">>, sha, #{sign_fun => fun(M, D, O) -> {signed, M, D, O} end}, [opt])
    ]),
    Errors = [
        fun() -> public_key:sign(Msg, sha256, not_a_key) end,
        fun() -> public_key:verify(Msg, sha256, Signature, not_a_key) end,
        fun() -> public_key:verify(Msg, sha256, not_a_binary, RsaPublic) end,
        fun() -> (legacy()):encrypt_public(not_a_binary, RsaPublic) end,
        fun() -> (legacy()):decrypt_private(<<"x">>, RsaPublic) end,
        fun() ->
            public_key:verify(Msg, sha, <<"s">>, #'DSAPrivateKey'{version = 0, p = 23, q = 11, g = 4, y = 8, x = 3})
        end
    ],
    [io:format("rsa error ~p~n", [safe(Call)]) || Call <- Errors].

curves(Ec) ->
    Msg = <<"curve message">>,
    Signature = public_key:sign(Msg, sha256, Ec),
    Public = {#'ECPoint'{point = Ec#'ECPrivateKey'.publicKey}, Ec#'ECPrivateKey'.parameters},
    io:format("ecdsa ~p ~p ~p~n", [
        public_key:verify(Msg, sha256, Signature, Public),
        public_key:verify(Msg, sha256, Signature, Ec),
        public_key:verify(<<"other">>, sha256, Signature, Public)
    ]),
    [eddsa(Curve, Oid) || {Curve, Oid} <- [{ed25519, ?'id-Ed25519'}, {ed448, ?'id-Ed448'}]].

%% An EdDSA key made from a fixed seed: its signature is deterministic.
eddsa(Curve, Oid) ->
    Size =
        case Curve of
            ed25519 -> 32;
            ed448 -> 57
        end,
    {Pub, Priv} = crypto:generate_key(eddsa, Curve, binary:copy(<<7>>, Size)),
    Key = #'ECPrivateKey'{version = 1, privateKey = Priv, parameters = {namedCurve, Oid}, publicKey = Pub},
    Signature = public_key:sign(<<"edwards">>, none, Key),
    io:format("~p ~s~n  ~p ~p ~p~n", [
        Curve,
        hex(Signature),
        public_key:verify(<<"edwards">>, none, Signature, {#'ECPoint'{point = Pub}, {namedCurve, Oid}}),
        public_key:verify(<<"edwards">>, none, Signature, {ed_pub, Curve, Pub}),
        public_key:sign(<<"edwards">>, none, {ed_pri, Curve, Pub, Priv}) =:= Signature
    ]).

post_quantum() ->
    Msg = <<"quantum">>,
    [post_quantum(Msg, Algorithm) || Algorithm <- [mldsa44, mldsa87, slh_dsa_shake_128f, slh_dsa_sha2_128f]],
    io:format("unknown ~p~n", [safe(fun() -> public_key:generate_key(mlkem512) end)]).

%% A generated key pair signs and verifies; OTP's ML-DSA private keys have an empty seed, which crypto rejects.
post_quantum(Msg, Algorithm) ->
    {Public, Private} = public_key:generate_key(Algorithm),
    case safe(fun() -> public_key:sign(Msg, none, Private) end) of
        {ok, Signature} ->
            io:format("~p ~p ~p ~p~n", [
                Algorithm,
                element(1, Public),
                public_key:verify(Msg, none, Signature, Public),
                public_key:verify(<<"other">>, none, Signature, Public)
            ]);
        Error ->
            io:format("~p ~p ~p~n", [Algorithm, element(1, Public), Error])
    end,
    element(1, Private) =:= 'ML-DSAPrivateKey' andalso expanded(Msg, Public, Private).

%% An ML-DSA private key without its seed signs with its expanded key.
expanded(Msg, Public, Private) ->
    Expanded = Private#'ML-DSAPrivateKey'{seed = undefined},
    io:format("  expanded ~p~n", [
        case safe(fun() -> public_key:sign(Msg, none, Expanded) end) of
            {ok, Signature} -> public_key:verify(Msg, none, Signature, Public);
            Error -> Error
        end
    ]).

generated() ->
    Rsa = public_key:generate_key({rsa, 1024, 65537}),
    io:format("rsa ~p ~p ~p~n", [
        Rsa#'RSAPrivateKey'.version,
        bit_size(binary:encode_unsigned(Rsa#'RSAPrivateKey'.modulus)),
        Rsa#'RSAPrivateKey'.publicExponent
    ]),
    Curves = [{namedCurve, secp256r1}, {namedCurve, ?secp384r1}, {namedCurve, x25519}, {namedCurve, ?'id-X448'}],
    [agreement(Params) || Params <- Curves],
    {ok, {Size, {G, P}}} = public_key:dh_gex_group(2048, 2048, 2048, undefined),
    Dh = #'DHParameter'{prime = P, base = G},
    {PublicA, PrivateA} = public_key:generate_key(Dh),
    {PublicB, PrivateB} = public_key:generate_key(Dh),
    io:format("dh ~p ~p~n", [
        Size, public_key:compute_key(PublicA, PrivateB, Dh) =:= public_key:compute_key(PublicB, PrivateA, Dh)
    ]),
    io:format("unknown curve ~p~n", [safe(fun() -> public_key:generate_key({namedCurve, {1, 2, 3}}) end)]).

%% Two generated keys of a curve agree on their shared secret.
agreement(Params) ->
    A = public_key:generate_key(Params),
    B = public_key:generate_key(Params),
    Shared = public_key:compute_key(#'ECPoint'{point = B#'ECPrivateKey'.publicKey}, A),
    io:format("~p ~p ~p ~p~n", [
        Params,
        A#'ECPrivateKey'.parameters,
        byte_size(Shared),
        Shared =:= public_key:compute_key(#'ECPoint'{point = A#'ECPrivateKey'.publicKey}, B)
    ]).

certificates(Data, Rsa, RsaPublic, Ec) ->
    [Peer | CaCerts] = [proplists:get_value(cert, Data) | proplists:get_value(cacerts, Data)],
    Keys = [issuer_key(Der) || Der <- [Peer | CaCerts]],
    [
        io:format("pkix_verify ~p~n", [[public_key:pkix_verify(Der, Key) || Key <- Keys]])
     || Der <- [Peer | CaCerts]
    ],
    Tbs = (public_key:pkix_decode_cert(Peer, otp))#'OTPCertificate'.tbsCertificate,
    RsaAlg = #'SignatureAlgorithm'{algorithm = ?sha256WithRSAEncryption, parameters = asn1_NOVALUE},
    Signed = public_key:pkix_sign(Tbs#'OTPTBSCertificate'{signature = RsaAlg}, Rsa),
    io:format("pkix_sign rsa ~s~n  ~p ~p~n", [
        hex(Signed), public_key:pkix_verify(Signed, RsaPublic), public_key:pkix_verify(Signed, hd(Keys))
    ]),
    EcAlg = #'SignatureAlgorithm'{algorithm = ?'ecdsa-with-SHA256', parameters = asn1_NOVALUE},
    EcSigned = public_key:pkix_sign(Tbs#'OTPTBSCertificate'{signature = EcAlg}, Ec),
    EcPublic = {#'ECPoint'{point = Ec#'ECPrivateKey'.publicKey}, Ec#'ECPrivateKey'.parameters},
    io:format("pkix_sign ecdsa ~p~n", [public_key:pkix_verify(EcSigned, EcPublic)]),
    {Pub, Priv} = crypto:generate_key(eddsa, ed25519, binary:copy(<<3>>, 32)),
    Ed = #'ECPrivateKey'{version = 1, privateKey = Priv, parameters = {namedCurve, ?'id-Ed25519'}, publicKey = Pub},
    EdAlg = #'SignatureAlgorithm'{algorithm = ?'id-Ed25519', parameters = asn1_NOVALUE},
    EdSigned = public_key:pkix_sign(Tbs#'OTPTBSCertificate'{signature = EdAlg}, Ed),
    EdPublic = {#'ECPoint'{point = Pub}, {namedCurve, ?'id-Ed25519'}},
    io:format("pkix_sign eddsa ~s~n  ~p ~p ~p~n", [
        hex(EdSigned),
        public_key:pkix_verify(EdSigned, EdPublic),
        public_key:pkix_verify(Signed, EdPublic),
        public_key:pkix_verify(EdSigned, EcPublic)
    ]),
    #'RSASSA-AlgorithmIdentifier'{parameters = Default} = ?'rSASSA-PSS-Default-Identifier',
    Sha256 = #'HashAlgorithm'{algorithm = ?'id-sha256', parameters = 'NULL'},
    Pss = Default#'RSASSA-PSS-params'{
        hashAlgorithm = Sha256,
        maskGenAlgorithm = #'MaskGenAlgorithm'{algorithm = ?'id-mgf1', parameters = Sha256},
        saltLength = 32
    },
    PssAlg = #'SignatureAlgorithm'{algorithm = ?'id-RSASSA-PSS', parameters = Pss},
    PssSigned = public_key:pkix_sign(Tbs#'OTPTBSCertificate'{signature = PssAlg}, {Rsa, Pss}),
    io:format("pkix_sign pss ~p ~p~n", [
        public_key:pkix_verify(PssSigned, {RsaPublic, Pss}), public_key:pkix_verify(PssSigned, RsaPublic)
    ]),
    io:format("pkix_sign error ~p~n", [safe(fun() -> public_key:pkix_sign(Tbs, not_a_key) end)]).

%% The public key of a certificate, as pkix_verify/2 takes it.
issuer_key(Der) ->
    #'OTPCertificate'{tbsCertificate = #'OTPTBSCertificate'{subjectPublicKeyInfo = Info}} =
        public_key:pkix_decode_cert(Der, otp),
    #'OTPSubjectPublicKeyInfo'{algorithm = #'PublicKeyAlgorithm'{parameters = Params}, subjectPublicKey = Key} = Info,
    case Key of
        #'RSAPublicKey'{} -> Key;
        _ -> {Key, Params}
    end.

names(Data) ->
    Certs = [proplists:get_value(cert, Data) | proplists:get_value(cacerts, Data)],
    [
        begin
            #'OTPCertificate'{tbsCertificate = #'OTPTBSCertificate'{issuer = Issuer, subject = Subject}} =
                public_key:pkix_decode_cert(Der, otp),
            io:format("short_name_hash ~s ~s~n", [
                public_key:short_name_hash(Issuer), public_key:short_name_hash(Subject)
            ])
        end
     || Der <- Certs
    ],
    Spaced =
        {rdnSequence, [
            [
                #'AttributeTypeAndValue'{
                    type = ?'id-at-commonName', value = {utf8String, <<"  Mixed   CASE\tName  "/utf8>>}
                }
            ],
            [#'AttributeTypeAndValue'{type = ?'id-at-countryName', value = "SE"}],
            [#'AttributeTypeAndValue'{type = ?'id-at-organizationName', value = {printableString, "Örg  Ä"}}]
        ]},
    io:format("short_name_hash ~s~n", [public_key:short_name_hash(Spaced)]),
    io:format("short_name_hash ~p~n", [safe(fun() -> public_key:short_name_hash({rdnSequence, [[bad]]}) end)]).

groups() ->
    io:format("sizes ~p~n", [public_key:dh_gex_group_sizes()]),
    Groups = [{1023, [{2, 7}]}, {2047, [{2, 11}]}, {3071, [{5, 13}]}, {4095, [{2, 17}]}],
    Requests = [
        {1024, 2048, 4096},
        {1024, 3000, 4096},
        {1024, 2600, 4096},
        {2050, 2050, 2050},
        {3000, 8000, 3080},
        {100, 200, 300},
        {5000, 5000, 9000}
    ],
    [
        io:format("dh_gex_group ~p ~p~n", [Request, public_key:dh_gex_group(Min, N, Max, Groups)])
     || {Min, N, Max} = Request <- Requests
    ],
    [
        io:format("default ~p ~p~n", [
            N,
            case public_key:dh_gex_group(Min, N, Max, undefined) of
                {ok, {Size, {G, P}}} -> {Size, is_integer(G), bit_size(binary:encode_unsigned(P))};
                Other -> Other
            end
        ])
     || {Min, N, Max} <- [{1024, 2048, 8192}, {3000, 3072, 3100}, {6000, 7000, 9000}, {1024, 1024, 1024}]
    ].

traces() ->
    Events = [
        {crt, {call, {public_key, pkix_decode_cert, [<<1, 2, 3, 4, 5, 6, 7, 8>>, otp]}}},
        {csp, {call, {public_key, pkix_ocsp_validate, [{a, {b, {c, {d, {e}}}}}, [1, 2, 3, 4, 5, 6, 7, 8], x, y, z]}}},
        {csp, {call, {public_key, otp_cert, [cert]}}},
        {csp, {return_from, {public_key, pkix_ocsp_validate, 5}, {ok, []}}}
    ],
    [
        io:format("trace ~s~n", [element(1, public_key:handle_trace(Kind, Event, stack))])
     || {Kind, Event} <- Events
    ],
    io:format("trace ~p~n", [safe(fun() -> public_key:handle_trace(other, event, stack) end)]).

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
