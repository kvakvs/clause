%% OTP 29 gen_udp types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([
    ip6_membership/0,
    ip6_multicast_if/0,
    ip_membership/0,
    ip_multicast_if/0,
    membership/0,
    multicast_if/0,
    open_option/0,
    option/0,
    option_name/0,
    socket/0
]).
-type option() ::
    {active, true | false | once | -32768..32767}
    | {add_membership, membership()}
    | {broadcast, boolean()}
    | {buffer, non_neg_integer()}
    | {debug, boolean()}
    | {deliver, port | term}
    | {dontroute, boolean()}
    | {drop_membership, membership()}
    | {exclusiveaddruse, boolean()}
    | {header, non_neg_integer()}
    | {high_msgq_watermark, pos_integer()}
    | {low_msgq_watermark, pos_integer()}
    | {mode, list | binary}
    | list
    | binary
    | {multicast_if, multicast_if()}
    | {multicast_loop, boolean()}
    | {multicast_ttl, non_neg_integer()}
    | {priority, non_neg_integer()}
    | {raw, Protocol :: non_neg_integer(), OptionNum :: non_neg_integer(), ValueBin :: binary()}
    | {read_packets, non_neg_integer()}
    | {recbuf, non_neg_integer()}
    | {reuseaddr, boolean()}
    | {reuseport, boolean()}
    | {reuseport_lb, boolean()}
    | {sndbuf, non_neg_integer()}
    | {tos, non_neg_integer()}
    | {tclass, non_neg_integer()}
    | {ttl, non_neg_integer()}
    | {recvtos, boolean()}
    | {recvtclass, boolean()}
    | {recvttl, boolean()}
    | {ipv6_v6only, boolean()}.
-type option_name() ::
    active
    | broadcast
    | buffer
    | debug
    | deliver
    | dontroute
    | exclusiveaddruse
    | header
    | high_msgq_watermark
    | low_msgq_watermark
    | mode
    | multicast_if
    | multicast_loop
    | multicast_ttl
    | priority
    | {raw, Protocol :: non_neg_integer(), OptionNum :: non_neg_integer(),
        ValueSpec ::
            (ValueSize :: non_neg_integer()) | (ValueBin :: binary())}
    | read_packets
    | recbuf
    | reuseaddr
    | reuseport
    | reuseport_lb
    | sndbuf
    | tos
    | tclass
    | ttl
    | recvtos
    | recvtclass
    | recvttl
    | pktoptions
    | ipv6_v6only.
-type open_option() ::
    {ip, inet:socket_address()}
    | {fd, non_neg_integer()}
    | {ifaddr,
        socket:sockaddr_in()
        | socket:sockaddr_in6()
        | inet:socket_address()}
    | inet:address_family()
    | {port, inet:port_number()}
    | {netns, file:filename_all()}
    | {bind_to_device, binary()}
    | option().
-type socket() :: inet:socket().
-type ip_multicast_if() :: inet:ip4_address().
-type ip6_multicast_if() :: integer().
-type multicast_if() :: ip_multicast_if() | ip6_multicast_if().
-type ip_membership() ::
    {MultiAddress :: inet:ip4_address(), Interface :: inet:ip4_address()}
    | {MultiAddress :: inet:ip4_address(), Address :: inet:ip4_address(), IfIndex :: integer()}.
-type ip6_membership() ::
    {MultiAddress :: inet:ip6_address(), IfIndex :: integer()}.
-type membership() :: ip_membership() | ip6_membership().
