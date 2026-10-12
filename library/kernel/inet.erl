-module(inet).
-export([port/1, sockname/1, peername/1, setopts/2, close/1]).
%% OTP 29 exports Clause does not implement yet: stubs raising error({not_implemented, F, A}),
%% or catalog builtins the runtime puts in their place (docs/library.md#runtime-module-replacements).
-export([
    cancel_monitor/1,
    connect_options/2,
    ensure_sockaddr/1,
    fdopen/6,
    format_error/1,
    gen_tcp_module/1,
    gen_udp_module/1,
    get_rc/0,
    getaddr/2,
    getaddr/3,
    getaddr_tm/3,
    getaddrs/2,
    getaddrs/3,
    getaddrs_tm/3,
    getfd/1,
    gethostbyaddr/1,
    gethostbyaddr/2,
    gethostbyaddr_tm/2,
    gethostbyname/1,
    gethostbyname/2,
    gethostbyname/3,
    gethostbyname_self/2,
    gethostbyname_string/2,
    gethostbyname_tm/3,
    gethostname/0,
    gethostname/1,
    getif/0,
    getif/1,
    getifaddrs/0,
    getifaddrs/1,
    getiflist/0,
    getiflist/1,
    getll/1,
    getopts/2,
    getservbyname/2,
    getservbyport/2,
    getstat/1,
    getstat/2,
    i/0,
    i/1,
    i/2,
    ifget/2,
    ifget/3,
    ifset/2,
    ifset/3,
    inet_backend/0,
    info/1,
    ip/1,
    ipv4_mapped_ipv6_address/1,
    is_ip_address/1,
    is_ipv4_address/1,
    is_ipv6_address/1,
    listen_options/2,
    lock_socket/2,
    monitor/1,
    ntoa/1,
    open/8,
    open_bind/8,
    options/0,
    parse_address/1,
    parse_address/2,
    parse_ipv4_address/1,
    parse_ipv4strict_address/1,
    parse_ipv6_address/1,
    parse_ipv6strict_address/1,
    parse_strict_address/1,
    parse_strict_address/2,
    peernames/1,
    peernames/2,
    popf/1,
    pushf/3,
    sctp_module/1,
    sctp_options/2,
    send/2,
    setpeername/2,
    setsockname/2,
    socket_to_list/1,
    socknames/1,
    socknames/2,
    start_timer/1,
    stats/0,
    stop_timer/1,
    tcp_close/1,
    tcp_controlling_process/2,
    tcp_module/1,
    tcp_module/2,
    timeout/1,
    timeout/2,
    translate_ip/2,
    udp_close/1,
    udp_controlling_process/2,
    udp_module/1,
    udp_module/2,
    udp_options/2
]).
-include("inet.types.hrl").

% The project-owned subset of inet (docs/library.md, docs/ports.md#sockets) for sockets of gen_tcp and gen_udp.

% The local port number of Socket.
port(Socket) ->
    case sockname(Socket) of
        {ok, {_, Port}} -> {ok, Port};
        Error -> Error
    end.

sockname(Socket) -> clause_socket:names(Socket, false).

peername(Socket) -> clause_socket:names(Socket, true).

% Change active, packet and binary or list; other options are accepted and not applied.
setopts(Socket, Options) -> clause_socket:setopts(Socket, Options).

close(Socket) -> clause_socket:close(Socket).

cancel_monitor(_) -> erlang:error({not_implemented, cancel_monitor, 1}).
connect_options(_, _) -> erlang:error({not_implemented, connect_options, 2}).
ensure_sockaddr(_) -> erlang:error({not_implemented, ensure_sockaddr, 1}).
fdopen(_, _, _, _, _, _) -> erlang:error({not_implemented, fdopen, 6}).
format_error(_) -> erlang:error({not_implemented, format_error, 1}).
gen_tcp_module(_) -> erlang:error({not_implemented, gen_tcp_module, 1}).
gen_udp_module(_) -> erlang:error({not_implemented, gen_udp_module, 1}).
get_rc() -> erlang:error({not_implemented, get_rc, 0}).
getaddr(_, _) -> erlang:error({not_implemented, getaddr, 2}).
getaddr(_, _, _) -> erlang:error({not_implemented, getaddr, 3}).
getaddr_tm(_, _, _) -> erlang:error({not_implemented, getaddr_tm, 3}).
getaddrs(_, _) -> erlang:error({not_implemented, getaddrs, 2}).
getaddrs(_, _, _) -> erlang:error({not_implemented, getaddrs, 3}).
getaddrs_tm(_, _, _) -> erlang:error({not_implemented, getaddrs_tm, 3}).
getfd(_) -> erlang:error({not_implemented, getfd, 1}).
gethostbyaddr(_) -> erlang:error({not_implemented, gethostbyaddr, 1}).
gethostbyaddr(_, _) -> erlang:error({not_implemented, gethostbyaddr, 2}).
gethostbyaddr_tm(_, _) -> erlang:error({not_implemented, gethostbyaddr_tm, 2}).
gethostbyname(_) -> erlang:error({not_implemented, gethostbyname, 1}).
gethostbyname(_, _) -> erlang:error({not_implemented, gethostbyname, 2}).
gethostbyname(_, _, _) -> erlang:error({not_implemented, gethostbyname, 3}).
gethostbyname_self(_, _) -> erlang:error({not_implemented, gethostbyname_self, 2}).
gethostbyname_string(_, _) -> erlang:error({not_implemented, gethostbyname_string, 2}).
gethostbyname_tm(_, _, _) -> erlang:error({not_implemented, gethostbyname_tm, 3}).
gethostname() -> erlang:error({not_implemented, gethostname, 0}).
gethostname(_) -> erlang:error({not_implemented, gethostname, 1}).
getif() -> erlang:error({not_implemented, getif, 0}).
getif(_) -> erlang:error({not_implemented, getif, 1}).
getifaddrs() -> erlang:error({not_implemented, getifaddrs, 0}).
getifaddrs(_) -> erlang:error({not_implemented, getifaddrs, 1}).
getiflist() -> erlang:error({not_implemented, getiflist, 0}).
getiflist(_) -> erlang:error({not_implemented, getiflist, 1}).
getll(_) -> erlang:error({not_implemented, getll, 1}).
getopts(_, _) -> erlang:error({not_implemented, getopts, 2}).
getservbyname(_, _) -> erlang:error({not_implemented, getservbyname, 2}).
getservbyport(_, _) -> erlang:error({not_implemented, getservbyport, 2}).
getstat(_) -> erlang:error({not_implemented, getstat, 1}).
getstat(_, _) -> erlang:error({not_implemented, getstat, 2}).
i() -> erlang:error({not_implemented, i, 0}).
i(_) -> erlang:error({not_implemented, i, 1}).
i(_, _) -> erlang:error({not_implemented, i, 2}).
ifget(_, _) -> erlang:error({not_implemented, ifget, 2}).
ifget(_, _, _) -> erlang:error({not_implemented, ifget, 3}).
ifset(_, _) -> erlang:error({not_implemented, ifset, 2}).
ifset(_, _, _) -> erlang:error({not_implemented, ifset, 3}).
inet_backend() -> erlang:error({not_implemented, inet_backend, 0}).
info(_) -> erlang:error({not_implemented, info, 1}).
ip(_) -> erlang:error({not_implemented, ip, 1}).
ipv4_mapped_ipv6_address(_) -> erlang:error({not_implemented, ipv4_mapped_ipv6_address, 1}).
is_ip_address(_) -> erlang:error({not_implemented, is_ip_address, 1}).
is_ipv4_address(_) -> erlang:error({not_implemented, is_ipv4_address, 1}).
is_ipv6_address(_) -> erlang:error({not_implemented, is_ipv6_address, 1}).
listen_options(_, _) -> erlang:error({not_implemented, listen_options, 2}).
lock_socket(_, _) -> erlang:error({not_implemented, lock_socket, 2}).
monitor(_) -> erlang:error({not_implemented, monitor, 1}).
ntoa(_) -> erlang:error({not_implemented, ntoa, 1}).
open(_, _, _, _, _, _, _, _) -> erlang:error({not_implemented, open, 8}).
open_bind(_, _, _, _, _, _, _, _) -> erlang:error({not_implemented, open_bind, 8}).
options() -> erlang:error({not_implemented, options, 0}).
parse_address(_) -> erlang:error({not_implemented, parse_address, 1}).
parse_address(_, _) -> erlang:error({not_implemented, parse_address, 2}).
parse_ipv4_address(_) -> erlang:error({not_implemented, parse_ipv4_address, 1}).
parse_ipv4strict_address(_) -> erlang:error({not_implemented, parse_ipv4strict_address, 1}).
parse_ipv6_address(_) -> erlang:error({not_implemented, parse_ipv6_address, 1}).
parse_ipv6strict_address(_) -> erlang:error({not_implemented, parse_ipv6strict_address, 1}).
parse_strict_address(_) -> erlang:error({not_implemented, parse_strict_address, 1}).
parse_strict_address(_, _) -> erlang:error({not_implemented, parse_strict_address, 2}).
peernames(_) -> erlang:error({not_implemented, peernames, 1}).
peernames(_, _) -> erlang:error({not_implemented, peernames, 2}).
popf(_) -> erlang:error({not_implemented, popf, 1}).
pushf(_, _, _) -> erlang:error({not_implemented, pushf, 3}).
sctp_module(_) -> erlang:error({not_implemented, sctp_module, 1}).
sctp_options(_, _) -> erlang:error({not_implemented, sctp_options, 2}).
send(_, _) -> erlang:error({not_implemented, send, 2}).
setpeername(_, _) -> erlang:error({not_implemented, setpeername, 2}).
setsockname(_, _) -> erlang:error({not_implemented, setsockname, 2}).
socket_to_list(_) -> erlang:error({not_implemented, socket_to_list, 1}).
socknames(_) -> erlang:error({not_implemented, socknames, 1}).
socknames(_, _) -> erlang:error({not_implemented, socknames, 2}).
start_timer(_) -> erlang:error({not_implemented, start_timer, 1}).
stats() -> erlang:error({not_implemented, stats, 0}).
stop_timer(_) -> erlang:error({not_implemented, stop_timer, 1}).
tcp_close(_) -> erlang:error({not_implemented, tcp_close, 1}).
tcp_controlling_process(_, _) -> erlang:error({not_implemented, tcp_controlling_process, 2}).
tcp_module(_) -> erlang:error({not_implemented, tcp_module, 1}).
tcp_module(_, _) -> erlang:error({not_implemented, tcp_module, 2}).
timeout(_) -> erlang:error({not_implemented, timeout, 1}).
timeout(_, _) -> erlang:error({not_implemented, timeout, 2}).
translate_ip(_, _) -> erlang:error({not_implemented, translate_ip, 2}).
udp_close(_) -> erlang:error({not_implemented, udp_close, 1}).
udp_controlling_process(_, _) -> erlang:error({not_implemented, udp_controlling_process, 2}).
udp_module(_) -> erlang:error({not_implemented, udp_module, 1}).
udp_module(_, _) -> erlang:error({not_implemented, udp_module, 2}).
udp_options(_, _) -> erlang:error({not_implemented, udp_options, 2}).
