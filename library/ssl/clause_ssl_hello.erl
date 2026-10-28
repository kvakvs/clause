%% The {handshake, hello} pause of ssl connections (plan 21 step LIB81C2, docs/libraries/ssl.md#connections): the first
%% handshake message the peer sends (a ClientHello to a server, a ServerHello to a client), read from its TLS records
%% before libssl sees them, its extensions as OTP's ssl returns them, and the plaintext alerts a paused handshake ends
%% with.
-module(clause_ssl_hello).
-export([message/1, extensions/2, alert/1]).

-define(HANDSHAKE, 22).
-define(ALERT, 21).
-define(FATAL, 2).
% The largest TLS record (a ciphertext's) and handshake message (OTP's max_handshake_size default) waited for.
-define(MAX_RECORD, 18432).
-define(MAX_MESSAGE, 262144).

%% The first handshake message of the received bytes: {ok, Message}, more while it is incomplete, or invalid when the
%% bytes do not start with it (libssl then reports what is wrong).
message(Received) ->
    message(Received, <<>>).

message(<<?HANDSHAKE, _:16, Length:16, Fragment:Length/binary, Rest/binary>>, Acc) ->
    case <<Acc/binary, Fragment/binary>> of
        <<Type, Size:24, Body:Size/binary, _/binary>> -> {ok, <<Type, Size:24, Body/binary>>};
        <<_, Size:24, _/binary>> when Size > ?MAX_MESSAGE -> invalid;
        Handshake -> message(Rest, Handshake)
    end;
message(<<?HANDSHAKE, _:16, Length:16, _/binary>>, _Acc) when Length > ?MAX_RECORD ->
    invalid;
message(<<?HANDSHAKE, _/binary>>, _Acc) ->
    more;
message(<<>>, _Acc) ->
    more;
message(_, _Acc) ->
    invalid.

%% {ok, Extensions} of a hello message read for the connection's highest version, else error. Certificate
%% authorities are normalized names, as OTP's; names that do not decode are left out.
extensions(Message, #{versions := Versions}) ->
    Local = lists:max([version(V) || V <- Versions]),
    case clause_ssl_algorithms:hello_extensions(Message, Local) of
        #{certificate_authorities := Names} = Extensions when is_list(Names) ->
            {ok, Extensions#{certificate_authorities => [N || DER <- Names, N <- normalized(DER)]}};
        #{} = Extensions ->
            {ok, Extensions};
        error ->
            error
    end.

normalized(DER) ->
    try
        [public_key:pkix_normalize_name(DER)]
    catch
        _:_ -> []
    end.

version('tlsv1.3') -> {3, 4};
version('tlsv1.2') -> {3, 3};
version('tlsv1.1') -> {3, 2};
version(tlsv1) -> {3, 1}.

%% The record of a fatal alert sent before any keys are in use, as a paused handshake is.
alert(Description) ->
    <<?ALERT, 3, 3, 2:16, ?FATAL, Description>>.
