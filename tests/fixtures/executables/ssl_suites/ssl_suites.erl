%% ssl's utility functions (plan 21 step LIB81A): the cipher suite lists of every version and description as maps,
%% RFC and OpenSSL names, names parsed back, the name walks on odd maps and strings, filters, signature algorithms,
%% curves, groups, versions and their environment, format_error/1 and the application's start and PEM cache.
-module(ssl_suites).
-export([main/1]).

-define(VERSIONS, ['tlsv1.3', 'tlsv1.2', 'tlsv1.1', tlsv1, 'dtlsv1.2', dtlsv1]).
-define(DESCRIPTIONS, [default, all, exclusive, anonymous, exclusive_anonymous]).

main(_) ->
    lists(),
    names(),
    round_trips(),
    odd_maps(),
    odd_names(),
    filters(),
    algorithms(),
    versions(),
    errors(),
    application(),
    io:format("ssl_suites: ok~n").

%% A call's result, or its exception class and reason.
safe(Fun) ->
    try
        {ok, Fun()}
    catch
        Class:Reason -> {Class, Reason}
    end.

id(X) -> X.

% A suite map as a sorted list, so the output does not depend on map printing.
sorted(#{} = Suite) -> lists:sort(maps:to_list(Suite));
sorted(Other) -> Other.

% A suite map with exactly the four parts as a tuple of them.
parts(#{key_exchange := K, cipher := C, mac := M, prf := P} = Suite) when map_size(Suite) == 4 -> {K, C, M, P};
parts(Other) -> {not_a_suite, sorted(Other)}.

lists() ->
    [
        io:format("~p ~p: ~w~n", [D, V, [parts(S) || S <- ssl:cipher_suites(D, V)]])
     || V <- ?VERSIONS, D <- ?DESCRIPTIONS
    ],
    Bad = [{bogus, 'tlsv1.2'}, {default, sslv3}, {default, "tlsv1.2"}, {all, bogus}],
    [
        io:format("cipher_suites(~p, ~p): ~p~n", [D, V, safe(fun() -> ssl:cipher_suites(id(D), id(V)) end)])
     || {D, V} <- Bad
    ].

names() ->
    [
        io:format("~p ~p ~p: ~s~n", [D, V, T, lists:join(" ", ssl:cipher_suites(D, V, T))])
     || V <- ?VERSIONS, D <- ?DESCRIPTIONS, T <- [rfc, openssl]
    ],
    Bad = [{default, 'tlsv1.2', bogus}, {bogus, 'tlsv1.2', rfc}, {default, bogus, rfc}, {bogus, 'tlsv1.2', bogus}],
    [
        io:format("cipher_suites(~p, ~p, ~p): ~p~n", [
            D, V, T, safe(fun() -> ssl:cipher_suites(id(D), id(V), id(T)) end)
        ])
     || {D, V, T} <- Bad
    ].

% Every listed suite's names parse back to it.
round_trips() ->
    Suites = lists:usort(lists:append([ssl:cipher_suites(D, V) || V <- ?VERSIONS, D <- ?DESCRIPTIONS])),
    Failed = [
        {Suite, Name, Parsed}
     || Suite <- Suites,
        Name <- [ssl:suite_to_str(Suite), ssl:suite_to_openssl_str(Suite)],
        Parsed <- [ssl:str_to_suite(Name)],
        Parsed =/= Suite
    ],
    io:format("round trips of ~p suites: ~p~n", [length(Suites), [{sorted(S), N, sorted(P)} || {S, N, P} <- Failed]]).

odd_maps() ->
    Maps = [
        #{key_exchange => null, cipher => null, mac => null, prf => null},
        #{key_exchange => null, cipher => null, mac => null},
        #{key_exchange => any, cipher => aes_128_gcm, mac => aead},
        #{key_exchange => any, cipher => aes_128_gcm, mac => aead, prf => sha256},
        #{key_exchange => rsa, cipher => aes_128_gcm, mac => aead, prf => default_prf},
        #{key_exchange => rsa, cipher => aes_128_gcm, mac => aead, prf => sha384},
        #{key_exchange => rsa, cipher => '3des_ede_cbc', mac => sha},
        #{key_exchange => rsa, cipher => des_cbc, mac => md5, prf => default_prf},
        #{key_exchange => dhe_rsa, cipher => des_cbc, mac => sha},
        #{key_exchange => dhe_rsa, cipher => '3des_ede_cbc', mac => sha},
        #{key_exchange => psk, cipher => '3des_ede_cbc', mac => sha},
        #{key_exchange => srp_anon, cipher => aes_256_cbc, mac => sha},
        #{key_exchange => srp_rsa, cipher => aes_128_cbc, mac => sha},
        #{key_exchange => srp_dss, cipher => aes_128_cbc, mac => sha},
        #{key_exchange => ecdhe_psk, cipher => aes_128_cbc_extra, mac => sha},
        #{key_exchange => dh_anon, cipher => aes_128_gcm_8, mac => aead, prf => default_prf},
        #{key_exchange => ecdh_anon, cipher => aes_256_ccm_8, mac => aead, prf => sha},
        #{key_exchange => ecdhe_ecdsa, cipher => chacha20_poly1305, mac => aead, prf => sha256},
        #{key_exchange => ecdhe_rsa, cipher => chacha20_poly1305, mac => aead, prf => sha384},
        #{key_exchange => ecdhe_rsa, cipher => rc4_md5, mac => md5},
        #{key_exchange => foo, cipher => bar, mac => baz},
        #{key_exchange => foo, cipher => bar, mac => aead, prf => qux, extra => 1},
        #{key_exchange => "rsa", cipher => bar, mac => baz},
        #{key_exchange => rsa, cipher => "3des_ede_cbc", mac => sha},
        #{key_exchange => rsa, cipher => aes_128_gcm, mac => aead, prf => 7},
        #{key_exchange => rsa, cipher => aes_128_cbc},
        #{key_exchange => any, mac => aead},
        #{cipher => aes_128_gcm, mac => aead, prf => sha256},
        #{},
        not_a_map
    ],
    [
        io:format("~p: ~p ~p~n", [
            sorted(M), safe(fun() -> ssl:suite_to_str(id(M)) end), safe(fun() -> ssl:suite_to_openssl_str(id(M)) end)
        ])
     || M <- Maps
    ],
    % Latin-1 letters are upper-cased, other characters kept (string:to_upper/1).
    Latin1 = #{
        key_exchange => list_to_atom([233, $l, 247]),
        cipher => list_to_atom([255, $b, 223, 16#100]),
        mac => list_to_atom([224, 254])
    },
    io:format("latin-1: ~w ~w~n", [ssl:suite_to_str(Latin1), ssl:suite_to_openssl_str(Latin1)]).

odd_names() ->
    Names = [
        "TLS_EMPTY_RENEGOTIATION_INFO_SCSV",
        "TLS_AES_128_GCM_SHA256",
        "TLS_RSA_WITH_AES_128_CBC_MD5",
        "TLS_ANY_WITH_AES_128_GCM_SHA256",
        "TLS_ANY_WITH_AES_128_GCM_",
        "TLS_RSA_WITH_CHACHA20_POLY1305_",
        "TLS_ECDHE_ECDSA_WITH_AES_128_CCM",
        "TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8",
        "TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256",
        "TLS_DHE_RSA_WITH_AES_128_CBC_SHA256",
        "TLS_SRP_SHA_WITH_AES_128_CBC_SHA",
        "TLS_DH_anon_WITH_AES_128_GCM_SHA256",
        "TLS_FOO_WITH_BAR_SHA",
        "TLS_RSA_WITH_AES",
        "TLS_",
        "TLS_NOUNDERSCORE",
        "DES-CBC-SHA",
        "DES-CBC3-SHA",
        "SRP-DSS-DES-CBC3-SHA",
        "ADH-AES128-SHA",
        "ADH-AES128-GCM-SHA256",
        "AECDH-AES256-SHA",
        "EDH-RSA-DES-CBC3-SHA",
        "EDH-DSS-DES-CBC3-SHA",
        "AES128-SHA",
        "AES256-GCM-SHA384",
        "AES128-CCM8",
        "AES256-CCM",
        "RC4-MD5",
        "RC4-SHA",
        "ECDHE-ECDSA-CHACHA20-POLY1305",
        "ECDHE-RSA-AES128-GCM-SHA256",
        "ECDHE-PSK-AES128-CBC-SHA256",
        "PSK-AES128-GCM8",
        "PSK-3DES-EDE-CBC-SHA",
        "RSA-PSK-AES256-CBC-SHA384",
        "DHE-PSK-CHACHA20-POLY1305",
        "SRP-AES-128-CBC-SHA",
        "SRP-RSA-AES-256-CBC-SHA",
        "AES",
        "garbage",
        "",
        "aes128-sha",
        <<"TLS_AES_128_GCM_SHA256">>,
        ['TLS_AES_128_GCM_SHA256'],
        "TLS_AES_128_GCM_SHA256" ++ [-1],
        42
    ],
    [io:format("~p: ~p~n", [N, sorted(ssl:str_to_suite(id(N)))]) || N <- Names].

filters() ->
    % RC4 and DES need OpenSSL's legacy provider, which the OTP of the golden lacks.
    Suites = [S || #{cipher := C} = S <- ssl:cipher_suites(all, 'tlsv1.2'), C =/= rc4_128, C =/= des_cbc],
    Print = fun(Label, Result) -> io:format("~s: ~p~n", [Label, [ssl:suite_to_str(S) || S <- Result]]) end,
    Print("crypto filter", ssl:filter_cipher_suites(Suites, [])),
    Print("ecdhe", ssl:filter_cipher_suites(Suites, [{key_exchange, fun(K) -> K == ecdhe_rsa end}])),
    Print(
        "gcm",
        ssl:filter_cipher_suites(Suites, [{cipher, fun(C) -> C == aes_128_gcm end}, {mac, fun(M) -> M == aead end}])
    ),
    Print("prf", ssl:filter_cipher_suites(Suites, [{prf, fun(P) -> P == sha384 end}])),
    Default = ssl:cipher_suites(default, 'tlsv1.2'),
    Chacha = [S || #{cipher := chacha20_poly1305} = S <- Suites],
    Print("prepend suites", ssl:prepend_cipher_suites(Chacha, Default)),
    Print("prepend filters", ssl:prepend_cipher_suites([{key_exchange, fun(K) -> K == ecdhe_ecdsa end}], Default)),
    Print("append suites", ssl:append_cipher_suites(Chacha, Default)),
    Print("append filters", ssl:append_cipher_suites([{cipher, fun(C) -> C == aes_256_gcm end}], Default)),
    io:format("filter of a short map: ~p~n", [
        safe(fun() -> ssl:filter_cipher_suites([#{key_exchange => rsa, cipher => null, mac => sha}], []) end)
    ]),
    io:format("filter of non-lists: ~p~n", [safe(fun() -> ssl:filter_cipher_suites(id(nope), []) end)]).

algorithms() ->
    [
        io:format("signature_algs(~p, ~p): ~p~n", [D, V, safe(fun() -> ssl:signature_algs(id(D), id(V)) end)])
     || D <- [default, all, exclusive, bogus], V <- ['tlsv1.3', 'tlsv1.2', 'dtlsv1.2', 'tlsv1.1', bogus]
    ],
    io:format("eccs(): ~p~n", [ssl:eccs()]),
    [
        io:format("eccs(~p): ~p~n", [V, safe(fun() -> ssl:eccs(id(V)) end)])
     || V <- ['tlsv1.3', 'tlsv1.2', 'tlsv1.1', tlsv1, 'dtlsv1.2', dtlsv1, bogus]
    ],
    io:format("groups(): ~p~n", [ssl:groups()]),
    io:format("groups(default): ~p~n", [ssl:groups(default)]),
    io:format("groups(all): ~p~n", [safe(fun() -> ssl:groups(id(all)) end)]).

versions() ->
    io:format("env before: ~p ~p~n", [
        application:get_env(ssl, protocol_version), application:get_env(ssl, dtls_protocol_version)
    ]),
    io:format("versions: ~p~n", [ssl:versions()]),
    io:format("env after: ~p ~p~n", [
        application:get_env(ssl, protocol_version), application:get_env(ssl, dtls_protocol_version)
    ]),
    Settings = [
        {['tlsv1.2'], ['dtlsv1.2', dtlsv1]},
        {tlsv1, dtlsv1},
        {[sslv2], []},
        {[tlsv1, 'tlsv1.1'], ['dtlsv1.2']},
        {[sslv3], ['dtlsv1.2']},
        {[bogus], ['dtlsv1.2']},
        {['tlsv1.3'], [bogus]}
    ],
    [
        begin
            application:set_env(ssl, protocol_version, Tls),
            application:set_env(ssl, dtls_protocol_version, Dtls),
            io:format("versions with ~p ~p: ~p~n", [Tls, Dtls, safe(fun ssl:versions/0)])
        end
     || {Tls, Dtls} <- Settings
    ],
    application:unset_env(ssl, protocol_version),
    application:unset_env(ssl, dtls_protocol_version).

errors() ->
    Reasons = [
        "a text",
        closed,
        {error, closed},
        {tls_alert, {handshake_failure, "TLS client: In state hello received SERVER ALERT: Fatal - Handshake Failure"}},
        {options, {cacertfile, "ca.pem", {error, enoent}}},
        {options, {certfile, "cert.pem", {error, eacces}}},
        {options, {keyfile, "key.pem", {error, bogus}}},
        {options, {dhfile, "dh.pem", undefined}},
        {options, {socket_options, {packet, bogus}, einval}},
        {options, {socket_options, {packet, bogus}}},
        {options, incompatible, [{verify, verify_peer}, {cacerts, undefined}]},
        {option, server_only, [sni_fun]},
        {options, role_mismatch, [sni_hosts]},
        {options, {missing_version, [{versions, []}]}},
        {options, {option_not_a_key_value_tuple, [bogus]}},
        {options, {no_supported_algorithms, [{signature_algs, []}]}},
        {options, {no_supported_signature_schemes, [{signature_algs_cert, []}]}},
        {options, {insufficient_crypto_support, [{versions, ['tlsv1.3']}]}},
        {options, {versions, [bogus]}},
        einval,
        econnrefused,
        timeout,
        {weird, error}
    ],
    [io:format("format_error(~p): ~p~n", [R, safe(fun() -> ssl:format_error(R) end)]) || R <- Reasons].

application() ->
    io:format("clear before start: ~p~n", [
        case safe(fun ssl:clear_pem_cache/0) of
            {exit, {noproc, {gen_server, call, [ssl_pem_cache, {Request, Pid}, Timeout]}}} when Pid == self() ->
                {exit, noproc, Request, Timeout};
            Other ->
                Other
        end
    ]),
    io:format("start: ~p~n", [ssl:start()]),
    io:format("running: ~p~n", [lists:keymember(ssl, 1, application:which_applications())]),
    io:format("ssl_sup: ~p~n", [is_pid(whereis(ssl_sup))]),
    io:format("clear after start: ~p ~p~n", [ssl:clear_pem_cache(), get(ssl_pem_cache)]),
    io:format("start again: ~p~n", [ssl:start()]),
    io:format("stop: ~p~n", [ssl:stop()]),
    io:format("running after stop: ~p~n", [lists:keymember(ssl, 1, application:which_applications())]),
    io:format("start permanent: ~p~n", [ssl:start(permanent)]),
    io:format("stop: ~p~n", [ssl:stop()]).
