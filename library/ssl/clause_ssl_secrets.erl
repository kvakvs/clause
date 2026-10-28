%% The secrets of one ssl connection (plan 21 step LIB81D1, docs/libraries/ssl.md#secrets): the key log items of
%% connection_information/2 and of the keep_secrets funs, from the secrets libssl logs as engine events, the TLS 1.3
%% traffic secrets after KeyUpdates (RFC 8446 section 7.2), and the TLS 1.2 PRF of ssl:prf/5's legacy form.
-module(clause_ssl_secrets).
-export([new/1, events/4, keylog/3, handshake_failed/3, prf/5]).

-define(HS_CLIENT, <<"CLIENT_HANDSHAKE_TRAFFIC_SECRET">>).
-define(HS_SERVER, <<"SERVER_HANDSHAKE_TRAFFIC_SECRET">>).
-define(TRAFFIC_CLIENT, <<"CLIENT_TRAFFIC_SECRET_0">>).
-define(TRAFFIC_SERVER, <<"SERVER_TRAFFIC_SECRET_0">>).
-define(PRE_1_3, <<"CLIENT_RANDOM">>).

%% The secrets of a connection whose keep_secrets option is Keep: the logged secrets by label, and the current TLS 1.3
%% traffic secret and KeyUpdate count of each side.
new(Keep) ->
    #{keep => Keep, logged => #{}, client => undefined, server => undefined, updates => #{client => 0, server => 0}}.

%% Take the engine's events: secrets and KeyUpdates update Keys (and go to a {keylog, Fun}); the rest are returned
%% for the connection, oldest first.
events(Events, Engine, Role, Keys) ->
    lists:foldl(fun(Event, {K, Rest}) -> event(Event, Engine, Role, K, Rest) end, {Keys, []}, Events).

event({secret, Label, Secret}, Engine, _Role, #{logged := Logged} = Keys0, Rest) ->
    Keys = traffic(Label, Secret, Keys0#{logged => Logged#{Label => Secret}}),
    {logged(Label, Engine, Keys), Rest};
event(key_update_sent, Engine, Role, Keys, Rest) ->
    {updated(Role, Engine, Keys), Rest};
event(key_update_received, Engine, Role, Keys, Rest) ->
    {updated(opposite(Role), Engine, Keys), Rest};
event(Other, _Engine, _Role, Keys, Rest) ->
    {Keys, Rest ++ [Other]}.

traffic(?TRAFFIC_CLIENT, Secret, Keys) -> Keys#{client => Secret};
traffic(?TRAFFIC_SERVER, Secret, Keys) -> Keys#{server => Secret};
traffic(_, _, Keys) -> Keys.

% A {keylog, Fun} sees the handshake secrets together, then each traffic secret, as OTP's ssl calls it.
logged(Label, Engine, #{keep := {keylog, Fun}, logged := Logged} = Keys) ->
    Items =
        case Label of
            ?HS_CLIENT when is_map_key(?HS_SERVER, Logged) -> handshake_items(Engine, Keys);
            ?HS_SERVER when is_map_key(?HS_CLIENT, Logged) -> handshake_items(Engine, Keys);
            ?TRAFFIC_CLIENT -> [traffic_item(client, Engine, Keys)];
            ?TRAFFIC_SERVER -> [traffic_item(server, Engine, Keys)];
            ?PRE_1_3 -> [item(?PRE_1_3, Engine, maps:get(?PRE_1_3, Logged))];
            _ -> []
        end,
    call(Fun, Items, Engine),
    Keys;
logged(_Label, _Engine, Keys) ->
    Keys.

% A KeyUpdate of Side's keys: its next traffic secret (HKDF-Expand-Label(Secret, "traffic upd", "", Hash.length)).
updated(Side, Engine, #{updates := Updates} = Keys0) ->
    case maps:get(Side, Keys0) of
        undefined ->
            Keys0;
        Secret ->
            Next = expand_label(hash(Engine), Secret, <<"traffic upd">>, byte_size(Secret)),
            Keys = Keys0#{Side => Next, updates => Updates#{Side => maps:get(Side, Updates) + 1}},
            case Keys of
                #{keep := {keylog, Fun}} -> call(Fun, [traffic_item(Side, Engine, Keys)], Engine);
                _ -> ok
            end,
            Keys
    end.

%% The keylog item of connection_information/2: OTP's NSS key log lines, the handshake secrets while the peer has
%% not updated its keys. A keylog fun has no such item: OTP's connection_information/2 fails then.
keylog(Engine, Role, #{keep := true, updates := Updates} = Keys) ->
    case maps:get(protocol, clause_ssl_engine:info(Engine)) of
        'tlsv1.3' ->
            Handshake =
                case maps:get(opposite(Role), Updates) of
                    0 -> handshake_items(Engine, Keys);
                    _ -> []
                end,
            Handshake ++ [traffic_item(client, Engine, Keys), traffic_item(server, Engine, Keys)];
        _ ->
            #{master_secret := Master} = clause_ssl_engine:info(Engine),
            [item(?PRE_1_3, Engine, Master)]
    end;
keylog(_Engine, _Role, #{keep := false}) ->
    undefined;
keylog(_Engine, _Role, #{keep := Keep}) ->
    erlang:error({case_clause, {keep_secrets, Keep}}).

%% A TLS 1.3 handshake that failed with a fatal alert gives a {keylog_hs, Fun} the secrets it had. A libssl client
%% derives its own handshake secret only after the server's Finished, so its failures before then lack that line.
handshake_failed(Engine, _Role, #{keep := {keylog_hs, Fun}, logged := Logged} = Keys) ->
    Handshake = [item(L, Engine, maps:get(L, Logged)) || L <- [?HS_CLIENT, ?HS_SERVER], is_map_key(L, Logged)],
    Traffic = [
        traffic_item(Side, Engine, Keys)
     || {Side, L} <- [{client, ?TRAFFIC_CLIENT}, {server, ?TRAFFIC_SERVER}],
        is_map_key(L, Logged)
    ],
    case Handshake of
        [] -> ok;
        _ -> call(Fun, Handshake ++ Traffic, Engine)
    end;
handshake_failed(_Engine, _Role, _Keys) ->
    ok.

handshake_items(Engine, #{logged := Logged}) ->
    [item(?HS_CLIENT, Engine, maps:get(?HS_CLIENT, Logged)), item(?HS_SERVER, Engine, maps:get(?HS_SERVER, Logged))].

traffic_item(Side, Engine, #{updates := Updates} = Keys) ->
    Prefix =
        case Side of
            client -> "CLIENT_TRAFFIC_SECRET_";
            server -> "SERVER_TRAFFIC_SECRET_"
        end,
    item(Prefix ++ integer_to_list(maps:get(Side, Updates)), Engine, maps:get(Side, Keys)).

% A key log line: label, client random and secret, in OTP's upper-case hexadecimal.
item(Label, Engine, Secret) ->
    #{client_random := Random} = clause_ssl_engine:info(Engine),
    lists:flatten([unicode:characters_to_list(Label), $\s, hex(Random), $\s, hex(Secret)]).

hex(Binary) -> binary_to_list(binary:encode_hex(Binary)).

% The fun of keep_secrets, called as OTP's ssl calls it: a failure is logged, not raised.
call(_Fun, [], _Engine) ->
    ok;
call(Fun, Items, Engine) ->
    #{client_random := Random} = clause_ssl_engine:info(Engine),
    try
        Fun(#{items => Items, client_random => Random})
    catch
        _:function_clause:Stack ->
            logger:warning("user keylogging fun failed with function_clause: ~p ~n", [tl(Stack)]);
        _:Reason:Stack ->
            logger:warning("user keylogging fun failed : ~p ~p ~n", [Reason, Stack])
    end,
    ok.

opposite(client) -> server;
opposite(server) -> client.

% The hash of the negotiated suite's key schedule.
hash(Engine) ->
    #{prf := Hash} = clause_ssl_algorithms:code_suite(maps:get(cipher_suite, clause_ssl_engine:info(Engine))),
    Hash.

expand_label(Hash, Secret, Label, Length) ->
    Full = <<"tls13 ", Label/binary>>,
    Info = <<Length:16, (byte_size(Full)):8, Full/binary, 0:8>>,
    binary:part(crypto:mac(hmac, Hash, Secret, <<Info/binary, 1>>), 0, Length).

%% TLS 1.2's PRF (RFC 5246 section 5) over the Hash HMAC, as ssl:prf/5's legacy form computes it.
prf(Hash, Secret, Label, Seed, Length) ->
    LabelSeed = list_to_binary([Label, Seed]),
    p_hash(Hash, Secret, LabelSeed, LabelSeed, Length, <<>>).

p_hash(_Hash, _Secret, _A, _Seed, Length, Acc) when byte_size(Acc) >= Length ->
    binary:part(Acc, 0, Length);
p_hash(Hash, Secret, A0, Seed, Length, Acc) ->
    A = crypto:mac(hmac, Hash, Secret, A0),
    p_hash(
        Hash,
        Secret,
        A,
        Seed,
        Length,
        <<Acc/binary, (crypto:mac(hmac, Hash, Secret, <<A/binary, Seed/binary>>))/binary>>
    ).
