%% The ssl socket term (plan 21 step LIB81C1), with the fields of OTP's record: a connection's transport socket, its
%% connection process (also the payload sender) and transport module; a listening socket's transport socket and
%% {Configuration, EngineContext}.
-record(sslsocket, {
    socket_handle,
    connection_handler,
    payload_sender,
    transport_cb,
    connection_cb,
    tab,
    listener_config
}).
