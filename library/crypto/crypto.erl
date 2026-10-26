%% Clause's replacement for OTP's crypto (docs/library.md#runtime-module-replacements, plan 21 step LIB79), written
%% from OTP's documentation over the clause_crypto builtins of the crypto runtime archive, which a program links when
%% it calls crypto (docs/builtins.md#crypto-module). The builtins raise OTP's NIF errors; ?nif_call turns them into
%% OTP's {Class, {File, Line}, Description} errors with error_info. Engines are declined: they raise notsup, as an
%% OTP whose OpenSSL has no engine support does.
-module(crypto).
-export([
    bytes_to_integer/1,
    cipher_info/1,
    compute_key/4,
    crypto_final/1,
    crypto_get_data/1,
    crypto_init/3,
    crypto_init/4,
    crypto_one_time/4,
    crypto_one_time/5,
    crypto_one_time_aead/4,
    crypto_one_time_aead/6,
    crypto_one_time_aead/7,
    crypto_one_time_aead_init/4,
    crypto_update/2,
    decapsulate_key/3,
    ec_curve/1,
    ec_curves/0,
    enable_fips_mode/1,
    encapsulate_key/2,
    engine_add/1,
    engine_by_id/1,
    engine_ctrl_cmd_string/3,
    engine_ctrl_cmd_string/4,
    engine_get_all_methods/0,
    engine_get_id/1,
    engine_get_name/1,
    engine_list/0,
    engine_load/3,
    engine_load/4,
    engine_methods_convert_to_bitmask/2,
    engine_register/2,
    engine_remove/1,
    engine_unload/1,
    engine_unload/2,
    engine_unregister/2,
    ensure_engine_loaded/2,
    ensure_engine_loaded/3,
    ensure_engine_unloaded/1,
    ensure_engine_unloaded/2,
    exor/2,
    format_error/2,
    generate_key/2,
    generate_key/3,
    get_test_engine/0,
    hash/2,
    hash_equals/2,
    hash_final/1,
    hash_final_xof/2,
    hash_info/1,
    hash_init/1,
    hash_update/2,
    hash_xof/3,
    info/0,
    info_fips/0,
    info_lib/0,
    mac/3,
    mac/4,
    macN/4,
    macN/5,
    mac_final/1,
    mac_finalN/2,
    mac_init/2,
    mac_init/3,
    mac_update/2,
    mod_pow/3,
    packed_openssl_version/4,
    pbkdf2_hmac/5,
    private_decrypt/4,
    private_encrypt/4,
    privkey_to_pubkey/2,
    public_decrypt/4,
    public_encrypt/4,
    rand_cache_plugin_bytes/2,
    rand_cache_plugin_next/1,
    rand_plugin_aes_jump/1,
    rand_plugin_aes_jump_2pow20/1,
    rand_plugin_aes_next/1,
    rand_plugin_bytes/2,
    rand_plugin_next/1,
    rand_plugin_prng_bytes/2,
    rand_plugin_prng_jump/1,
    rand_plugin_prng_next/1,
    rand_plugin_uniform/1,
    rand_plugin_uniform/2,
    rand_seed/0,
    rand_seed/1,
    rand_seed_alg/1,
    rand_seed_alg/2,
    rand_seed_alg_s/1,
    rand_seed_alg_s/2,
    rand_seed_s/0,
    rand_uniform/2,
    sign/4,
    sign/5,
    start/0,
    stop/0,
    strong_rand_bytes/1,
    strong_rand_range/1,
    supports/0,
    supports/1,
    verify/5,
    verify/6,
    version/0
]).
-include("crypto.types.hrl").

-deprecated([
    {start, 0, "use application:start(crypto) instead"},
    {stop, 0, "use application:stop(crypto) instead"},
    {enable_fips_mode, 1, "use config parameter fips_mode"},
    {rand_uniform, 2, "use strong_rand_range/1 instead"}
]).

%% The crypto application version this module answers as (OTP 29's crypto-5.10).
-define(CRYPTO_VSN, "5.10").
%% Bytes one hash, MAC or exor builtin call takes at most; longer inputs go in pieces, as OTP's do.
-define(MAX_BYTES, 20000).
%% Used by rand_plugin_uniform/1: math:pow(2, -53).
-define(HALF_DBL_EPSILON, 1.1102230246251565e-16).
%% Bits of the crypto_cache generator's numbers (a multiple of 8).
-define(CRYPTO_CACHE_BITS, 56).
%% Bits of the crypto_aes and crypto_prng1 generators' numbers.
-define(CRYPTO_PRNG_BITS, 58).

%% Run a builtin; its {Class, #{c_file_name, ...}, Message} error becomes OTP's {Class, {File, Line}, Message}, raised
%% with the builtin's arguments Args (OTP's NIF frame has them) and the Erlang argument number in error_info. ArgMap
%% maps the builtin's argument numbers to the caller's.
-define(nif_call(Call, Args), ?nif_call(Call, Args, {})).
-define(nif_call(Call, Args0, ArgMap),
    try
        Call
    catch
        error:{Id, #{c_file_name := CFile, c_file_line_num := CLine, c_function_arg_num := ArgNum}, Msg}:Stack when
            is_list(CFile), is_integer(CLine), is_integer(ArgNum)
        ->
            erlang:error(
                {Id, {CFile, CLine}, Msg},
                err_find_args(Args0, Stack),
                [{error_info, #{erl_function_arg_num => err_remap_C_argnum(ArgNum, ArgMap)}}]
            )
    end
).

%%% ---------------------------------------------------------------------------
%%% Information

version() -> ?CRYPTO_VSN.

format_error({Ex, {CFile, CLine}, Msg}, [{_M, _F, _Args, Opts} | _CallStack]) when Ex == badarg; Ex == notsup ->
    case proplists:get_value(error_info, Opts) of
        #{erl_function_arg_num := ErlArgNum} ->
            FileMsg = io_lib:format("(Found in the internal file ~s at line ~p)", [CFile, CLine]),
            case ErlArgNum of
                undefined -> #{general => [Msg, " ", FileMsg]};
                N -> #{N => Msg, general => FileMsg}
            end
    end.

start() -> application:start(crypto).

stop() -> application:stop(crypto).

supports() ->
    [
        {hashs, supports(hashs)},
        {ciphers, supports(ciphers)},
        {kems, supports(kems)}
        | [{T, supports(T)} || T <- [public_keys, macs, curves, rsa_opts]]
    ].

supports(hashs) -> clause_crypto:hash_algorithms();
supports(public_keys) -> clause_crypto:pubkey_algorithms();
supports(ciphers) -> add_cipher_aliases(clause_crypto:cipher_algorithms());
supports(kems) -> clause_crypto:kem_algorithms();
supports(macs) -> clause_crypto:mac_algorithms();
supports(curves) -> clause_crypto:curve_algorithms();
supports(rsa_opts) -> clause_crypto:rsa_opts_algorithms().

info_lib() -> clause_crypto:info_lib().

info() -> (clause_crypto:info())#{otp_crypto_version => version()}.

%% Clause's libcrypto has no FIPS provider.
info_fips() -> not_supported.

enable_fips_mode(true) -> false;
enable_fips_mode(false) -> true;
enable_fips_mode(_) -> erlang:error(badarg).

hash_info(Type) -> clause_crypto:hash_info(Type).

%% The packed version number OpenSSL headers use (OPENSSL_VERSION_NUMBER of 1.1 and later).
packed_openssl_version(Maj, Min, Fix, P0) ->
    P1 = atom_to_list(P0),
    P = lists:sum([C - $a || C <- P1]),
    ((((((((Maj bsl 8) bor Min) bsl 8) bor Fix) bsl 8) bor (P + 1)) bsl 4) bor 16#f).

%%% ---------------------------------------------------------------------------
%%% Hashing

hash(Type, Data) ->
    Data1 = iolist_to_binary(Data),
    hash(Type, Data1, byte_size(Data1)).

hash(Type, Data, Size) when Size =< ?MAX_BYTES ->
    ?nif_call(clause_crypto:hash(Type, Data), [Type, Data]);
hash(Type, Data, Size) ->
    hash_final(hash_update(hash_init(Type), Data, Size)).

hash_xof(Type, Data, Length) ->
    Data1 = iolist_to_binary(Data),
    hash_final_xof(hash_update(hash_init(Type), Data1, byte_size(Data1)), Length).

hash_init(Type) -> ?nif_call(clause_crypto:hash_init(Type), [Type]).

hash_update(State, Data) ->
    Data1 = iolist_to_binary(Data),
    hash_update(State, Data1, byte_size(Data1)).

hash_update(State, Data, Size) when Size =< ?MAX_BYTES ->
    ?nif_call(clause_crypto:hash_update(State, Data), [State, Data], {1, 2});
hash_update(State0, Data, _) ->
    <<Increment:?MAX_BYTES/binary, Rest/binary>> = Data,
    State = ?nif_call(clause_crypto:hash_update(State0, Increment), [State0, Increment], {1, 2}),
    hash_update(State, Rest, byte_size(Rest)).

hash_final(State) -> ?nif_call(clause_crypto:hash_final(State), [State]).

hash_final_xof(State, Length) -> ?nif_call(clause_crypto:hash_final_xof(State, Length), [State, Length]).

%%% ---------------------------------------------------------------------------
%%% MACs

mac(poly1305, Key, Data) -> mac(poly1305, undefined, Key, Data).

mac(Type, SubType, Key0, Data) ->
    Key = iolist_to_binary(Key0),
    case text_size(Data) =< ?MAX_BYTES of
        true ->
            ?nif_call(
                clause_crypto:mac(Type, alias(SubType, Key), Key, Data), [Type, alias(SubType, Key), Key, Data]
            );
        false ->
            mac_final(mac_pieces(mac_init(Type, SubType, Key), iolist_to_binary(Data)))
    end.

%% The size of a text, 0 for one that is not iodata (the builtin rejects it with OTP's error).
text_size(Data) ->
    try
        iolist_size(Data)
    catch
        error:badarg -> 0
    end.

%% Update a MAC state with a long text in pieces.
mac_pieces(State, <<Piece:?MAX_BYTES/binary, Rest/binary>>) when Rest =/= <<>> ->
    mac_pieces(mac_update(State, Piece), Rest);
mac_pieces(State, Text) ->
    mac_update(State, Text).

macN(Type, Key, Data, MacLength) -> macN(Type, undefined, Key, Data, MacLength).

macN(Type, SubType, Key, Data, MacLength) ->
    Mac = mac(Type, SubType, Key, Data),
    erlang:binary_part(Mac, 0, min(MacLength, byte_size(Mac))).

mac_init(poly1305, Key) -> ?nif_call(clause_crypto:mac_init(poly1305, undefined, Key), [poly1305, undefined, Key]).

mac_init(Type, SubType, Key0) ->
    Key = iolist_to_binary(Key0),
    ?nif_call(clause_crypto:mac_init(Type, alias(SubType, Key), Key), [Type, alias(SubType, Key), Key]).

mac_update(Ref, Data) ->
    case text_size(Data) =< ?MAX_BYTES of
        true -> ?nif_call(clause_crypto:mac_update(Ref, Data), [Ref, Data]);
        false -> mac_pieces(Ref, iolist_to_binary(Data))
    end.

mac_final(Ref) -> ?nif_call(clause_crypto:mac_final(Ref), [Ref]).

mac_finalN(Ref, MacLength) -> erlang:binary_part(mac_final(Ref), 0, MacLength).

pbkdf2_hmac(Digest, Pass, Salt, Iter, KeyLen) ->
    ?nif_call(
        clause_crypto:pbkdf2_hmac(Digest, Pass, Salt, Iter, KeyLen), [Digest, Pass, Salt, Iter, KeyLen]
    ).

%%% ---------------------------------------------------------------------------
%%% Cipher aliases (the key length selects the cipher)

add_cipher_aliases(Ciphers) ->
    Ciphers ++
        lists:usort(
            lists:foldl(
                fun(C, Acc) ->
                    case alias1_rev(C) of
                        C -> Acc;
                        A -> [A | Acc]
                    end
                end,
                [],
                Ciphers
            )
        ).

alias(aes_cbc, Key) -> alias1(aes_cbc, iolist_size(Key));
alias(aes_cfb8, Key) -> alias1(aes_cfb8, iolist_size(Key));
alias(aes_cfb128, Key) -> alias1(aes_cfb128, iolist_size(Key));
alias(aes_ctr, Key) -> alias1(aes_ctr, iolist_size(Key));
alias(aes_ecb, Key) -> alias1(aes_ecb, iolist_size(Key));
alias(aes_gcm, Key) -> alias1(aes_gcm, iolist_size(Key));
alias(aes_ccm, Key) -> alias1(aes_ccm, iolist_size(Key));
alias(Alg, _) -> Alg.

alias1(aes_cbc, 16) -> aes_128_cbc;
alias1(aes_cbc, 24) -> aes_192_cbc;
alias1(aes_cbc, 32) -> aes_256_cbc;
alias1(aes_cfb8, 16) -> aes_128_cfb8;
alias1(aes_cfb8, 24) -> aes_192_cfb8;
alias1(aes_cfb8, 32) -> aes_256_cfb8;
alias1(aes_cfb128, 16) -> aes_128_cfb128;
alias1(aes_cfb128, 24) -> aes_192_cfb128;
alias1(aes_cfb128, 32) -> aes_256_cfb128;
alias1(aes_ctr, 16) -> aes_128_ctr;
alias1(aes_ctr, 24) -> aes_192_ctr;
alias1(aes_ctr, 32) -> aes_256_ctr;
alias1(aes_ecb, 16) -> aes_128_ecb;
alias1(aes_ecb, 24) -> aes_192_ecb;
alias1(aes_ecb, 32) -> aes_256_ecb;
alias1(aes_gcm, 16) -> aes_128_gcm;
alias1(aes_gcm, 24) -> aes_192_gcm;
alias1(aes_gcm, 32) -> aes_256_gcm;
alias1(aes_ccm, 16) -> aes_128_ccm;
alias1(aes_ccm, 24) -> aes_192_ccm;
alias1(aes_ccm, 32) -> aes_256_ccm;
alias1(Alg, _) -> Alg.

alias1_rev(aes_128_cbc) -> aes_cbc;
alias1_rev(aes_192_cbc) -> aes_cbc;
alias1_rev(aes_256_cbc) -> aes_cbc;
alias1_rev(aes_128_cfb8) -> aes_cfb8;
alias1_rev(aes_192_cfb8) -> aes_cfb8;
alias1_rev(aes_256_cfb8) -> aes_cfb8;
alias1_rev(aes_128_cfb128) -> aes_cfb128;
alias1_rev(aes_192_cfb128) -> aes_cfb128;
alias1_rev(aes_256_cfb128) -> aes_cfb128;
alias1_rev(aes_128_ctr) -> aes_ctr;
alias1_rev(aes_192_ctr) -> aes_ctr;
alias1_rev(aes_256_ctr) -> aes_ctr;
alias1_rev(aes_128_ecb) -> aes_ecb;
alias1_rev(aes_192_ecb) -> aes_ecb;
alias1_rev(aes_256_ecb) -> aes_ecb;
alias1_rev(aes_128_gcm) -> aes_gcm;
alias1_rev(aes_192_gcm) -> aes_gcm;
alias1_rev(aes_256_gcm) -> aes_gcm;
alias1_rev(aes_128_ccm) -> aes_ccm;
alias1_rev(aes_192_ccm) -> aes_ccm;
alias1_rev(aes_256_ccm) -> aes_ccm;
alias1_rev(C) -> C.

%%% ---------------------------------------------------------------------------
%%% Ciphers

cipher_info(Type) ->
    try
        clause_crypto:cipher_info(Type)
    catch
        error:badarg ->
            %% Maybe an alias: the key length is unknown, so it reads as the 16-byte one.
            case alias1(Type, 16) of
                Type -> erlang:error(badarg);
                NewType -> cipher_info(NewType)
            end
    end.

crypto_init(Cipher, Key, FlagOrOptions) ->
    Alg = alias(Cipher, Key),
    ?nif_call(clause_crypto:crypto_init(Alg, Key, <<>>, FlagOrOptions), [Alg, Key, <<>>, FlagOrOptions], {1, 2, -1, 3}).

crypto_init(Cipher, Key, IV, FlagOrOptions) ->
    Alg = alias(Cipher, Key),
    ?nif_call(clause_crypto:crypto_init(Alg, Key, IV, FlagOrOptions), [Alg, Key, IV, FlagOrOptions]).

crypto_update(State, Data) ->
    case text_size(Data) =< ?MAX_BYTES of
        true -> crypto_update_nif(State, Data, Data);
        false -> crypto_update_pieces(State, iolist_to_binary(Data), Data, [])
    end.

%% Encrypt or decrypt a long text in pieces; errors name the whole text.
crypto_update_pieces(State, <<Piece:?MAX_BYTES/binary, Rest/binary>>, Data, Acc) when Rest =/= <<>> ->
    crypto_update_pieces(State, Rest, Data, [crypto_update_nif(State, Piece, Data) | Acc]);
crypto_update_pieces(State, Last, Data, Acc) ->
    iolist_to_binary(lists:reverse(Acc, [crypto_update_nif(State, Last, Data)])).

crypto_update_nif(State, Piece, Data) -> ?nif_call(clause_crypto:crypto_update(State, Piece), [State, Data]).

crypto_final(State) -> ?nif_call(clause_crypto:crypto_final(State), [State]).

crypto_get_data(State) -> ?nif_call(clause_crypto:crypto_get_data(State), [State]).

crypto_one_time(Cipher, Key, Data, FlagOrOptions) ->
    ?nif_call(
        clause_crypto:crypto_one_time(alias(Cipher, Key), Key, <<>>, Data, FlagOrOptions),
        [Cipher, Key, Data, FlagOrOptions],
        {1, 2, -1, 3, 4}
    ).

crypto_one_time(Cipher, Key, IV, Data, FlagOrOptions) ->
    ?nif_call(
        clause_crypto:crypto_one_time(alias(Cipher, Key), Key, IV, Data, FlagOrOptions),
        [Cipher, Key, IV, Data, FlagOrOptions]
    ).

crypto_one_time_aead(Cipher, Key, IV, PlainText, AAD, true) ->
    Alg = alias(Cipher, Key),
    TagLength = aead_tag_len(Cipher),
    ?nif_call(
        clause_crypto:aead_cipher(Alg, Key, IV, PlainText, AAD, TagLength, true),
        [Alg, Key, IV, PlainText, AAD, TagLength, true],
        {1, 2, 3, 4, 5, -1, 6}
    ).

crypto_one_time_aead(Cipher, Key, IV, TextIn, AAD, TagOrTagLength, EncFlg) ->
    ?nif_call(
        clause_crypto:aead_cipher(alias(Cipher, Key), Key, IV, TextIn, AAD, TagOrTagLength, EncFlg),
        [Cipher, Key, IV, TextIn, AAD, TagOrTagLength, EncFlg]
    ).

%% The default tag length of an AEAD cipher.
aead_tag_len(aes_ccm) -> 12;
aead_tag_len(aes_128_ccm) -> 12;
aead_tag_len(aes_192_ccm) -> 12;
aead_tag_len(aes_256_ccm) -> 12;
aead_tag_len(aes_gcm) -> 16;
aead_tag_len(aes_128_gcm) -> 16;
aead_tag_len(aes_192_gcm) -> 16;
aead_tag_len(aes_256_gcm) -> 16;
aead_tag_len(chacha20_poly1305) -> 16;
aead_tag_len(sm4_gcm) -> 16;
aead_tag_len(sm4_ccm) -> 16;
aead_tag_len(_) -> erlang:error({badarg, "Not an AEAD cipher"}).

crypto_one_time_aead_init(Cipher, Key, Length, Encode) when is_integer(Length) ->
    ?nif_call(
        clause_crypto:aead_cipher_init(alias(Cipher, Key), iolist_to_binary(Key), Length, Encode),
        [Cipher, Key, Length, Encode]
    ).

crypto_one_time_aead(State, IV, InText, AAD) ->
    ?nif_call(clause_crypto:aead_cipher(State, IV, InText, AAD), [State, IV, InText, AAD]).

%%% ---------------------------------------------------------------------------
%%% Public keys (plan 21 step LIB79C)

sign(_, _, _, _) -> erlang:error({not_implemented, sign, 4}).
sign(_, _, _, _, _) -> erlang:error({not_implemented, sign, 5}).
verify(_, _, _, _, _) -> erlang:error({not_implemented, verify, 5}).
verify(_, _, _, _, _, _) -> erlang:error({not_implemented, verify, 6}).
public_encrypt(_, _, _, _) -> erlang:error({not_implemented, public_encrypt, 4}).
private_decrypt(_, _, _, _) -> erlang:error({not_implemented, private_decrypt, 4}).
private_encrypt(_, _, _, _) -> erlang:error({not_implemented, private_encrypt, 4}).
public_decrypt(_, _, _, _) -> erlang:error({not_implemented, public_decrypt, 4}).
generate_key(_, _) -> erlang:error({not_implemented, generate_key, 2}).
generate_key(_, _, _) -> erlang:error({not_implemented, generate_key, 3}).
compute_key(_, _, _, _) -> erlang:error({not_implemented, compute_key, 4}).
encapsulate_key(_, _) -> erlang:error({not_implemented, encapsulate_key, 2}).
decapsulate_key(_, _, _) -> erlang:error({not_implemented, decapsulate_key, 3}).
privkey_to_pubkey(_, _) -> erlang:error({not_implemented, privkey_to_pubkey, 2}).
ec_curves() -> erlang:error({not_implemented, ec_curves, 0}).
ec_curve(_) -> erlang:error({not_implemented, ec_curve, 1}).

%%% ---------------------------------------------------------------------------
%%% Random numbers

strong_rand_bytes(Bytes) ->
    case clause_crypto:strong_rand_bytes(Bytes) of
        false -> erlang:error(low_entropy);
        Bin -> Bin
    end.

strong_rand_range(Range) when is_integer(Range), Range > 0 ->
    bin_to_int(strong_rand_range(int_to_bin(Range)));
strong_rand_range(BinRange) when is_binary(BinRange) ->
    case clause_crypto:strong_rand_range(BinRange) of
        false ->
            case bin_to_int(BinRange) of
                V when 0 < V -> erlang:error(low_entropy);
                _ -> erlang:error(badarg, [BinRange])
            end;
        <<BinResult/binary>> ->
            BinResult
    end;
strong_rand_range(Range) ->
    erlang:error(badarg, [Range]).

rand_uniform(From, To) when is_binary(From), is_binary(To) ->
    case clause_crypto:rand_uniform(From, To) of
        <<Len:32/integer, MSB, Rest/binary>> when MSB > 127 ->
            <<(Len + 1):32/integer, 0, MSB, Rest/binary>>;
        Whatever ->
            Whatever
    end;
rand_uniform(From, To) when is_integer(From), is_integer(To) ->
    if
        From < 0 -> rand_uniform_pos(0, To - From) + From;
        true -> rand_uniform_pos(From, To)
    end.

rand_uniform_pos(From, To) when From < To ->
    case rand_uniform(mpint(From), mpint(To)) of
        Result when is_binary(Result) -> erlint(Result);
        Other -> Other
    end;
rand_uniform_pos(_, _) ->
    erlang:error(badarg).

rand_seed(Seed) when is_binary(Seed) -> clause_crypto:rand_seed(Seed).

rand_seed() -> rand:seed(rand_seed_s()).

rand_seed_s() -> rand_seed_alg_s(?MODULE).

rand_seed_alg(Alg) -> rand:seed(rand_seed_alg_s(Alg)).

rand_seed_alg(Alg, Seed) -> rand:seed(rand_seed_alg_s(Alg, Seed)).

rand_seed_alg_s({#{type := Alg}, _AlgState} = State) when
    Alg =:= crypto; Alg =:= crypto_cache; Alg =:= crypto_aes; Alg =:= crypto_prng1
->
    State;
rand_seed_alg_s({Alg = crypto_prng1, AlgState}) ->
    if
        tuple_size(AlgState) =:= 4 -> {mk_alg_handler(Alg), AlgState};
        is_list(AlgState) -> erlang:error(not_implemented)
    end;
rand_seed_alg_s({Alg, AlgState}) when is_atom(Alg) ->
    {mk_alg_handler(Alg), AlgState};
rand_seed_alg_s(Alg) when is_atom(Alg) ->
    {mk_alg_handler(Alg), mk_alg_state(Alg)}.

rand_seed_alg_s(Alg, Seed) when is_atom(Alg) ->
    {mk_alg_handler(Alg), mk_alg_state({Alg, Seed})}.

mk_alg_handler(?MODULE = Alg) ->
    #{
        type => Alg,
        bits => 64,
        next => fun ?MODULE:rand_plugin_next/1,
        uniform => fun ?MODULE:rand_plugin_uniform/1,
        uniform_n => fun ?MODULE:rand_plugin_uniform/2,
        bytes => fun ?MODULE:rand_plugin_bytes/2
    };
mk_alg_handler(crypto_cache = Alg) ->
    #{
        type => Alg,
        bits => ?CRYPTO_CACHE_BITS,
        next => fun ?MODULE:rand_cache_plugin_next/1,
        bytes => fun ?MODULE:rand_cache_plugin_bytes/2
    };
mk_alg_handler(crypto_aes = Alg) ->
    #{
        type => Alg,
        bits => ?CRYPTO_PRNG_BITS,
        next => fun ?MODULE:rand_plugin_aes_next/1,
        jump => fun ?MODULE:rand_plugin_aes_jump/1
    };
mk_alg_handler(crypto_prng1 = Alg) ->
    #{
        type => Alg,
        bits => ?CRYPTO_PRNG_BITS,
        next => fun ?MODULE:rand_plugin_prng_next/1,
        jump => fun ?MODULE:rand_plugin_prng_jump/1,
        bytes => fun ?MODULE:rand_plugin_prng_bytes/2
    }.

mk_alg_state(?MODULE) ->
    no_seed;
mk_alg_state(crypto_cache) ->
    %% The cache holds an even number of numbers, rounded up, at least one.
    CacheBits = ?CRYPTO_CACHE_BITS,
    BytesPerWord = (CacheBits + 7) div 8,
    GenBytes = ((rand_cache_size() + (2 * BytesPerWord - 1)) div BytesPerWord) * BytesPerWord,
    {CacheBits, GenBytes, <<>>};
mk_alg_state({crypto_aes, Seed}) ->
    %% 16-byte words (128-bit cipher blocks).
    GenWords = (rand_cache_size() + 31) div 16,
    Key = hash(sha256, Seed),
    {F, Count} = longcount_seed(Seed),
    {Key, GenWords, F, Count};
mk_alg_state({crypto_prng1, Seed}) ->
    rand_plugin_prng_seed(aes_256_ctr, hash(sha384, Seed)).

rand_cache_size() ->
    DefaultCacheSize = 1024,
    %% OTP's crypto loads its application as the module loads (on_load); Clause loads it before reading its
    %% environment.
    _ = application:load(crypto),
    CacheSize = application:get_env(crypto, rand_cache_size, DefaultCacheSize),
    if
        is_integer(CacheSize), 0 =< CacheSize -> CacheSize;
        true -> DefaultCacheSize
    end.

rand_plugin_next(State) -> {strong_rand_range(1 bsl 64), State}.

rand_plugin_uniform(State) -> {?HALF_DBL_EPSILON * strong_rand_range(1 bsl 53), State}.

rand_plugin_uniform(Max, State) -> {strong_rand_range(Max) + 1, State}.

rand_plugin_bytes(N, State) -> {strong_rand_bytes(N), State}.

rand_cache_plugin_next({CacheBits, GenBytes, Cache}) ->
    case Cache of
        <<I:CacheBits, NewCache/binary>> ->
            {I, {CacheBits, GenBytes, NewCache}};
        <<>> ->
            rand_cache_plugin_next({CacheBits, GenBytes, strong_rand_bytes(GenBytes)});
        <<_/binary>> ->
            <<NewBytes:((CacheBits bsr 3) - byte_size(Cache))/binary, NewCache/binary>> =
                strong_rand_bytes(GenBytes),
            <<I:CacheBits>> = <<Cache/binary, NewBytes/binary>>,
            {I, {CacheBits, GenBytes, NewCache}}
    end.

rand_cache_plugin_bytes(N, State = {AlgHandler, {CacheBits, GenBytes, Cache}}) when is_integer(N), 0 =< N ->
    case Cache of
        <<Bytes:N/binary, NewCache/binary>> ->
            {Bytes, {AlgHandler, {CacheBits, GenBytes, NewCache}}};
        <<>> ->
            {strong_rand_bytes(N), State};
        <<_/binary>> when N =< GenBytes ->
            <<NewBytes:(N - byte_size(Cache))/binary, NewCache/binary>> = strong_rand_bytes(GenBytes),
            {<<Cache/binary, NewBytes/binary>>, {AlgHandler, {CacheBits, GenBytes, NewCache}}};
        <<_/binary>> ->
            {strong_rand_bytes(N), State}
    end.

%% crypto_aes: 128-bit counters, each two 58-bit words of a Xoroshiro928 state under a 12-bit F, encrypted with
%% AES-ECB; the lowest 58 bits of each block are the numbers, generated GenWords at a time and cached.
rand_plugin_aes_next([V | Cache]) ->
    {V, Cache};
rand_plugin_aes_next({Key, GenWords, F, Count}) ->
    rand_plugin_aes_next(Key, GenWords, F, Count);
rand_plugin_aes_next({Key, GenWords, F, _JumpBase, Count}) ->
    rand_plugin_aes_next(Key, GenWords, F, Count).

rand_plugin_aes_next(Key, GenWords, F, Count) ->
    {Cleartext, NewCount} = aes_cleartext(<<>>, F, Count, GenWords),
    Encrypted = block_encrypt(Key, Cleartext),
    [V | Cache] = aes_cache(Encrypted, {Key, GenWords, F, Count, NewCount}),
    {V, Cache}.

block_encrypt(Key, Data) ->
    Cipher =
        case byte_size(Key) of
            16 -> aes_128_ecb;
            24 -> aes_192_ecb;
            32 -> aes_256_ecb;
            _ -> erlang:error(badarg)
        end,
    try
        crypto_one_time(Cipher, Key, Data, true)
    catch
        error:{error, {_File, _Line}, _Reason} -> erlang:error(badarg);
        error:{E, {_File, _Line}, _Reason} when E == notsup; E == badarg -> erlang:error(E)
    end.

%% A jump advances the counter 2^512 steps from the jump base; the numbers already taken from the cache are
%% skipped after it.
rand_plugin_aes_jump({#{type := crypto_aes} = Alg, Cache}) ->
    {Alg, rand_plugin_aes_jump(fun rand:exro928_jump_2pow512/1, 0, Cache)}.

rand_plugin_aes_jump(Jump, J, [_ | Cache]) ->
    rand_plugin_aes_jump(Jump, J + 1, Cache);
rand_plugin_aes_jump(Jump, J, {Key, GenWords, F, JumpBase, _Count}) ->
    rand_plugin_aes_jump(Jump, GenWords - J, Key, GenWords, F, JumpBase);
rand_plugin_aes_jump(Jump, 0, {Key, GenWords, F, JumpBase}) ->
    rand_plugin_aes_jump(Jump, 0, Key, GenWords, F, JumpBase).

rand_plugin_aes_jump(Jump, Skip, Key, GenWords, F, JumpBase) ->
    Count = longcount_next_count(Skip, Jump(JumpBase)),
    {Key, GenWords, F, Count}.

rand_plugin_aes_jump_2pow20(Cache) ->
    rand_plugin_aes_jump(fun rand:exro928_jump_2pow20/1, 0, Cache).

longcount_seed(Seed) ->
    <<X:64, _:6, F:12, S2:58, S1:58, S0:58>> = hash(sha256, [Seed, <<"Xoroshiro928">>]),
    {F, rand:exro928_seed([S0, S1, S2 | rand:seed58(13, X)])}.

longcount_next_count(0, Count) -> Count;
longcount_next_count(N, Count) -> longcount_next_count(N - 1, rand:exro928_next_state(Count)).

%% The cleartext of GenWords counter blocks.
aes_cleartext(Cleartext, _F, Count, 0) ->
    {Cleartext, Count};
aes_cleartext(Cleartext, F, Count, GenWords) ->
    {{S0, S1}, NewCount} = rand:exro928_next(Count),
    aes_cleartext(<<Cleartext/binary, F:12, S1:58, S0:58>>, F, NewCount, GenWords - 1).

%% The numbers of the encrypted blocks, in front of the state they continue from.
aes_cache(<<>>, Cache) ->
    Cache;
aes_cache(<<_:(128 - ?CRYPTO_PRNG_BITS), V:?CRYPTO_PRNG_BITS, Encrypted/binary>>, Cache) ->
    [V | aes_cache(Encrypted, Cache)].

%% crypto_prng1: the key stream of a stream cipher (AES-256-CTR) keyed from the seed's hash, read in cached chunks
%% of whole 128-byte blocks; only its initial state {Alg, ZeroData, Key, IV} can be exported.
rand_plugin_prng_seed(CryptoAlg, SeedBin) ->
    case cipher_info(CryptoAlg) of
        #{prop_aead := false, block_size := 1, key_length := KeyLength, iv_length := IVLength} when 0 < IVLength ->
            <<Key:KeyLength/binary, IV:IVLength/binary, _/binary>> = SeedBin,
            Block = <<0:1024>>,
            Size = byte_size(Block),
            Count = (rand_cache_size() + Size - 1) div Size,
            {CryptoAlg, binary:copy(Block, Count), Key, IV}
    end.

rand_plugin_prng_next({CryptoAlg, ZeroData, Key, IV}) ->
    CryptoState = crypto_init(CryptoAlg, Key, IV, true),
    rand_plugin_prng_next([<<>> | {CryptoState, ZeroData}]);
rand_plugin_prng_next([Cache | AlgState = {CryptoState, ZeroData}]) ->
    case Cache of
        <<_:(64 - ?CRYPTO_PRNG_BITS), I:?CRYPTO_PRNG_BITS, NewCache/binary>> ->
            {I, [NewCache | AlgState]};
        <<_/binary>> ->
            MissingSize = 8 - byte_size(Cache),
            <<NewBytes:MissingSize/bytes, NewCache/binary>> = crypto_update(CryptoState, ZeroData),
            <<_:(64 - ?CRYPTO_PRNG_BITS), I:?CRYPTO_PRNG_BITS>> = <<Cache/binary, NewBytes/bytes>>,
            {I, [NewCache | AlgState]}
    end.

rand_plugin_prng_bytes(N, {AlgHandler, {CryptoAlg, ZeroData, Key, IV}}) when is_integer(N), 0 =< N ->
    CryptoState = crypto_init(CryptoAlg, Key, IV, true),
    rand_plugin_prng_bytes(N, {AlgHandler, [<<>> | {CryptoState, ZeroData}]});
rand_plugin_prng_bytes(N, {AlgHandler, [Cache | AlgState]}) when is_integer(N), 0 =< N ->
    case Cache of
        <<Bytes:N/binary, NewCache/binary>> ->
            {Bytes, {AlgHandler, [NewCache | AlgState]}};
        <<_/binary>> ->
            rand_plugin_prng_bytes(N - byte_size(Cache), AlgHandler, AlgState, Cache)
    end.

rand_plugin_prng_bytes(M, AlgHandler, AlgState = {CryptoState, ZeroData}, Cache) when M =< byte_size(ZeroData) ->
    <<NewBytes:M/binary, NewCache/binary>> = crypto_update(CryptoState, ZeroData),
    {<<Cache/binary, NewBytes/binary>>, {AlgHandler, [NewCache | AlgState]}};
rand_plugin_prng_bytes(M, AlgHandler, AlgState = {CryptoState, ZeroData}, Cache) ->
    NewBytes = crypto_update(CryptoState, ZeroData),
    rand_plugin_prng_bytes(M - byte_size(NewBytes), AlgHandler, AlgState, <<Cache/binary, NewBytes/binary>>).

%% Only the initial state jumps: to the next IV, counted up from its first byte.
rand_plugin_prng_jump({AlgHandler, {Alg, ZeroData, Key, IV}}) ->
    {AlgHandler, {Alg, ZeroData, Key, jump_iv(IV, <<>>)}};
rand_plugin_prng_jump({_AlgHandler, [_Cache | _AlgState]}) ->
    erlang:error(not_implemented).

jump_iv(<<>>, JumpIV) -> JumpIV;
jump_iv(<<X, Rest/binary>>, JumpIV) when X < 255 -> <<JumpIV/binary, (X + 1), (binary:copy(Rest))/binary>>;
jump_iv(<<_, Rest/binary>>, JumpIV) -> jump_iv(Rest, <<JumpIV/binary, 0>>).

%%% ---------------------------------------------------------------------------
%%% Numbers and bytes

mod_pow(Base, Exponent, Prime) ->
    case clause_crypto:mod_exp(ensure_int_as_bin(Base), ensure_int_as_bin(Exponent), ensure_int_as_bin(Prime), 0) of
        <<0>> -> error;
        R -> R
    end.

exor(Bin1, Bin2) ->
    Data1 = iolist_to_binary(Bin1),
    Data2 = iolist_to_binary(Bin2),
    exor(Data1, Data2, byte_size(Data1), []).

exor(Data1, Data2, Size, []) when Size =< ?MAX_BYTES ->
    clause_crypto:exor(Data1, Data2);
exor(Data1, Data2, Size, Acc) when Size =< ?MAX_BYTES ->
    list_to_binary(lists:reverse([clause_crypto:exor(Data1, Data2) | Acc]));
exor(Data1, Data2, _Size, Acc) ->
    <<Increment1:?MAX_BYTES/binary, Rest1/binary>> = Data1,
    <<Increment2:?MAX_BYTES/binary, Rest2/binary>> = Data2,
    Result = clause_crypto:exor(Increment1, Increment2),
    exor(Rest1, Rest2, byte_size(Rest1), [Result | Acc]).

hash_equals(A, B) -> clause_crypto:hash_equals(A, B).

bytes_to_integer(Bin) -> bin_to_int(Bin).

int_to_bin(X) when X < 0 -> int_to_bin_neg(X, []);
int_to_bin(X) -> int_to_bin_pos(X, []).

int_to_bin_pos(0, Ds = [_ | _]) -> list_to_binary(Ds);
int_to_bin_pos(X, Ds) -> int_to_bin_pos(X bsr 8, [(X band 255) | Ds]).

int_to_bin_neg(-1, Ds = [MSB | _]) when MSB >= 16#80 -> list_to_binary(Ds);
int_to_bin_neg(X, Ds) -> int_to_bin_neg(X bsr 8, [(X band 255) | Ds]).

bin_to_int(Bin) when is_binary(Bin) ->
    Bits = bit_size(Bin),
    <<Integer:Bits/integer>> = Bin,
    Integer;
bin_to_int(undefined) ->
    undefined.

ensure_int_as_bin(Int) when is_integer(Int) -> int_to_bin(Int);
ensure_int_as_bin(Bin) -> Bin.

%% A large integer in a binary with a 32-bit length (SSH2's mpint).
mpint(X) when X < 0 ->
    Bin = int_to_bin_neg(X, []),
    <<(byte_size(Bin)):32/unsigned-big-integer, Bin/binary>>;
mpint(X) ->
    Bin = int_to_bin_pos(X, []),
    <<MSB, _/binary>> = Bin,
    Sz = byte_size(Bin),
    if
        MSB band 16#80 == 16#80 -> <<(Sz + 1):32/unsigned-big-integer, 0, Bin/binary>>;
        true -> <<Sz:32/unsigned-big-integer, Bin/binary>>
    end.

erlint(<<MPIntSize:32/integer, MPIntValue/binary>>) ->
    Bits = MPIntSize * 8,
    <<Integer:Bits/integer>> = MPIntValue,
    Integer.

%%% ---------------------------------------------------------------------------
%%% Engines: declined, as an OTP whose OpenSSL has no engine support (OpenSSL 4 has none)

engine_get_all_methods() -> erlang:error(notsup).

engine_load(EngineId, PreCmds, PostCmds) when is_list(PreCmds), is_list(PostCmds) ->
    _ = EngineId,
    erlang:error(notsup).

engine_load(EngineId, PreCmds, PostCmds, _EngineMethods) when is_list(PreCmds), is_list(PostCmds) ->
    engine_load(EngineId, PreCmds, PostCmds).

engine_unload(_Engine) -> erlang:error(notsup).

engine_unload(Engine, _EngineMethods) -> engine_unload(Engine).

engine_by_id(_EngineId) -> erlang:error(notsup).

engine_add(_Engine) -> erlang:error(notsup).

engine_remove(_Engine) -> erlang:error(notsup).

engine_register(_Engine, EngineMethods) when is_list(EngineMethods) -> erlang:error(notsup).

engine_unregister(_Engine, EngineMethods) when is_list(EngineMethods) -> erlang:error(notsup).

engine_get_id(_Engine) -> erlang:error(notsup).

engine_get_name(_Engine) -> erlang:error(notsup).

engine_list() -> erlang:error(notsup).

engine_ctrl_cmd_string(Engine, CmdName, CmdArg) -> engine_ctrl_cmd_string(Engine, CmdName, CmdArg, false).

engine_ctrl_cmd_string(_Engine, _CmdName, _CmdArg, _Optional) -> erlang:error(notsup).

ensure_engine_loaded(_EngineId, _LibPath) -> erlang:error(notsup).

ensure_engine_loaded(EngineId, LibPath, _EngineMethods) -> ensure_engine_loaded(EngineId, LibPath).

ensure_engine_unloaded(Engine) -> engine_unload(Engine).

ensure_engine_unloaded(Engine, _EngineMethods) -> ensure_engine_unloaded(Engine).

%% OTP's test helpers: there is no test engine without engine support.
get_test_engine() -> {error, notexist}.

engine_methods_convert_to_bitmask([], BitMask) ->
    BitMask;
engine_methods_convert_to_bitmask(engine_method_all, _BitMask) ->
    16#FFFF;
engine_methods_convert_to_bitmask(engine_method_none, _BitMask) ->
    16#0000;
engine_methods_convert_to_bitmask([M | Ms], BitMask) ->
    engine_methods_convert_to_bitmask(Ms, BitMask bor engine_method_atom_to_int(M)).

engine_method_atom_to_int(engine_method_rsa) -> 16#0001;
engine_method_atom_to_int(engine_method_dsa) -> 16#0002;
engine_method_atom_to_int(engine_method_dh) -> 16#0004;
engine_method_atom_to_int(engine_method_rand) -> 16#0008;
engine_method_atom_to_int(engine_method_ecdh) -> 16#0010;
engine_method_atom_to_int(engine_method_ecdsa) -> 16#0020;
engine_method_atom_to_int(engine_method_ciphers) -> 16#0040;
engine_method_atom_to_int(engine_method_digests) -> 16#0080;
engine_method_atom_to_int(engine_method_store) -> 16#0100;
engine_method_atom_to_int(engine_method_pkey_meths) -> 16#0200;
engine_method_atom_to_int(engine_method_pkey_asn1_meths) -> 16#0400;
engine_method_atom_to_int(engine_method_ec) -> 16#0800;
engine_method_atom_to_int(X) -> erlang:error(badarg, [X]).

%%% ---------------------------------------------------------------------------
%%% Errors of the builtins

err_find_args(Args, _Stack) -> Args.

err_remap_C_argnum(ArgNum, ArgMap) ->
    try element(ArgNum + 1, ArgMap) of
        N when is_integer(N), N > 0 -> N;
        _ -> undefined
    catch
        error:badarg when ArgNum >= 0 -> ArgNum + 1;
        error:badarg -> undefined
    end.
