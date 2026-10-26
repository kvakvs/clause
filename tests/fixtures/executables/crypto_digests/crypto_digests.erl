%% crypto's digests, MACs, PBKDF2, strong random numbers and helpers (plan 21 step LIB79A): known vectors, OTP's
%% error classes and descriptions, and the probed algorithm lists checked by what they must hold and by every listed
%% algorithm working. File and line of the errors are Clause's own, so only their class and description print.
-module(crypto_digests).
-export([main/1]).
-compile(nowarn_deprecated_function).

main(_) ->
    hashes(),
    streams(),
    macs(),
    derivation(),
    helpers(),
    random(),
    information(),
    errors(),
    io:format("crypto_digests: ok~n").

id(X) -> X.

hex(Bin) -> binary:encode_hex(Bin, lowercase).

hashes() ->
    Names = [
        sha,
        sha224,
        sha256,
        sha384,
        sha512,
        sha512_224,
        sha512_256,
        sha3_224,
        sha3_256,
        sha3_384,
        sha3_512,
        blake2b,
        blake2s,
        md5,
        ripemd160,
        sm3,
        shake128,
        shake256
    ],
    [io:format("hash ~p ~s~n", [Name, hex(crypto:hash(Name, <<"abc">>))]) || Name <- Names],
    io:format("hash iodata ~s~n", [hex(crypto:hash(sha256, [$a, [<<"b">>], $c]))]),
    io:format("hash empty ~s~n", [hex(crypto:hash(sha, <<>>))]),
    [
        io:format("hash_info ~p ~p~n", [Name, lists:sort(maps:to_list(crypto:hash_info(Name)))])
     || Name <- [sha, sha256, sha512, sha3_256, md5, blake2b]
    ],
    io:format("xof ~s ~s~n", [hex(crypto:hash_xof(shake128, <<"abc">>, 256)), hex(crypto:hash_xof(shake256, <<>>, 64))]).

streams() ->
    Long = binary:copy(<<"0123456789">>, 5000),
    io:format("long ~s~n", [hex(crypto:hash(sha256, Long))]),
    S0 = crypto:hash_init(sha256),
    S1 = crypto:hash_update(S0, <<"ab">>),
    S2 = crypto:hash_update(S1, [<<"c">>]),
    io:format("stream ~s~n", [hex(crypto:hash_final(S2))]),
    % Earlier states stay usable: hash_update returns a new state.
    io:format("branch ~s ~s~n", [hex(crypto:hash_final(S1)), hex(crypto:hash_final(crypto:hash_update(S1, <<"x">>)))]),
    io:format("final twice ~p~n", [crypto:hash_final(S2) =:= crypto:hash_final(S2)]),
    io:format("long stream ~s~n", [hex(crypto:hash_final(crypto:hash_update(crypto:hash_init(sha512), Long)))]),
    X = crypto:hash_update(crypto:hash_init(shake256), <<"abc">>),
    io:format("xof stream ~s ~s~n", [hex(crypto:hash_final(X)), hex(crypto:hash_final_xof(X, 128))]),
    io:format("state ~p~n", [is_reference(S0)]).

