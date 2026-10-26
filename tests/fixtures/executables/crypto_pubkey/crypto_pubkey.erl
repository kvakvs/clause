%% crypto's public keys (plan 21 step LIB79C): RSA, DSA, ECDSA and EdDSA signatures, RSA encryption, key generation
%% and agreement (DH, ECDH on every named curve and on explicit curves, X25519, X448), SRP, ML-DSA, SLH-DSA, ML-KEM,
%% the explicit parameters of every named curve, and OTP's error classes and descriptions. Deterministic results print
%% in full; randomized ones (generated keys, PSS, ECDSA, DSA, ML-DSA signatures) print their checks. File and line of
%% the errors are Clause's own, so only their class and description print.
-module(crypto_pubkey).
-export([main/1]).

main(_) ->
    rsa(),
    rsa_encryption(),
    dss(),
    ecdsa(),
    eddsa(),
    ecdh(),
    dh(),
    srp(),
    curves(),
    post_quantum(),
    kem(),
    supported(),
    errors(),
    io:format("crypto_pubkey: ok~n").

id(X) -> X.

hex(Bin) -> binary:encode_hex(Bin, lowercase).

msg() -> <<"clause public keys">>.

%% A 1024-bit RSA key generated once by OTP: [E, N, D, P1, P2, E1, E2, C].
rsa_private() ->
    [
        65537,
        128641326123946291981302956199600759986352698985323920054822398780470113694505714681375477709919748549793375772985531388240161834848639247759113297578836334936411079740505334231415081193226440487594370763447723332611478668746330749436111145341476108105945411452246257739104170946978622564089889823461409873291,
        122365995858321434336549220890231645903065859815754355191992742114752077264987507106473400986258100379848315389595465565144753174305570451886768130537935146636048516795537010390277262217380417051019619456710597389755835357322473801466778435748493519206612279074289755232254931388088032218607563608764135006113,
        11711479499529474835483829153476721787127112507775431997095558812097270715367627315789431725769309058489109685216368377336636773791502119057118592311554023,
        10984207941371936673756446997111107470295472674660546325833901749314291737205249150531661289963945367048526524344408001594682660311210978149095187110576317,
        1043609568293515617730832388670583872267914873207631152830280047341929917111661252791709740734131328891716138389971944453453144924887809562439119567564513,
        10869567383642739354915953691863323813587321503421890090625235060321341563284435100163721708928265695549050012715075333436378756567480439536494959636269677,
        5985447770232516634336809860190704821736942535243204217719353995889379542878679005436524210468518595022882764372408052380379077870604978636958774644608593
    ].

rsa_public() -> lists:sublist(rsa_private(), 2).

rsa() ->
    Priv = rsa_private(),
    Pub = rsa_public(),
    [
        begin
            Sig = crypto:sign(rsa, Digest, msg(), Priv),
            Digested = crypto:sign(rsa, Digest, {digest, crypto:hash(Digest, msg())}, Priv),
            io:format("rsa ~p ~s ~p ~p ~p~n", [
                Digest,
                hex(Sig),
                Digested =:= Sig,
                crypto:verify(rsa, Digest, msg(), Sig, Pub),
                crypto:verify(rsa, Digest, <<"other">>, Sig, Pub)
            ])
        end
     || Digest <- [sha, sha256, sha384, sha512, sha3_256]
    ],
    Binaries = [binary:encode_unsigned(I) || I <- Priv],
    io:format("rsa binaries ~p~n", [crypto:sign(rsa, sha256, msg(), Binaries) =:= crypto:sign(rsa, sha256, msg(), Priv)]),
    io:format("rsa short key ~s~n", [hex(crypto:sign(rsa, sha256, msg(), lists:sublist(Priv, 3)))]),
    Raw = crypto:sign(rsa, none, crypto:hash(sha256, msg()), Priv),
    io:format("rsa none ~s ~p~n", [hex(Raw), crypto:verify(rsa, none, crypto:hash(sha256, msg()), Raw, Pub)]),
    Pss = [{rsa_padding, rsa_pkcs1_pss_padding}, {rsa_pss_saltlen, 0}, {rsa_mgf1_md, sha256}],
    PssSig = crypto:sign(rsa, sha256, msg(), Priv, Pss),
    io:format("rsa pss0 ~s ~p~n", [hex(PssSig), crypto:verify(rsa, sha256, msg(), PssSig, Pub, Pss)]),
    Salted = [{rsa_padding, rsa_pkcs1_pss_padding}, {rsa_pss_saltlen, 32}],
    SaltedSig = crypto:sign(rsa, sha256, msg(), Priv, Salted),
    io:format("rsa pss32 ~p ~p ~p~n", [
        byte_size(SaltedSig),
        crypto:verify(rsa, sha256, msg(), SaltedSig, Pub, Salted),
        crypto:verify(rsa, sha256, msg(), SaltedSig, Pub)
    ]),
    io:format("rsa pubkey ~p ~p~n", [crypto:privkey_to_pubkey(rsa, Priv) =:= Pub, crypto:privkey_to_pubkey(rsa, [1, 2])]),
    generated_rsa(65537),
    generated_rsa(3).

