%% clause_public_key_keys: the keys and signatures of Clause's public_key (plan 21 step LIB80C,
%% docs/libraries/public_key.md#keys-and-signatures), which public_key's functions call: key records turned into
%% crypto's key lists, signing and verification, RSA encryption, key generation and agreement, certificate signing,
%% the signature and digest types of object identifiers, OpenSSL's short name hash and the SSH DH groups.
-module(clause_public_key_keys).
-export([
    compute_key/2,
    compute_key/3,
    decrypt_private/3,
    decrypt_public/3,
    dh_gex_group/4,
    dh_gex_group_sizes/0,
    encrypt_private/3,
    encrypt_public/3,
    first_element/1,
    generate_key/1,
    handle_trace/3,
    oid2ssh_curvename/1,
    pkix_hash_type/1,
    pkix_sign/2,
    pkix_sign_types/1,
    pkix_verify/2,
    rsa_opts/1,
    short_name_hash/1,
    sign_algorithm_types/1,
    sign/4,
    ssh_curvename2oid/1,
    verify/5
]).

-include("include/public_key.hrl").

%% Records of the generated modules that public_key.hrl does not define.
-record('DSA-Params', {p, q, g}).
-record('HashSingleAttribute', {type, value}).

%% The DSA signature algorithms of PKIXAlgs-2009, which public_key.hrl does not define.
-define('dsa-with-sha1', {1, 2, 840, 10040, 4, 3}).
-define('dsa-with-sha224', {2, 16, 840, 1, 101, 3, 4, 3, 1}).
-define('dsa-with-sha256', {2, 16, 840, 1, 101, 3, 4, 3, 2}).

%%--------------------------------------------------------------------------------------------------------------------
%% RSA encryption
%%--------------------------------------------------------------------------------------------------------------------