macs() ->
    Key = <<"key">>,
    Data = <<"The quick brown fox jumps over the lazy dog">>,
    [
        io:format("hmac ~p ~s~n", [Digest, hex(crypto:mac(hmac, Digest, Key, Data))])
     || Digest <- [sha, sha224, sha256, sha384, sha512, sha3_256, md5]
    ],
    AesKey = <<16#2b7e151628aed2a6abf7158809cf4f3c:128>>,
    io:format("cmac ~s~n", [hex(crypto:mac(cmac, aes_128_cbc, AesKey, Data))]),
    io:format("cmac alias ~s~n", [hex(crypto:mac(cmac, aes_cbc, AesKey, Data))]),
    io:format("cmac 3des ~s~n", [hex(crypto:mac(cmac, des_ede3_cbc, binary:copy(<<1, 2, 3, 4, 5, 6, 7, 8>>, 3), Data))]),
    PolyKey = binary:copy(<<7>>, 32),
    io:format("poly1305 ~s ~s~n", [
        hex(crypto:mac(poly1305, PolyKey, Data)), hex(crypto:mac(poly1305, undefined, PolyKey, Data))
    ]),
    io:format("macN ~s ~s~n", [
        hex(crypto:macN(hmac, sha256, Key, Data, 8)), hex(crypto:macN(poly1305, PolyKey, Data, 4))
    ]),
    io:format("macN long ~p~n", [byte_size(crypto:macN(hmac, sha, Key, Data, 100))]),
    M0 = crypto:mac_init(hmac, sha256, Key),
    M1 = crypto:mac_update(M0, <<"The quick brown fox ">>),
    M2 = crypto:mac_update(M1, [<<"jumps over">>, " the lazy dog"]),
    io:format("mac stream ~s ~p~n", [hex(crypto:mac_final(M2)), M0 =:= M2]),
    io:format("mac_finalN ~s~n", [
        hex(crypto:mac_finalN(crypto:mac_update(crypto:mac_init(cmac, aes_128_cbc, AesKey), Data), 6))
    ]),
    io:format("poly stream ~s~n", [hex(crypto:mac_final(crypto:mac_update(crypto:mac_init(poly1305, PolyKey), Data)))]),
    Long = binary:copy(<<"abcdefghij">>, 4500),
    io:format("mac long ~s~n", [hex(crypto:mac(hmac, sha256, Key, Long))]),
    io:format("mac long stream ~s~n", [
        hex(crypto:mac_final(crypto:mac_update(crypto:mac_init(hmac, sha256, Key), Long)))
    ]).

derivation() ->
    io:format("pbkdf2 ~s~n", [hex(crypto:pbkdf2_hmac(sha256, <<"password">>, <<"salt">>, 1, 16))]),
    io:format("pbkdf2 sha ~s~n", [hex(crypto:pbkdf2_hmac(sha, <<"password">>, <<"salt">>, 4096, 20))]),
    io:format("pbkdf2 sha512 ~s~n", [hex(crypto:pbkdf2_hmac(sha512, <<"pass">>, <<"NaCl">>, 80, 64))]),
    io:format("pbkdf2 long ~p~n", [byte_size(crypto:pbkdf2_hmac(sha384, <<"p">>, <<"s">>, 2, 100))]).

