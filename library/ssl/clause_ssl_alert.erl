%% TLS alerts of ssl connections (plan 21 step LIB81C1, docs/libraries/ssl.md#connections): OTP's description atoms
%% and texts, the {tls_alert, {Description, Text}} error terms and the alert log entries. The text names Clause's
%% connection state but no source location (docs/differences.md#ssl).
-module(clause_ssl_alert).
-export([error_reason/4, log/5, description/1]).

-define(DEPTH, 20).

%% The error term of an alert: closed for close_notify, else {tls_alert, {Description, Text}}.
error_reason(0, _, _, _) ->
    closed;
error_reason(Code, Origin, Role, Context) ->
    {tls_alert, {description(Code), text(Code, Origin, Role, Context)}}.

%% Log an alert as OTP does at notice level, unless the connection's log_level is above it.
log(Code, Origin, Role, Context, LogLevel) ->
    case lists:member(LogLevel, [notice, info, debug, all]) of
        true -> logger:notice("~ts", [text(Code, Origin, Role, Context)]);
        false -> ok
    end.

% "TLS client: In state certify generated CLIENT ALERT: Fatal - Unknown CA\n", with the reason when there is one.
text(Code, Origin, Role, {State, Level, Reason}) ->
    Prefix = io_lib:format("TLS ~p: In state ~p ", [Role, State]),
    Alert = alert_text(Origin, Role, Level, Code),
    Detail =
        case {Origin, Reason} of
            {sent, undefined} -> "\n";
            {sent, _} -> io_lib:format("\n ~P", [Reason, ?DEPTH]);
            {received, _} -> "\n"
        end,
    lists:flatten([Prefix, Alert, Detail]).

alert_text(sent, Role, Level, Code) ->
    ["generated ", string:uppercase(atom_to_list(Role)), " ALERT: ", level(Level), description_text(Code)];
alert_text(received, Role, Level, Code) ->
    ["received ", string:uppercase(atom_to_list(peer(Role))), " ALERT: ", level(Level), description_text(Code)].

peer(client) -> server;
peer(server) -> client.

level(1) -> "Warning - ";
level(_) -> "Fatal - ".

%% The atom of an alert description.
description(Code) ->
    case lists:keyfind(Code, 1, descriptions()) of
        {_, Atom, _} -> Atom;
        false -> 'unsupported/unknown_alert'
    end.

description_text(Code) ->
    case lists:keyfind(Code, 1, descriptions()) of
        {_, _, Text} -> Text;
        false -> lists:flatten(io_lib:format("unsupported/unknown alert: ~p", [Code]))
    end.

descriptions() ->
    [
        {0, close_notify, "Close Notify"},
        {10, unexpected_message, "Unexpected Message"},
        {20, bad_record_mac, "Bad Record MAC"},
        {21, decryption_failed_reserved, "Decryption Failed Reserved"},
        {22, record_overflow, "Record Overflow"},
        {30, decompression_failure, "Decompression Failure"},
        {40, handshake_failure, "Handshake Failure"},
        {41, no_certificate_reserved, "No Certificate Reserved"},
        {42, bad_certificate, "Bad Certificate"},
        {43, unsupported_certificate, "Unsupported Certificate"},
        {44, certificate_revoked, "Certificate Revoked"},
        {45, certificate_expired, "Certificate Expired"},
        {46, certificate_unknown, "Certificate Unknown"},
        {47, illegal_parameter, "Illegal Parameter"},
        {48, unknown_ca, "Unknown CA"},
        {49, access_denied, "Access Denied"},
        {50, decode_error, "Decode Error"},
        {51, decrypt_error, "Decrypt Error"},
        {60, export_restriction, "Export Restriction"},
        {70, protocol_version, "Protocol Version"},
        {71, insufficient_security, "Insufficient Security"},
        {80, internal_error, "Internal Error"},
        {86, inappropriate_fallback, "Inappropriate Fallback"},
        {90, user_canceled, "User Canceled"},
        {100, no_renegotiation, "No Renegotiation"},
        {109, missing_extension, "Missing Extension"},
        {110, unsupported_extension, "Unsupported Extension"},
        {111, certificate_unobtainable, "Certificate Unobtainable"},
        {112, unrecognized_name, "Unrecognized Name"},
        {113, bad_certificate_status_response, "Bad Certificate Status Response"},
        {114, bad_certificate_hash_value, "Bad Certificate Hash Value"},
        {115, unknown_psk_identity, "Unknown Psk Identity"},
        {116, certificate_required, "Certificate Required"},
        {120, no_application_protocol, "No Application Protocol"}
    ].
