%% crypto's ciphers, AEAD ciphers, the crypto_aes and crypto_prng1 rand generators and rand's exro928 helpers they
%% count with, and beam_lib's decryption of debug information (plan 21 step LIB79B): known vectors, round trips,
%% padding, states, OTP's error classes and descriptions, and the probed cipher list checked by what it must hold and
%% by every listed cipher working. File and line of the errors are Clause's own, so only their class and description
%% print.
-module(crypto_ciphers).
-export([main/1]).

main(_) ->
    one_time(),
    padding(),
    streams(),
    information(),
    aead(),
    aead_states(),
    generators(),
    exro928(),
    debug_info(),
    supported(),
    errors(),
    io:format("crypto_ciphers: ok~n").

id(X) -> X.

hex(Bin) -> binary:encode_hex(Bin, lowercase).

%% A state's data as a sorted list.
data(State) -> lists:sort(maps:to_list(crypto:crypto_get_data(State))).

key(16) -> <<16#2b7e151628aed2a6abf7158809cf4f3c:128>>;
key(24) -> <<16#8e73b0f7da0e6452c810f32b809079e562f8ead2522c6b7b:192>>;
key(32) -> <<16#603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4:256>>.

iv() -> <<16#000102030405060708090a0b0c0d0e0f:128>>.

plain() -> <<16#6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51:256>>.

one_time() ->
    Cases = [
        {aes_128_cbc, key(16), iv()},
        {aes_192_cbc, key(24), iv()},
        {aes_256_cbc, key(32), iv()},
        {aes_128_ofb, key(16), iv()},
        {aes_256_ofb, key(32), iv()},
        {aes_128_cfb8, key(16), iv()},
        {aes_128_cfb128, key(16), iv()},
        {aes_192_cfb128, key(24), iv()},
        {aes_128_ctr, key(16), iv()},
        {aes_256_ctr, key(32), iv()},
        {aes_cbc, key(32), iv()},
        {aes_ctr, key(24), iv()},
        {aes_cfb8, key(24), iv()},
        {chacha20, key(32), iv()},
        {des_ede3_cbc, key(24), <<1, 2, 3, 4, 5, 6, 7, 8>>},
        {des_ede3_cfb, key(24), <<1, 2, 3, 4, 5, 6, 7, 8>>},
        {sm4_cbc, key(16), iv()},
        {sm4_ctr, key(16), iv()}
    ],
    [
        begin
            Encrypted = crypto:crypto_one_time(Cipher, Key, IV, plain(), true),
            Decrypted = crypto:crypto_one_time(Cipher, Key, IV, Encrypted, false),
            io:format("one_time ~p ~s ~p~n", [Cipher, hex(Encrypted), Decrypted =:= plain()])
        end
     || {Cipher, Key, IV} <- Cases
    ],
    Ecb = crypto:crypto_one_time(aes_128_ecb, key(16), plain(), true),
    io:format("ecb ~s ~p~n", [hex(Ecb), crypto:crypto_one_time(aes_ecb, key(16), Ecb, false) =:= plain()]),
    <<K1:7/binary, K2/binary>> = key(16),
    Iodata = crypto:crypto_one_time(aes_128_ctr, [K1, [K2]], [iv()], [<<"ab">>, "cd", [$e]], true),
    io:format("iodata ~s~n", [hex(Iodata)]),
    io:format("options ~s~n", [hex(crypto:crypto_one_time(aes_128_ctr, key(16), iv(), <<"abc">>, [{encrypt, true}]))]),
    io:format("decrypt option ~p~n", [
        crypto:crypto_one_time(aes_128_ctr, key(16), iv(), Iodata, [{encrypt, false}, {padding, undefined}])
    ]),
    Long = binary:copy(<<"0123456789">>, 5000),
    LongEncrypted = crypto:crypto_one_time(aes_256_ctr, key(32), iv(), Long, true),
    io:format("long ~b ~s~n", [byte_size(LongEncrypted), hex(crypto:hash(sha256, LongEncrypted))]),
    io:format("empty ~p~n", [crypto:crypto_one_time(aes_128_cbc, key(16), iv(), <<>>, true)]).