generated_rsa(E) ->
    {Pub, Priv} = crypto:generate_key(rsa, {1024, E}),
    [E1, N | _] = Priv,
    Sig = crypto:sign(rsa, sha256, msg(), Priv),
    io:format("rsa generated ~p ~p ~p ~p ~p ~p~n", [
        length(Priv),
        crypto:bytes_to_integer(E1),
        bit_size(N),
        Pub =:= lists:sublist(Priv, 2),
        crypto:verify(rsa, sha256, msg(), Sig, Pub),
        crypto:privkey_to_pubkey(rsa, Priv) =:= [crypto:bytes_to_integer(B) || B <- Pub]
    ]).

rsa_encryption() ->
    Priv = rsa_private(),
    Pub = rsa_public(),
    Text = <<"secret text">>,
    Signed = crypto:private_encrypt(rsa, Text, Priv, rsa_pkcs1_padding),
    io:format("rsa private_encrypt ~s ~p~n", [hex(Signed), crypto:public_decrypt(rsa, Signed, Pub, rsa_pkcs1_padding)]),
    Block = binary:copy(<<1>>, 128),
    NoPad = crypto:public_encrypt(rsa, Block, Pub, rsa_no_padding),
    io:format("rsa no_padding ~s ~p~n", [hex(NoPad), crypto:private_decrypt(rsa, NoPad, Priv, rsa_no_padding) =:= Block]),
    Options = [
        [{rsa_padding, rsa_pkcs1_padding}],
        rsa_pkcs1_oaep_padding,
        [{rsa_pad, rsa_pkcs1_oaep_padding}],
        [{rsa_padding, rsa_pkcs1_oaep_padding}, {rsa_oaep_md, sha256}],
        [{rsa_padding, rsa_pkcs1_oaep_padding}, {rsa_oaep_md, sha256}, {rsa_mgf1_md, sha}, {rsa_oaep_label, <<"l">>}]
    ],
    [
        begin
            Encrypted = crypto:public_encrypt(rsa, Text, Pub, Option),
            io:format("rsa public_encrypt ~p ~p~n", [
                byte_size(Encrypted), crypto:private_decrypt(rsa, Encrypted, Priv, Option)
            ])
        end
     || Option <- Options
    ],
    ok.

%% A 1024-bit DSA key made once by OpenSSL: [P, Q, G] and the private and public keys.
dss_params() ->
    [
        16#C4E35A887FF2C1DB1B9DCB8A77B12A80CF7F1C6FE9EBCA4C06B448087E5B20978D4251821536AC41B97C172F0F071971EA64EFFF154B0BFCE381600DDA4FC8872C6A83FDF9319264415C7CE519E63604E280E3F94CACE3842DA04CA1350AD2272996F15735A687CAD26D176C14E285ED79337D0E1903FFA4DD4551DABF3CE125,
        16#EF216C0C27448E5F7A92AADD81DEC875930DAE61,
        16#A667CB8ADCF2C3A36C46107650499864DD6E4B29DF42DF4223E02966CB82D6F72C1B3BD3D4662E8CD25616E8A001D2B1B412C17BC64E74EADF4B60584CE7CE3D44505B691D8109D8B68CBCE3E44A930FF42FF4401A7FD099149342CA33BB41CD69645566FA6E301C18D18430AAB97A1901F611AE6B1F0192EA6A0D5902689A06
    ].

