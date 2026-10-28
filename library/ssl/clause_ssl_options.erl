%% The options of ssl's connect, listen and handshake (plan 21 step LIB81C1, docs/libraries/ssl.md#connections),
%% written from OTP's documentation and observed results: the list splits into ssl options, the socket options ssl
%% emulates (mode, packet, header, active) and the transport's own, and each part is validated with OTP's error terms.
%% A configuration is #{role, ssl, emulated, socket, cb_info}; the ssl map holds the validated values by name.
-module(clause_ssl_options).
-export([client/2, server/1, update/3, emulated/2, split_socket/1]).
-export([load_files/1, cert_chain/2, issued_by/3, engine_options/1, ssl_option_names/0]).

-include_lib("public_key/include/public_key.hrl").

-define(EMULATED, [mode, packet, packet_size, header, active, line_delimiter]).
-define(LOG_LEVELS, [none, all, emergency, alert, critical, error, warning, notice, info, debug]).
-define(DEFAULT_DEPTH, 10).
-define(DEFAULT_CB_INFO, {gen_tcp, tcp, tcp_closed, tcp_error, tcp_passive}).

%% The configuration of ssl:connect/2,3,4 to Host (a name, an address or undefined for an upgraded socket).
client(Options, Host) -> configure(Options, #{role => client, host => Host}).

%% The configuration of ssl:listen/2 and of a server handshake on an upgraded socket.
server(Options) -> configure(Options, #{role => server, host => undefined}).

%% A configuration updated by the ssl options of ssl:handshake/2,3 (the rest are ignored, as OTP's are).
update(#{ssl := Ssl0} = Config, Options, Role) ->
    try
        {SslList, _, _} = split(Options),
        {ok, Config#{ssl => ssl_options(SslList, Ssl0, #{role => Role, host => maps:get(host, Ssl0, undefined)})}}
    catch
        throw:{error, _} = Error -> Error
    end.

configure(Options, Env) ->
    try
        {SslList, EmulatedList, Socket} = split(Options),
        Ssl = ssl_options(SslList, #{}, Env),
        {ok, #{
            role => maps:get(role, Env),
            ssl => Ssl,
            emulated => emulated(EmulatedList, default_emulated()),
            socket => Socket,
            cb_info => maps:get(cb_info, Ssl)
        }}
    catch
        throw:{error, _} = Error -> Error
    end.

%% Options split into ssl options, emulated socket options and transport options, each in the given order.
split(Options) ->
    Expanded = proplists:expand([{binary, [{mode, binary}]}, {list, [{mode, list}]}], Options),
    lists:foldr(fun split_option/2, {[], [], []}, Expanded).

split_option({Key, _} = Option, {Ssl, Emulated, Socket}) when is_atom(Key) ->
    case {lists:member(Key, ssl_option_names()), lists:member(Key, ?EMULATED)} of
        {true, _} -> {[Option | Ssl], Emulated, Socket};
        {_, true} -> {Ssl, [Option | Emulated], Socket};
        _ -> {Ssl, Emulated, [Option | Socket]}
    end;
split_option(Option, {Ssl, Emulated, Socket}) when Option == inet; Option == inet6; element(1, Option) == raw ->
    {Ssl, Emulated, [Option | Socket]};
split_option(Option, _) ->
    throw({error, {options, {option_not_a_key_value_tuple, Option}}}).

%% Socket options of ssl:setopts/2 and getopts/2: {Emulated, Transport}, both in the given order.
split_socket(Options) ->
    lists:partition(fun(Option) -> lists:member(option_key(Option), ?EMULATED) end, Options).

option_key({Key, _}) -> Key;
option_key(Key) -> Key.

ssl_option_names() ->
    [
        alpn_advertised_protocols,
        alpn_preferred_protocols,
        anti_replay,
        beast_mitigation,
        cacertfile,
        cacerts,
        cb_info,
        cert,
        certs_keys,
        certfile,
        certificate_authorities,
        ciphers,
        client_preferred_next_protocols,
        client_renegotiation,
        cookie,
        crl_cache,
        crl_check,
        customize_hostname_check,
        depth,
        dh,
        dhfile,
        early_data,
        eccs,
        erl_dist,
        fail_if_no_peer_cert,
        fallback,
        handshake,
        hibernate_after,
        honor_cipher_order,
        honor_ecc_order,
        keep_secrets,
        key,
        keyfile,
        key_update_at,
        ktls,
        log_alert,
        log_level,
        max_handshake_size,
        middlebox_comp_mode,
        max_fragment_length,
        next_protocol_selector,
        next_protocols_advertised,
        stapling,
        padding_check,
        partial_chain,
        password,
        protocol,
        psk_groups,
        psk_identity,
        receiver_spawn_opts,
        renegotiate_at,
        reuse_session,
        reuse_sessions,
        secure_renegotiate,
        sender_spawn_opts,
        server_name_indication,
        session_tickets,
        stateless_tickets_seed,
        signature_algs,
        signature_algs_cert,
        sni_fun,
        sni_hosts,
        srp_identity,
        ssl_imp,
        supported_groups,
        use_ticket,
        use_srtp,
        user_lookup_fun,
        verify,
        verify_fun,
        cert_policy_opts,
        allow_any_ca_purpose,
        versions
    ].

%%--------------------------------------------------------------------------------------------------------------------
%% Emulated socket options
%%--------------------------------------------------------------------------------------------------------------------

default_emulated() ->
    #{mode => list, packet => 0, packet_size => 0, header => 0, active => true, line_delimiter => $\n}.

%% Emulated options validated over Emulated0, as OTP's tls_socket validates them.
emulated(Options, Emulated0) ->
    lists:foldl(fun({Key, Value}, Emulated) -> Emulated#{Key => emulated_value(Key, Value)} end, Emulated0, Options).

emulated_value(mode, Value) when Value == list; Value == binary -> Value;
emulated_value(packet, Value) when is_atom(Value); is_integer(Value) -> Value;
emulated_value(packet_size, Value) when is_integer(Value) -> Value;
emulated_value(header, Value) when is_integer(Value) -> Value;
emulated_value(active, Value) when Value == true; Value == false; Value == once -> Value;
emulated_value(active, Value) when is_integer(Value), Value >= -32768, Value =< 32767 -> Value;
emulated_value(line_delimiter, Value) when is_integer(Value), Value >= 0, Value =< 255 -> Value;
emulated_value(Key, Value) -> throw({error, {options, {Key, Value}}}).

%%--------------------------------------------------------------------------------------------------------------------
%% ssl options, in OTP's order of checks
%%--------------------------------------------------------------------------------------------------------------------

%% The ssl options of a list (the last of a repeated key wins) over Ssl0, as OTP checks them for a role.
ssl_options(List, Ssl0, Env) ->
    User = maps:from_list(List),
    Steps = [
        fun protocol/3,
        fun versions/3,
        fun verification/3,
        fun certs/3,
        fun server_name/3,
        fun tickets/3,
        fun signature_algs/3,
        fun alpn/3,
        fun role_only/3,
        fun booleans/3,
        fun sessions/3,
        fun groups_and_ciphers/3,
        fun handshake/3,
        fun others/3
    ],
    lists:foldl(fun(Step, Ssl) -> Step(User, Ssl, Env) end, Ssl0#{host => maps:get(host, Env)}, Steps).

% A value the user gave ({new, V}), the one set before ({old, V}) or the default ({default, Default}).
get_opt(Key, Default, User, Ssl) ->
    case User of
        #{Key := Value} ->
            {new, Value};
        _ ->
            case Ssl of
                #{Key := Value} -> {old, Value};
                _ -> {default, Default}
            end
    end.

option_error(Key, Value) -> throw({error, {options, {Key, Value}}}).

one_of(Key, Values, Default, User, Ssl) ->
    {_, Value} = get_opt(Key, Default, User, Ssl),
    case lists:member(Value, Values) of
        true -> Value;
        false -> option_error(Key, Value)
    end.

protocol(User, Ssl, _Env) ->
    Protocol = one_of(protocol, [tls], tls, User, Ssl),
    LogLevel =
        case User of
            #{log_alert := false} -> one_of(log_level, ?LOG_LEVELS, warning, User, Ssl);
            _ -> one_of(log_level, ?LOG_LEVELS, notice, User, Ssl)
        end,
    Ssl#{protocol => Protocol, log_level => LogLevel, cb_info => cb_info(User, Ssl)}.

cb_info(User, Ssl) ->
    case get_opt(cb_info, ?DEFAULT_CB_INFO, User, Ssl) of
        {_, {M, Data, Closed, Error}} when is_atom(M) -> {M, Data, Closed, Error, tcp_passive};
        {_, {M, _, _, _, _} = CbInfo} when is_atom(M) -> CbInfo;
        {_, Value} -> option_error(cb_info, Value)
    end.

%% Versions: highest first; a gap between TLS 1.3 and an older version is missing 'tlsv1.2'.
versions(User, Ssl, _Env) ->
    case get_opt(versions, default, User, Ssl) of
        {default, _} -> Ssl#{versions => clause_ssl_algorithms:tls_versions()};
        {old, Versions} -> Ssl#{versions => Versions};
        {new, Versions} -> Ssl#{versions => validate_versions(Versions)}
    end.

validate_versions(Versions) ->
    Known = ['tlsv1.3', 'tlsv1.2', 'tlsv1.1', tlsv1],
    [
        case lists:member(V, Known) of
            true -> V;
            false -> option_error(V, {versions, Versions})
        end
     || V <- Versions
    ],
    Gap =
        lists:member('tlsv1.3', Versions) andalso not lists:member('tlsv1.2', Versions) andalso
            (lists:member('tlsv1.1', Versions) orelse lists:member(tlsv1, Versions)),
    Gap andalso throw({error, {options, missing_version, {'tlsv1.2', {versions, Versions}}}}),
    Versions == [] andalso option_error(versions, Versions),
    [V || V <- Known, lists:member(V, Versions)].

%% Verification: verify, cacerts, partial_chain, fail_if_no_peer_cert, depth and verify_fun.
verification(User, Ssl0, #{role := Role} = Env) ->
    Default =
        case Role of
            client -> verify_peer;
            server -> verify_none
        end,
    Verify = one_of(verify, [verify_none, verify_peer], Default, User, Ssl0),
    Ssl1 = cacerts(User, Ssl0#{verify => Verify}, Env),
    {_, Partial} = get_opt(partial_chain, fun(_) -> unknown_ca end, User, Ssl1),
    is_function(Partial, 1) orelse option_error(partial_chain, Partial),
    FailNoPeer = boolean(fail_if_no_peer_cert, Role == server andalso Verify == verify_peer, User, Ssl1),
    Role == client andalso FailNoPeer andalso throw({error, {option, server_only, fail_if_no_peer_cert}}),
    FailNoPeer andalso Verify == verify_none andalso
        throw({error, {options, incompatible, [{verify, verify_none}, {fail_if_no_peer_cert, true}]}}),
    Ssl1#{
        partial_chain => Partial,
        fail_if_no_peer_cert => FailNoPeer,
        depth => depth(User, Ssl1),
        verify_fun => verify_fun(User, Ssl1),
        customize_hostname_check => customize_hostname_check(User, Ssl1)
    }.

cacerts(User, #{verify := Verify} = Ssl, #{role := Role}) ->
    CaCerts =
        case get_opt(cacerts, undefined, User, Ssl) of
            {_, undefined} -> undefined;
            {_, Certs} when is_list(Certs) -> [cacert(C) || C <- Certs];
            {_, Other} -> option_error(cacerts, Other)
        end,
    CaCertFile =
        case get_opt(cacertfile, undefined, User, Ssl) of
            {_, undefined} -> undefined;
            {_, File} -> filename(cacertfile, File)
        end,
    Role == client andalso Verify == verify_peer andalso CaCerts == undefined andalso CaCertFile == undefined andalso
        throw({error, {options, incompatible, [{verify, verify_peer}, {cacerts, undefined}]}}),
    Ssl#{cacerts => CaCerts, cacertfile => CaCertFile}.

cacert(Der) when is_binary(Der) -> Der;
cacert(Other) -> option_error(cacerts, Other).

depth(User, Ssl) ->
    case get_opt(depth, ?DEFAULT_DEPTH, User, Ssl) of
        {_, Depth} when is_integer(Depth), Depth >= 0, Depth =< 255 -> Depth;
        {_, Other} -> option_error(depth, Other)
    end.

verify_fun(User, Ssl) ->
    case get_opt(verify_fun, undefined, User, Ssl) of
        {_, undefined} -> undefined;
        {_, {Fun, _} = VerifyFun} when is_function(Fun, 3); is_function(Fun, 4) -> VerifyFun;
        {_, Other} -> option_error(verify_fun, Other)
    end.

customize_hostname_check(User, Ssl) ->
    case get_opt(customize_hostname_check, [], User, Ssl) of
        {_, Value} when is_list(Value) -> Value;
        {_, Other} -> option_error(customize_hostname_check, Other)
    end.

filename(_Key, File) when is_binary(File), File =/= <<>> -> File;
filename(_Key, [_ | _] = File) -> File;
filename(Key, File) -> option_error(Key, File).

boolean(Key, Default, User, Ssl) ->
    case get_opt(Key, Default, User, Ssl) of
        {_, Value} when is_boolean(Value) -> Value;
        {_, Other} -> option_error(Key, Other)
    end.

%% Certificates and keys: certs_keys, else cert/key/certfile/keyfile/password, as a list of maps.
certs(User, Ssl, _Env) ->
    CertsKeys =
        case get_opt(certs_keys, undefined, User, Ssl) of
            {_, undefined} -> [old_cert_key(User, Ssl)];
            {_, List} when is_list(List) -> [cert_key(M) || M <- List];
            {_, Other} -> option_error(certs_keys, Other)
        end,
    Ssl#{certs_keys => [C || C <- CertsKeys, C =/= #{}]}.

old_cert_key(User, Ssl) ->
    Entries = [{K, V} || K <- [cert, key, certfile, keyfile, password], {_, V} <- [get_opt(K, undefined, User, Ssl)]],
    cert_key(maps:from_list([{K, V} || {K, V} <- Entries, V =/= undefined])).

cert_key(#{} = Options) ->
    maps:fold(fun cert_key_value/3, #{}, Options);
cert_key(Other) ->
    option_error(certs_keys, Other).

cert_key_value(cert, Der, Acc) when is_binary(Der) -> Acc#{cert => [Der]};
cert_key_value(cert, [Der | _] = Ders, Acc) when is_binary(Der) -> Acc#{cert => Ders};
cert_key_value(key, {Type, Der} = Key, Acc) when is_binary(Der) ->
    case lists:member(Type, ['RSAPrivateKey', 'DSAPrivateKey', 'ECPrivateKey', 'PrivateKeyInfo']) of
        true -> Acc#{key => Key};
        false -> option_error(key, Key)
    end;
cert_key_value(key, #{algorithm := _} = Key, Acc) ->
    Acc#{key => Key};
cert_key_value(certfile, File, Acc) ->
    Acc#{certfile => filename(certfile, File)};
cert_key_value(keyfile, File, Acc) ->
    Acc#{keyfile => filename(keyfile, File)};
cert_key_value(password, Password, Acc) when is_list(Password); is_binary(Password); is_function(Password, 0) ->
    Acc#{password => Password};
cert_key_value(Key, Value, _) ->
    option_error(Key, Value).

%% server_name_indication: a client's host name, disable or the host it connects to.
server_name(User, Ssl, #{role := server}) ->
    client_only(server_name_indication, User),
    Ssl;
server_name(User, Ssl, #{role := client} = Env) ->
    case get_opt(server_name_indication, undefined, User, Ssl) of
        {_, disable} -> Ssl#{server_name_indication => disable};
        {_, undefined} -> Ssl#{server_name_indication => sni_default(maps:get(host, Env))};
        {_, [_ | _] = Name} -> Ssl#{server_name_indication => Name};
        {_, Other} -> option_error(server_name_indication, Other)
    end.

% A host name connected to is the default name, without a trailing dot.
sni_default(Host) when is_list(Host) -> string:strip(Host, right, $.);
sni_default(_) -> undefined.

client_only(Key, User) ->
    maps:is_key(Key, User) andalso throw({error, {option, client_only, Key}}).

server_only(Key, User) ->
    maps:is_key(Key, User) andalso throw({error, {option, server_only, Key}}).

signature_algs(User, Ssl, _Env) ->
    Known = known_schemes(),
    Check = fun(Key) ->
        case get_opt(Key, undefined, User, Ssl) of
            {_, undefined} ->
                undefined;
            {_, [_ | _] = Algs} = Found ->
                lists:all(fun(A) -> lists:member(A, Known) orelse hash_sign(A) end, Algs) orelse
                    option_error(Key, Algs),
                element(2, Found);
            {_, Other} ->
                option_error(Key, Other)
        end
    end,
    Ssl#{signature_algs => Check(signature_algs), signature_algs_cert => Check(signature_algs_cert)}.

known_schemes() ->
    clause_ssl_algorithms:signature_algs(all, 'tlsv1.3') ++ clause_ssl_algorithms:signature_algs(all, 'tlsv1.2').

hash_sign({Hash, Sign}) ->
    lists:member(Hash, [sha512, sha384, sha256, sha224, sha, md5]) andalso lists:member(Sign, [rsa, ecdsa, dsa]);
hash_sign(_) ->
    false.

%% ALPN: a client advertises, a server prefers; each protocol is 1 to 255 bytes.
alpn(User, Ssl, #{role := client}) ->
    server_only(alpn_preferred_protocols, User),
    Ssl#{alpn_advertised_protocols => protocols(alpn_advertised_protocols, User, Ssl)};
alpn(User, Ssl, #{role := server}) ->
    client_only(alpn_advertised_protocols, User),
    Ssl#{alpn_preferred_protocols => protocols(alpn_preferred_protocols, User, Ssl)}.

protocols(Key, User, Ssl) ->
    case get_opt(Key, undefined, User, Ssl) of
        {_, undefined} ->
            undefined;
        {_, Protocols} when is_list(Protocols) ->
            [
                case P of
                    <<_, _/binary>> when byte_size(P) =< 255 -> P;
                    _ -> option_error(Key, {invalid_protocol, P})
                end
             || P <- Protocols
            ];
        {_, Other} ->
            option_error(Key, Other)
    end.

%% Options only one role takes.
role_only(User, Ssl, #{role := client}) ->
    [server_only(K, User) || K <- [honor_cipher_order, honor_ecc_order, client_renegotiation, sni_fun, sni_hosts]],
    Ssl;
role_only(User, Ssl, #{role := server}) ->
    [client_only(K, User) || K <- [fallback, psk_identity, srp_identity]],
    Ssl.

booleans(User, Ssl, _Env) ->
    Defaults = [
        {honor_cipher_order, false},
        {honor_ecc_order, false},
        {secure_renegotiate, true},
        {client_renegotiation, true},
        {middlebox_comp_mode, true},
        {allow_any_ca_purpose, false},
        {certificate_authorities, true}
    ],
    Ssl1 = lists:foldl(fun({K, D}, S) -> S#{K => boolean(K, D, User, S)} end, Ssl, Defaults),
    Ssl1#{keep_secrets => keep_secrets(User, Ssl1)}.

%% Session reuse (LIB81D2a): a client's reuse_sessions (true, false or save) and reuse_session (an id or {Id, Data});
%% a server's reuse_sessions (false by default when it verifies clients) and reuse_session policy fun. Both are
%% options of TLS 1.2 and older.
sessions(User, Ssl, #{role := client}) ->
    Reuse = one_of(reuse_sessions, [true, false, save], true, User, Ssl),
    Session =
        case get_opt(reuse_session, undefined, User, Ssl) of
            {new, Id} when is_binary(Id) -> Id;
            {new, {Id, Data} = Ref} when is_binary(Id), is_binary(Data) -> Ref;
            {new, Bad} -> option_error(reuse_session, Bad);
            {_, Value} -> Value
        end,
    before_tls13([reuse_sessions, reuse_session], User, Ssl),
    Ssl#{reuse_sessions => Reuse, reuse_session => Session};
sessions(User, #{verify := Verify} = Ssl, #{role := server}) ->
    Reuse = boolean(reuse_sessions, Verify == verify_none, User, Ssl),
    Policy =
        case get_opt(reuse_session, fun(_, _, _, _) -> true end, User, Ssl) of
            {_, Fun} when is_function(Fun, 4) -> Fun;
            {_, Bad} -> option_error(reuse_session, Bad)
        end,
    before_tls13([reuse_sessions, reuse_session], User, Ssl),
    Ssl#{reuse_sessions => Reuse, reuse_session => Policy}.

% Options given for TLS 1.2 and older while only TLS 1.3 is configured, as OTP's.
before_tls13(Keys, User, #{versions := ['tlsv1.3'] = Versions}) ->
    [throw({error, {options, incompatible, [K, {versions, Versions}]}}) || K <- Keys, is_map_key(K, User)],
    ok;
before_tls13(_Keys, _User, _Ssl) ->
    ok.

%% TLS 1.3 session tickets (LIB81D2b): a client's session_tickets (disabled, manual or auto), use_ticket and
%% early_data; a server's session_tickets (disabled, stateful, stateless, or either _with_cert), early_data,
%% anti_replay and stateless_tickets_seed. OTP checks them in this order, with these errors.
tickets(User, #{versions := Versions} = Ssl, #{role := client}) ->
    Tickets = one_of(session_tickets, [disabled, manual, auto], disabled, User, Ssl),
    Tickets =/= disabled andalso tls13_only(session_tickets, Versions),
    UseTicket =
        case get_opt(use_ticket, undefined, User, Ssl) of
            {new, NotList} when not is_list(NotList) -> option_error(use_ticket, NotList);
            {_, Value} -> Value
        end,
    Tickets == manual andalso is_list(UseTicket) andalso same_server(UseTicket, Ssl),
    UseTicket == [] andalso option_error(use_ticket, UseTicket),
    UseTicket =/= undefined andalso Tickets =/= manual andalso
        incompatible([{use_ticket, UseTicket}, {session_tickets, Tickets}]),
    EarlyData = binary_option(early_data, User, Ssl),
    is_binary(EarlyData) andalso Tickets == disabled andalso incompatible([early_data, {session_tickets, disabled}]),
    is_binary(EarlyData) andalso Tickets == manual andalso UseTicket == undefined andalso
        incompatible([early_data, {session_tickets, manual}, {use_ticket, undefined}]),
    [server_only(K, User) || K <- [anti_replay, stateless_tickets_seed]],
    Ssl#{session_tickets => Tickets, use_ticket => UseTicket, early_data => EarlyData};
tickets(User, #{versions := Versions} = Ssl, #{role := server}) ->
    Modes = [disabled, stateful, stateless, stateful_with_cert, stateless_with_cert],
    Tickets = one_of(session_tickets, Modes, disabled, User, Ssl),
    Tickets =/= disabled andalso tls13_only(session_tickets, Versions),
    EarlyData = one_of(early_data, [enabled, disabled], disabled, User, Ssl),
    Tickets == disabled andalso EarlyData == enabled andalso incompatible([early_data, {session_tickets, disabled}]),
    Stateless = lists:member(Tickets, [stateless, stateless_with_cert]),
    AntiReplay = anti_replay(get_opt(anti_replay, undefined, User, Ssl), Stateless, Tickets),
    Seed = binary_option(stateless_tickets_seed, User, Ssl),
    Seed =/= undefined andalso not Stateless andalso
        incompatible([stateless_tickets_seed, {session_tickets, Tickets}]),
    EarlyData == enabled andalso Stateless andalso AntiReplay == undefined andalso
        logger:warning("early_data enabled without anti_replay; 0-RTT data is replayable"),
    client_only(use_ticket, User),
    Ssl#{
        session_tickets => Tickets, early_data => EarlyData, anti_replay => AntiReplay, stateless_tickets_seed => Seed
    }.

% An option of TLS 1.3 only, given with versions without it.
tls13_only(Key, Versions) ->
    lists:member('tlsv1.3', Versions) orelse incompatible([Key, {versions, Versions}]).

incompatible(Options) -> throw({error, {options, incompatible, Options}}).

% Manual tickets are for the server the client names.
same_server(Tickets, Ssl) ->
    Name = maps:get(server_name_indication, Ssl, undefined),
    [option_error(ticket_for_other_SNI, {T, Name}) || T <- Tickets, not is_map(T) orelse maps:get(sni, T) =/= Name].

binary_option(Key, User, Ssl) ->
    case get_opt(Key, undefined, User, Ssl) of
        {new, Value} when not is_binary(Value) -> option_error(Key, Value);
        {_, Value} -> Value
    end.

% The Bloom filters of anti_replay ({WindowSeconds, HashFunctions, Bits}), for stateless tickets only.
anti_replay({_, undefined}, _Stateless, _Tickets) -> undefined;
anti_replay({_, Value}, false, Tickets) -> incompatible([{anti_replay, Value}, {session_tickets, Tickets}]);
anti_replay({_, '10k'}, true, _Tickets) -> {10, 5, 72985};
anti_replay({_, '100k'}, true, _Tickets) -> {10, 5, 729845};
anti_replay({_, {_, _, _} = Filters}, true, _Tickets) -> Filters;
anti_replay({_, Value}, true, _Tickets) -> option_error(anti_replay, Value).

%% keep_secrets: a boolean, or {keylog | keylog_hs, Fun} (LIB81D1).
keep_secrets(User, Ssl) ->
    case get_opt(keep_secrets, false, User, Ssl) of
        {_, Value} when is_boolean(Value) -> Value;
        {_, {Type, Fun} = Value} when (Type == keylog orelse Type == keylog_hs), is_function(Fun) -> Value;
        {_, Other} -> option_error(keep_secrets, Other)
    end.

%% Groups (TLS 1.3) and cipher suites, as two-byte codes the versions take.
groups_and_ciphers(User, #{versions := Versions} = Ssl, _Env) ->
    Groups =
        case get_opt(supported_groups, undefined, User, Ssl) of
            {_, undefined} -> clause_ssl_algorithms:groups(default);
            {_, [_ | _] = Names} -> valid_groups(Names);
            {_, Other} -> option_error(supported_groups, Other)
        end,
    Ciphers =
        case get_opt(ciphers, [], User, Ssl) of
            {old, Codes} -> Codes;
            {_, Value} -> cipher_codes(Value, Versions)
        end,
    Ssl#{supported_groups => Groups, ciphers => Ciphers}.

valid_groups(Names) ->
    case [N || N <- Names, lists:member(N, clause_ssl_algorithms:groups())] of
        [] -> option_error(supported_groups, none_valid);
        Groups -> Groups
    end.

cipher_codes(Value, Versions) ->
    try codes(Value, Versions) of
        [] -> default_codes(Versions);
        Codes -> Codes
    catch
        _:_ -> option_error(ciphers, Value)
    end.

codes([], _) ->
    [];
codes([#{} | _] = Suites, Versions) ->
    known_codes([code(S) || S <- Suites], Versions);
codes([Tuple | _] = Suites, Versions) when is_tuple(Tuple) ->
    known_codes([code(tuple_suite(S)) || S <- Suites], Versions);
codes([<<_, _>> | _] = Codes, Versions) ->
    known_codes([C || <<C:16>> <- Codes], Versions);
codes([[_ | _] | _] = Names, Versions) ->
    known_codes([code(name_suite(N)) || N <- Names], Versions);
codes([_ | _] = Text, Versions) ->
    codes(string:lexemes(Text, ":"), Versions).

code(Suite) ->
    case clause_ssl_algorithms:suite_code(Suite) of
        undefined -> erlang:error(badarg);
        Code -> Code
    end.

tuple_suite({Kex, Cipher, Mac}) -> #{key_exchange => Kex, cipher => Cipher, mac => Mac, prf => default_prf};
tuple_suite({Kex, Cipher, Mac, Prf}) -> #{key_exchange => Kex, cipher => Cipher, mac => Mac, prf => Prf}.

name_suite(Name) ->
    case ssl:str_to_suite(Name) of
        #{} = Suite -> Suite;
        {error, _} -> erlang:error(badarg)
    end.

% The codes among the suites the versions take, else the default suites.
known_codes(Codes, Versions) ->
    All = [code(S) || S <- all_suites(Versions)],
    [C || C <- Codes, lists:member(C, All)].

all_suites(['tlsv1.3']) ->
    ssl:cipher_suites(exclusive, 'tlsv1.3');
all_suites(['tlsv1.3', Next | _]) ->
    all_suites(['tlsv1.3']) ++ ssl:cipher_suites(all, Next) ++ ssl:cipher_suites(anonymous, Next);
all_suites([Version | _]) ->
    ssl:cipher_suites(all, Version) ++ ssl:cipher_suites(anonymous, Version).

default_codes(['tlsv1.3']) ->
    [code(S) || S <- ssl:filter_cipher_suites(ssl:cipher_suites(exclusive, 'tlsv1.3'), [])];
default_codes([Version | _]) ->
    [code(S) || S <- ssl:filter_cipher_suites(ssl:cipher_suites(default, Version), [])].

handshake(User, Ssl, _Env) ->
    Ssl#{handshake => one_of(handshake, [hello, full], full, User, Ssl)}.

%% The other ssl options, kept as given.
others(User, Ssl, _Env) ->
    Handled = maps:keys(Ssl) ++ [cert, key, certfile, keyfile, password, cacertfile, log_alert, ssl_imp],
    maps:merge(Ssl, maps:without(Handled, User)).

%%--------------------------------------------------------------------------------------------------------------------
%% Certificates for the engine
%%--------------------------------------------------------------------------------------------------------------------

%% A configuration whose certfile, keyfile and cacertfile options are read: {ok, Config} or OTP's
%% {error, {options, {Key, {File, Reason}}}}.
load_files(#{ssl := Ssl} = Config) ->
    try
        CertsKeys = [cert_key_files(C) || C <- maps:get(certs_keys, Ssl)],
        {ok, Config#{ssl => Ssl#{certs_keys => CertsKeys, cacerts => cacert_file(Ssl)}}}
    catch
        throw:{error, _} = Error -> Error
    end.

cert_key_files(#{certfile := File} = CertKey) ->
    Certs = [Der || {'Certificate', Der, not_encrypted} <- pem_entries(certfile, File)],
    KeyFile = maps:get(keyfile, CertKey, File),
    cert_key_files(maps:without([certfile], CertKey#{cert => Certs, keyfile => KeyFile}));
cert_key_files(#{keyfile := File} = CertKey) ->
    Password = maps:get(password, CertKey, ""),
    Keys = [key(E, Password) || {Type, _, _} = E <- pem_entries(keyfile, File), private_key(Type)],
    case Keys of
        [Key | _] -> maps:without([keyfile], CertKey#{key => Key});
        [] -> option_error(keyfile, {File, no_key})
    end;
cert_key_files(CertKey) ->
    CertKey.

cacert_file(#{cacertfile := undefined, cacerts := CaCerts}) ->
    CaCerts;
cacert_file(#{cacertfile := File, cacerts := CaCerts}) ->
    Read = [Der || {'Certificate', Der, not_encrypted} <- pem_entries(cacertfile, File)],
    case CaCerts of
        undefined -> Read;
        _ -> CaCerts ++ Read
    end.

pem_entries(Key, File) ->
    case file:read_file(File) of
        {ok, Pem} -> public_key:pem_decode(Pem);
        {error, Reason} -> option_error(Key, {File, Reason})
    end.

private_key(Type) -> lists:member(Type, ['RSAPrivateKey', 'DSAPrivateKey', 'ECPrivateKey', 'PrivateKeyInfo']).

key({Type, Der, not_encrypted}, _) ->
    {Type, Der};
key(Entry, Password0) ->
    Password =
        case is_function(Password0) of
            true -> Password0();
            false -> Password0
        end,
    Decoded = public_key:pem_entry_decode(Entry, Password),
    Type = element(1, Decoded),
    {Type, public_key:der_encode(Type, Decoded)}.

%% The chain a peer sends with Cert: the certificate and its issuers among CaCerts, as OTP's certificate_chain.
cert_chain([Leaf], CaCerts) when is_list(CaCerts) ->
    issuers(Leaf, CaCerts, [Leaf]);
cert_chain(Chain, _) ->
    Chain.

issuers(Cert, CaCerts, Chain) when length(Chain) < 10 ->
    Otp = public_key:pkix_decode_cert(Cert, otp),
    case public_key:pkix_is_self_signed(Otp) of
        true ->
            lists:reverse(Chain);
        false ->
            Issued = [Ca || Ca <- CaCerts, issued_by(Cert, Otp, public_key:pkix_decode_cert(Ca, otp))],
            case [Ca || Ca <- Issued, not lists:member(Ca, Chain)] of
                [Issuer | _] -> issuers(Issuer, CaCerts, [Issuer | Chain]);
                [] -> lists:reverse(Chain)
            end
    end;
issuers(_, _, Chain) ->
    lists:reverse(Chain).

%% Whether Candidate issued a certificate (DER and decoded): its name and its key's signature, as OTP's find_issuer.
issued_by(Der, Otp, Candidate) ->
    public_key:pkix_is_issuer(Otp, Candidate) andalso
        try
            public_key:pkix_verify(Der, public_key(Candidate))
        catch
            _:_ -> false
        end.

public_key(#'OTPCertificate'{tbsCertificate = #'OTPTBSCertificate'{subjectPublicKeyInfo = Info}}) ->
    #'OTPSubjectPublicKeyInfo'{subjectPublicKey = Key, algorithm = #'PublicKeyAlgorithm'{algorithm = Algorithm} = Alg} =
        Info,
    case {Key, Algorithm} of
        {#'RSAPublicKey'{}, _} -> Key;
        {_, ?'id-Ed25519'} -> {Key, {namedCurve, Algorithm}};
        {_, ?'id-Ed448'} -> {Key, {namedCurve, Algorithm}};
        _ -> {Key, Alg#'PublicKeyAlgorithm'.parameters}
    end.

%% The engine's context options of a configuration whose certificate files are loaded.
engine_options(#{role := Role, ssl := Ssl}) ->
    CaCerts = maps:get(cacerts, Ssl, undefined),
    Certs = [{cert_chain(Cert, CaCerts), Key} || #{cert := Cert, key := Key} <- maps:get(certs_keys, Ssl)],
    Base = #{
        versions => maps:get(versions, Ssl),
        ciphers => maps:get(ciphers, Ssl),
        honor_cipher_order => maps:get(honor_cipher_order, Ssl),
        groups => maps:get(supported_groups, Ssl),
        certs => Certs,
        verify => maps:get(verify, Ssl),
        fail_if_no_peer_cert => maps:get(fail_if_no_peer_cert, Ssl)
    },
    maps:merge(Base, maps:from_list(engine_extras(Role, Ssl, CaCerts))).

engine_extras(Role, Ssl, CaCerts) ->
    Alpn =
        case Role of
            client -> maps:get(alpn_advertised_protocols, Ssl);
            server -> maps:get(alpn_preferred_protocols, Ssl)
        end,
    Optional = [
        {alpn, Alpn},
        {signature_algs, maps:get(signature_algs, Ssl)},
        {client_cas, client_cas(Role, Ssl, CaCerts)},
        {keylog, maps:get(keep_secrets, Ssl) =/= false orelse undefined},
        {client_renegotiation, client_renegotiation(Role, Ssl)},
        {session_cache, session_cache(Role, Ssl)},
        {tickets, tickets(Role, Ssl)},
        {early_data, early_data(Role, Ssl)}
    ],
    [{K, V} || {K, V} <- Optional, V =/= undefined].

client_renegotiation(server, #{client_renegotiation := Allow}) -> Allow;
client_renegotiation(client, _) -> undefined.

% Added for session reuse (LIB81D2a): a server that reuses TLS 1.2 sessions keeps them in Erlang's cache.
session_cache(server, #{reuse_sessions := true, versions := Versions}) when Versions =/= ['tlsv1.3'] ->
    clause_ssl_session:lifetime();
session_cache(_, _) ->
    undefined.

% Added for TLS 1.3 session tickets (LIB81D2b): a client keeps tickets; a server issues stateless or stateful ones with
% the count, lifetime and store size of the application environment.
tickets(client, #{session_tickets := Tickets}) when Tickets =/= disabled ->
    true;
tickets(server, #{session_tickets := Tickets, stateless_tickets_seed := Seed}) when Tickets =/= disabled ->
    Mode =
        case lists:member(Tickets, [stateless, stateless_with_cert]) of
            true -> stateless;
            false -> stateful
        end,
    {Mode, clause_ssl_tickets:server_amount(), clause_ssl_tickets:server_lifetime(),
        clause_ssl_tickets:server_store_size(), Seed};
tickets(_, _) ->
    undefined.

% Added for TLS 1.3 early data (LIB81D2c): a server that takes early data, up to the size its tickets name, with the
% window of its anti_replay filters.
early_data(server, #{early_data := enabled, anti_replay := AntiReplay}) ->
    Window =
        case AntiReplay of
            {Seconds, _, _} -> Seconds;
            undefined -> undefined
        end,
    {clause_ssl_tickets:max_early_data(), Window};
early_data(_, _) ->
    undefined.

client_cas(server, #{verify := verify_peer, certificate_authorities := true}, [_ | _] = CaCerts) -> CaCerts;
client_cas(_, _, _) -> undefined.
