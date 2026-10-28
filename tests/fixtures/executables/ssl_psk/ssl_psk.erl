%% TLS 1.2 pre-shared keys over loopback (plan 21 step LIB81D5): PSK and ECDHE_PSK suites whose keys user_lookup_fun
%% gives (the server's fun the client's identity, the client's fun the server's hint), servers with and without a
%% hint, clients with and without an identity, keys that differ, a failing lookup, and the options' errors.
-module(ssl_psk).
-export([main/1]).

-define(KEY, <<"0123456789abcdef">>).

main(_) ->
    ok = ssl:start(),
    [
        show(Label, seen(Label, connect(Suite, ServerOptions, ClientOptions)))
     || {Label, Suite, ServerOptions, ClientOptions} <- cases()
    ],
    show("option errors", [connect_error(Options) || Options <- errors()]),
    io:format("ssl_psk: ok~n").

show(Label, Value) -> io:format("~s ~p~n", [Label, Value]).

%% With other keys the server fails the client's Finished; OTP's client then sees the transport close, a client that
%% reads the server's alert sees bad_record_mac.
seen("other keys", {{error, _}, Server, Negotiated, Lookups}) -> {failed, Server, Negotiated, Lookups};
seen(_Label, Seen) -> Seen.

cases() ->
    Psk = "TLS_PSK_WITH_AES_128_GCM_SHA256",
    % (libssl has no ECDHE_PSK suite with AES-GCM.)
    Ecdhe = "TLS_ECDHE_PSK_WITH_AES_128_CBC_SHA256",
    Hint = [{psk_identity, "server hint"}],
    Identity = [{psk_identity, "client identity"}],
    [
        {"psk", Psk, Hint, Identity},
        {"ecdhe_psk", Ecdhe, Hint, Identity},
        {"psk without hint", Psk, [], Identity},
        {"ecdhe_psk without hint", Ecdhe, [], Identity},
        {"psk without identity", Psk, Hint, []},
        {"other keys", Psk, Hint, [lookup(other_client) | Identity]},
        {"unknown identity", Psk, Hint, [{psk_identity, "unknown"}]}
    ].

%% The lookup fun of a side: it reports its arguments, gives ?KEY (another key on the other_client side) and error
%% for "unknown".
lookup(Side) ->
    Self = self(),
    Fun = fun(psk, Name, State) ->
        Self ! {lookup, Side, Name, State},
        case {Side, Name} of
            {other_client, _} -> {ok, <<"fedcba9876543210">>};
            {_, <<"unknown">>} -> error;
            _ -> {ok, ?KEY}
        end
    end,
    {user_lookup_fun, {Fun, Side}}.

%% One connection over Suite: both sides' results with their alerts, the negotiated suite, and the lookups made.
connect(Suite, ServerOptions, ClientOptions) ->
    Suites = [ssl:str_to_suite(Suite)],
    Common = [{versions, ['tlsv1.2']}, {ciphers, Suites}, {active, false}, {log_level, none}],
    {ok, Listen} = ssl:listen(0, [{ip, {127, 0, 0, 1}}, lookup(server) | ServerOptions ++ Common]),
    {ok, {_, Port}} = ssl:sockname(Listen),
    Self = self(),
    spawn(fun() ->
        {ok, Accepted} = ssl:transport_accept(Listen),
        Self ! {server, ssl:handshake(Accepted, 5000)}
    end),
    Client = ssl:connect("localhost", Port, [{verify, verify_none}, lookup(client) | ClientOptions ++ Common], 5000),
    % (A later user_lookup_fun in ClientOptions replaces the client's.)
    Server =
        receive
            {server, R} -> R
        after 5000 -> timeout
        end,
    Negotiated =
        case Client of
            {ok, Socket} ->
                {ok, [{selected_cipher_suite, S}]} = ssl:connection_information(Socket, [selected_cipher_suite]),
                ssl:suite_to_str(S);
            _ ->
                none
        end,
    [ssl:close(S) || {ok, S} <- [Client, Server]],
    ssl:close(Listen),
    {alert(Client), alert(Server), Negotiated, lists:sort(lookups())}.

lookups() ->
    receive
        {lookup, Side, Name, State} -> [{Side, Name, State} | lookups()]
    after 0 -> []
    end.

alert({ok, _}) -> ok;
alert({error, {tls_alert, {Description, _Text}}}) -> {error, Description};
alert(Other) -> Other.

errors() ->
    [
        [{user_lookup_fun, {fun(_, _) -> error end, state}}],
        [{user_lookup_fun, nofun}],
        [{versions, ['tlsv1.3']}, {user_lookup_fun, {fun(_, _, _) -> error end, state}}],
        [{versions, ['tlsv1.3']}, {psk_identity, "id"}],
        [{psk_identity, lists:duplicate(70000, $a)}]
    ].

connect_error(Options) ->
    case ssl:connect("localhost", 1, [{verify, verify_none} | Options], 1000) of
        {error, {options, {user_lookup_fun, {Fun, State}}}} when is_function(Fun) ->
            {error, {options, {user_lookup_fun, {{'fun', element(2, erlang:fun_info(Fun, arity))}, State}}}};
        {error, {options, {psk_identity, {psk_identity, Long}}}} ->
            {error, {options, {psk_identity, {psk_identity, length(Long)}}}};
        {error, {options, _} = Reason} ->
            {error, Reason};
        {error, {options, _, _} = Reason} ->
            {error, Reason};
        {error, _} ->
            accepted
    end.