padding() ->
    Short = <<"short">>,
    io:format("partial ~p~n", [crypto:crypto_one_time(aes_128_cbc, key(16), iv(), Short, true)]),
    Pkcs = crypto:crypto_one_time(aes_128_cbc, key(16), iv(), Short, [{padding, pkcs_padding}]),
    io:format("pkcs ~s ~p~n", [
        hex(Pkcs), crypto:crypto_one_time(aes_128_cbc, key(16), iv(), Pkcs, [{encrypt, false}, {padding, pkcs_padding}])
    ]),
    Whole = crypto:crypto_one_time(aes_128_cbc, key(16), iv(), binary:copy(<<"x">>, 16), [{padding, pkcs_padding}]),
    io:format("pkcs whole ~b~n", [byte_size(Whole)]),
    Zero = crypto:crypto_one_time(aes_128_cbc, key(16), iv(), Short, [{padding, zero}]),
    io:format("zero ~s ~p~n", [
        hex(Zero), crypto:crypto_one_time(aes_128_cbc, key(16), iv(), Zero, [{encrypt, false}, {padding, none}])
    ]),
    Random = crypto:crypto_one_time(aes_128_cbc, key(16), iv(), Short, [{padding, random}]),
    {RandomPlain, _} = split_binary(
        crypto:crypto_one_time(aes_128_cbc, key(16), iv(), Random, [{encrypt, false}, {padding, none}]), 5
    ),
    io:format("random ~b ~p~n", [byte_size(Random), RandomPlain]),
    io:format("none ~b~n", [
        byte_size(crypto:crypto_one_time(aes_128_cbc, key(16), iv(), binary:copy(<<"y">>, 32), [{padding, none}]))
    ]),
    io:format("stream padding ~p~n", [
        byte_size(crypto:crypto_one_time(aes_128_ctr, key(16), iv(), Short, [{padding, pkcs_padding}]))
    ]),
    [
        begin
            S = crypto:crypto_init(aes_128_cbc, key(16), iv(), [{padding, Padding}]),
            Out = crypto:crypto_update(S, <<"0123456789abcdef0123">>),
            Final = crypto:crypto_final(S),
            io:format("padded ~p ~b ~b ~p~n", [Padding, byte_size(Out), byte_size(Final), data(S)])
        end
     || Padding <- [undefined, pkcs_padding, zero, random]
    ].

streams() ->
    S = crypto:crypto_init(aes_128_ctr, key(16), iv(), true),
    io:format("state ~p ~p~n", [is_reference(S), data(S)]),
    U1 = crypto:crypto_update(S, <<"abc">>),
    U2 = crypto:crypto_update(S, [<<"defg">>, "h"]),
    Final = crypto:crypto_final(S),
    io:format("stream ~s ~s ~p ~p~n", [hex(U1), hex(U2), Final, data(S)]),
    io:format("stream equal ~p~n", [
        <<U1/binary, U2/binary>> =:= crypto:crypto_one_time(aes_128_ctr, key(16), iv(), <<"abcdefgh">>, true)
    ]),
    E = crypto:crypto_init(aes_128_ecb, key(16), true),
    io:format("ecb stream ~s~n", [hex(crypto:crypto_update(E, binary:part(plain(), 0, 16)))]),
    D = crypto:crypto_init(aes_128_ctr, key(16), iv(), [{encrypt, false}]),
    io:format("decrypt stream ~p ~p~n", [crypto:crypto_update(D, <<U1/binary, U2/binary>>), data(D)]),
    Long = binary:copy(<<"0123456789">>, 5000),
    L = crypto:crypto_init(aes_256_ctr, key(32), iv(), true),
    LongOut = crypto:crypto_update(L, Long),
    io:format("long stream ~p ~p~n", [
        LongOut =:= crypto:crypto_one_time(aes_256_ctr, key(32), iv(), Long, true), data(L)
    ]),
    C = crypto:crypto_init(aes_128_cbc, key(16), iv(), true),
    Redirected = crypto:crypto_init(C, key(16), iv(), false),
    io:format("redirect ~p ~p~n", [Redirected =:= C, maps:get(encrypt, crypto:crypto_get_data(C))]).

