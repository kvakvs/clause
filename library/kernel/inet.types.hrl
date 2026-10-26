%% OTP 29 inet types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([
    address_family/0,
    ancillary_data/0,
    family_address/0,
    hostent/0,
    hostname/0,
    inet_backend/0,
    ip4_address/0,
    ip6_address/0,
    ip_address/0,
    local_address/0,
    port_number/0,
    posix/0,
    returned_non_ip_address/0,
    socket/0,
    socket_address/0,
    socket_getopt/0,
    socket_optval/0,
    socket_protocol/0,
    socket_setopt/0,
    stat_option/0
]).
-record(hostent, {
    h_name :: inet:hostname(),
    h_aliases = [] :: [inet:hostname()],
    h_addrtype :: inet | inet6,
    h_length :: non_neg_integer(),
    h_addr_list = [] :: [inet:ip_address()]
}).
-type hostent() :: #hostent{}.
-type hostname() :: atom() | string().
-type ip4_address() :: {0..255, 0..255, 0..255, 0..255}.
-type ip6_address() ::
    {0..65535, 0..65535, 0..65535, 0..65535, 0..65535, 0..65535, 0..65535, 0..65535}.
-type ip_address() :: ip4_address() | ip6_address().
-type port_number() :: 0..65535.
-type family_address() ::
    inet_address() | inet6_address() | local_address().
-type inet_address() ::
    {inet, {ip4_address() | any | loopback, port_number()}}.
-type inet6_address() ::
    {inet6, {ip6_address() | any | loopback, port_number()}}.
-type local_address() :: {local, File :: binary() | string()}.
-type returned_non_ip_address() ::
    {local, binary()} | {unspec, <<>>} | {undefined, any()}.
-type posix() ::
    eaddrinuse
    | eaddrnotavail
    | eafnosupport
    | ealready
    | econnaborted
    | econnrefused
    | econnreset
    | edestaddrreq
    | ehostdown
    | ehostunreach
    | einprogress
    | eisconn
    | emsgsize
    | enetdown
    | enetunreach
    | enopkg
    | enoprotoopt
    | enotconn
    | enotty
    | enotsock
    | eproto
    | eprotonosupport
    | eprototype
    | esocktnosupport
    | etimedout
    | ewouldblock
    | exbadport
    | exbadseq
    | file:posix().
-type module_socket() ::
    {'$inet', Handler :: module(), Handle :: term()}.
-type socket() :: port() | module_socket().
-type inet_backend() :: {inet_backend, inet | socket}.
-type socket_setopt() ::
    gen_sctp:option() | gen_tcp:option() | gen_udp:option().
-type socket_optval() ::
    gen_sctp:option_value()
    | gen_tcp:option()
    | gen_udp:option()
    | gen_tcp:pktoptions_value().
-type socket_getopt() ::
    gen_sctp:option_name()
    | gen_tcp:option_name()
    | gen_udp:option_name().
-type address_family() :: inet | inet6 | local.
-type socket_protocol() :: tcp | udp | sctp.
-type socket_address() ::
    ip_address() | any | loopback | local_address().
-type stat_option() ::
    recv_cnt
    | recv_max
    | recv_avg
    | recv_oct
    | recv_dvi
    | send_cnt
    | send_max
    | send_avg
    | send_oct
    | send_pend.
-type ancillary_data() ::
    [{tos, byte()} | {tclass, byte()} | {ttl, byte()}].