helpers() ->
    io:format("exor ~w ~w~n", [crypto:exor(<<1, 2, 3>>, <<3, 2, 1>>), crypto:exor([1, <<2>>], <<255, 255>>)]),
    Long = binary:copy(<<16#5a>>, 25000),
    io:format("exor long ~p~n", [crypto:exor(Long, Long) =:= binary:copy(<<0>>, 25000)]),
    io:format(
        "mod_pow ~p ~p ~p ~p~n",
        [
            crypto:mod_pow(2, 10, 17),
            crypto:mod_pow(5, 0, 23),
            crypto:mod_pow(<<3>>, <<200>>, <<1, 0, 1>>),
            crypto:mod_pow(4, 2, 16)
        ]
    ),
    io:format("mod_pow big ~p~n", [
        crypto:mod_pow(16#ffffffffffffffffffff, 3, 16#10000000000000000000000000000000000000001)
    ]),
    io:format("bytes_to_integer ~p ~p ~p~n", [
        crypto:bytes_to_integer(<<1, 0>>), crypto:bytes_to_integer(<<255>>), crypto:bytes_to_integer(<<>>)
    ]),
    io:format("hash_equals ~p ~p~n", [crypto:hash_equals(<<1, 2>>, <<1, 2>>), crypto:hash_equals(<<1, 2>>, <<1, 3>>)]).

random() ->
    Bytes = crypto:strong_rand_bytes(37),
    io:format("strong_rand_bytes ~p ~p~n", [byte_size(Bytes), byte_size(crypto:strong_rand_bytes(0))]),
    Ranges = [crypto:strong_rand_range(10) || _ <- lists:seq(1, 200)],
    io:format("strong_rand_range ~p~n", [lists:all(fun(N) -> N >= 0 andalso N < 10 end, Ranges)]),
    Big = crypto:strong_rand_range(1 bsl 200),
    io:format("strong_rand_range big ~p ~p~n", [is_integer(Big), Big < 1 bsl 200]),
    io:format("strong_rand_range binary ~p~n", [is_binary(crypto:strong_rand_range(<<1, 0>>))]),
    io:format("rand_uniform ~p~n", [
        lists:all(fun(_) -> lists:member(crypto:rand_uniform(-3, 4), lists:seq(-3, 3)) end, lists:seq(1, 50))
    ]),
    io:format("rand_seed ~p~n", [crypto:rand_seed(<<"more entropy">>)]),
    S0 = crypto:rand_seed_s(),
    {F, S1} = rand:uniform_s(S0),
    {I, S2} = rand:uniform_s(100, S1),
    {B, _} = rand:bytes_s(11, S2),
    io:format("plugin ~p ~p ~p ~p~n", [
        is_float(F) andalso F >= 0.0 andalso F < 1.0, I >= 1 andalso I =< 100, byte_size(B), element(2, S0)
    ]),
    C0 = crypto:rand_seed_alg_s(crypto_cache),
    {Ints, _} = lists:mapfoldl(fun(_, S) -> rand:uniform_s(1000, S) end, C0, lists:seq(1, 300)),
    io:format("cache ~p ~p~n", [lists:all(fun(N) -> N >= 1 andalso N =< 1000 end, Ints), maps:get(type, element(1, C0))]),
    {CB, _} = rand:bytes_s(5, C0),
    io:format("cache bytes ~p~n", [byte_size(CB)]),
    _ = crypto:rand_seed(),
    io:format("seeded ~p~n", [is_integer(rand:uniform(10))]),
    io:format("export ~p~n", [element(1, rand:export_seed_s(crypto:rand_seed_alg_s(crypto)))]).

information() ->
    Hashes = crypto:supports(hashs),
    Macs = crypto:supports(macs),
    Ciphers = crypto:supports(ciphers),
    Keys = crypto:supports(public_keys),
    io:format("required hashes ~p~n", [
        [H || H <- [sha, sha256, sha512, sha3_256, md5, blake2b, shake128], not lists:member(H, Hashes)]
    ]),
    io:format("every hash works ~p~n", [lists:all(fun(H) -> is_binary(crypto:hash(H, <<"x">>)) end, Hashes)]),
    io:format("macs ~p~n", [Macs]),
    io:format("required ciphers ~p~n", [
        [C || C <- [aes_128_cbc, aes_256_gcm, chacha20_poly1305, aes_cbc, aes_gcm], not lists:member(C, Ciphers)]
    ]),
    io:format("required keys ~p~n", [
        [K || K <- [rsa, dss, dh, ecdsa, ecdh, eddsa, eddh, srp], not lists:member(K, Keys)]
    ]),
    io:format("curves ~p~n", [
        [C || C <- [secp256r1, prime256v1, secp384r1, ed25519, x25519], not lists:member(C, crypto:supports(curves))]
    ]),
    io:format("rsa_opts ~p~n", [crypto:supports(rsa_opts)]),
    io:format("supports keys ~p~n", [[K || {K, _} <- crypto:supports()]]),
    [{Name, Number, Text}] = crypto:info_lib(),
    io:format("info_lib ~p ~p ~p~n", [Name, is_integer(Number), is_binary(Text)]),
    Info = crypto:info(),
    io:format("info ~p ~p~n", [lists:sort(maps:keys(Info)), maps:get(otp_crypto_version, Info)]),
    io:format("fips ~p ~p ~p~n", [crypto:info_fips(), crypto:enable_fips_mode(true), crypto:enable_fips_mode(false)]),
    io:format("packed ~p~n", [crypto:packed_openssl_version(3, 0, 0, a)]),
    io:format("start ~p~n", [application:start(crypto)]).

%% The class and description of an OTP crypto error, or the whole error otherwise.
caught(Fun) ->
    try Fun() of
        Value -> {value, Value}
    catch
        error:{Class, {File, Line}, Description} when is_list(File), is_integer(Line) -> {Class, Description};
        Class:Reason -> {Class, Reason}
    end.

errors() ->
    Cases = [
        {hash_type, fun() -> crypto:hash(id(foo), <<>>) end},
        {hash_data, fun() -> crypto:hash(sha, id(atom)) end},
        {hash_init, fun() -> crypto:hash_init(id(sha999)) end},
        {hash_update, fun() -> crypto:hash_update(id(make_ref()), <<>>) end},
        {hash_final, fun() -> crypto:hash_final(id(state)) end},
        {hash_info, fun() -> crypto:hash_info(id(foo)) end},
        {mac_type, fun() -> crypto:mac(id(foo), sha, <<>>, <<>>) end},
        {mac_digest, fun() -> crypto:mac(hmac, id(foo), <<>>, <<>>) end},
        {mac_key, fun() -> crypto:mac(poly1305, id(<<"short">>), <<>>) end},
        {mac_cipher, fun() -> crypto:mac(cmac, id(foo), <<1:128>>, <<>>) end},
        {mac_cipher_key, fun() -> crypto:mac(cmac, aes_128_cbc, id(<<1:64>>), <<>>) end},
        {mac_text, fun() -> crypto:mac(hmac, sha, <<"k">>, id(foo)) end},
        {mac_init_type, fun() -> crypto:mac_init(id(foo), sha, <<>>) end},
        {mac_update_ref, fun() -> crypto:mac_update(id(make_ref()), <<>>) end},
        {mac_final_ref, fun() -> crypto:mac_final(id(ref)) end},
        {pbkdf2_digest, fun() -> crypto:pbkdf2_hmac(id(md5), <<"p">>, <<"s">>, 1, 16) end},
        {pbkdf2_unknown, fun() -> crypto:pbkdf2_hmac(id(foo), <<"p">>, <<"s">>, 1, 16) end},
        {pbkdf2_iterations, fun() -> crypto:pbkdf2_hmac(sha, <<"p">>, <<"s">>, id(0), 16) end},
        {pbkdf2_negative, fun() -> crypto:pbkdf2_hmac(sha, <<"p">>, <<"s">>, id(-1), 16) end},
        {pbkdf2_length, fun() -> crypto:pbkdf2_hmac(sha, <<"p">>, <<"s">>, 1, id(0)) end},
        {pbkdf2_password, fun() -> crypto:pbkdf2_hmac(sha, id("p"), <<"s">>, 1, 16) end},
        {hash_equals, fun() -> crypto:hash_equals(<<1>>, id(<<1, 2>>)) end},
        {exor, fun() -> crypto:exor(<<1>>, id(<<1, 2>>)) end},
        {mod_pow_zero, fun() -> crypto:mod_pow(2, 3, id(0)) end},
        {strong_rand_bytes, fun() -> crypto:strong_rand_bytes(id(-1)) end},
        {strong_rand_range, fun() -> crypto:strong_rand_range(id(0)) end},
        {strong_rand_range_bin, fun() -> crypto:strong_rand_range(id(<<0>>)) end},
        {rand_uniform, fun() -> crypto:rand_uniform(5, id(5)) end},
        {fips, fun() -> crypto:enable_fips_mode(id(perhaps)) end},
        {engine_list, fun() -> crypto:engine_list() end},
        {engine_methods, fun() -> crypto:engine_get_all_methods() end},
        {engine_by_id, fun() -> crypto:engine_by_id(<<"dynamic">>) end},
        {bitmask, fun() -> crypto:engine_methods_convert_to_bitmask([engine_method_rsa, engine_method_ec], 0) end}
    ],
    [io:format("error ~p ~p~n", [Name, caught(Fun)]) || {Name, Fun} <- Cases],
    ok.