information() ->
    [
        io:format("cipher_info ~p ~p~n", [Cipher, lists:sort(maps:to_list(crypto:cipher_info(Cipher)))])
     || Cipher <- [
            aes_128_cbc,
            aes_256_ecb,
            aes_128_ctr,
            aes_192_ofb,
            aes_128_cfb8,
            aes_256_cfb128,
            aes_128_gcm,
            aes_256_ccm,
            chacha20,
            chacha20_poly1305,
            des_ede3_cbc,
            sm4_cbc,
            aes_ctr,
            aes_gcm,
            aes_cbc
        ]
    ].

gcm_key() -> <<16#feffe9928665731c6d6a8f9467308308:128>>.

gcm_iv() -> <<16#cafebabefacedbaddecaf888:96>>.

gcm_plain() ->
    <<
        16#d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39:480
    >>.

gcm_aad() -> <<16#feedfacedeadbeeffeedfacedeadbeefabaddad2:160>>.

aead() ->
    {GcmText, GcmTag} = crypto:crypto_one_time_aead(aes_128_gcm, gcm_key(), gcm_iv(), gcm_plain(), gcm_aad(), true),
    io:format("gcm ~s ~s~n", [hex(GcmText), hex(GcmTag)]),
    io:format("gcm decrypt ~p~n", [
        crypto:crypto_one_time_aead(aes_128_gcm, gcm_key(), gcm_iv(), GcmText, gcm_aad(), GcmTag, false) =:= gcm_plain()
    ]),
    <<First, Rest/binary>> = GcmTag,
    BadTag = <<(First bxor 1), Rest/binary>>,
    io:format("gcm bad tag ~p~n", [
        crypto:crypto_one_time_aead(aes_128_gcm, gcm_key(), gcm_iv(), GcmText, gcm_aad(), BadTag, false)
    ]),
    {_, Short} = crypto:crypto_one_time_aead(aes_gcm, gcm_key(), gcm_iv(), gcm_plain(), gcm_aad(), 12, true),
    io:format("gcm alias tag ~s~n", [hex(Short)]),
    io:format("gcm short tag ~p~n", [
        crypto:crypto_one_time_aead(aes_gcm, gcm_key(), gcm_iv(), GcmText, gcm_aad(), Short, false) =:= gcm_plain()
    ]),
    io:format("gcm empty ~p~n", [crypto:crypto_one_time_aead(aes_256_gcm, key(32), gcm_iv(), <<>>, <<>>, true)]),
    {IoText, IoTag} = crypto:crypto_one_time_aead(
        aes_128_gcm, [gcm_key()], [gcm_iv()], ["ab", <<"c">>], [<<"a">>, $d], true
    ),
    io:format("gcm iodata ~s ~s~n", [hex(IoText), hex(IoTag)]),
    Nonce = <<16#101112131415161718191a1b:96>>,
    {CcmText, CcmTag} = crypto:crypto_one_time_aead(aes_128_ccm, key(16), Nonce, <<"ccm text">>, <<"aad">>, true),
    io:format("ccm ~s ~s~n", [hex(CcmText), hex(CcmTag)]),
    io:format("ccm decrypt ~p ~p~n", [
        crypto:crypto_one_time_aead(aes_128_ccm, key(16), Nonce, CcmText, <<"aad">>, CcmTag, false),
        crypto:crypto_one_time_aead(aes_128_ccm, key(16), Nonce, CcmText, <<"bad">>, CcmTag, false)
    ]),
    {_, Ccm8} = crypto:crypto_one_time_aead(aes_ccm, key(24), Nonce, <<"ccm text">>, <<>>, 8, true),
    io:format("ccm 8 ~s~n", [hex(Ccm8)]),
    ChachaKey = list_to_binary(lists:seq(16#80, 16#9f)),
    ChachaNonce = <<16#070000004041424344454647:96>>,
    ChachaAad = <<16#50515253c0c1c2c3c4c5c6c7:96>>,
    Sunscreen =
        <<"Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, ",
            "sunscreen would be it.">>,
    {ChachaText, ChachaTag} = crypto:crypto_one_time_aead(
        chacha20_poly1305, ChachaKey, ChachaNonce, Sunscreen, ChachaAad, true
    ),
    io:format("chacha20_poly1305 ~s ~s~n", [hex(ChachaText), hex(ChachaTag)]),
    io:format("chacha20_poly1305 decrypt ~p~n", [
        crypto:crypto_one_time_aead(chacha20_poly1305, ChachaKey, ChachaNonce, ChachaText, ChachaAad, ChachaTag, false) =:=
            Sunscreen
    ]).

aead_states() ->
    E = crypto:crypto_one_time_aead_init(aes_128_gcm, gcm_key(), 16, true),
    Out = crypto:crypto_one_time_aead(E, gcm_iv(), gcm_plain(), gcm_aad()),
    io:format("aead state ~p ~b ~p~n", [
        is_reference(E), byte_size(Out), Out =:= crypto:crypto_one_time_aead(E, gcm_iv(), gcm_plain(), gcm_aad())
    ]),
    {Text, Tag} = crypto:crypto_one_time_aead(aes_128_gcm, gcm_key(), gcm_iv(), gcm_plain(), gcm_aad(), true),
    io:format("aead state equal ~p~n", [Out =:= <<Text/binary, Tag/binary>>]),
    D = crypto:crypto_one_time_aead_init(aes_gcm, [gcm_key()], 16, false),
    io:format("aead state decrypt ~p ~p~n", [
        crypto:crypto_one_time_aead(D, gcm_iv(), Out, gcm_aad()) =:= gcm_plain(),
        crypto:crypto_one_time_aead(D, gcm_iv(), Out, <<"other">>)
    ]),
    Nonce = <<16#101112131415161718191a1b:96>>,
    CE = crypto:crypto_one_time_aead_init(aes_128_ccm, key(16), 12, true),
    CcmOut = crypto:crypto_one_time_aead(CE, Nonce, <<"ccm text">>, <<"aad">>),
    CD = crypto:crypto_one_time_aead_init(aes_128_ccm, key(16), 12, false),
    io:format("ccm state ~s ~p~n", [hex(CcmOut), crypto:crypto_one_time_aead(CD, Nonce, CcmOut, <<"aad">>)]),
    ChachaKey = list_to_binary(lists:seq(16#80, 16#9f)),
    CC = crypto:crypto_one_time_aead_init(chacha20_poly1305, ChachaKey, 16, true),
    io:format("chacha state ~s~n", [hex(crypto:crypto_one_time_aead(CC, gcm_iv(), <<"chacha">>, <<>>))]).

%% The first numbers of a rand state.
numbers(State, N) ->
    lists:mapfoldl(fun(_, S) -> rand:uniform_s(1000000, S) end, State, lists:seq(1, N)).

generators() ->
    A0 = crypto:rand_seed_alg_s(crypto_aes, "my seed"),
    {AesNumbers, A1} = numbers(A0, 6),
    io:format("crypto_aes ~p~n", [AesNumbers]),
    {Float, _} = rand:uniform_s(A1),
    io:format("crypto_aes float ~p~n", [Float]),
    {AesJumped, _} = numbers(rand:jump(A1), 3),
    io:format("crypto_aes jump ~p~n", [AesJumped]),
    {AesFresh, _} = numbers(rand:jump(A0), 3),
    io:format("crypto_aes jump fresh ~p~n", [AesFresh]),
    {Handler, Cache} = A1,
    {AesShort, _} = numbers({Handler, crypto:rand_plugin_aes_jump_2pow20(Cache)}, 3),
    io:format("crypto_aes jump 2pow20 ~p~n", [AesShort]),
    {AesAgain, _} = numbers(crypto:rand_seed_alg_s(crypto_aes, <<"my seed">>), 2),
    io:format("crypto_aes again ~p~n", [AesAgain]),
    io:format("crypto_aes handler ~p~n", [lists:sort(maps:keys(Handler))]),
    P0 = crypto:rand_seed_alg_s(crypto_prng1, "my seed"),
    {PrngNumbers, P1} = numbers(P0, 6),
    io:format("crypto_prng1 ~p~n", [PrngNumbers]),
    {Bytes, _} = rand:bytes_s(20, P1),
    {FreshBytes, _} = rand:bytes_s(300, P0),
    io:format("crypto_prng1 bytes ~s ~s~n", [hex(Bytes), hex(crypto:hash(sha256, FreshBytes))]),
    {PrngJumped, _} = numbers(rand:jump(P0), 3),
    io:format("crypto_prng1 jump ~p~n", [PrngJumped]),
    Exported = rand:export_seed_s(P0),
    {Imported, _} = numbers(crypto:rand_seed_alg_s(Exported), 6),
    io:format("crypto_prng1 export ~p~n", [Imported =:= PrngNumbers]),
    {PrngHandler, {PrngAlg, ZeroData, Key, IV}} = P0,
    io:format("crypto_prng1 state ~p ~b ~b ~b~n", [PrngAlg, byte_size(ZeroData), byte_size(Key), byte_size(IV)]),
    io:format("crypto_prng1 jump used ~p~n", [caught(fun() -> rand:jump(P1) end)]),
    io:format("crypto_prng1 iv ~p~n", [
        [
            element(4, element(2, rand:jump({PrngHandler, {PrngAlg, ZeroData, Key, IVIn}})))
         || IVIn <- [<<1, 2>>, <<255, 7>>, <<255, 255>>]
        ]
    ]).

exro928() ->
    S0 = rand:exro928_seed(42),
    {Words, S1} = rand:exro928_next(S0),
    io:format("exro928_next ~p ~p~n", [Words, erlang:phash2(S1)]),
    io:format("exro928_seed ~p ~p ~p~n", [
        erlang:phash2(S0), erlang:phash2(rand:exro928_seed([1, 2, 3])), erlang:phash2(rand:exro928_seed({1, 2, 3}))
    ]),
    io:format("exro928_next_state ~p~n", [erlang:phash2(rand:exro928_next_state(S0))]),
    Wrapped = lists:foldl(fun(_, S) -> rand:exro928_next_state(S) end, S0, lists:seq(1, 20)),
    io:format("exro928 wrapped ~p ~p~n", [element(1, rand:exro928_next(Wrapped)), erlang:phash2(Wrapped)]),
    io:format("exro928 jumps ~p ~p~n", [
        erlang:phash2(rand:exro928_jump_2pow512(S1)), erlang:phash2(rand:exro928_jump_2pow20(S1))
    ]),
    io:format("seed58 ~p ~p~n", [rand:seed58(3, 1), rand:seed58(0, x)]).

atoms(Atoms) ->
    [<<(-length(Atoms)):32/signed>> | [[<<(byte_size(A)):4, 0:4>>, A] || A <- Atoms]].

%% A beam of debug information encrypted as the compiler encrypts it with the key string "secret".
encrypted_beam() ->
    Term = {debug_info_v1, erl_abstract_code, {[{attribute, 1, module, enc_mod}], [debug_info]}},
    {des3_cbc, Key, IVec, 8} = beam_lib:make_crypto_key(des3_cbc, "secret"),
    Bin0 = term_to_binary(Term),
    Bin = <<Bin0/binary, 0:(((8 - byte_size(Bin0) rem 8) rem 8) * 8)>>,
    Encrypted = crypto:crypto_one_time(des_ede3_cbc, Key, IVec, Bin, true),
    Dbgi = iolist_to_binary([0, 8, "des3_cbc", Encrypted]),
    {ok, Beam} = beam_lib:build_module([{"AtU8", iolist_to_binary(atoms([<<"enc_mod">>]))}, {"Dbgi", Dbgi}]),
    Beam.

debug_info() ->
    Beam = encrypted_beam(),
    KeyFun = fun(KeyString) ->
        fun
            (init) -> ok;
            ({debug_info, des3_cbc, enc_mod, _File}) -> KeyString;
            (clear) -> ok
        end
    end,
    ok = beam_lib:crypto_key_fun(KeyFun("secret")),
    io:format("decrypted ~p~n", [beam_lib:chunks(Beam, [debug_info])]),
    beam_lib:clear_crypto_key_fun(),
    ok = beam_lib:crypto_key_fun(KeyFun("wrong")),
    io:format("wrong key ~p~n", [beam_lib:chunks(Beam, [debug_info])]),
    beam_lib:clear_crypto_key_fun().

%% Encrypt and decrypt one text with a listed cipher.
works(Cipher) ->
    Info = crypto:cipher_info(Cipher),
    #{key_length := KeyLength, iv_length := IVLength, block_size := BlockSize, prop_aead := Aead} = Info,
    Key = binary:copy(<<7>>, KeyLength),
    Text = binary:copy(<<"t">>, 2 * BlockSize),
    case Aead of
        true ->
            IV = binary:copy(<<9>>, 12),
            {Out, Tag} = crypto:crypto_one_time_aead(Cipher, Key, IV, Text, <<"a">>, true),
            Text =:= crypto:crypto_one_time_aead(Cipher, Key, IV, Out, <<"a">>, Tag, false);
        false ->
            IV = binary:copy(<<9>>, IVLength),
            Out = crypto:crypto_one_time(Cipher, Key, IV, Text, true),
            Text =:= crypto:crypto_one_time(Cipher, Key, IV, Out, false)
    end.

supported() ->
    Ciphers = crypto:supports(ciphers),
    Required = [
        aes_128_cbc,
        aes_256_cbc,
        aes_128_ctr,
        aes_256_ctr,
        aes_128_ecb,
        aes_128_gcm,
        aes_256_gcm,
        aes_128_ccm,
        chacha20,
        chacha20_poly1305,
        des_ede3_cbc,
        aes_cbc,
        aes_gcm
    ],
    io:format("required ciphers ~p~n", [Required -- Ciphers]),
    io:format("every cipher works ~p~n", [[C || C <- Ciphers, not works(C)]]).

%% The class and description of an OTP crypto error, or the whole error otherwise.
caught(Fun) ->
    try Fun() of
        Value -> {value, Value}
    catch
        error:{Class, {File, Line}, Description} when is_list(File), is_integer(Line) -> {Class, Description};
        Class:Reason -> {Class, Reason}
    end.

errors() ->
    K = key(16),
    IV = iv(),
    State = crypto:crypto_init(aes_128_ctr, K, IV, true),
    Aead = crypto:crypto_one_time_aead_init(aes_128_gcm, K, 16, false),
    Cases = [
        {unknown_cipher, fun() -> crypto:crypto_one_time(id(foo), K, IV, <<>>, true) end},
        {key_size, fun() -> crypto:crypto_one_time(aes_128_cbc, id(<<1, 2, 3>>), IV, <<>>, true) end},
        {empty_key, fun() -> crypto:crypto_one_time(aes_128_cbc, id(<<>>), IV, <<>>, true) end},
        {cipher_type, fun() -> crypto:crypto_one_time(id("aes"), K, IV, <<>>, true) end},
        {aead_one_time, fun() -> crypto:crypto_one_time(chacha20_poly1305, key(32), IV, <<>>, true) end},
        {iv_size, fun() -> crypto:crypto_one_time(aes_128_cbc, K, id(<<1:64>>), <<>>, true) end},
        {iv_type, fun() -> crypto:crypto_one_time(aes_128_cbc, K, id(iv), <<>>, true) end},
        {dynamic_iv, fun() -> crypto:crypto_one_time(aes_128_cbc, K, id(undefined), <<>>, true) end},
        {key_type, fun() -> crypto:crypto_one_time(aes_128_cbc, id(key), IV, <<>>, true) end},
        {data_type, fun() -> crypto:crypto_one_time(aes_128_cbc, K, IV, id(data), true) end},
        {options, fun() -> crypto:crypto_one_time(aes_128_cbc, K, IV, <<>>, id(perhaps)) end},
        {improper, fun() -> crypto:crypto_one_time(aes_128_cbc, K, IV, <<>>, id([{encrypt, true} | x])) end},
        {option_element, fun() -> crypto:crypto_one_time(aes_128_cbc, K, IV, <<>>, id([foo])) end},
        {encrypt_twice, fun() ->
            crypto:crypto_one_time(aes_128_cbc, K, IV, <<>>, id([{encrypt, true}, {encrypt, false}]))
        end},
        {encrypt_value, fun() -> crypto:crypto_one_time(aes_128_cbc, K, IV, <<>>, id([{encrypt, 1}])) end},
        {padding_twice, fun() ->
            crypto:crypto_one_time(aes_128_cbc, K, IV, <<>>, id([{padding, none}, {padding, zero}]))
        end},
        {padding_value, fun() -> crypto:crypto_one_time(aes_128_cbc, K, IV, <<>>, id([{padding, pkcs7}])) end},
        {option_tag, fun() -> crypto:crypto_one_time(aes_128_cbc, K, IV, <<>>, id([{foo, 1}])) end},
        {option_term, fun() -> crypto:crypto_one_time(aes_128_cbc, K, IV, <<>>, id([{{a, "b", 3}, 1}])) end},
        {option_long, fun() -> crypto:crypto_one_time(aes_128_cbc, K, IV, <<>>, id([{lists:seq(1, 40), 1}])) end},
        {unfilled, fun() -> crypto:crypto_one_time(aes_128_cbc, K, IV, <<"abc">>, [{padding, none}]) end},
        {bad_pkcs, fun() ->
            crypto:crypto_one_time(aes_128_cbc, K, IV, binary:copy(<<1>>, 16), [
                {encrypt, false}, {padding, pkcs_padding}
            ])
        end},
        {init_cipher, fun() -> crypto:crypto_init(id("aes"), K, IV, true) end},
        {init_unknown, fun() -> crypto:crypto_init(id(foo), K, IV, true) end},
        {init_flag, fun() -> crypto:crypto_init(State, K, IV, id(perhaps)) end},
        {init_options, fun() -> crypto:crypto_init(aes_128_ecb, K, id(perhaps)) end},
        {update_state, fun() -> crypto:crypto_update(id(make_ref()), <<>>) end},
        {update_data, fun() -> crypto:crypto_update(State, id(data)) end},
        {update_both, fun() -> crypto:crypto_update(id(state), id(data)) end},
        {final_state, fun() -> crypto:crypto_final(id(state)) end},
        {get_data_state, fun() -> crypto:crypto_get_data(id(Aead)) end},
        {cipher_info, fun() -> crypto:cipher_info(id(foo)) end},
        {aead_not, fun() -> crypto:crypto_one_time_aead(aes_128_cbc, K, IV, <<>>, <<>>, true) end},
        {aead_decrypt6, fun() -> crypto:crypto_one_time_aead(aes_128_gcm, K, IV, <<>>, <<>>, id(false)) end},
        {aead_not7, fun() -> crypto:crypto_one_time_aead(aes_128_cbc, K, IV, <<>>, <<>>, 16, true) end},
        {aead_unknown, fun() -> crypto:crypto_one_time_aead(id(foo), K, IV, <<>>, <<>>, 16, true) end},
        {aead_key_size, fun() -> crypto:crypto_one_time_aead(aes_128_gcm, id(<<1:72>>), IV, <<>>, <<>>, 16, true) end},
        {aead_flag, fun() -> crypto:crypto_one_time_aead(aes_128_gcm, K, IV, <<>>, <<>>, 16, id(perhaps)) end},
        {aead_type, fun() -> crypto:crypto_one_time_aead(id("gcm"), K, IV, <<>>, <<>>, 16, true) end},
        {aead_key, fun() -> crypto:crypto_one_time_aead(aes_128_gcm, id(key), IV, <<>>, <<>>, 16, true) end},
        {aead_iv, fun() -> crypto:crypto_one_time_aead(aes_128_gcm, K, id(iv), <<>>, <<>>, 16, true) end},
        {aead_text, fun() -> crypto:crypto_one_time_aead(aes_128_gcm, K, IV, id(text), <<>>, 16, true) end},
        {aead_aad, fun() -> crypto:crypto_one_time_aead(aes_128_gcm, K, IV, <<>>, id(aad), 16, true) end},
        {aead_tag_length, fun() -> crypto:crypto_one_time_aead(aes_128_gcm, K, IV, <<>>, <<>>, id(-1), true) end},
        {aead_tag, fun() -> crypto:crypto_one_time_aead(aes_128_gcm, K, IV, <<>>, <<>>, id(tag), false) end},
        {aead_iv_length, fun() -> crypto:crypto_one_time_aead(aes_128_gcm, K, id(<<>>), <<"x">>, <<>>, 16, true) end},
        {aead_long_tag, fun() -> crypto:crypto_one_time_aead(aes_128_gcm, K, IV, <<"x">>, <<>>, id(17), true) end},
        {ccm_odd_tag, fun() ->
            crypto:crypto_one_time_aead(aes_128_ccm, K, binary:copy(<<1>>, 12), <<"x">>, <<>>, id(5), true)
        end},
        {ccm_iv_length, fun() ->
            crypto:crypto_one_time_aead(aes_128_ccm, K, id(<<1:160>>), <<"x">>, <<>>, 12, true)
        end},
        {aead_init_length, fun() -> crypto:crypto_one_time_aead_init(aes_128_gcm, K, id(sixteen), true) end},
        {aead_init_flag, fun() -> crypto:crypto_one_time_aead_init(aes_128_gcm, K, 16, id(perhaps)) end},
        {aead_init_not, fun() -> crypto:crypto_one_time_aead_init(aes_128_cbc, K, 16, true) end},
        {aead_init_key, fun() -> crypto:crypto_one_time_aead_init(aes_128_gcm, id(key), 16, true) end},
        {aead_init_type, fun() -> crypto:crypto_one_time_aead_init(id("gcm"), K, 16, true) end},
        {aead_state, fun() -> crypto:crypto_one_time_aead(id(State), IV, <<>>, <<>>) end},
        {aead_state_short, fun() -> crypto:crypto_one_time_aead(Aead, IV, <<"short">>, <<>>) end},
        {aead_state_iv, fun() -> crypto:crypto_one_time_aead(Aead, id(iv), <<>>, <<>>) end},
        {aes_no_seed, fun() -> crypto:rand_seed_alg_s(crypto_aes) end},
        {exro928_state, fun() -> rand:exro928_next(id(state)) end}
    ],
    [io:format("error ~p ~p~n", [Name, caught(Fun)]) || {Name, Fun} <- Cases],
    ok.
