-module(clause_socket_options).
-export([record/2, request/3, decode/2, ancillary/1]).

-define(IS_BYTE(Value), (is_integer(Value) andalso Value >= 0 andalso Value =< 255)).

% The socket options beyond the data mode (docs/ports.md#sockets), created for inet:getopts/2 and inet:setopts/2:
% each option's code in the socket driver (runtime/src/ports/socket_options.cpp) and how its value is encoded.

% {Code, Type} of an option, or none.
info(reuseaddr) -> {1, boolean};
info(keepalive) -> {2, boolean};
info(dontroute) -> {3, boolean};
info(linger) -> {4, linger};
info(broadcast) -> {5, boolean};
info(sndbuf) -> {6, integer};
info(recbuf) -> {7, integer};
info(nodelay) -> {8, boolean};
info(tos) -> {9, integer};
info(tclass) -> {10, integer};
info(ipv6_v6only) -> {11, boolean};
info(multicast_ttl) -> {12, integer};
info(multicast_loop) -> {13, boolean};
info(multicast_if) -> {14, address};
info(add_membership) -> {15, membership};
info(drop_membership) -> {16, membership};
info(exclusiveaddruse) -> {17, boolean};
info(priority) -> {18, integer};
info(reuseport) -> {19, boolean};
info(reuseport_lb) -> {20, boolean};
info(nopush) -> {21, boolean};
info(buffer) -> {32, integer};
info(header) -> {33, integer};
info(deliver) -> {34, deliver};
info(exit_on_close) -> {35, boolean};
info(send_timeout) -> {36, timeout};
info(send_timeout_close) -> {37, boolean};
info(delay_send) -> {38, boolean};
info(high_watermark) -> {39, integer};
info(low_watermark) -> {40, integer};
info(high_msgq_watermark) -> {41, integer};
info(low_msgq_watermark) -> {42, integer};
info(packet_size) -> {43, integer};
info(show_econnreset) -> {44, boolean};
info(read_packets) -> {45, integer};
info(recvtos) -> {46, boolean};
info(recvttl) -> {47, boolean};
info(recvtclass) -> {48, boolean};
info(debug) -> {49, boolean};
info(line_delimiter) -> {50, integer};
info(_) -> none.

% {ok, Record}: option Name with Value as a driver record <<Code, Size, Value/binary>>; error when either is not one.
record(Name, Value) ->
    case info(Name) of
        {Code, Type} -> encoded(Code, value(Type, Value));
        none -> error
    end.

encoded(_, error) -> error;
encoded(Code, Bytes) -> {ok, <<Code, (byte_size(Bytes)), Bytes/binary>>}.

value(boolean, true) -> <<1:64>>;
value(boolean, false) -> <<0:64>>;
value(integer, N) when is_integer(N), N >= -16#80000000, N =< 16#7fffffff -> <<N:64/signed>>;
value(linger, {On, Seconds}) when is_boolean(On), is_integer(Seconds), Seconds >= 0 -> <<(flag(On)):64, Seconds:64>>;
value(address, {_, _, _, _} = Address) -> ipv4(Address);
value(membership, {Group, Interface}) -> membership(ipv4(Group), ipv4(Interface));
value(deliver, port) -> <<0:64>>;
value(deliver, term) -> <<1:64>>;
value(timeout, infinity) -> <<-1:64/signed>>;
value(timeout, N) when is_integer(N), N >= 0, N =< 16#7fffffff -> <<N:64>>;
value(_, _) -> error.

ipv4({_, _, _, _} = Address) ->
    case clause_socket:address(Address) of
        <<4, _:4/binary>> = Bytes -> Bytes;
        _ -> error
    end;
ipv4(_) ->
    error.

membership(error, _) -> error;
membership(_, error) -> error;
membership(Group, Interface) -> <<Group/binary, Interface/binary>>.

flag(true) -> 1;
flag(false) -> 0.

% How getopts reads option Name of a socket of Type (stream or dgram) and Family (4 or 6): {code, Code, ValueType},
% skip for an option the socket type does not have (left out, as inet_drv does), or einval.
request(Name, Type, Family) ->
    case {info(Name), lists:member(Name, unreadable()), lists:member(Name, missing(Type, Family))} of
        {none, _, _} -> einval;
        {_, true, _} -> einval;
        {_, _, true} -> skip;
        {{Code, ValueType}, false, false} -> {code, Code, ValueType}
    end.

% Options getopts refuses, as OTP's inet_drv has no reading for them.
unreadable() -> [add_membership, drop_membership, line_delimiter].

missing(stream, Family) ->
    [broadcast, read_packets, multicast_ttl, multicast_loop, recvtos, recvttl | ipv4_missing(Family) ++ [recvtclass]];
missing(dgram, Family) ->
    [
        nodelay,
        keepalive,
        send_timeout,
        send_timeout_close,
        delay_send,
        high_watermark,
        low_watermark,
        show_econnreset,
        linger
        | ipv4_missing(Family)
    ].

ipv4_missing(4) -> [tclass, recvtclass];
ipv4_missing(_) -> [].

% The value of a driver record's bytes as Type's term.
decode(boolean, <<Value:64>>) -> Value =/= 0;
decode(integer, <<Value:64/signed>>) -> Value;
decode(linger, <<On:64, Seconds:64>>) -> {On =/= 0, Seconds};
decode(address, <<4, A, B, C, D>>) -> {A, B, C, D};
decode(deliver, <<0:64>>) -> port;
decode(deliver, <<_:64>>) -> term;
decode(timeout, <<-1:64/signed>>) -> infinity;
decode(timeout, <<Value:64/signed>>) -> Value.

% {ok, Records} of gen_udp:send/5's ancillary data ({tos | tclass | ttl, Byte}), or error.
ancillary(Data) when is_list(Data) ->
    try
        {ok, <<<<(ancillary_record(Item))/binary>> || Item <- Data>>}
    catch
        throw:error -> error
    end;
ancillary(_) ->
    error.

ancillary_record({tos, Value}) when ?IS_BYTE(Value) -> <<9, 8, Value:64>>;
ancillary_record({tclass, Value}) when ?IS_BYTE(Value) -> <<10, 8, Value:64>>;
ancillary_record({ttl, Value}) when ?IS_BYTE(Value) -> <<22, 8, Value:64>>;
ancillary_record(_) -> throw(error).