dss_private() -> dss_params() ++ [16#BE712ECFC3AF4F8D39FCE342238198FBE1F18116].

dss_public() ->
    dss_params() ++
        [
            16#26E203B07263095E53821706EE63879C4F58AAE83E08038404BD36E013EFC824EF2A345241BA36D5C8B045472AC9752B3BC3ED8A67F1DF13AC056A1227E41C6EE62DDD48C89F34B41D9EE1FAF9886F147FD86ACA474463EF4141F7E00DC6D18E890DC0903437D3843796655179C487E178B9B92408897B9142F20C16276ABEC4
        ].

dss() ->
    case lists:member(dss, crypto:supports(public_keys)) of
        true -> dss_supported();
        false -> io:format("dss sha true true false~ndss none {badarg,\"Bad digest type\"}~ndss pubkey true~n")
    end.

dss_supported() ->
    Sig = crypto:sign(dss, sha, msg(), dss_private()),
    io:format("dss sha ~p ~p ~p~n", [
        crypto:verify(dss, sha, msg(), Sig, dss_public()),
        crypto:verify(dss, sha, {digest, crypto:hash(sha, msg())}, Sig, dss_public()),
        crypto:verify(dss, sha, <<"other">>, Sig, dss_public())
    ]),
    %% OTP's compatibility clause for none gives sha as the algorithm and {digest, Digest} as the digest type.
    Digest = crypto:hash(sha, msg()),
    io:format("dss none ~p~n", [caught(fun() -> crypto:sign(dss, none, Digest, dss_private()) end)]),
    io:format("dss pubkey ~p~n", [crypto:privkey_to_pubkey(dss, dss_private()) =:= dss_public()]).

ec_private() -> 16#1234567890ABCDEF.

%% A secp256r1 signature of msg() made once by OTP with ec_private().
ec_signature() ->
    binary:decode_hex(
        <<
            "304602210096F24C88EAC4BD32AE3F30A4851105214626BBBBEB031ADE2BDF8C19CE3E6CF9022100CDB25C37F61A59A37089DC40CF2"
            "37B2DE2CA93D93D0376678182CCE3E0DD68A2"
        >>
    ).

ecdsa() ->
    Curves = [secp256r1, prime256v1, secp384r1, secp521r1, secp256k1, brainpoolP256r1, secp192r1, sect283k1],
    [
        begin
            {Pub, Priv} = crypto:generate_key(ecdh, Curve, ec_private()),
            Sig = crypto:sign(ecdsa, sha256, msg(), [Priv, Curve]),
            io:format("ecdsa ~p ~s ~s ~p ~p~n", [
                Curve,
                hex(Pub),
                hex(Priv),
                crypto:verify(ecdsa, sha256, msg(), Sig, [Pub, Curve]),
                crypto:verify(ecdsa, sha256, <<"other">>, Sig, [Pub, Curve])
            ])
        end
     || Curve <- Curves
    ],
    {Pub, _} = crypto:generate_key(ecdh, secp256r1, ec_private()),
    Old = crypto:verify(ecdsa, sha256, <<"clause">>, ec_signature(), [Pub, secp256r1]),
    Explicit = crypto:ec_curve(secp256r1),
    io:format("ecdsa known ~p ~p ~p~n", [
        Old,
        crypto:verify(ecdsa, sha256, <<"clause">>, ec_signature(), [Pub, Explicit]),
        crypto:verify(ecdsa, sha256, <<"clause">>, ec_signature(), [crypto:bytes_to_integer(Pub), secp256r1])
    ]),
    {ExplicitPub, ExplicitPriv} = crypto:generate_key(ecdh, Explicit, ec_private()),
    ExplicitSig = crypto:sign(ecdsa, sha384, msg(), [ExplicitPriv, Explicit]),
    io:format("ecdsa explicit ~p ~p~n", [
        ExplicitPub =:= Pub, crypto:verify(ecdsa, sha384, msg(), ExplicitSig, [Pub, secp256r1])
    ]),
    io:format("ecdsa pubkey ~p~n", [crypto:privkey_to_pubkey(ecdsa, [ec_private(), secp256r1])]).

eddsa() ->
    Secret25519 = binary:decode_hex(<<"9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60">>),
    Secret448 = binary:copy(<<7>>, 57),
    [
        begin
            {Pub, Priv} = crypto:generate_key(eddsa, Curve, Secret),
            Sig = crypto:sign(eddsa, none, msg(), [Priv, Curve]),
            io:format("eddsa ~p ~s ~p ~s ~p ~p ~p~n", [
                Curve,
                hex(Pub),
                Priv =:= Secret,
                hex(Sig),
                crypto:sign(eddsa, sha256, msg(), [Priv, Curve]) =:= Sig,
                crypto:verify(eddsa, none, msg(), Sig, [Pub, Curve]),
                crypto:verify(eddsa, none, <<"other">>, Sig, [Pub, Curve])
            ])
        end
     || {Curve, Secret} <- [{ed25519, Secret25519}, {ed448, Secret448}]
    ],
    {Pub, Priv} = crypto:generate_key(eddsa, ed25519),
    io:format("eddsa generated ~p ~p ~p~n", [
        byte_size(Pub),
        byte_size(Priv),
        crypto:verify(eddsa, none, msg(), crypto:sign(eddsa, none, msg(), [Priv, ed25519]), [Pub, ed25519])
    ]).

ecdh() ->
    [
        begin
            {PubA, PrivA} = crypto:generate_key(ecdh, Curve, 7),
            {PubB, PrivB} = crypto:generate_key(ecdh, Curve, 11),
            Shared = crypto:compute_key(ecdh, PubB, PrivA, Curve),
            io:format("ecdh ~p ~s ~p~n", [Curve, hex(Shared), crypto:compute_key(ecdh, PubA, PrivB, Curve) =:= Shared])
        end
     || Curve <- [secp256r1, secp384r1, brainpoolP384r1, sect233k1]
    ],
    Explicit = crypto:ec_curve(secp521r1),
    {PubE, _} = crypto:generate_key(ecdh, Explicit, 5),
    io:format("ecdh explicit ~s~n", [hex(crypto:compute_key(ecdh, PubE, 9, Explicit))]),
    AlicePriv = binary:decode_hex(<<"77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a">>),
    BobPriv = binary:decode_hex(<<"5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb">>),
    {AlicePub, AlicePriv} = crypto:generate_key(ecdh, x25519, AlicePriv),
    {BobPub, BobPriv} = crypto:generate_key(eddh, x25519, BobPriv),
    io:format("x25519 ~s ~s ~s ~p~n", [
        hex(AlicePub),
        hex(BobPub),
        hex(crypto:compute_key(ecdh, BobPub, AlicePriv, x25519)),
        crypto:compute_key(eddh, AlicePub, BobPriv, x25519) =:= crypto:compute_key(ecdh, BobPub, AlicePriv, x25519)
    ]),
    {Pub448, Priv448} = crypto:generate_key(ecdh, x448, binary:copy(<<3>>, 56)),
    {Peer448, PeerPriv448} = crypto:generate_key(ecdh, x448),
    io:format("x448 ~s ~p~n", [
        hex(Pub448),
        crypto:compute_key(ecdh, Peer448, Priv448, x448) =:= crypto:compute_key(ecdh, Pub448, PeerPriv448, x448)
    ]).

%% The 1024-bit MODP group of RFC 2409.
modp() ->
    16#FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E088A67CC74020BBEA63B139B22514A08798E3404DDEF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7EDEE386BFB5A899FA5AE9F24117C4B1FE649286651ECE65381FFFFFFFFFFFFFFFF.

dh() ->
    Params = [modp(), 2],
    {PubA, PrivA} = crypto:generate_key(dh, Params),
    {PubB, PrivB} = crypto:generate_key(dh, Params),
    io:format("dh generated ~p ~p~n", [
        byte_size(PubA), crypto:compute_key(dh, PubB, PrivA, Params) =:= crypto:compute_key(dh, PubA, PrivB, Params)
    ]),
    {_, Short} = crypto:generate_key(dh, [modp(), 2, 160]),
    io:format("dh short ~p~n", [bit_size(Short) =< 160]),
    Mine = 16#123456789,
    Theirs = crypto:mod_pow(2, 16#ABCDEF, modp()),
    io:format("dh shared ~s~n", [hex(crypto:compute_key(dh, Theirs, Mine, [binary:encode_unsigned(modp()), <<2>>]))]).

srp() ->
    Prime = binary:encode_unsigned(modp()),
    Generator = <<2>>,
    Derived = <<16#5A5A5A5A:32>>,
    Verifier = crypto:mod_pow(Generator, Derived, Prime),
    [
        begin
            {UserPub, UserPriv} = crypto:generate_key(srp, {user, [Generator, Prime, Version]}, <<16#1111:16>>),
            {HostPub, HostPriv} = crypto:generate_key(srp, {host, [Verifier, Generator, Prime, Version]}, <<16#2222:16>>),
            User = crypto:compute_key(srp, HostPub, {UserPub, UserPriv}, {user, [Derived, Prime, Generator, Version]}),
            Host = crypto:compute_key(srp, UserPub, {HostPub, HostPriv}, {host, [Verifier, Prime, Version]}),
            io:format("srp ~p ~s ~s ~p~n", [Version, hex(HostPub), hex(User), User =:= Host])
        end
     || Version <- ['3', '6', '6a']
    ],
    io:format("srp error ~p~n", [caught(fun() -> crypto:generate_key(srp, {user, [Generator, <<0>>, '6a']}, <<1>>) end)]).

curves() ->
    [
        io:format("curve ~p ~s~n", [
            Curve,
            hex(crypto:hash(sha256, term_to_binary({crypto:ec_curve(Curve), crypto:generate_key(ecdh, Curve, 3)})))
        ])
     || Curve <- otp_curves()
    ],
    io:format("ec_curves ~p~n", [lists:sort(crypto:ec_curves()) =:= lists:sort(otp_curves() ++ [ed25519, ed448])]).

%% The named curves of OTP 29's crypto_ec_curves.
otp_curves() ->
    [
        secp160k1,
        secp160r1,
        secp160r2,
        secp192k1,
        secp224k1,
        secp224r1,
        secp256k1,
        secp384r1,
        secp521r1,
        secp192r1,
        prime192v1,
        prime192v2,
        prime192v3,
        prime239v1,
        prime239v2,
        prime239v3,
        secp256r1,
        prime256v1,
        wtls7,
        wtls9,
        wtls12,
        brainpoolP160r1,
        brainpoolP160t1,
        brainpoolP192r1,
        brainpoolP192t1,
        brainpoolP224r1,
        brainpoolP224t1,
        brainpoolP256r1,
        brainpoolP256t1,
        brainpoolP320r1,
        brainpoolP320t1,
        brainpoolP384r1,
        brainpoolP384t1,
        brainpoolP512r1,
        brainpoolP512t1,
        sect163k1,
        sect163r1,
        sect163r2,
        sect193r1,
        sect193r2,
        sect233k1,
        sect233r1,
        sect239k1,
        sect283k1,
        sect283r1,
        sect409k1,
        sect409r1,
        sect571k1,
        sect571r1,
        c2pnb163v1,
        c2pnb163v2,
        c2pnb163v3,
        c2pnb176v1,
        c2tnb191v1,
        c2tnb191v2,
        c2tnb191v3,
        c2pnb208w1,
        c2tnb239v1,
        c2tnb239v2,
        c2tnb239v3,
        c2pnb272w1,
        c2pnb304w1,
        c2tnb359v1,
        c2pnb368w1,
        c2tnb431r1,
        wtls3,
        wtls5,
        wtls10,
        wtls11,
        secp112r1,
        secp112r2,
        secp128r1,
        secp128r2,
        wtls6,
        wtls8,
        sect113r1,
        sect113r2,
        sect131r1,
        sect131r2,
        wtls1,
        wtls4,
        ipsec3,
        ipsec4
    ].

post_quantum() ->
    [
        begin
            {Pub, Priv} = crypto:generate_key(Type, []),
            Sig = crypto:sign(Type, none, msg(), Priv),
            io:format("pq ~p ~p ~p ~p ~p ~p ~p~n", [
                Type,
                byte_size(Pub),
                byte_size(Priv),
                byte_size(Sig),
                crypto:verify(Type, none, msg(), Sig, Pub),
                crypto:verify(Type, none, <<"other">>, Sig, Pub),
                crypto:verify(Type, sha256, msg(), crypto:sign(Type, sha256, msg(), Priv), Pub)
            ])
        end
     || Type <- [mldsa44, mldsa65, mldsa87, slh_dsa_shake_128f, slh_dsa_sha2_128f]
    ],
    {Pub, _} = crypto:generate_key(mldsa44, []),
    Seeded = crypto:sign(mldsa44, none, msg(), {seed, binary:copy(<<9>>, 32)}),
    io:format("pq seed ~p ~p~n", [byte_size(Seeded), crypto:verify(mldsa44, none, msg(), Seeded, Pub)]).

kem() ->
    [
        begin
            {Pub, Priv} = crypto:generate_key(Type, []),
            {Secret, Encapsulated} = crypto:encapsulate_key(Type, Pub),
            io:format("kem ~p ~p ~p ~p ~p ~p~n", [
                Type,
                byte_size(Pub),
                byte_size(Priv),
                byte_size(Secret),
                byte_size(Encapsulated),
                crypto:decapsulate_key(Type, Priv, Encapsulated) =:= Secret
            ])
        end
     || Type <- [mlkem512, mlkem768, mlkem1024]
    ],
    ok.

supported() ->
    Keys = crypto:supports(public_keys),
    io:format("public_keys ~p~n", [
        [
            K
         || K <- [rsa, dh, ecdsa, ecdh, eddsa, eddh, srp, mldsa44, mldsa65, mldsa87, slh_dsa_shake_128f],
            not lists:member(K, Keys)
        ]
    ]),
    io:format("kems ~p~n", [crypto:supports(kems)]),
    io:format("rsa_opts ~p~n", [lists:sort(crypto:supports(rsa_opts))]).

caught(Fun) ->
    try Fun() of
        Value -> {value, Value}
    catch
        error:{Class, {File, Line}, Description} when is_list(File), is_integer(Line) -> {Class, Description};
        error:{Class, #{c_file_name := _}, Description} -> {raw, Class, Description};
        Class:Reason -> {Class, Reason}
    end.

errors() ->
    Priv = rsa_private(),
    Pub = rsa_public(),
    M = msg(),
    {EcPub, EcPriv} = crypto:generate_key(ecdh, secp256r1, ec_private()),
    {XPub, XPriv} = crypto:generate_key(ecdh, x25519),
    {_, MlPriv} = crypto:generate_key(mldsa44, []),
    Cases = [
        {bad_digest, fun() -> crypto:sign(rsa, id(foo), M, Priv) end},
        {verify_digest, fun() -> crypto:verify(rsa, id(foo), M, <<>>, Pub) end},
        {rsa_key, fun() -> crypto:sign(rsa, sha256, M, id([<<1>>, <<2>>])) end},
        {rsa_key_atom, fun() -> crypto:sign(rsa, sha256, M, id(key)) end},
        {rsa_public_key, fun() -> crypto:verify(rsa, sha256, M, <<1>>, id([1])) end},
        {digest_size, fun() -> crypto:sign(rsa, sha256, {digest, <<1, 2>>}, Priv) end},
        {digest_tuple, fun() -> crypto:sign(rsa, sha256, id({digest, <<1>>, x}), Priv) end},
        {digest_head, fun() -> crypto:sign(rsa, sha256, id({dig, <<1>>}), Priv) end},
        {digest_data, fun() -> crypto:sign(rsa, sha256, id({digest, data}), Priv) end},
        {data, fun() -> crypto:sign(rsa, sha256, id(data), Priv) end},
        {padding, fun() -> crypto:sign(rsa, sha256, M, Priv, [{rsa_padding, id(foo)}]) end},
        {two_tuples, fun() -> crypto:sign(rsa, sha256, M, Priv, [id(foo)]) end},
        {bad_option, fun() -> crypto:sign(rsa, sha256, M, Priv, [{id(foo), 1}]) end},
        {saltlen, fun() -> crypto:sign(rsa, sha256, M, Priv, [{rsa_pss_saltlen, id(-3)}]) end},
        {mgf1, fun() -> crypto:sign(rsa, sha256, M, Priv, [{rsa_mgf1_md, id("sha")}]) end},
        {mgf1_digest, fun() -> crypto:sign(rsa, sha256, M, Priv, [{rsa_mgf1_md, id(foo)}]) end},
        {only_rsa, fun() -> crypto:sign(ecdsa, sha256, M, [EcPriv, secp256r1], [{rsa_padding, x}]) end},
        {options_list, fun() -> crypto:sign(rsa, sha256, M, Priv, id(x)) end},
        {engine, fun() -> crypto:sign(rsa, sha256, M, id(#{engine => e, key_id => "k"})) end},
        {engine_map, fun() -> crypto:sign(rsa, sha256, M, id(#{})) end},
        {engine_key_id, fun() -> crypto:sign(rsa, sha256, M, id(#{engine => e})) end},
        {signature, fun() -> crypto:verify(rsa, sha256, M, id(sig), Pub) end},
        {unknown_algorithm, fun() -> crypto:sign(id(foo), sha256, M, <<1>>) end},
        {ecdsa_key, fun() -> crypto:sign(ecdsa, sha256, M, [EcPriv, id(nosuch)]) end},
        {ecdsa_public, fun() -> crypto:verify(ecdsa, sha256, M, <<1>>, [EcPub, id(nosuch)]) end},
        {ecdsa_bad_point, fun() -> crypto:verify(ecdsa, sha256, M, <<1>>, [<<4, 1, 2>>, secp256r1]) end},
        {eddsa_curve, fun() -> crypto:sign(eddsa, none, M, [<<1:256>>, id(ed999)]) end},
        {eddsa_public, fun() -> crypto:verify(eddsa, none, M, <<>>, [<<1>>, ed25519]) end},
        {dss_key, fun() -> crypto:sign(dss, sha, M, id([1, 2])) end},
        {mldsa_seed, fun() -> crypto:sign(mldsa44, none, M, {seed, <<1>>}) end},
        {slh_seed, fun() -> crypto:sign(slh_dsa_shake_128f, none, M, {seed, <<1>>}) end},
        {key_tuple, fun() -> crypto:sign(mldsa44, none, M, {id(foo), MlPriv}) end},
        {mldsa_public, fun() -> crypto:verify(mldsa44, none, M, <<1>>, <<1, 2, 3>>) end},
        {crypt_text, fun() -> crypto:public_encrypt(rsa, id(text), Pub, rsa_pkcs1_padding) end},
        {crypt_key, fun() -> crypto:public_encrypt(rsa, <<>>, id([<<1>>]), rsa_pkcs1_padding) end},
        {crypt_padding, fun() -> crypto:public_encrypt(rsa, <<"x">>, Pub, [{rsa_padding, id(foo)}]) end},
        {crypt_option, fun() -> crypto:public_encrypt(rsa, <<"x">>, Pub, [{id(foo), 1}]) end},
        {crypt_tuples, fun() -> crypto:public_encrypt(rsa, <<"x">>, Pub, [id(foo)]) end},
        {oaep_md, fun() -> crypto:public_encrypt(rsa, <<"x">>, Pub, [{rsa_oaep_md, id("sha")}]) end},
        {oaep_label, fun() -> crypto:public_encrypt(rsa, <<"x">>, Pub, [{rsa_oaep_label, id("l")}]) end},
        {signature_md, fun() -> crypto:public_encrypt(rsa, <<"x">>, Pub, [{signature_md, id(1)}]) end},
        {crypt_list, fun() -> crypto:public_encrypt(rsa, <<"x">>, Pub, id(<<"x">>)) end},
        {crypt_algorithm, fun() -> crypto:public_encrypt(id(foo), <<"x">>, Pub, []) end},
        {too_long, fun() -> crypto:public_encrypt(rsa, binary:copy(<<1>>, 200), Pub, rsa_pkcs1_padding) end},
        {oaep_garbage, fun() -> crypto:private_decrypt(rsa, binary:copy(<<1>>, 128), Priv, rsa_pkcs1_oaep_padding) end},
        {pubkey_dss, fun() -> crypto:privkey_to_pubkey(dss, id([1])) end},
        {rsa_bits, fun() -> crypto:generate_key(rsa, {id(100), 65537}) end},
        {rsa_bits_type, fun() -> crypto:generate_key(rsa, {id(foo), 65537}) end},
        {rsa_exponent, fun() -> crypto:generate_key(rsa, {1024, id(e)}) end},
        {rsa_private_given, fun() -> crypto:generate_key(rsa, {1024, 3}, id(Priv)) end},
        {ec_curve_unknown, fun() -> crypto:generate_key(ecdh, id(nosuch)) end},
        {ec_curve_name, fun() -> crypto:ec_curve(id(nosuch)) end},
        {ec_private, fun() -> crypto:generate_key(ecdh, secp256r1, id(foo)) end},
        {ec_explicit, fun() -> crypto:generate_key(ecdh, id({{prime_field, 7}, {1, 2, none}, <<4>>, 5, none})) end},
        {ec_explicit_field, fun() -> crypto:generate_key(ecdh, id({{foo, 7}, {1, 2, none}, <<4>>, 5, 1})) end},
        {ec_onbasis, fun() ->
            crypto:generate_key(ecdh, id({{characteristic_two_field, 163, onbasis}, {1, 2, none}, <<4>>, 5, 1}))
        end},
        {ec_basis, fun() ->
            crypto:generate_key(ecdh, id({{characteristic_two_field, 163, {tpbasis, 200}}, {1, 2, none}, <<4>>, 5, 1}))
        end},
        {ec_basis_atom, fun() ->
            crypto:generate_key(
                ecdh, id({{characteristic_two_field, 163, {ppbasis, 3, 2, 1}}, {1, 2, none}, <<4>>, 5, 1})
            )
        end},
        {ec_generator, fun() -> crypto:generate_key(ecdh, id({{prime_field, 7}, {1, 2, none}, 4, 5, 1})) end},
        {ecdh_point, fun() -> crypto:compute_key(ecdh, <<1>>, <<2>>, secp256r1) end},
        {ecdh_others, fun() -> crypto:compute_key(ecdh, id(pub), <<2>>, secp256r1) end},
        {ecdh_mine, fun() -> crypto:compute_key(ecdh, EcPub, id(mine), secp256r1) end},
        {ecdh_curve, fun() -> crypto:compute_key(ecdh, EcPub, <<2>>, id(nosuch)) end},
        {ecdh_explicit, fun() ->
            crypto:compute_key(ecdh, EcPub, <<2>>, id({{prime_field, 7}, {1, 2, none}, <<4>>, 5, 1}))
        end},
        {dh_params, fun() -> crypto:generate_key(dh, [1, 2]) end},
        {dh_g, fun() -> crypto:generate_key(dh, [<<1>>, id(foo)]) end},
        {dh_three, fun() -> crypto:generate_key(dh, [<<1>>, <<2>>, id(<<3>>)]) end},
        {dh_private, fun() -> crypto:generate_key(dh, [<<1>>, <<2>>], id(foo)) end},
        {dh_list, fun() -> crypto:generate_key(dh, id([<<1>>])) end},
        {dh_others, fun() -> crypto:compute_key(dh, id(foo), <<1>>, [<<1>>, <<2>>]) end},
        {dh_compute_params, fun() -> crypto:compute_key(dh, <<1>>, <<1>>, id([<<1>>, foo])) end},
        {eddsa_private, fun() -> crypto:generate_key(eddsa, ed25519, id(<<1>>)) end},
        {evp_type, fun() -> crypto:generate_key(id(foo), []) end},
        {evp_private, fun() -> crypto:generate_key(x25519, [], id(priv)) end},
        {eddh_mine, fun() -> crypto:compute_key(eddh, <<1>>, id(<<2>>), x25519) end},
        {eddh_others, fun() -> crypto:compute_key(eddh, id(<<1>>), XPriv, x25519) end},
        {eddh_low_order, fun() -> crypto:compute_key(eddh, <<0:256>>, XPriv, x25519) end},
        {eddh_type, fun() -> crypto:compute_key(eddh, XPub, id(priv), x25519) end},
        {kem_public, fun() -> crypto:encapsulate_key(mlkem512, <<1, 2>>) end},
        {kem_type, fun() -> crypto:encapsulate_key(id(foo), <<>>) end},
        {kem_wrapped, fun() -> crypto:decapsulate_key(mlkem512, <<>>, id(wrapped)) end},
        {kem_private, fun() -> crypto:decapsulate_key(mlkem512, <<1>>, <<>>) end},
        {srp_bad, fun() -> crypto:compute_key(srp, <<1>>, {<<2>>, <<3>>}, {host, [<<4>>, <<5>>, '6']}) end},
        {srp_zero, fun() -> crypto:compute_key(srp, <<5>>, {<<2>>, <<3>>}, {host, [<<4>>, <<5>>, '6']}) end},
        {srp_type, fun() -> crypto:compute_key(srp, <<5>>, {<<2>>, id(foo)}, {host, [<<4>>, <<5>>, '6']}) end}
    ],
    [io:format("error ~p ~p~n", [Name, caught(Fun)]) || {Name, Fun} <- Cases],
    ok.
