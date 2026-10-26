%% clause_public_key_codec: the DER and PEM codec of Clause's public_key (plan 21 step LIB80B,
%% docs/libraries/public_key.md), which public_key's functions call: entries, PEM encryption (PKCS #5 and OpenSSL's
%% legacy encryption over crypto), private keys in PKCS #8, and certificates in the plain and otp forms over the ASN.1
%% modules clau generates from library/public_key/asn1. A module of its own because public_key.erl includes
%% public_key.types.hrl, whose records public_key.hrl defines too.
-module(clause_public_key_codec).
-export([
    der_decode/2,
    der_encode/2,
    pem_decode/1,
    pem_encode/1,
    pem_entry_decode/1,
    pem_entry_decode/2,
    pem_entry_encode/2,
    pem_entry_encode/3,
    pkix_decode_cert/2,
    pkix_encode/3,
    transform/2
]).

-include("include/public_key.hrl").

%% Records of the generated modules that public_key.hrl does not define.
-record('PrivateKeyAlgorithmIdentifier', {algorithm, parameters = asn1_NOVALUE}).
-record('SubjectPublicKeyInfo_algorithm', {algorithm, parameters = asn1_NOVALUE}).
-record('DSA-Params', {p, q, g}).
-record('OTPCertificate_signatureAlgorithm', {algorithm, parameters = asn1_NOVALUE}).
-record('OTPTBSCertificate_signature', {algorithm, parameters = asn1_NOVALUE}).
-record('OTPPolicyInformation', {policyIdentifier, policyQualifiers = asn1_NOVALUE}).
-record('OTPPolicyQualifierInfo', {policyQualifierId, qualifier}).
-record('OTPUserNotice', {noticeRef = asn1_NOVALUE, explicitText = asn1_NOVALUE}).

%% The DER encoding of NULL.
-define(DER_NULL, <<5, 0>>).
%% The type DSA private keys of PKCS #8 are encoded with (an INTEGER).
-define(DSA_PRIVATE_KEY_TYPE, 'DSAPublicKey').

%%--------------------------------------------------------------------------------------------------------------------
%% PEM
%%--------------------------------------------------------------------------------------------------------------------

%% The entries of PEM text, as pubkey_pem:decode/1; the text after an encrypted PKCS #8 entry is read once that entry
%% is decoded.
pem_decode(PemBin) when is_binary(PemBin) ->
    case clause_public_key:pem_decode(PemBin) of
        {Entries, done} ->
            Entries;
        {Entries, Encrypted, Rest} ->
            Entry = encrypted_private_key(Encrypted),
            Entries ++ [Entry | pem_decode(Rest)]
    end.

%% PEM text of entries, as pubkey_pem:encode/1.
pem_encode(PemEntries) when is_list(PemEntries) ->
    iolist_to_binary([clause_public_key:pem_entry(pem_entry(Entry)) || Entry <- PemEntries]).

%% An ENCRYPTED PRIVATE KEY entry: the encrypted data and the cipher of its algorithm.
encrypted_private_key(Der) ->
    #'EncryptedPrivateKeyInfo'{encryptionAlgorithm = Algorithm, encryptedData = Data} =
        der_decode('EncryptedPrivateKeyInfo', Der),
    {'PrivateKeyInfo', Data, pbe_decrypt_parameters(Algorithm)}.

%% An encrypted PKCS #8 entry as the ENCRYPTED PRIVATE KEY entry of its EncryptedPrivateKeyInfo.
pem_entry({'PrivateKeyInfo', Der, EncryptParams}) when EncryptParams =/= not_encrypted ->
    Algorithm = pbe_encrypt_parameters(EncryptParams),
    Info = #'EncryptedPrivateKeyInfo'{encryptionAlgorithm = Algorithm, encryptedData = Der},
    {'EncryptedPrivateKeyInfo', der_encode('EncryptedPrivateKeyInfo', Info), not_encrypted};
pem_entry(Entry) ->
    Entry.

%%--------------------------------------------------------------------------------------------------------------------
%% PEM entries
%%--------------------------------------------------------------------------------------------------------------------

