%% The parts of ssl's utility API that depend on what the linked crypto supports (plan 21 step LIB81A,
%% docs/libraries/ssl.md#utility-functions): cipher suite filters, signature algorithms, curves, groups and protocol
%% versions, as OTP's ssl computes them from crypto:supports/1.
-module(clause_ssl_algorithms).
-export([filter_cipher_suites/2, signature_algs/2, eccs/0, eccs/1, groups/0, groups/1, versions/0]).
-export([tls_versions/0, dtls_versions/0, sufficient_crypto/1]).
-export([suite_code/1, code_suite/1]).

-define(VSN, "11.7.7").
-define(SUPPORTED_TLS, ['tlsv1.3', 'tlsv1.2']).
-define(AVAILABLE_TLS, ['tlsv1.3', 'tlsv1.2', 'tlsv1.1', tlsv1]).
-define(AVAILABLE_DTLS, ['dtlsv1.2', dtlsv1]).

%% Cipher suite filters.

% Keep the suites every filter (a user filter fun, then crypto's support) accepts, part by part.
filter_cipher_suites(Suites, Filters) ->
    Hashes = crypto:supports(hashs),
    PubKeys = crypto:supports(public_keys),
    Ciphers = crypto:supports(ciphers),
    Parts = [
        {key_exchange, add_filter(proplists:get_value(key_exchange, Filters), fun(K) -> key_exchange(K, PubKeys) end)},
        {cipher, add_filter(proplists:get_value(cipher, Filters), fun(C) -> cipher(C, Ciphers) end)},
        {mac, add_filter(proplists:get_value(mac, Filters), fun(H) -> hash(H, Hashes) end)},
        {prf, add_filter(proplists:get_value(prf, Filters), fun(P) -> prf(P, Hashes) end)}
    ],
    lists:filter(fun(Suite) -> accepted(Suite, Parts) end, Suites).

add_filter(undefined, Crypto) -> [Crypto];
add_filter(User, Crypto) -> [User, Crypto].

accepted(#{key_exchange := _, cipher := _, mac := _, prf := _} = Suite, Parts) ->
    lists:all(fun({Key, Funs}) -> lists:all(fun(F) -> F(maps:get(Key, Suite)) end, Funs) end, Parts).

key_exchange(K, _) when K == psk; K == null; K == any -> true;
key_exchange(K, P) when K == dh_anon; K == dhe_psk -> proplists:get_bool(dh, P);
key_exchange(dhe_dss, P) ->
    proplists:get_bool(dh, P) andalso proplists:get_bool(dss, P);
key_exchange(dhe_rsa, P) ->
    proplists:get_bool(dh, P) andalso proplists:get_bool(rsa, P);
key_exchange(K, P) when K == ecdh_anon; K == ecdhe_psk -> proplists:get_bool(ecdh, P);
key_exchange(K, P) when K == ecdh_ecdsa; K == ecdhe_ecdsa ->
    proplists:get_bool(ecdh, P) andalso proplists:get_bool(ecdsa, P);
key_exchange(K, P) when K == ecdh_rsa; K == ecdhe_rsa -> proplists:get_bool(ecdh, P) andalso proplists:get_bool(rsa, P);
key_exchange(K, P) when K == rsa; K == rsa_psk -> proplists:get_bool(rsa, P);
key_exchange(srp_anon, P) ->
    proplists:get_bool(srp, P);
key_exchange(srp_dss, P) ->
    proplists:get_bool(srp, P) andalso proplists:get_bool(dss, P);
key_exchange(srp_rsa, P) ->
    proplists:get_bool(srp, P) andalso proplists:get_bool(rsa, P);
key_exchange(_, _) ->
    false.

cipher(null, _) -> true;
cipher(rc4_128, C) -> proplists:get_bool(rc4, C);
cipher('3des_ede_cbc', C) -> proplists:get_bool(des_ede3_cbc, C);
cipher(aes_128_ccm_8, C) -> proplists:get_bool(aes_128_ccm, C);
cipher(aes_256_ccm_8, C) -> proplists:get_bool(aes_256_ccm, C);
cipher(Cipher, C) -> proplists:get_bool(Cipher, C).

hash(H, _) when H == null; H == aead -> true;
hash(H, Hashes) -> proplists:get_bool(H, Hashes).

prf(default_prf, _) -> true;
prf(P, Hashes) -> proplists:get_bool(P, Hashes).

%% Signature algorithms.

signature_algs(default, 'tlsv1.3') ->
    algorithms_1_3();
signature_algs(default, 'tlsv1.2') ->
    algorithms_1_2();
signature_algs(all, 'tlsv1.3') ->
    algorithms_1_3() ++ [ecdsa_sha1, rsa_pkcs1_sha1 | legacy_pre_1_3()] -- [{sha, ecdsa}, {sha, rsa}];
signature_algs(all, 'tlsv1.2') ->
    algorithms_1_2() ++ legacy_pre_1_3();
signature_algs(exclusive, 'tlsv1.3') ->
    algorithms_1_3();
signature_algs(exclusive, 'tlsv1.2') ->
    algorithms_1_2() ++ legacy_pre_1_3();
signature_algs(Description, 'dtlsv1.2') ->
    signature_algs(Description, 'tlsv1.2');
signature_algs(Description, Version) when Description == default; Description == all; Description == exclusive ->
    erlang:error({signature_algs_not_supported_in_protocol_version, Version});
signature_algs(Description, Version) ->
    erlang:error(badarg, [Description, Version]).

% TLS 1.3's default schemes, then its legacy ones for certificates.
algorithms_1_3() ->
    [Fastest | SlhDsa] = slh_dsa_schemes(),
    Default =
        [
            mldsa87,
            mldsa65,
            mldsa44,
            Fastest,
            eddsa_ed25519,
            eddsa_ed448,
            ecdsa_secp521r1_sha512,
            ecdsa_secp384r1_sha384,
            ecdsa_secp256r1_sha256,
            ecdsa_brainpoolP512r1tls13_sha512,
            ecdsa_brainpoolP384r1tls13_sha384,
            ecdsa_brainpoolP256r1tls13_sha256,
            rsa_pss_pss_sha512,
            rsa_pss_pss_sha384,
            rsa_pss_pss_sha256,
            rsa_pss_rsae_sha512,
            rsa_pss_rsae_sha384,
            rsa_pss_rsae_sha256
            | SlhDsa
        ],
    supported_schemes(Default) ++ supported_schemes([rsa_pkcs1_sha512, rsa_pkcs1_sha384, rsa_pkcs1_sha256]).

% TLS 1.2's defaults: hash and sign pairs with the RSA-PSS schemes.
algorithms_1_2() ->
    Default =
        [
            {sha512, ecdsa},
            rsa_pss_pss_sha512,
            rsa_pss_rsae_sha512,
            {sha512, rsa},
            {sha384, ecdsa},
            rsa_pss_pss_sha384,
            rsa_pss_rsae_sha384,
            {sha384, rsa},
            {sha256, ecdsa},
            rsa_pss_pss_sha256,
            rsa_pss_rsae_sha256,
            {sha256, rsa}
        ],
    Hashes = crypto:supports(hashs),
    PubKeys = crypto:supports(public_keys),
    Schemes = rsa_schemes(),
    lists:filtermap(fun(Alg) -> algorithm_1_2(Alg, Hashes, PubKeys, Schemes) end, Default).

algorithm_1_2({Hash, Sign0}, Hashes, PubKeys, _Schemes) ->
    proplists:get_bool(sign_key(Sign0), PubKeys) andalso proplists:get_bool(Hash, Hashes) andalso
        pair(Hash, Sign0, Hashes);
algorithm_1_2(Alg, _Hashes, _PubKeys, Schemes) when is_atom(Alg) ->
    case lists:member(Alg, Schemes) of
        true ->
            [Supported] = supported_schemes([Alg]),
            {true, Supported};
        false ->
            false
    end.

legacy_pre_1_3() ->
    [{sha224, ecdsa}, {sha224, rsa}, {sha, ecdsa}, {sha, rsa}, {sha, dsa}].

slh_dsa_schemes() ->
    [
        slh_dsa_sha2_256f,
        slh_dsa_sha2_256s,
        slh_dsa_shake_256f,
        slh_dsa_shake_256s,
        slh_dsa_shake_192f,
        slh_dsa_shake_192s,
        slh_dsa_sha2_192f,
        slh_dsa_sha2_192s,
        slh_dsa_shake_128f,
        slh_dsa_shake_128s,
        slh_dsa_sha2_128f,
        slh_dsa_sha2_128s
    ].

% The RSA-PSS schemes when crypto has PSS padding, salt lengths and MGF1 digests.
rsa_schemes() ->
    Options = crypto:supports(rsa_opts),
    case
        lists:member(rsa_pkcs1_pss_padding, Options) andalso lists:member(rsa_pss_saltlen, Options) andalso
            lists:member(rsa_mgf1_md, Options)
    of
        true ->
            [
                rsa_pss_pss_sha512,
                rsa_pss_pss_sha384,
                rsa_pss_pss_sha256,
                rsa_pss_rsae_sha512,
                rsa_pss_rsae_sha384,
                rsa_pss_rsae_sha256
            ];
        false ->
            []
    end.

% The schemes crypto can sign and verify with.
supported_schemes([]) ->
    [];
supported_schemes(Schemes) ->
    Support = #{
        hashes => crypto:supports(hashs),
        public_keys => crypto:supports(public_keys),
        curves => crypto:supports(curves),
        pss => lists:member(rsa_pkcs1_pss_padding, crypto:supports(rsa_opts))
    },
    lists:filter(fun(Scheme) -> supported_scheme(Scheme, Support) end, Schemes).

supported_scheme(Scheme, #{public_keys := PubKeys} = Support) when is_atom(Scheme) ->
    lists:member(Scheme, PubKeys) orelse supported_components(components(Scheme), Support);
supported_scheme({Hash, Sign0}, #{hashes := Hashes, public_keys := PubKeys}) ->
    lists:member(sign_key(Sign0), PubKeys) andalso proplists:get_bool(Hash, Hashes) andalso pair(Hash, Sign0, Hashes).

supported_components({Hash, Sign0, Curve}, #{hashes := Hashes, public_keys := PubKeys, curves := Curves, pss := Pss}) ->
    Sign = scheme_key(Sign0, Pss),
    (lists:member(Sign, PubKeys) andalso
        (proplists:get_bool(Hash, Hashes) andalso
            (Curve =:= undefined orelse proplists:get_bool(Curve, Curves)) andalso
            pair(Hash, Sign, Hashes))) orelse
        (Sign == eddsa andalso (Curve == ed448 orelse Curve == ed25519)).

scheme_key(rsa_pkcs1, _) -> rsa;
scheme_key(rsa_pss_rsae, true) -> rsa;
scheme_key(rsa_pss_pss, true) -> rsa;
scheme_key(Sign, _) -> Sign.

sign_key(dsa) -> dss;
sign_key(Sign) -> Sign.

% Whether a hash may sign with a key type in TLS 1.2's signature_algorithms.
pair(sha, dsa, _) -> true;
pair(_, dsa, _) -> false;
pair(Hash, Sign, Hashes) when Sign == ecdsa; Sign == rsa -> lists:member(Hash, Hashes -- [md2, md4, md5]);
pair(_, _, _) -> false.

% A TLS 1.3 scheme's hash, signature and curve.
components(rsa_pkcs1_sha256) -> {sha256, rsa_pkcs1, undefined};
components(rsa_pkcs1_sha384) -> {sha384, rsa_pkcs1, undefined};
components(rsa_pkcs1_sha512) -> {sha512, rsa_pkcs1, undefined};
components(ecdsa_secp256r1_sha256) -> {sha256, ecdsa, secp256r1};
components(ecdsa_secp384r1_sha384) -> {sha384, ecdsa, secp384r1};
components(ecdsa_secp521r1_sha512) -> {sha512, ecdsa, secp521r1};
components(ecdsa_brainpoolP256r1tls13_sha256) -> {sha256, ecdsa, brainpoolP256r1};
components(ecdsa_brainpoolP384r1tls13_sha384) -> {sha384, ecdsa, brainpoolP384r1};
components(ecdsa_brainpoolP512r1tls13_sha512) -> {sha512, ecdsa, brainpoolP512r1};
components(rsa_pss_rsae_sha256) -> {sha256, rsa_pss_rsae, undefined};
components(rsa_pss_rsae_sha384) -> {sha384, rsa_pss_rsae, undefined};
components(rsa_pss_rsae_sha512) -> {sha512, rsa_pss_rsae, undefined};
components(eddsa_ed25519) -> {none, eddsa, ed25519};
components(eddsa_ed448) -> {none, eddsa, ed448};
components(rsa_pss_pss_sha256) -> {sha256, rsa_pss_pss, undefined};
components(rsa_pss_pss_sha384) -> {sha384, rsa_pss_pss, undefined};
components(rsa_pss_pss_sha512) -> {sha512, rsa_pss_pss, undefined};
components(rsa_pkcs1_sha1) -> {sha, rsa_pkcs1, undefined};
components(ecdsa_sha1) -> {sha, ecdsa, undefined};
components(Scheme) when Scheme == mldsa44; Scheme == mldsa65; Scheme == mldsa87 -> {none, Scheme, undefined};
components(Scheme) -> {Scheme, slhdsa, undefined}.

%% Curves and groups.

eccs() ->
    supported_curves(default_curves() ++ legacy_curves()).

eccs(dtlsv1) ->
    eccs('tlsv1.1');
eccs('dtlsv1.2') ->
    eccs('tlsv1.2');
eccs(Version) when Version == 'tlsv1.2'; Version == 'tlsv1.1'; Version == tlsv1 ->
    supported_curves(default_curves());
eccs('tlsv1.3') ->
    erlang:error({badarg, not_sup_in, 'tlsv1.3'});
eccs(Other) ->
    erlang:error({badarg, Other}).

default_curves() ->
    [x25519, x448, secp521r1, brainpoolP512r1, secp384r1, brainpoolP384r1, secp256r1, brainpoolP256r1].

legacy_curves() ->
    [
        sect571r1,
        sect571k1,
        sect409k1,
        sect409r1,
        sect283k1,
        sect283r1,
        secp256k1,
        sect239k1,
        sect233k1,
        sect233r1,
        secp224k1,
        secp224r1,
        sect193r1,
        sect193r2,
        secp192k1,
        secp192r1,
        sect163k1,
        sect163r1,
        sect163r2,
        secp160k1,
        secp160r1,
        secp160r2
    ].

supported_curves(Curves) ->
    Crypto = crypto:supports(curves),
    [Curve || Curve <- Curves, lists:member(Curve, Crypto)].

groups() ->
    supported_groups(
        [x25519mlkem768] ++ ec_groups() ++ [secp384r1mlkem1024, secp256r1mlkem768, mlkem1024, mlkem768, mlkem512] ++
            [ffdhe2048, ffdhe3072, ffdhe4096, ffdhe6144, ffdhe8192]
    ).

groups(default) ->
    supported_groups([x25519mlkem768 | ec_groups()]).

ec_groups() ->
    [x25519, x448, secp521r1, secp384r1, secp256r1, brainpoolP512r1tls13, brainpoolP384r1tls13, brainpoolP256r1tls13].

% The groups crypto has the curves and KEMs of (the finite field groups always).
supported_groups(Groups) ->
    Crypto =
        crypto:supports(curves) ++ crypto:supports(kems) ++ [ffdhe2048, ffdhe3072, ffdhe4096, ffdhe6144, ffdhe8192],
    lists:filter(
        fun(Group) -> lists:all(fun(Part) -> proplists:get_bool(Part, Crypto) end, group_parts(Group)) end,
        Groups
    ).

group_parts(x25519mlkem768) -> [mlkem768, x25519];
group_parts(secp256r1mlkem768) -> [mlkem768, secp256r1];
group_parts(secp384r1mlkem1024) -> [mlkem1024, secp384r1];
group_parts(brainpoolP512r1tls13) -> [brainpoolP512r1];
group_parts(brainpoolP384r1tls13) -> [brainpoolP384r1];
group_parts(brainpoolP256r1tls13) -> [brainpoolP256r1];
group_parts(Group) -> [Group].

%% Suite codes (LIB81C1): connections hand libssl suites by their two-byte codes; builtins over the suite table.
suite_code(_Suite) -> clause_builtin_placeholder.
code_suite(_Code) -> clause_builtin_placeholder.

%% Protocol versions.

versions() ->
    Tls = [V || V <- tls_versions(), sufficient_crypto(V)],
    Dtls = [V || V <- dtls_versions(), sufficient_crypto(tls_version(V))],
    [
        {ssl_app, ?VSN},
        {supported, Tls},
        {supported_dtls, Dtls},
        {available, [V || V <- ?AVAILABLE_TLS, sufficient_crypto(V)]},
        {available_dtls, [V || V <- ?AVAILABLE_DTLS, sufficient_crypto(tls_version(V))]},
        {implemented, ?AVAILABLE_TLS},
        {implemented_dtls, ?AVAILABLE_DTLS}
    ].

% The TLS versions the protocol_version environment of ssl configures, as OTP's tls_record reads it; the default
% (crypto permitting) is stored there on first use.
tls_versions() ->
    case application:get_env(ssl, protocol_version) of
        {ok, [_ | _] = Names} -> configured_tls([tls_name(N) || N <- Names]);
        {ok, Name} when Name =/= [] -> configured_tls([tls_name(Name)]);
        _ -> default_tls()
    end.

% A configuration of only sslv2 stores the default but fails, as OTP's tls_record returns it unconverted then.
configured_tls(Names) ->
    case [N || N <- Names, N =/= sslv2] of
        [] ->
            default_tls(),
            erlang:error(function_clause);
        Versions ->
            [V || V <- Versions, sufficient_crypto(V)]
    end.

default_tls() ->
    Versions = [V || V <- ?SUPPORTED_TLS, sufficient_crypto(V)],
    application:set_env(ssl, protocol_version, Versions),
    Versions.

tls_name(Name) when Name == 'tlsv1.3'; Name == 'tlsv1.2'; Name == 'tlsv1.1'; Name == tlsv1 -> Name;
tls_name(Name) when Name == sslv3; Name == sslv2 -> Name.

% The DTLS versions of the dtls_protocol_version environment, as OTP's dtls_record reads it.
dtls_versions() ->
    case application:get_env(ssl, dtls_protocol_version) of
        {ok, [_ | _] = Names} -> configured_dtls([dtls_name(N) || N <- Names]);
        {ok, Name} when Name =/= [] -> configured_dtls([dtls_name(Name)]);
        _ -> default_dtls()
    end.

configured_dtls(Versions) ->
    case {dtls_1_2_crypto(), Versions -- ['dtlsv1.2']} of
        {true, _} -> Versions;
        {false, []} -> [dtlsv1];
        {false, Older} -> Older
    end.

default_dtls() ->
    Versions =
        case dtls_1_2_crypto() of
            true -> ['dtlsv1.2'];
            false -> [dtlsv1]
        end,
    application:set_env(ssl, dtls_protocol_version, Versions),
    Versions.

dtls_name(Name) when Name == 'dtlsv1.2'; Name == dtlsv1 -> Name.

dtls_1_2_crypto() ->
    proplists:get_bool(sha256, crypto:supports(hashs)).

tls_version('dtlsv1.2') -> 'tlsv1.2';
tls_version(dtlsv1) -> 'tlsv1.1'.

% Whether crypto has what a TLS version needs, as OTP's tls_record:sufficient_crypto_support/1.
sufficient_crypto(Version) when Version == tlsv1; Version == 'tlsv1.1' ->
    Hashes = crypto:supports(hashs),
    PubKeys = crypto:supports(public_keys),
    proplists:get_bool(sha, Hashes) andalso proplists:get_bool(md5, Hashes) andalso
        proplists:get_bool(aes_cbc, crypto:supports(ciphers)) andalso signs_and_agrees(PubKeys);
sufficient_crypto('tlsv1.2') ->
    proplists:get_bool(sha256, crypto:supports(hashs)) andalso proplists:get_bool(aes_cbc, crypto:supports(ciphers)) andalso
        signs_and_agrees(crypto:supports(public_keys));
sufficient_crypto('tlsv1.3') ->
    all_supported(ciphers, [aes_gcm]) andalso all_supported(public_keys, [ecdh, rsa, ecdsa]) andalso
        all_supported(hashs, [sha256]) andalso all_supported(curves, [secp256r1]) andalso
        all_supported(rsa_opts, [rsa_pkcs1_padding, rsa_pkcs1_pss_padding]).

signs_and_agrees(PubKeys) ->
    (proplists:get_bool(ecdsa, PubKeys) orelse proplists:get_bool(rsa, PubKeys) orelse
        proplists:get_bool(dss, PubKeys)) andalso
        (proplists:get_bool(ecdh, PubKeys) orelse proplists:get_bool(dh, PubKeys)).

all_supported(Type, Algorithms) ->
    Supported = crypto:supports(Type),
    lists:all(fun(A) -> proplists:get_bool(A, Supported) end, Algorithms).