%% public_key:decrypt_private/3.
decrypt_private(CipherText, #'RSAPrivateKey'{} = Key, Options) when is_binary(CipherText), is_list(Options) ->
    crypto:private_decrypt(rsa, CipherText, rsa_private_key(Key), default_options(Options)).

%% public_key:decrypt_public/3.
decrypt_public(CipherText, #'RSAPublicKey'{modulus = N, publicExponent = E}, Options) when
    is_binary(CipherText), is_list(Options)
->
    crypto:public_decrypt(rsa, CipherText, [E, N], default_options(Options)).

%% public_key:encrypt_public/3.
encrypt_public(PlainText, #'RSAPublicKey'{modulus = N, publicExponent = E}, Options) when
    is_binary(PlainText), is_list(Options)
->
    crypto:public_encrypt(rsa, PlainText, [E, N], default_options(Options)).

%% public_key:encrypt_private/3: an RSA key or a map with the fun that encrypts.
encrypt_private(PlainText, Key, Options) when is_binary(PlainText), is_list(Options) ->
    Opts = default_options(Options),
    case sign_key(Key) of
        {extern, Fun} -> Fun(PlainText, Opts);
        {rsa, CryptoKey} -> crypto:private_encrypt(rsa, PlainText, CryptoKey, Opts)
    end.

%% The options with their padding first: rsa_pad or rsa_padding, else the first atom, else PKCS #1.
default_options([]) ->
    [{rsa_padding, rsa_pkcs1_padding}];
default_options(Opts) ->
    case {proplists:get_value(rsa_pad, Opts), proplists:get_value(rsa_padding, Opts)} of
        {undefined, undefined} ->
            case lists:dropwhile(fun erlang:is_tuple/1, Opts) of
                [Pad | _] -> set_padding(Pad, Opts);
                [] -> set_padding(rsa_pkcs1_padding, Opts)
            end;
        {undefined, Pad} ->
            set_padding(Pad, Opts);
        {Pad, _} ->
            set_padding(Pad, Opts)
    end.

set_padding(Pad, Opts) ->
    [{rsa_padding, Pad} | [{T, V} || {T, V} <- Opts, T =/= rsa_padding, T =/= rsa_pad]].

%%--------------------------------------------------------------------------------------------------------------------
%% Signatures
%%--------------------------------------------------------------------------------------------------------------------

%% public_key:sign/4: a DSA key signs a digest of type none as a SHA-1 digest.
sign(Digest, none, Key = #'DSAPrivateKey'{}, Options) when is_binary(Digest) ->
    sign({digest, Digest}, sha, Key, Options);
sign(DigestOrPlainText, DigestType, Key, Options) ->
    case sign_key(Key) of
        badarg ->
            erlang:error(badarg, [DigestOrPlainText, DigestType, Key, Options]);
        {extern, Fun} when is_function(Fun) ->
            Fun(DigestOrPlainText, DigestType, Options);
        {Algorithm, CryptoKey} ->
            crypto:sign(Algorithm, DigestType, DigestOrPlainText, CryptoKey, Options)
    end.

%% public_key:verify/5: false for a signature that is not a binary.
verify(Digest, none, Signature, Key = {_, #'Dss-Parms'{}}, Options) when is_binary(Digest) ->
    verify({digest, Digest}, sha, Signature, Key, Options);
verify(DigestOrPlainText, DigestType, Signature, Key, Options) when is_binary(Signature) ->
    case verify_key(Key) of
        badarg ->
            erlang:error(badarg, [DigestOrPlainText, DigestType, Signature, Key, Options]);
        {Algorithm, CryptoKey} ->
            crypto:verify(Algorithm, DigestType, DigestOrPlainText, Signature, CryptoKey, Options)
    end;
verify(_, _, _, _, _) ->
    false.

%% crypto's algorithm and key list of a private key, a map with an external fun, or badarg.
sign_key(#{encrypt_fun := KeyFun}) ->
    {extern, KeyFun};
sign_key(#{sign_fun := KeyFun}) ->
    {extern, KeyFun};
sign_key(#'RSAPrivateKey'{} = Key) ->
    {rsa, rsa_private_key(Key)};
sign_key({#'RSAPrivateKey'{} = Key, _}) ->
    {rsa, rsa_private_key(Key)};
sign_key(#'DSAPrivateKey'{p = P, q = Q, g = G, x = X}) ->
    {dss, [P, Q, G, X]};
sign_key(#'ECPrivateKey'{privateKey = PrivKey, parameters = {namedCurve, Curve} = Param}) when
    (Curve == ?'id-Ed25519') orelse (Curve == ?'id-Ed448')
->
    {eddsa, [PrivKey, curve_spec(Param)]};
sign_key(#'ECPrivateKey'{privateKey = PrivKey, parameters = Param}) ->
    {ecdsa, [PrivKey, curve_spec(Param)]};
sign_key(#'ML-DSAPrivateKey'{algorithm = Algo, seed = Key}) when Key =/= undefined ->
    {Algo, {seed, Key}};
sign_key(#'ML-DSAPrivateKey'{algorithm = Algo, expandedkey = Key}) when Key =/= undefined ->
    {Algo, {expandedkey, Key}};
sign_key(#'SLH-DSAPrivateKey'{algorithm = Algo, key = Key}) ->
    {Algo, Key};
sign_key({ed_pri, Curve, _Pub, Priv}) ->
    {eddsa, [Priv, Curve]};
sign_key(_) ->
    badarg.

%% crypto's algorithm and key list of a public key, or of the public part of a private key; badarg for another term.
verify_key(#'RSAPublicKey'{modulus = Mod, publicExponent = Exp}) ->
    {rsa, [Exp, Mod]};
verify_key({#'ECPoint'{point = Point}, {namedCurve, Curve} = Param}) when
    (Curve == ?'id-Ed25519') orelse (Curve == ?'id-Ed448')
->
    {eddsa, [Point, curve_spec(Param)]};
verify_key({#'ECPoint'{point = Point}, Param}) ->
    {ecdsa, [Point, curve_spec(Param)]};
verify_key({Key, #'Dss-Parms'{p = P, q = Q, g = G}}) ->
    {dss, [P, Q, G, Key]};
verify_key({ed_pub, Curve, Key}) ->
    {eddsa, [Key, Curve]};
verify_key(#'ML-DSAPublicKey'{algorithm = Algo, key = Key}) ->
    {Algo, Key};
verify_key({#'ML-DSAPublicKey'{algorithm = Algo, key = Key}, _}) ->
    {Algo, Key};
verify_key(#'SLH-DSAPublicKey'{algorithm = Algo, key = Key}) ->
    {Algo, Key};
verify_key(#'RSAPrivateKey'{modulus = Mod, publicExponent = Exp}) ->
    verify_key(#'RSAPublicKey'{modulus = Mod, publicExponent = Exp});
verify_key(#'ECPrivateKey'{parameters = Param, publicKey = {_, Point}}) ->
    verify_key({#'ECPoint'{point = Point}, Param});
verify_key(#'ECPrivateKey'{parameters = Param, publicKey = Point}) ->
    verify_key({#'ECPoint'{point = Point}, Param});
verify_key(#'DSAPrivateKey'{y = Y, p = P, q = Q, g = G}) ->
    %% As OTP: DSA-Params, which the Dss-Parms clause above does not take.
    verify_key({Y, #'DSA-Params'{p = P, q = Q, g = G}});
verify_key(_) ->
    badarg.

%% crypto's RSA private key: all CRT components when they are integers, else [E, N, D].
rsa_private_key(#'RSAPrivateKey'{
    modulus = N,
    publicExponent = E,
    privateExponent = D,
    prime1 = P1,
    prime2 = P2,
    exponent1 = E1,
    exponent2 = E2,
    coefficient = C
}) when
    is_integer(N),
    is_integer(E),
    is_integer(D),
    is_integer(P1),
    is_integer(P2),
    is_integer(E1),
    is_integer(E2),
    is_integer(C)
->
    [E, N, D, P1, P2, E1, E2, C];
rsa_private_key(#'RSAPrivateKey'{modulus = N, publicExponent = E, privateExponent = D}) when
    is_integer(N), is_integer(E), is_integer(D)
->
    [E, N, D].

%% crypto's curve of EC parameters: a curve name, or the explicit curve's field, coefficients, base, order and
%% cofactor.
curve_spec(#'ECParameters'{
    fieldID = #'FieldID'{fieldType = Type, parameters = Params},
    curve = PCurve,
    base = Base,
    order = Order,
    cofactor = CoFactor
}) ->
    Field = field(table_name(curve_type, Type), Params),
    {Field, {PCurve#'Curve'.a, PCurve#'Curve'.b, none}, Base, Order, CoFactor};
curve_spec({ecParameters, ECParams}) ->
    curve_spec(ECParams);
curve_spec({namedCurve, OID}) when is_tuple(OID), is_integer(element(1, OID)) ->
    curve_spec({namedCurve, table_name(named_curve, OID)});
curve_spec({namedCurve, Name}) when is_atom(Name) ->
    Name.

%% As OTP: the prime decoded as the type Prime-p, which der_decode/2 does not know.
field(prime_field, Params) ->
    {prime_field, public_key:der_decode('Prime-p', Params)}.

%%--------------------------------------------------------------------------------------------------------------------
%% Keys
%%--------------------------------------------------------------------------------------------------------------------

%% public_key:generate_key/1.
generate_key(#'DHParameter'{prime = P, base = G}) ->
    crypto:generate_key(dh, [P, G]);
generate_key({namedCurve, _} = Params) ->
    ec_generate_key(Params);
generate_key({ecParameters, _} = Params) ->
    ec_generate_key(Params);
generate_key(#'ECParameters'{} = Params) ->
    ec_generate_key(Params);
generate_key({rsa, ModulusSize, PublicExponent}) ->
    case crypto:generate_key(rsa, {ModulusSize, PublicExponent}) of
        {[E, N], [E, N, D, P, Q, DModP, DModQ, QInvModP]} ->
            #'RSAPrivateKey'{
                version = 'two-prime',
                modulus = crypto:bytes_to_integer(N),
                publicExponent = crypto:bytes_to_integer(E),
                privateExponent = crypto:bytes_to_integer(D),
                prime1 = crypto:bytes_to_integer(P),
                prime2 = crypto:bytes_to_integer(Q),
                exponent1 = crypto:bytes_to_integer(DModP),
                exponent2 = crypto:bytes_to_integer(DModQ),
                coefficient = crypto:bytes_to_integer(QInvModP)
            };
        {[E, N], [E, N, D]} ->
            #'RSAPrivateKey'{
                version = 'two-prime',
                modulus = crypto:bytes_to_integer(N),
                publicExponent = crypto:bytes_to_integer(E),
                privateExponent = crypto:bytes_to_integer(D),
                prime1 = '?',
                prime2 = '?',
                exponent1 = '?',
                exponent2 = '?',
                coefficient = '?'
            };
        Other ->
            Other
    end;
generate_key(KeyAlg) when KeyAlg == mldsa44; KeyAlg == mldsa65; KeyAlg == mldsa87 ->
    {Public, Private} = crypto:generate_key(KeyAlg, []),
    {#'ML-DSAPublicKey'{algorithm = KeyAlg, key = Public}, #'ML-DSAPrivateKey'{
        algorithm = KeyAlg, expandedkey = Private
    }};
generate_key(KeyAlg) ->
    case clause_public_key:name_oid(slh_dsa, KeyAlg) of
        undefined ->
            erlang:error(function_clause, [KeyAlg]);
        _ ->
            {Public, Private} = crypto:generate_key(KeyAlg, []),
            {#'SLH-DSAPublicKey'{algorithm = KeyAlg, key = Public}, #'SLH-DSAPrivateKey'{
                algorithm = KeyAlg, key = Private
            }}
    end.

%% An EC key pair of crypto's key type for the curve, with the parameters a curve name normalised to its OID.
ec_generate_key(Params) ->
    Curve = curve_spec(Params),
    {PubKey, PrivateKey} = crypto:generate_key(curve_key_type(Curve), Curve),
    #'ECPrivateKey'{
        version = ecPrivkeyVer1,
        privateKey = PrivateKey,
        parameters = normalised_params(Params),
        publicKey = PubKey
    }.

normalised_params({namedCurve, Name}) when is_atom(Name) ->
    {namedCurve, table_oid(named_curve, Name)};
normalised_params(#'ECParameters'{} = ECParams) ->
    {ecParameters, ECParams};
normalised_params(Other) ->
    Other.

curve_key_type(ed25519) -> eddsa;
curve_key_type(ed448) -> eddsa;
curve_key_type(x25519) -> eddh;
curve_key_type(x448) -> eddh;
curve_key_type(_) -> ecdh.

%% public_key:compute_key/2: ECDH, or X25519/X448.
compute_key(#'ECPoint'{point = Point}, #'ECPrivateKey'{privateKey = PrivKey, parameters = {namedCurve, Curve} = Param}) when
    (Curve == ?'id-X25519') orelse (Curve == ?'id-X448')
->
    crypto:compute_key(eddh, Point, PrivKey, curve_spec(Param));
compute_key(#'ECPoint'{point = Point}, #'ECPrivateKey'{privateKey = PrivKey, parameters = Param}) ->
    crypto:compute_key(ecdh, Point, PrivKey, curve_spec(Param)).

%% public_key:compute_key/3: Diffie-Hellman.
compute_key(PubKey, PrivKey, #'DHParameter'{prime = P, base = G}) ->
    crypto:compute_key(dh, PubKey, PrivKey, [P, G]).

%%--------------------------------------------------------------------------------------------------------------------
%% Certificates
%%--------------------------------------------------------------------------------------------------------------------

%% public_key:pkix_sign/2: the TBS certificate signed and the certificate encoded.
pkix_sign(#'OTPTBSCertificate'{signature = #'SignatureAlgorithm'{} = SigAlg} = TBSCert, Key) ->
    Msg = public_key:pkix_encode('OTPTBSCertificate', TBSCert, otp),
    {DigestType, _, Opts} = sign_algorithm_types(SigAlg),
    SignKey =
        case Key of
            {#'RSAPrivateKey'{} = RsaKey, _} -> RsaKey;
            _ -> Key
        end,
    Signature = sign(Msg, DigestType, SignKey, Opts),
    Cert = #'OTPCertificate'{tbsCertificate = TBSCert, signatureAlgorithm = SigAlg, signature = Signature},
    public_key:pkix_encode('OTPCertificate', Cert, otp).

%% public_key:pkix_verify/2: the signature of a DER certificate checked with the issuer's key.
pkix_verify(DerCert, {Key, #'Dss-Parms'{}} = DSAKey) when is_binary(DerCert), is_integer(Key) ->
    {DigestType, PlainText, Signature} = verify_data(DerCert),
    verify(PlainText, DigestType, Signature, DSAKey, []);
pkix_verify(DerCert, #'RSAPublicKey'{} = RSAKey) when is_binary(DerCert) ->
    {DigestType, PlainText, Signature} = verify_data(DerCert),
    verify(PlainText, DigestType, Signature, RSAKey, []);
pkix_verify(DerCert, {#'RSAPublicKey'{} = RSAKey, #'RSASSA-PSS-params'{} = Params}) when is_binary(DerCert) ->
    {DigestType, PlainText, Signature} = verify_data(DerCert),
    verify(PlainText, DigestType, Signature, RSAKey, rsa_opts(Params));
pkix_verify(DerCert, Key = {#'ECPoint'{}, {namedCurve, Curve}}) when
    (Curve == ?'id-Ed25519' orelse Curve == ?'id-Ed448') andalso is_binary(DerCert)
->
    case verify_data(DerCert) of
        {none = DigestType, PlainText, Signature} -> verify(PlainText, DigestType, Signature, Key, []);
        _ -> false
    end;
pkix_verify(DerCert, Key = {#'ECPoint'{}, _}) when is_binary(DerCert) ->
    case verify_data(DerCert) of
        {none, _, _} -> false;
        {DigestType, PlainText, Signature} -> verify(PlainText, DigestType, Signature, Key, [])
    end;
pkix_verify(DerCert, #'ML-DSAPublicKey'{} = Key) ->
    {DigestType, PlainText, Signature} = verify_data(DerCert),
    verify(PlainText, DigestType, Signature, Key, []);
pkix_verify(DerCert, #'SLH-DSAPublicKey'{} = Key) ->
    {DigestType, PlainText, Signature} = verify_data(DerCert),
    verify(PlainText, DigestType, Signature, Key, []).

%% The digest type, the signed TBS bytes and the signature of a DER certificate (pubkey_cert:verify_data/1).
verify_data(DerCert) ->
    #'OTPCertificate'{signature = Signature, signatureAlgorithm = SigAlg} =
        public_key:pkix_decode_cert(DerCert, relaxed),
    {DigestType, _, _} = sign_algorithm_types(SigAlg),
    {DigestType, first_element(DerCert), Signature}.

%% The first element of a DER SEQUENCE, with its tag and length: the TBS certificate as the certificate holds it.
first_element(<<16#30, Rest/binary>>) ->
    element_tlv(content(Rest)).

element_tlv(<<Tag, Rest/binary>>) ->
    {Header, Length} = length_header(Rest),
    <<Value:Length/binary, _/binary>> = binary:part(Rest, byte_size(Header), byte_size(Rest) - byte_size(Header)),
    <<Tag, Header/binary, Value/binary>>.

content(Bytes) ->
    {Header, _} = length_header(Bytes),
    binary:part(Bytes, byte_size(Header), byte_size(Bytes) - byte_size(Header)).

%% The DER length octets at the start of bytes, and the length they give.
length_header(<<0:1, Length:7, _/binary>>) ->
    {<<0:1, Length:7>>, Length};
length_header(<<1:1, Count:7, Rest/binary>>) ->
    <<Length:Count/unit:8, _/binary>> = Rest,
    {<<1:1, Count:7, Length:Count/unit:8>>, Length}.

%% The digest type, signature type and crypto options of a certificate's signature algorithm
%% (pubkey_cert:x509_pkix_sign_types/1).
sign_algorithm_types(#'SignatureAlgorithm'{
    algorithm = ?'id-RSASSA-PSS',
    parameters = #'RSASSA-PSS-params'{saltLength = SaltLen, hashAlgorithm = #'HashAlgorithm'{algorithm = Alg}}
}) ->
    Hash = pkix_hash_type(Alg),
    {Hash, rsa_pss_pss, [{rsa_padding, rsa_pkcs1_pss_padding}, {rsa_pss_saltlen, SaltLen}, {rsa_mgf1_md, Hash}]};
sign_algorithm_types(#'SignatureAlgorithm'{algorithm = Alg}) ->
    {Hash, Sign} = pkix_sign_types(Alg),
    {Hash, Sign, []}.

%% crypto's options of RSASSA-PSS parameters.
rsa_opts(#'RSASSA-PSS-params'{
    saltLength = SaltLen,
    maskGenAlgorithm = #'MaskGenAlgorithm'{
        algorithm = ?'id-mgf1', parameters = #'HashAlgorithm'{algorithm = HashAlgoOid}
    }
}) ->
    HashAlgo = pkix_hash_type(HashAlgoOid),
    [{rsa_padding, rsa_pkcs1_pss_padding}, {rsa_pss_saltlen, SaltLen}, {rsa_mgf1_md, HashAlgo}].

%% public_key:pkix_sign_types/1.
pkix_sign_types(Oid) ->
    case lists:keyfind(Oid, 1, sign_types()) of
        {_, Digest, Sign} -> {Digest, Sign};
        false -> erlang:error(function_clause, [Oid])
    end.

%% The signature algorithms of certificates: {OID, digest type, signature type}.
sign_types() ->
    [
        {?sha1WithRSAEncryption, sha, rsa},
        {?'sha-1WithRSAEncryption', sha, rsa},
        {?sha224WithRSAEncryption, sha224, rsa},
        {?sha256WithRSAEncryption, sha256, rsa},
        {?sha384WithRSAEncryption, sha384, rsa},
        {?sha512WithRSAEncryption, sha512, rsa},
        {?md2WithRSAEncryption, md2, rsa},
        {?md5WithRSAEncryption, md5, rsa},
        {?'dsa-with-sha1', sha, dsa},
        {?'dsa-with-sha224', sha224, dsa},
        {?'dsa-with-sha256', sha256, dsa},
        {?'ecdsa-with-SHA1', sha, ecdsa},
        {?'ecdsa-with-SHA256', sha256, ecdsa},
        {?'ecdsa-with-SHA384', sha384, ecdsa},
        {?'ecdsa-with-SHA512', sha512, ecdsa},
        {?'id-Ed25519', none, eddsa},
        {?'id-Ed448', none, eddsa}
    ] ++
        [{Oid, none, table_name(ml_dsa, Oid)} || Oid <- [?'id-ml-dsa-44', ?'id-ml-dsa-65', ?'id-ml-dsa-87']] ++
        [
            {Oid, none, table_name(slh_dsa, Oid)}
         || Oid <- [
                ?'id-slh-dsa-sha2-128s',
                ?'id-slh-dsa-sha2-128f',
                ?'id-slh-dsa-sha2-192s',
                ?'id-slh-dsa-sha2-192f',
                ?'id-slh-dsa-sha2-256s',
                ?'id-slh-dsa-sha2-256f',
                ?'id-slh-dsa-shake-128s',
                ?'id-slh-dsa-shake-128f',
                ?'id-slh-dsa-shake-192s',
                ?'id-slh-dsa-shake-192f',
                ?'id-slh-dsa-shake-256s',
                ?'id-slh-dsa-shake-256f'
            ]
        ].

%% public_key:pkix_hash_type/1.
pkix_hash_type(?'id-sha1') -> sha;
pkix_hash_type(?'id-sha512') -> sha512;
pkix_hash_type(?'id-sha384') -> sha384;
pkix_hash_type(?'id-sha256') -> sha256;
pkix_hash_type(?'id-sha224') -> sha224;
pkix_hash_type(?'id-md5') -> md5.

%% public_key:short_name_hash/1: OpenSSL's hash of a name, the low 32 bits of the SHA-1 of its canonical encoding.
short_name_hash({rdnSequence, _Attributes} = Name) ->
    <<HashValue:32/little, _/binary>> = crypto:hash(sha, short_hash_encoding(Name)),
    string:to_lower(string:right(integer_to_list(HashValue, 16), 8, $0)).

%% The name's attributes normalised and encoded as OTP-PKIX's HashRDNSequence, without the outer tag and length.
short_hash_encoding({rdnSequence, Attributes0}) ->
    Attributes = lists:map(fun normalise_attribute/1, Attributes0),
    {ok, Der} = 'OTP-PKIX':encode('HashRDNSequence', Attributes),
    <<16#30, Rest/binary>> = Der,
    content(Rest).

normalise_attribute([#'AttributeTypeAndValue'{type = Type, value = {Encoding, String}}]) when
    Encoding =:= utf8String; Encoding =:= printableString; Encoding =:= teletexString; Encoding =:= ia5String
->
    [#'HashSingleAttribute'{type = Type, value = normalise_attribute_value(String)}];
normalise_attribute([#'AttributeTypeAndValue'{type = Type, value = String}]) when is_list(String) ->
    [#'HashSingleAttribute'{type = Type, value = normalise_attribute_value(String)}].

%% An attribute value as OpenSSL hashes it: ASCII white space trimmed and collapsed, ASCII letters lowercased.
normalise_attribute_value(String) ->
    Converted = unicode:characters_to_binary(String),
    TrimmedLeft = re:replace(Converted, "^[\s\f\n\r\t\v]+", "", [unicode, global]),
    TrimmedRight = re:replace(TrimmedLeft, "[\s\f\n\r\t\v]+$", "", [unicode, global]),
    Collapsed = re:replace(TrimmedRight, "[\s\f\n\r\t\v]+", "\s", [unicode, global]),
    unicode:characters_to_list(ascii_to_lower(Collapsed)).

ascii_to_lower(String) ->
    <<
        <<
            (if
                $A =< C, C =< $Z -> C + ($a - $A);
                true -> C
            end)
        >>
     || <<C>> <= iolist_to_binary(String)
    >>.

%%--------------------------------------------------------------------------------------------------------------------
%% SSH
%%--------------------------------------------------------------------------------------------------------------------

%% public_key:dh_gex_group/4: a random group of the size closest to N within [Min - 10, Max + 10].
dh_gex_group(Min, N, Max, undefined) ->
    dh_gex_group(Min, N, Max, clause_public_key_moduli:groups());
dh_gex_group(Min, N, Max, Groups) ->
    case select_by_keylen(Min - 10, N, Max + 10, Groups) of
        {ok, {Size, GPs}} -> {ok, {Size, lists:nth(rand:uniform(length(GPs)), GPs)}};
        Other -> Other
    end.

%% public_key:dh_gex_group_sizes/0.
dh_gex_group_sizes() ->
    [KeyLen || {KeyLen, _} <- clause_public_key_moduli:groups()].

%% The groups of the size closest to N within [Min, Max], from groups sorted by size.
select_by_keylen(Min, N, Max, [{K, _} | Groups]) when K < Min ->
    select_by_keylen(Min, N, Max, Groups);
select_by_keylen(Min, N, Max, [{K, Gs} | Groups]) when K =< Max ->
    {ok, select_by_keylen(Min, N, Max, Groups, {K, Gs})};
select_by_keylen(_, _, _, _) ->
    {error, no_group_found}.

select_by_keylen(_, _, Max, [{K, _} | _], Previous) when K > Max ->
    Previous;
select_by_keylen(Min, N, Max, [{K, Gs} | Groups], {KPrevious, _} = Previous) ->
    if
        N == K -> {K, Gs};
        N > K -> select_by_keylen(Min, N, Max, Groups, {K, Gs});
        N < K, (K - N) < (N - KPrevious) -> {K, Gs};
        N < K -> Previous
    end;
select_by_keylen(_, _, _, [], Previous) ->
    Previous.

%% public_key:ssh_curvename2oid/1.
ssh_curvename2oid(<<"nistp256">>) -> ?'secp256r1';
ssh_curvename2oid(<<"nistp384">>) -> ?'secp384r1';
ssh_curvename2oid(<<"nistp521">>) -> ?'secp521r1'.

%% public_key:oid2ssh_curvename/1.
oid2ssh_curvename(?'secp256r1') -> <<"nistp256">>;
oid2ssh_curvename(?'secp384r1') -> <<"nistp384">>;
oid2ssh_curvename(?'secp521r1') -> <<"nistp521">>.

%%--------------------------------------------------------------------------------------------------------------------
%% Tracing
%%--------------------------------------------------------------------------------------------------------------------

%% public_key:handle_trace/3: ssl_trace's texts of public_key calls.
handle_trace(crt, {call, {public_key, pkix_decode_cert, [Cert, _Type]}}, Stack) ->
    {io_lib:format("Cert = ~W", [Cert, 5]), Stack};
handle_trace(csp, {call, {public_key, pkix_ocsp_validate, [Cert, IssuerCert | _]}}, Stack) ->
    {io_lib:format("#2 OCSP validation started~nCert = ~W IssuerCert = ~W", [Cert, 7, IssuerCert, 7]), Stack};
handle_trace(csp, {call, {public_key, otp_cert, [Cert]}}, Stack) ->
    {io_lib:format("Cert = ~W", [Cert, 5]), Stack};
handle_trace(csp, {return_from, {public_key, pkix_ocsp_validate, 5}, Return}, Stack) ->
    {io_lib:format("#2 OCSP validation result = ~p", [Return]), Stack}.

%%--------------------------------------------------------------------------------------------------------------------
%% Tables
%%--------------------------------------------------------------------------------------------------------------------

%% A table's name of an object identifier; function_clause without a row, as OTP's table functions.
table_name(Table, Oid) ->
    case clause_public_key:oid_name(Table, Oid) of
        undefined -> erlang:error(function_clause, [Oid]);
        Name -> Name
    end.

%% A table's object identifier of a name; function_clause without a row.
table_oid(Table, Name) ->
    case clause_public_key:name_oid(Table, Name) of
        undefined -> erlang:error(function_clause, [Name]);
        Oid -> Oid
    end.