%% public_key:pem_entry_decode/1,2.
pem_entry_decode({'SubjectPublicKeyInfo', Der, _}) ->
    {_, {'AlgorithmIdentifier', Algorithm, Params}, Key} = der_decode('SubjectPublicKeyInfo', Der),
    case public_key_type(Algorithm) of
        'RSAPublicKey' ->
            der_decode('RSAPublicKey', Key);
        'DSAPublicKey' ->
            {params, DssParams} = Params,
            {der_decode('DSAPublicKey', Key), DssParams};
        'ECPoint' ->
            {#'ECPoint'{point = Key}, ec_decode_params(Algorithm, Params)};
        'ML-DSAPublicKey' ->
            #'ML-DSAPublicKey'{algorithm = oid_name(ml_dsa, Algorithm), key = Key};
        'ML-KEMPublicKey' ->
            #'ML-KEMPublicKey'{algorithm = oid_name(ml_kem, Algorithm), key = Key};
        'SLH-DSAPublicKey' ->
            #'SLH-DSAPublicKey'{algorithm = oid_name(slh_dsa, Algorithm), key = Key}
    end;
pem_entry_decode({Asn1Type, Der, not_encrypted}) when is_atom(Asn1Type), is_binary(Der) ->
    der_decode(Asn1Type, Der).

pem_entry_decode(PemEntry, PasswordFun) when is_function(PasswordFun) ->
    pem_entry_decode(PemEntry, PasswordFun());
pem_entry_decode({Asn1Type, Der, not_encrypted}, _) when is_atom(Asn1Type), is_binary(Der) ->
    der_decode(Asn1Type, Der);
pem_entry_decode({Asn1Type, CryptDer, {Cipher, #'PBES2-params'{}}} = PemEntry, Password) when
    is_atom(Asn1Type) andalso is_binary(CryptDer) andalso is_list(Cipher)
->
    decrypt_entry(PemEntry, Password);
pem_entry_decode({Asn1Type, CryptDer, {Cipher, {#'PBEParameter'{}, _}}} = PemEntry, Password) when
    is_atom(Asn1Type) andalso is_binary(CryptDer) andalso is_list(Cipher) andalso is_list(Password)
->
    decrypt_entry(PemEntry, Password);
pem_entry_decode({Asn1Type, CryptDer, {Cipher, Salt}} = PemEntry, Password) when
    is_atom(Asn1Type) andalso is_binary(CryptDer) andalso is_list(Cipher) andalso is_binary(Salt) andalso
        ((erlang:byte_size(Salt) == 8) orelse (erlang:byte_size(Salt) == 16)) andalso is_list(Password)
->
    decrypt_entry(PemEntry, Password).

%% public_key:pem_entry_encode/2,3.
pem_entry_encode('SubjectPublicKeyInfo', #'RSAPublicKey'{} = Key) ->
    Algorithm = #'AlgorithmIdentifier'{algorithm = ?'rsaEncryption', parameters = ?DER_NULL},
    key_info_entry(Algorithm, der_encode('RSAPublicKey', Key));
pem_entry_encode('SubjectPublicKeyInfo', {Y, #'Dss-Parms'{p = P, q = Q, g = G}}) when is_integer(Y) ->
    Algorithm = #'SubjectPublicKeyInfo_algorithm'{
        algorithm = ?'id-dsa', parameters = #'DSA-Params'{p = P, q = Q, g = G}
    },
    key_info_entry(Algorithm, der_encode('DSAPublicKey', Y));
pem_entry_encode('SubjectPublicKeyInfo', {#'ECPoint'{point = Key}, {namedCurve, Id}}) when
    is_binary(Key), Id =:= ?'id-Ed448' orelse Id =:= ?'id-Ed25519'
->
    key_info_entry(#'AlgorithmIdentifier'{algorithm = Id}, Key);
pem_entry_encode('SubjectPublicKeyInfo', {#'ECPoint'{point = Key}, ECParams}) when is_binary(Key) ->
    key_info_entry(#'AlgorithmIdentifier'{algorithm = ?'id-ecPublicKey', parameters = ECParams}, Key);
pem_entry_encode('SubjectPublicKeyInfo', #'ML-DSAPublicKey'{algorithm = Algorithm, key = Key}) ->
    key_info_entry(#'AlgorithmIdentifier'{algorithm = name_oid(ml_dsa, Algorithm)}, Key);
pem_entry_encode('SubjectPublicKeyInfo', #'ML-KEMPublicKey'{algorithm = Algorithm, key = Key}) ->
    key_info_entry(#'AlgorithmIdentifier'{algorithm = name_oid(ml_kem, Algorithm)}, Key);
pem_entry_encode('SubjectPublicKeyInfo', #'SLH-DSAPublicKey'{algorithm = Algorithm, key = Key}) ->
    key_info_entry(#'AlgorithmIdentifier'{algorithm = name_oid(slh_dsa, Algorithm)}, Key);
pem_entry_encode(Asn1Type, Entity) when is_atom(Asn1Type) ->
    {Asn1Type, der_encode(Asn1Type, Entity), not_encrypted}.

pem_entry_encode(Asn1Type, Entity, {{Cipher, #'PBES2-params'{}} = CipherInfo, Password}) when
    is_atom(Asn1Type) andalso is_list(Password) andalso is_list(Cipher)
->
    encrypt_entry(Asn1Type, Entity, CipherInfo, Password);
pem_entry_encode(Asn1Type, Entity, {{Cipher, {#'PBEParameter'{}, _}} = CipherInfo, Password}) when
    is_atom(Asn1Type) andalso is_list(Password) andalso is_list(Cipher)
->
    encrypt_entry(Asn1Type, Entity, CipherInfo, Password);
pem_entry_encode(Asn1Type, Entity, {{Cipher, Salt} = CipherInfo, Password}) when
    is_atom(Asn1Type) andalso is_list(Password) andalso is_list(Cipher) andalso is_binary(Salt) andalso
        ((erlang:byte_size(Salt) == 8) orelse (erlang:byte_size(Salt) == 16))
->
    encrypt_entry(Asn1Type, Entity, CipherInfo, Password).

%% The SubjectPublicKeyInfo entry of an algorithm and key bits.
key_info_entry(Algorithm, Key) ->
    pem_entry_encode('SubjectPublicKeyInfo', #'SubjectPublicKeyInfo'{algorithm = Algorithm, subjectPublicKey = Key}).

%% An encrypted entry decrypted with a password and decoded.
decrypt_entry({Asn1Type, _, _} = PemEntry, Password) ->
    {_, Encrypted, {Cipher, Params}} = PemEntry,
    Der = pbe_decode(Encrypted, Password, Cipher, Params),
    der_decode(Asn1Type, Der).

%% An entry encoded and encrypted with a password.
encrypt_entry(Asn1Type, Entity, {Cipher, Params} = CipherInfo, Password) ->
    Der = der_encode(Asn1Type, Entity),
    {Asn1Type, pbe_encode(Der, Password, Cipher, Params), CipherInfo}.

%%--------------------------------------------------------------------------------------------------------------------
%% DER
%%--------------------------------------------------------------------------------------------------------------------

%% public_key:der_decode/2, its clauses in OTP's order.
der_decode(Asn1Type, Der) when
    ((Asn1Type == 'PrivateKeyInfo') orelse (Asn1Type == 'EncryptedPrivateKeyInfo') andalso is_binary(Der))
->
    try
        {ok, Decoded} = 'PKCS-FRAME':decode(Asn1Type, Der),
        private_key_decode(translate(decode, Decoded))
    catch
        error:{badmatch, {error, _}} = Error ->
            frame_error(Asn1Type, Der, Error)
    end;
der_decode(Asn1Type, Der) when
    ((Asn1Type == 'PBES2-params') orelse (Asn1Type == 'PBES2-params_encryptionScheme') orelse
        (Asn1Type == 'PBEParameter') orelse (Asn1Type == 'RC2-CBC-Parameter') andalso is_binary(Der))
->
    {ok, Decoded} = 'PKCS-FRAME':decode(Asn1Type, Der),
    Decoded;
der_decode('EcpkParameters', Der) ->
    {ok, Decoded} = 'PKIXAlgs-2009':decode('ECParameters', Der),
    translate(decode, Decoded);
der_decode('Dss-Sig-Value', Der) ->
    {ok, Decoded} = 'PKIXAlgs-2009':decode('DSA-Sig-Value', Der),
    translate(decode, Decoded);
der_decode('ExtensionRequest', Der) ->
    der_decode('ExtensionReq', Der);
der_decode('AttributeTypeAndValue', Der) ->
    {ok, Decoded} = 'PKIX1Explicit-2009':decode('SingleAttribute', Der),
    decode_transform(Decoded);
der_decode(Asn1Type, Der) when is_atom(Asn1Type), is_binary(Der) ->
    case clause_public_key:asn1_module(Asn1Type) of
        undefined ->
            extension_decode(Asn1Type, Der);
        Module ->
            {ok, Decoded} = decode(Module, Asn1Type, Der),
            translate(decode, Decoded)
    end.

%% A type no module names: an extension's value, decoded as the extension decodes it.
extension_decode(Asn1Type, Der) ->
    case clause_public_key:name_oid(extension, Asn1Type) of
        undefined ->
            error({badarg, {unknown_type, Asn1Type}});
        Oid ->
            [#'Extension'{extnValue = Value}] = decode_extensions([#'Extension'{extnID = Oid, extnValue = Der}]),
            Value
    end.

%% A PrivateKeyInfo the PKCS #8 v1 form does not decode is tried as OneAsymmetricKey.
frame_error('PrivateKeyInfo', Der, _) ->
    {ok, Decoded} = 'PKCS-FRAME':decode('OneAsymmetricKey', Der),
    private_key_decode(translate(decode, Decoded));
frame_error(_, _, Error) ->
    erlang:error(Error).

%% The key a PKCS #8 structure wraps, by its algorithm; a structure of another algorithm stays as it is.
private_key_decode(#'PrivateKeyInfo'{version = v1, privateKeyAlgorithm = Algorithm, privateKey = Key} = Info) ->
    wrapped_key(Algorithm, Key, v1, Info);
private_key_decode(
    #'OneAsymmetricKey'{
        privateKeyAlgorithm = #'PrivateKeyAlgorithmIdentifier'{algorithm = Curve},
        privateKey = CurveKey,
        attributes = Attributes,
        publicKey = PublicKey
    }
) when Curve == ?'id-Ed25519' orelse Curve == ?'id-Ed448' ->
    #'ECPrivateKey'{
        version = 2,
        parameters = {namedCurve, Curve},
        privateKey = der_decode('CurvePrivateKey', CurveKey),
        attributes = Attributes,
        publicKey = PublicKey
    };
private_key_decode(#'OneAsymmetricKey'{version = v1, privateKeyAlgorithm = Algorithm, privateKey = Key} = Info) ->
    wrapped_key(Algorithm, Key, one_asymmetric, Info);
private_key_decode(Info) ->
    Info.

wrapped_key(
    #'PrivateKeyAlgorithmIdentifier'{algorithm = ?'id-ecPublicKey', parameters = {asn1_OPENTYPE, Params}}, Key, v1, _
) ->
    EcKey = der_decode('ECPrivateKey', Key),
    EcKey#'ECPrivateKey'{parameters = der_decode('EcpkParameters', Params)};
wrapped_key(#'PrivateKeyAlgorithmIdentifier'{algorithm = Curve}, Key, v1, _) when
    Curve == ?'id-Ed25519' orelse Curve == ?'id-Ed448'
->
    #'ECPrivateKey'{version = 1, parameters = {namedCurve, Curve}, privateKey = der_decode('CurvePrivateKey', Key)};
wrapped_key(#'PrivateKeyAlgorithmIdentifier'{algorithm = ?'rsaEncryption'}, Key, _, _) ->
    der_decode('RSAPrivateKey', Key);
wrapped_key(
    #'PrivateKeyAlgorithmIdentifier'{algorithm = ?'id-RSASSA-PSS', parameters = {asn1_OPENTYPE, Params}}, Key, _, _
) ->
    {der_decode('RSAPrivateKey', Key), der_decode('RSASSA-PSS-params', Params)};
wrapped_key(#'PrivateKeyAlgorithmIdentifier'{algorithm = ?'id-RSASSA-PSS', parameters = asn1_NOVALUE}, Key, _, _) ->
    #'RSASSA-AlgorithmIdentifier'{parameters = Params} = ?'rSASSA-PSS-Default-Identifier',
    {der_decode('RSAPrivateKey', Key), Params};
wrapped_key(#'PrivateKeyAlgorithmIdentifier'{algorithm = ?'id-dsa', parameters = {asn1_OPENTYPE, Params}}, Key, _, _) ->
    {ok, #'DSA-Params'{p = P, q = Q, g = G}} = 'PKIXAlgs-2009':decode('DSA-Params', Params),
    #'DSAPrivateKey'{version = 1, p = P, q = Q, g = G, x = der_decode(?DSA_PRIVATE_KEY_TYPE, Key)};
wrapped_key(#'PrivateKeyAlgorithmIdentifier'{algorithm = Oid}, Key, v1, Info) ->
    post_quantum_key(Oid, Key, Info);
wrapped_key(_, _, _, Info) ->
    Info.

%% An ML-DSA, ML-KEM or SLH-DSA private key.
post_quantum_key(Oid, Key, Info) ->
    Tables = [ml_dsa, ml_kem, slh_dsa],
    case [{Table, Name} || Table <- Tables, Name <- [clause_public_key:oid_name(Table, Oid)], Name =/= undefined] of
        [{ml_dsa, Name}] -> ml_dsa_private_key(Name, Key);
        [{ml_kem, Name}] -> ml_kem_private_key(Name, Key);
        [{slh_dsa, Name}] -> #'SLH-DSAPrivateKey'{algorithm = Name, key = Key};
        [] -> Info
    end.

%% An ML-DSA private key from its seed, expanded key or both.
ml_dsa_private_key(Name, Der) ->
    Key = #'ML-DSAPrivateKey'{algorithm = Name},
    case der_decode(ml_dsa_type(Name), Der) of
        {seed, Seed} -> Key#'ML-DSAPrivateKey'{seed = Seed};
        {expandedkey, Expanded} -> Key#'ML-DSAPrivateKey'{expandedkey = Expanded};
        {both, {_, Seed, Expanded}} -> Key#'ML-DSAPrivateKey'{seed = Seed, expandedkey = Expanded}
    end.

%% An ML-KEM private key from its seed, expanded key or both.
ml_kem_private_key(Name, Der) ->
    Key = #'ML-KEMPrivateKey'{algorithm = Name},
    case der_decode(ml_kem_type(Name), Der) of
        {seed, Seed} -> Key#'ML-KEMPrivateKey'{seed = Seed};
        {expandedkey, Expanded} -> Key#'ML-KEMPrivateKey'{expandedkey = Expanded};
        {both, {_, Seed, Expanded}} -> Key#'ML-KEMPrivateKey'{seed = Seed, expandedkey = Expanded}
    end.

%% The ASN.1 type of an ML-DSA parameter set's private key.
ml_dsa_type(mldsa44) -> 'ML-DSA-44-PrivateKey';
ml_dsa_type(mldsa65) -> 'ML-DSA-65-PrivateKey';
ml_dsa_type(mldsa87) -> 'ML-DSA-87-PrivateKey'.

%% The ASN.1 type of an ML-KEM parameter set's private key.
ml_kem_type(mlkem512) -> 'ML-KEM-512-PrivateKey';
ml_kem_type(mlkem768) -> 'ML-KEM-768-PrivateKey';
ml_kem_type(mlkem1024) -> 'ML-KEM-1024-PrivateKey'.

%% public_key:der_encode/2, its clauses in OTP's order.
der_encode('PrivateKeyInfo', #'DSAPrivateKey'{p = P, q = Q, g = G, x = X}) ->
    Params = der_encode('DSA-Params', #'DSA-Params'{p = P, q = Q, g = G}),
    one_asymmetric_key(0, ?'id-dsa', {asn1_OPENTYPE, Params}, der_encode(?DSA_PRIVATE_KEY_TYPE, X));
der_encode('PrivateKeyInfo', #'RSAPrivateKey'{} = Key) ->
    one_asymmetric_key(0, ?'rsaEncryption', {asn1_OPENTYPE, ?DER_NULL}, der_encode('RSAPrivateKey', Key));
der_encode('PrivateKeyInfo', {#'RSAPrivateKey'{} = Key, Parameters}) ->
    #'RSASSA-AlgorithmIdentifier'{parameters = Default} = ?'rSASSA-PSS-Default-Identifier',
    Params =
        case Parameters of
            Default -> asn1_NOVALUE;
            _ -> {asn1_OPENTYPE, der_encode('RSASSA-PSS-params', Parameters)}
        end,
    one_asymmetric_key(0, ?'id-RSASSA-PSS', Params, der_encode('RSAPrivateKey', Key));
der_encode('PrivateKeyInfo', #'ECPrivateKey'{parameters = {namedCurve, Curve}, privateKey = Key}) when
    Curve == ?'id-Ed25519' orelse Curve == ?'id-Ed448'
->
    one_asymmetric_key(0, Curve, asn1_NOVALUE, der_encode('CurvePrivateKey', Key));
der_encode('PrivateKeyInfo', #'ECPrivateKey'{parameters = Parameters} = Key) ->
    Params = der_encode('EcpkParameters', Parameters),
    Inner = der_encode('ECPrivateKey', Key#'ECPrivateKey'{parameters = asn1_NOVALUE}),
    one_asymmetric_key(0, ?'id-ecPublicKey', {asn1_OPENTYPE, Params}, Inner);
der_encode('PrivateKeyInfo', #'ML-DSAPrivateKey'{algorithm = Name} = Key) ->
    one_asymmetric_key(v1, name_oid(ml_dsa, Name), asn1_NOVALUE, seed_key(ml_dsa_type(Name), Key));
der_encode('PrivateKeyInfo', #'ML-KEMPrivateKey'{algorithm = Name} = Key) ->
    one_asymmetric_key(v1, name_oid(ml_kem, Name), asn1_NOVALUE, seed_key(ml_kem_type(Name), Key));
der_encode('PrivateKeyInfo', #'SLH-DSAPrivateKey'{algorithm = Name, key = Key}) ->
    one_asymmetric_key(v1, name_oid(slh_dsa, Name), asn1_NOVALUE, Key);
der_encode('OneAsymmetricKey', #'ECPrivateKey'{
    parameters = {namedCurve, Curve}, privateKey = Key, attributes = Attributes, publicKey = PublicKey
}) ->
    Inner =
        case Curve == ?'id-Ed25519' orelse Curve == ?'id-Ed448' of
            true -> der_encode('CurvePrivateKey', Key);
            false -> Key
        end,
    der_encode('OneAsymmetricKey', #'OneAsymmetricKey'{
        version = 1,
        privateKeyAlgorithm = #'PrivateKeyAlgorithmIdentifier'{algorithm = Curve},
        privateKey = Inner,
        attributes = Attributes,
        publicKey = PublicKey
    });
der_encode(Asn1Type, Entity) when
    (Asn1Type == 'PrivateKeyInfo') orelse (Asn1Type == 'OneAsymmetricKey') orelse
        (Asn1Type == 'EncryptedPrivateKeyInfo')
->
    {ok, Encoded} = 'PKCS-FRAME':encode(Asn1Type, translate(encode, Entity)),
    Encoded;
der_encode(Asn1Type, Entity) when
    (Asn1Type == 'PBES2-params') orelse (Asn1Type == 'PBES2-params_encryptionScheme') orelse
        (Asn1Type == 'PBEParameter') orelse (Asn1Type == 'RC2-CBC-Parameter')
->
    {ok, Encoded} = 'PKCS-FRAME':encode(Asn1Type, Entity),
    Encoded;
der_encode('EcpkParameters', {namedCurve, _} = Entity) ->
    {ok, Encoded} = 'PKIXAlgs-2009':encode('ECParameters', Entity),
    Encoded;
der_encode('Dss-Sig-Value', Entity) ->
    {ok, Encoded} = 'PKIXAlgs-2009':encode('DSA-Sig-Value', Entity),
    Encoded;
der_encode('ExtensionRequest', Value) ->
    der_encode('ExtensionReq', Value);
der_encode('AttributeTypeAndValue', Value) ->
    {ok, Encoded} = 'PKIX1Explicit-2009':encode('SingleAttribute', encode_transform(Value)),
    Encoded;
der_encode(Asn1Type, Entity) when is_atom(Asn1Type) ->
    case clause_public_key:asn1_module(Asn1Type) of
        undefined ->
            extension_encode(Asn1Type, Entity);
        Module ->
            {ok, Encoded} = encode(Module, Asn1Type, translate(encode, Entity)),
            Encoded
    end.

%% A type no module names: an extension's value, encoded as the extension encodes it.
extension_encode(Asn1Type, Entity) ->
    case clause_public_key:name_oid(extension, Asn1Type) of
        undefined ->
            error({badarg, {unknown_type, Asn1Type}});
        Oid ->
            [#'Extension'{extnValue = Value}] = encode_extensions([#'Extension'{extnID = Oid, extnValue = Entity}]),
            Value
    end.

%% A PKCS #8 OneAsymmetricKey of an algorithm and its encoded private key.
one_asymmetric_key(Version, Oid, Params, Key) ->
    Algorithm = #'PrivateKeyAlgorithmIdentifier'{algorithm = Oid, parameters = Params},
    der_encode('OneAsymmetricKey', #'OneAsymmetricKey'{
        version = Version, privateKeyAlgorithm = Algorithm, privateKey = Key
    }).

%% An ML-DSA or ML-KEM private key as its seed, its expanded key, or both.
seed_key(Type, Key) ->
    case {element(3, Key), element(4, Key)} of
        {Seed, <<>>} -> der_encode(Type, {seed, Seed});
        {<<>>, Expanded} -> der_encode(Type, {expandedkey, Expanded});
        {Seed, Expanded} -> der_encode(Type, {both, {Type, Seed, Expanded}})
    end.

%% The curve parameters of an EC or EdDSA public key.
ec_decode_params(Algorithm, _) when Algorithm == ?'id-Ed25519'; Algorithm == ?'id-Ed448' ->
    {namedCurve, Algorithm};
ec_decode_params(_, {namedCurve, _} = Params) ->
    Params.

%%--------------------------------------------------------------------------------------------------------------------
%% Certificates
%%--------------------------------------------------------------------------------------------------------------------

%% public_key:pkix_decode_cert/2.
pkix_decode_cert(DerCert, plain) when is_binary(DerCert) ->
    der_decode('Certificate', DerCert);
pkix_decode_cert(DerCert, Type) when is_binary(DerCert), Type =:= otp orelse Type =:= relaxed ->
    {ok, Cert} =
        case Type of
            otp -> 'OTP-PKIX':decode('OTPCertificate', DerCert);
            relaxed -> 'OTP-PKIX-Relaxed':decode('OTPCertificate', DerCert)
        end,
    #'OTPCertificate'{} = decode_transform(Cert).

%% public_key:pkix_encode/3.
pkix_encode(Asn1Type, Term, plain) when is_atom(Asn1Type) ->
    der_encode(Asn1Type, Term);
pkix_encode('OTPSubjectPublicKeyInfo', Term, otp) ->
    der_encode('SubjectPublicKeyInfo', encode_public_key_info(Term));
pkix_encode(Type, Term, otp) when Type =:= 'OTPCertificate'; Type =:= 'OTPTBSCertificate' ->
    {ok, Encoded} = 'OTP-PKIX':encode(Type, encode_transform(Term)),
    Encoded;
pkix_encode(Type, Term, otp) ->
    pkix_encode(Type, Term, plain).

%% pubkey_cert_records:transform/2: the otp form of OTP-PKIX's records, or the reverse.
transform(Term, decode) -> decode_transform(Term);
transform(Term, encode) -> encode_transform(Term).

%% The otp form from OTP-PKIX's records: names with their values unwrapped, the public key and extensions decoded.
decode_transform(#'OTPCertificate'{tbsCertificate = Tbs, signatureAlgorithm = Algorithm} = Cert) ->
    Cert#'OTPCertificate'{tbsCertificate = decode_transform(Tbs), signatureAlgorithm = decode_transform(Algorithm)};
decode_transform(#'OTPCertificate_signatureAlgorithm'{algorithm = Algorithm, parameters = Params}) ->
    #'SignatureAlgorithm'{algorithm = Algorithm, parameters = decode_transform(Params)};
decode_transform(
    #'OTPTBSCertificate'{
        signature = Signature, issuer = Issuer, subject = Subject, subjectPublicKeyInfo = Info, extensions = Extensions
    } = Tbs
) ->
    Tbs#'OTPTBSCertificate'{
        issuer = decode_transform(Issuer),
        subject = decode_transform(Subject),
        signature = setelement(1, decode_transform(Signature), 'SignatureAlgorithm'),
        subjectPublicKeyInfo = decode_public_key_info(Info),
        extensions = decode_extensions(Extensions, crl_later)
    };
decode_transform(#'OTPTBSCertificate_signature'{algorithm = Algorithm, parameters = Params}) ->
    #'SignatureAlgorithm'{algorithm = Algorithm, parameters = decode_transform(Params)};
decode_transform({present, #'DSA-Params'{p = P, q = Q, g = G}}) ->
    {params, #'Dss-Parms'{p = P, q = Q, g = G}};
decode_transform({absent, 'NULL'}) ->
    'NULL';
decode_transform(#'SingleAttribute'{type = Id, value = Value}) ->
    case {Id, Value} of
        {?'id-at-countryName', {_, String}} -> #'AttributeTypeAndValue'{type = Id, value = String};
        {?'id-domainComponent', {_, String}} -> #'AttributeTypeAndValue'{type = Id, value = String};
        {?'id-emailAddress', {_, String}} -> #'AttributeTypeAndValue'{type = Id, value = String};
        {_, _} -> #'AttributeTypeAndValue'{type = Id, value = Value}
    end;
decode_transform(#'AuthorityKeyIdentifier'{authorityCertIssuer = Issuer} = Id) ->
    Id#'AuthorityKeyIdentifier'{authorityCertIssuer = decode_transform(Issuer)};
decode_transform({directoryName, Value}) ->
    {directoryName, decode_transform(Value)};
decode_transform({rdnSequence, Sequence}) when is_list(Sequence) ->
    {rdnSequence, [[decode_transform(Element) || Element <- Rdn] || Rdn <- Sequence]};
decode_transform(#'NameConstraints'{permittedSubtrees = Permitted, excludedSubtrees = Excluded}) ->
    #'NameConstraints'{permittedSubtrees = subtrees(decode, Permitted), excludedSubtrees = subtrees(decode, Excluded)};
decode_transform([#'DistributionPoint'{} | _] = Points) ->
    [decode_distribution_point(Point) || Point <- Points];
decode_transform(#'AccessDescription'{accessLocation = Name} = Description) ->
    Description#'AccessDescription'{accessLocation = decode_transform(Name)};
decode_transform(List) when is_list(List) ->
    [decode_transform(Element) || Element <- List];
decode_transform(Other) ->
    Other.

%% A CRL distribution point with its names in the otp form.
decode_distribution_point(#'DistributionPoint'{distributionPoint = {Type, Name}, cRLIssuer = Issuers} = Point) ->
    Point#'DistributionPoint'{
        distributionPoint = {Type, decode_transform(Name)}, cRLIssuer = decode_transform(Issuers)
    };
decode_distribution_point(Point) ->
    Point.

%% OTP-PKIX's records from the otp form, for encoding (pubkey_cert_records:transform/2).
encode_transform(#'OTPCertificate'{tbsCertificate = Tbs, signatureAlgorithm = Algorithm} = Cert) ->
    Cert#'OTPCertificate'{tbsCertificate = encode_transform(Tbs), signatureAlgorithm = encode_transform(Algorithm)};
encode_transform(
    #'OTPTBSCertificate'{
        signature = Signature, issuer = Issuer, subject = Subject, subjectPublicKeyInfo = Info, extensions = Extensions
    } = Tbs
) ->
    Tbs#'OTPTBSCertificate'{
        signature = encode_transform(Signature),
        issuer = encode_transform(Issuer),
        subject = encode_transform(Subject),
        subjectPublicKeyInfo = encode_public_key_info(Info),
        extensions = encode_extensions(Extensions)
    };
encode_transform(#'SignatureAlgorithm'{algorithm = Algorithm, parameters = Params}) ->
    #'OTPTBSCertificate_signature'{algorithm = Algorithm, parameters = encode_transform(Params)};
encode_transform({params, #'Dss-Parms'{p = P, q = Q, g = G}}) ->
    {present, #'DSA-Params'{p = P, q = Q, g = G}};
encode_transform(#'AttributeTypeAndValue'{type = Id, value = Value}) ->
    case Id of
        ?'id-at-countryName' -> #'SingleAttribute'{type = Id, value = {correct, Value}};
        ?'id-domainComponent' -> #'SingleAttribute'{type = Id, value = {correct, Value}};
        ?'id-emailAddress' -> #'SingleAttribute'{type = Id, value = {correct, Value}};
        _ -> #'SingleAttribute'{type = Id, value = Value}
    end;
encode_transform(#'AuthorityKeyIdentifier'{authorityCertIssuer = Issuer} = Id) ->
    Id#'AuthorityKeyIdentifier'{authorityCertIssuer = encode_transform(Issuer)};
encode_transform([{directoryName, _}] = List) ->
    [{directoryName, encode_transform(Value)} || {directoryName, Value} <- List];
encode_transform({directoryName, Value}) ->
    {directoryName, encode_transform(Value)};
encode_transform({rdnSequence, Sequence}) when is_list(Sequence) ->
    {rdnSequence, [[encode_transform(Element) || Element <- Rdn] || Rdn <- Sequence]};
encode_transform(#'NameConstraints'{permittedSubtrees = Permitted, excludedSubtrees = Excluded}) ->
    #'NameConstraints'{permittedSubtrees = subtrees(encode, Permitted), excludedSubtrees = subtrees(encode, Excluded)};
encode_transform(Other) ->
    Other.

%% Name constraint subtrees with their names transformed.
subtrees(_, asn1_NOVALUE) ->
    asn1_NOVALUE;
subtrees(decode, Trees) ->
    [Tree#'GeneralSubtree'{base = decode_transform(Name)} || #'GeneralSubtree'{base = Name} = Tree <- Trees];
subtrees(encode, Trees) ->
    [Tree#'GeneralSubtree'{base = encode_transform(Name)} || #'GeneralSubtree'{base = Name} = Tree <- Trees].

%% The otp form's public key: decoded by its algorithm.
decode_public_key_info(#'SubjectPublicKeyInfo'{algorithm = KeyAlgorithm, subjectPublicKey = Key0}) ->
    #'SubjectPublicKeyInfo_algorithm'{algorithm = Algorithm, parameters = Params0} = KeyAlgorithm,
    Key =
        case public_key_type(Algorithm) of
            'ECPoint' -> #'ECPoint'{point = Key0};
            'ML-DSAPublicKey' -> #'ML-DSAPublicKey'{algorithm = oid_name(ml_dsa, Algorithm), key = Key0};
            'SLH-DSAPublicKey' -> #'SLH-DSAPublicKey'{algorithm = oid_name(slh_dsa, Algorithm), key = Key0};
            Type -> der_decode(Type, Key0)
        end,
    Params =
        case Params0 of
            #'DSA-Params'{p = P, q = Q, g = G} -> {params, #'Dss-Parms'{p = P, q = Q, g = G}};
            _ -> Params0
        end,
    #'OTPSubjectPublicKeyInfo'{
        subjectPublicKey = Key, algorithm = #'PublicKeyAlgorithm'{algorithm = Algorithm, parameters = Params}
    }.

encode_public_key_info(#'OTPSubjectPublicKeyInfo'{
    algorithm = #'PublicKeyAlgorithm'{algorithm = Algorithm, parameters = Params0}, subjectPublicKey = Key0
}) ->
    Key =
        case public_key_type(Algorithm) of
            'ECPoint' ->
                Key0#'ECPoint'.point;
            'ML-DSAPublicKey' ->
                #'ML-DSAPublicKey'{key = Bits} = Key0,
                Bits;
            'SLH-DSAPublicKey' ->
                #'SLH-DSAPublicKey'{key = Bits} = Key0,
                Bits;
            Type ->
                der_encode(Type, Key0)
        end,
    Params =
        case Params0 of
            {params, #'Dss-Parms'{p = P, q = Q, g = G}} -> #'DSA-Params'{p = P, q = Q, g = G};
            _ -> Params0
        end,
    #'SubjectPublicKeyInfo'{
        subjectPublicKey = Key,
        algorithm = #'SubjectPublicKeyInfo_algorithm'{algorithm = Algorithm, parameters = Params}
    }.

%% The extensions of a CRL or a value, every known one decoded.
decode_extensions(Extensions) ->
    decode_extensions(Extensions, crl_now).

%% Each extension public_key knows decoded; a certificate's CRL distribution points stay undecoded (certificates
%% that do not use CRLs may carry ones that do not decode), and long user notices decode with OTP-PKIX.
decode_extensions(asn1_NOVALUE, _) ->
    asn1_NOVALUE;
decode_extensions(Extensions, WhenCrl) ->
    [decode_extension(Extension, WhenCrl) || Extension <- Extensions].

%% One extension, decoded when public_key knows its type.
decode_extension(#'Extension'{extnID = Id, extnValue = Value0} = Extension, WhenCrl) ->
    case clause_public_key:oid_name(extension, Id) of
        undefined ->
            Extension;
        'CertificatePolicies' ->
            Value = iolist_to_binary(Value0),
            try
                Extension#'Extension'{extnValue = decode_transform(extension_value('CertificatePolicies', Value))}
            catch
                exit:{_, {error, {asn1, bad_range}}} -> otp_policies(Extension, Value)
            end;
        'CRLDistributionPoints' when WhenCrl == crl_later ->
            Extension;
        Type ->
            Extension#'Extension'{extnValue = decode_transform(extension_value(Type, iolist_to_binary(Value0)))}
    end.

%% An extension's value; a decoding error raised as the extension's decoder raises it, exit({error, {asn1, Reason}}).
extension_value(Name, Der) ->
    {Module, Type} = extension_type(Name),
    case decode(Module, Type, Der) of
        {ok, Value} -> Value;
        {error, {asn1, {Reason, _}}} -> asn1_exit(Reason)
    end.

%% A codec error raised again as the type's own decoder or encoder raises it: a table constraint's exit as it is
%% (so that long user notices fall back to OTP-PKIX), others as exit({error, {asn1, Reason}}).
asn1_exit({'Type not compatible with table constraint', _} = Reason) -> exit(Reason);
asn1_exit(Reason) -> exit({error, {asn1, Reason}}).

%% The module and type of an extension's value (EXTENSION's &ExtnType); HoldInstructionCode's OBJECT IDENTIFIER and
%% InvalidityDate's GeneralizedTime have no type of their own and take another type of the same definition.
extension_type('SubjectKeyIdentifier') -> {'PKIX1Implicit-2009', 'KeyIdentifier'};
extension_type('SubjectAltName') -> {'PKIX1Implicit-2009', 'GeneralNames'};
extension_type('IssuerAltName') -> {'PKIX1Implicit-2009', 'GeneralNames'};
extension_type('CertificateIssuer') -> {'PKIX1Implicit-2009', 'GeneralNames'};
extension_type('InhibitAnyPolicy') -> {'PKIX1Implicit-2009', 'SkipCerts'};
extension_type('FreshestCRL') -> {'PKIX1Implicit-2009', 'CRLDistributionPoints'};
extension_type('BaseCRLNumber') -> {'PKIX1Implicit-2009', 'CRLNumber'};
extension_type('HoldInstructionCode') -> {'PKIX1Implicit-2009', 'KeyPurposeId'};
extension_type('InvalidityDate') -> {'OCSP-2024-08', 'ArchiveCutoff'};
extension_type(Name) -> {'PKIX1Implicit-2009', Name}.

%% Certificate policies whose explicit texts are longer than allowed, cut to 350 characters.
otp_policies(Extension, Der) ->
    {ok, Policies} = 'OTP-PKIX':decode('OTPCertificatePolicies', Der),
    Extension#'Extension'{extnValue = [otp_policy(Policy) || Policy <- Policies]}.

%% A policy of OTP-PKIX's relaxed form as PKIX1Implicit-2009's.
otp_policy(#'OTPPolicyInformation'{policyIdentifier = Id, policyQualifiers = asn1_NOVALUE}) ->
    #'PolicyInformation'{policyIdentifier = Id, policyQualifiers = asn1_NOVALUE};
otp_policy(#'OTPPolicyInformation'{policyIdentifier = Id, policyQualifiers = Qualifiers}) ->
    #'PolicyInformation'{policyIdentifier = Id, policyQualifiers = [otp_policy(Q) || Q <- Qualifiers]};
otp_policy(#'OTPPolicyQualifierInfo'{policyQualifierId = Id, qualifier = Qualifier0}) ->
    Qualifier =
        case Qualifier0 of
            #'OTPUserNotice'{noticeRef = Ref, explicitText = {Type, Text}} ->
                #'UserNotice'{noticeRef = Ref, explicitText = {Type, string:slice(Text, 0, 350)}};
            Other ->
                Other
        end,
    #'PolicyQualifierInfo'{policyQualifierId = Id, qualifier = Qualifier}.

%% Extensions with every known decoded value encoded again.
encode_extensions(asn1_NOVALUE) ->
    asn1_NOVALUE;
encode_extensions(Extensions) ->
    [encode_extension(Extension) || Extension <- Extensions].

%% One extension, its value encoded unless it is already a binary CRL distribution point.
encode_extension(#'Extension'{extnID = Id, extnValue = Value} = Extension) ->
    case clause_public_key:oid_name(extension, Id) of
        undefined ->
            Extension;
        'CRLDistributionPoints' when is_binary(Value) ->
            Extension;
        Name ->
            {Module, Type} = extension_type(Name),
            case encode(Module, Type, translate(encode, Value)) of
                {ok, Der} -> Extension#'Extension'{extnValue = Der};
                {error, {asn1, {Reason, _}}} -> asn1_exit(Reason)
            end
    end.

%%--------------------------------------------------------------------------------------------------------------------
%% Password-based encryption (PKCS #5 and OpenSSL's legacy PEM encryption)
%%--------------------------------------------------------------------------------------------------------------------

%% Data decrypted with a password, a cipher and its key derivation parameters.
pbe_decode(Data, Password, Cipher, Params) ->
    {Algorithm, _, _} = clause_public_key:pbe_cipher(Cipher),
    {Key, IV} = pbe_key(Password, Cipher, Params),
    crypto:crypto_one_time(Algorithm, Key, IV, Data, false).

%% Data padded and encrypted with a password, a cipher and its key derivation parameters.
pbe_encode(Data, Password, Cipher, Params) ->
    {Algorithm, BlockSize, _} = clause_public_key:pbe_cipher(Cipher),
    {Key, IV} = pbe_key(Password, Cipher, Params),
    crypto:crypto_one_time(Algorithm, Key, IV, pbe_pad(Data, BlockSize), true).

%% PKCS #5 padding to whole blocks.
pbe_pad(Data, BlockSize) ->
    N = BlockSize - (byte_size(Data) rem BlockSize),
    <<Data/binary, (binary:copy(<<N>>, N))/binary>>.

%% The key and IV: PBKDF2 for PBES2, PBKDF1 for PBES1, OpenSSL's MD5 derivation for legacy PEM encryption.
pbe_key(Password, _, #'PBES2-params'{} = Params) ->
    {Salt, Count, Length, Hash, IV} = pbes2_key_params(Params),
    {crypto:pbkdf2_hmac(Hash, iolist_to_binary(Password), Salt, Count, Length), IV};
pbe_key(Password, _, {#'PBEParameter'{salt = Salt, iterationCount = Count}, Hash}) ->
    <<Key:8/binary, IV:8/binary, _/binary>> = pbkdf1(Password, Salt, Count, Hash),
    {Key, IV};
pbe_key(Password, Cipher, Params) ->
    <<Salt:8/binary, _/binary>> = Params,
    {_, _, Length} = clause_public_key:pbe_cipher(Cipher),
    <<Key:Length/binary, _/binary>> = legacy_pem_key(<<>>, Password, Salt, round(Length div 16 + 0.5), <<>>),
    {Key, Params}.

pbkdf1(_, _, 0, Acc) ->
    Acc;
pbkdf1(Password, Salt, Count, Hash) ->
    First = crypto:hash(Hash, [Password, Salt]),
    %% The first digest checked the digest type; the rounds after it are a builtin loop.
    clause_public_key:pbkdf1_rounds(First, Count - 1, Hash).

%% OpenSSL's EVP_BytesToKey with MD5 and one iteration.
legacy_pem_key(_, _, _, 0, Acc) ->
    Acc;
legacy_pem_key(Previous, Password, Salt, Count, Acc) ->
    Next = crypto:hash(md5, [Previous, Password, Salt]),
    legacy_pem_key(Next, Password, Salt, Count - 1, <<Acc/binary, Next/binary>>).

pbes2_key_params(#'PBES2-params'{keyDerivationFunc = Derivation, encryptionScheme = Scheme}) ->
    #'PBES2-params_keyDerivationFunc'{
        algorithm = ?'id-PBKDF2',
        parameters = #'PBKDF2-params'{salt = {specified, Salt}, iterationCount = Count, keyLength = Length, prf = Prf}
    } = Derivation,
    #'PBES2-params_encryptionScheme'{algorithm = Algorithm} = Scheme,
    {Salt, Count, key_length(Algorithm, Length), prf_hash(Prf), pbes2_iv(Scheme)}.

%% The digest of a PBKDF2 pseudo-random function.
prf_hash(#'PBKDF2-params_prf'{algorithm = {_, _, _, 'id-hmacWithSHA1'}}) -> sha;
prf_hash(#'PBKDF2-params_prf'{algorithm = Oid}) -> clause_public_key:prf_digest(Oid).

pbes2_iv(#'PBES2-params_encryptionScheme'{algorithm = ?'rc2CBC', parameters = {asn1_OPENTYPE, Params}}) ->
    #'RC2-CBC-Parameter'{iv = IV} = der_decode('RC2-CBC-Parameter', Params),
    iolist_to_binary(IV);
pbes2_iv(#'PBES2-params_encryptionScheme'{parameters = {asn1_OPENTYPE, Params}}) ->
    <<4, Length:8/unsigned-big-integer, IV:Length/binary>> = Params,
    IV.

%% The key length of a PBES2 scheme, unless the parameters give one.
key_length(_, Length) when is_integer(Length) ->
    Length;
key_length(Algorithm, _) ->
    {_, _, Length} = clause_public_key:pbe_cipher(clause_public_key:pbes2_cipher(Algorithm)),
    Length.

%% The cipher and its parameters of an EncryptedPrivateKeyInfo's algorithm.
pbe_decrypt_parameters(#'EncryptedPrivateKeyInfo_encryptionAlgorithm'{
    algorithm = ?'id-PBES2', parameters = {asn1_OPENTYPE, Der}
}) ->
    Params = der_decode('PBES2-params', Der),
    #'PBES2-params_encryptionScheme'{algorithm = Oid} = Params#'PBES2-params'.encryptionScheme,
    {clause_public_key:pbes2_cipher(Oid), Params};
pbe_decrypt_parameters(#'EncryptedPrivateKeyInfo_encryptionAlgorithm'{
    algorithm = Oid, parameters = {asn1_OPENTYPE, Der}
}) ->
    {Cipher, Hash} = clause_public_key:pbes1(Oid),
    {Cipher, {der_decode('PBEParameter', Der), Hash}}.

%% The encryption algorithm of an EncryptedPrivateKeyInfo for a cipher and its parameters.
pbe_encrypt_parameters({_, #'PBES2-params'{} = Params}) ->
    #'EncryptedPrivateKeyInfo_encryptionAlgorithm'{
        algorithm = ?'id-PBES2', parameters = {asn1_OPENTYPE, der_encode('PBES2-params', Params)}
    };
pbe_encrypt_parameters({Cipher, {#'PBEParameter'{} = Params, Hash}}) ->
    #'EncryptedPrivateKeyInfo_encryptionAlgorithm'{
        algorithm = clause_public_key:pbes1_oid(Cipher, Hash),
        parameters = {asn1_OPENTYPE, der_encode('PBEParameter', Params)}
    }.

%%--------------------------------------------------------------------------------------------------------------------
%% Helpers
%%--------------------------------------------------------------------------------------------------------------------

%% pubkey_translation's walk, a builtin.
translate(Direction, Term) -> clause_public_key:translate(Direction, Term).

%% supportedPublicKeyAlgorithms/1: the key type of a public key algorithm; function_clause for another.
public_key_type(Oid) ->
    case clause_public_key:oid_name(public_key_algorithm, Oid) of
        undefined -> erlang:error(function_clause, [Oid]);
        Type -> Type
    end.

%% A table's name of an object identifier; function_clause without a row, as OTP's table functions.
oid_name(Table, Oid) ->
    case clause_public_key:oid_name(Table, Oid) of
        undefined -> erlang:error(function_clause, [Oid]);
        Name -> Name
    end.

%% A table's object identifier of a name; function_clause without a row.
name_oid(Table, Name) ->
    case clause_public_key:name_oid(Table, Name) of
        undefined -> erlang:error(function_clause, [Name]);
        Oid -> Oid
    end.

%% Module:decode/2 of the generated module a type is in, named for the compiler.
decode(Module, Type, Der) ->
    case Module of
        'PKIX1Implicit-2009' -> 'PKIX1Implicit-2009':decode(Type, Der);
        'PKIX1Explicit-2009' -> 'PKIX1Explicit-2009':decode(Type, Der);
        'PKIXAlgs-2009' -> 'PKIXAlgs-2009':decode(Type, Der);
        'PKIX1-PSS-OAEP-Algorithms-2009' -> 'PKIX1-PSS-OAEP-Algorithms-2009':decode(Type, Der);
        'OTP-PKIX' -> 'OTP-PKIX':decode(Type, Der);
        'PKCS-1' -> 'PKCS-1':decode(Type, Der);
        'PKCS-3' -> 'PKCS-3':decode(Type, Der);
        'DSS' -> 'DSS':decode(Type, Der);
        'Safecurves-pkix-18' -> 'Safecurves-pkix-18':decode(Type, Der);
        'ECPrivateKey' -> 'ECPrivateKey':decode(Type, Der);
        'X509-ML-DSA-2025' -> 'X509-ML-DSA-2025':decode(Type, Der);
        'X509-ML-KEM-2025' -> 'X509-ML-KEM-2025':decode(Type, Der);
        'SLH-DSA-Module-2024' -> 'SLH-DSA-Module-2024':decode(Type, Der);
        'PKCS-10' -> 'PKCS-10':decode(Type, Der);
        'EnrollmentMessageSyntax-2009' -> 'EnrollmentMessageSyntax-2009':decode(Type, Der);
        'CryptographicMessageSyntax-2009' -> 'CryptographicMessageSyntax-2009':decode(Type, Der);
        'OCSP-2024-08' -> 'OCSP-2024-08':decode(Type, Der);
        'PKIXCMP-2023' -> 'PKIXCMP-2023':decode(Type, Der);
        'PKIXCRMF-2009' -> 'PKIXCRMF-2009':decode(Type, Der)
    end.

%% Module:encode/2 of the generated module a type is in, named for the compiler.
encode(Module, Type, Value) ->
    case Module of
        'PKIX1Implicit-2009' -> 'PKIX1Implicit-2009':encode(Type, Value);
        'PKIX1Explicit-2009' -> 'PKIX1Explicit-2009':encode(Type, Value);
        'PKIXAlgs-2009' -> 'PKIXAlgs-2009':encode(Type, Value);
        'PKIX1-PSS-OAEP-Algorithms-2009' -> 'PKIX1-PSS-OAEP-Algorithms-2009':encode(Type, Value);
        'OTP-PKIX' -> 'OTP-PKIX':encode(Type, Value);
        'PKCS-1' -> 'PKCS-1':encode(Type, Value);
        'PKCS-3' -> 'PKCS-3':encode(Type, Value);
        'DSS' -> 'DSS':encode(Type, Value);
        'Safecurves-pkix-18' -> 'Safecurves-pkix-18':encode(Type, Value);
        'ECPrivateKey' -> 'ECPrivateKey':encode(Type, Value);
        'X509-ML-DSA-2025' -> 'X509-ML-DSA-2025':encode(Type, Value);
        'X509-ML-KEM-2025' -> 'X509-ML-KEM-2025':encode(Type, Value);
        'SLH-DSA-Module-2024' -> 'SLH-DSA-Module-2024':encode(Type, Value);
        'PKCS-10' -> 'PKCS-10':encode(Type, Value);
        'EnrollmentMessageSyntax-2009' -> 'EnrollmentMessageSyntax-2009':encode(Type, Value);
        'CryptographicMessageSyntax-2009' -> 'CryptographicMessageSyntax-2009':encode(Type, Value);
        'OCSP-2024-08' -> 'OCSP-2024-08':encode(Type, Value);
        'PKIXCMP-2023' -> 'PKIXCMP-2023':encode(Type, Value);
        'PKIXCRMF-2009' -> 'PKIXCRMF-2009':encode(Type, Value)
    end.
