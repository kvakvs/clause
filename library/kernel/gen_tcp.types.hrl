%% OTP 29 gen_tcp types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([
    connect_option/0,
    listen_option/0,
    option/0,
    option_name/0,
    pktoptions_value/0,
    socket/0
]).
-type option() ::
    {active, true | false | once | -32768..32767}
    | {buffer, non_neg_integer()}
    | {debug, boolean()}
    | {delay_send, boolean()}
    | {deliver, port | term}
    | {dontroute, boolean()}
    | {exit_on_close, boolean()}
    | {exclusiveaddruse, boolean()}
    | {header, non_neg_integer()}
    | {high_msgq_watermark, pos_integer()}
    | {high_watermark, non_neg_integer()}
    | {keepalive, boolean()}
    | {keepcnt, integer()}
    | {keepidle, integer()}
    | {keepintvl, integer()}
    | {linger, {boolean(), non_neg_integer()}}
    | {low_msgq_watermark, pos_integer()}
    | {low_watermark, non_neg_integer()}
    | {mode, list | binary}
    | list
    | binary
    | {nodelay, boolean()}
    | {packet,
        0
        | 1
        | 2
        | 4
        | raw
        | sunrm
        | asn1
        | cdr
        | fcgi
        | line
        | tpkt
        | http
        | httph
        | http_bin
        | httph_bin}
    | {packet_size, non_neg_integer()}
    | {priority, non_neg_integer()}
    | {raw, Protocol :: non_neg_integer(), OptionNum :: non_neg_integer(), ValueBin :: binary()}
    | {recbuf, non_neg_integer()}
    | {reuseaddr, boolean()}
    | {reuseport, boolean()}
    | {reuseport_lb, boolean()}
    | {send_timeout, timeout()}
    | {send_timeout_close, boolean()}
    | {show_econnreset, boolean()}
    | {sndbuf, non_neg_integer()}
    | {tos, non_neg_integer()}
    | {tclass, non_neg_integer()}
    | {ttl, non_neg_integer()}
    | {recvtos, boolean()}
    | {recvtclass, boolean()}
    | {recvttl, boolean()}
    | {user_timeout, non_neg_integer()}
    | {ipv6_v6only, boolean()}.
-type pktoptions_value() :: {pktoptions, inet:ancillary_data()}.
-type option_name() ::
    active
    | buffer
    | debug
    | delay_send
    | deliver
    | dontroute
    | exit_on_close
    | exclusiveaddruse
    | header
    | high_msgq_watermark
    | high_watermark
    | keepalive
    | keepcnt
    | keepidle
    | keepintvl
    | linger
    | low_msgq_watermark
    | low_watermark
    | mode
    | nodelay
    | packet
    | packet_size
    | priority
    | {raw, Protocol :: non_neg_integer(), OptionNum :: non_neg_integer(),
        ValueSpec ::
            (ValueSize :: non_neg_integer()) | (ValueBin :: binary())}
    | recbuf
    | reuseaddr
    | reuseport
    | reuseport_lb
    | send_timeout
    | send_timeout_close
    | show_econnreset
    | sndbuf
    | tos
    | tclass
    | ttl
    | recvtos
    | recvtclass
    | recvttl
    | pktoptions
    | user_timeout
    | ipv6_v6only.
-type connect_option() ::
    {fd, Fd :: non_neg_integer()}
    | inet:address_family()
    | {ifaddr,
        socket:sockaddr_in()
        | socket:sockaddr_in6()
        | inet:socket_address()}
    | {ip, inet:socket_address()}
    | {port, inet:port_number()}
    | {tcp_module, module()}
    | {netns, file:filename_all()}
    | {bind_to_device, binary()}
    | option().
-type listen_option() ::
    {fd, Fd :: non_neg_integer()}
    | inet:address_family()
    | {ifaddr,
        socket:sockaddr_in()
        | socket:sockaddr_in6()
        | inet:socket_address()}
    | {ip, inet:socket_address()}
    | {port, inet:port_number()}
    | {backlog, B :: non_neg_integer()}
    | {tcp_module, module()}
    | {netns, file:filename_all()}
    | {bind_to_device, binary()}
    | option().
-type socket() :: inet:socket().
