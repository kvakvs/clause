%% OTP's inet_db as libraries call it (plan 21 step MIM7, docs/libraries/library.md#undocumented-modules): the host name
%% and the resolver options. Clause has no inet_db server: the rest is declined.
-module(inet_db).
-export([gethostname/0, res_option/1]).
%% OTP 29 exports declined: stubs raising error({not_implemented, F, A})
%% (docs/libraries/library.md#runtime-module-replacements).
-export([
    add_alt_ns/1,
    add_alt_ns/2,
    add_host/2,
    add_hosts/1,
    add_ns/1,
    add_ns/2,
    add_rc/1,
    add_rc_bin/1,
    add_rc_list/1,
    add_resolv/1,
    add_rr/1,
    add_rr/5,
    add_search/1,
    add_socks_methods/1,
    add_socks_noproxy/1,
    clear_cache/0,
    clear_hosts/0,
    del_alt_ns/1,
    del_alt_ns/2,
    del_host/1,
    del_ns/1,
    del_ns/2,
    del_rr/4,
    del_search/1,
    del_socks_methods/0,
    del_socks_methods/1,
    del_socks_noproxy/1,
    eq_domains/2,
    get_hosts_file/0,
    get_rc/0,
    get_searchlist/0,
    getbyname/2,
    gethostbyaddr/2,
    handle_call/3,
    handle_cast/2,
    handle_info/2,
    init/1,
    ins_alt_ns/1,
    ins_alt_ns/2,
    ins_ns/1,
    ins_ns/2,
    ins_search/1,
    lookup_socket/1,
    put_socket_type/2,
    register_socket/2,
    res_check_option/2,
    res_gethostbyaddr/3,
    res_hostent_by_domain/3,
    res_option/2,
    res_update_conf/0,
    res_update_hosts/0,
    reset/0,
    sctp_module/0,
    set_cache_refresh/1,
    set_cache_size/1,
    set_dnssec_ok/1,
    set_domain/1,
    set_edns/1,
    set_hostname/1,
    set_hosts_file/1,
    set_inet6/1,
    set_lookup/1,
    set_recurse/1,
    set_resolv_conf/1,
    set_retry/1,
    set_sctp_module/1,
    set_servfail_retry_timeout/1,
    set_socks_port/1,
    set_socks_server/1,
    set_tcp_module/1,
    set_timeout/1,
    set_udp_module/1,
    set_udp_payload_size/1,
    set_usevc/1,
    socks_option/1,
    start/0,
    start_link/0,
    stop/0,
    take_socket_type/1,
    tcp_module/0,
    terminate/2,
    tolower/1,
    udp_module/0,
    unregister_socket/1
]).

gethostname() ->
    {ok, Name} = inet:gethostname(),
    Name.

% A resolver option, as inet_res uses it unless a call gives another; badarg for other names.
res_option(Option) ->
    Config = clause_inet_config:config(),
    case Option of
        domain -> maps:get(domain, Config);
        search -> maps:get(search, Config);
        lookup -> [native];
        hosts_file -> maps:get(hosts_file, Config);
        resolv_conf -> maps:get(resolv_conf, Config);
        hosts_file_name -> maps:get(hosts_file, Config);
        resolv_conf_name -> maps:get(resolv_conf, Config);
        _ -> res_default(Option, maps:get(nameservers, Config))
    end.

res_default(Option, Servers) when Option =:= nameserver; Option =:= nameservers -> Servers;
res_default(Option, _) when Option =:= alt_nameserver; Option =:= alt_nameservers -> [];
res_default(timeout, _) ->
    2000;
res_default(retry, _) ->
    3;
res_default(servfail_retry_timeout, _) ->
    1500;
res_default(udp_payload_size, _) ->
    1280;
res_default(recurse, _) ->
    true;
res_default(random, _) ->
    true;
res_default(Option, _) when
    Option =:= inet6; Option =:= usevc; Option =:= edns; Option =:= dnssec_ok
->
    false;
res_default(_, _) ->
    erlang:error(badarg).

add_alt_ns(_) -> erlang:error({not_implemented, add_alt_ns, 1}).
add_alt_ns(_, _) -> erlang:error({not_implemented, add_alt_ns, 2}).
add_host(_, _) -> erlang:error({not_implemented, add_host, 2}).
add_hosts(_) -> erlang:error({not_implemented, add_hosts, 1}).
add_ns(_) -> erlang:error({not_implemented, add_ns, 1}).
add_ns(_, _) -> erlang:error({not_implemented, add_ns, 2}).
add_rc(_) -> erlang:error({not_implemented, add_rc, 1}).
add_rc_bin(_) -> erlang:error({not_implemented, add_rc_bin, 1}).
add_rc_list(_) -> erlang:error({not_implemented, add_rc_list, 1}).
add_resolv(_) -> erlang:error({not_implemented, add_resolv, 1}).
add_rr(_) -> erlang:error({not_implemented, add_rr, 1}).
add_rr(_, _, _, _, _) -> erlang:error({not_implemented, add_rr, 5}).
add_search(_) -> erlang:error({not_implemented, add_search, 1}).
add_socks_methods(_) -> erlang:error({not_implemented, add_socks_methods, 1}).
add_socks_noproxy(_) -> erlang:error({not_implemented, add_socks_noproxy, 1}).
clear_cache() -> erlang:error({not_implemented, clear_cache, 0}).
clear_hosts() -> erlang:error({not_implemented, clear_hosts, 0}).
del_alt_ns(_) -> erlang:error({not_implemented, del_alt_ns, 1}).
del_alt_ns(_, _) -> erlang:error({not_implemented, del_alt_ns, 2}).
del_host(_) -> erlang:error({not_implemented, del_host, 1}).
del_ns(_) -> erlang:error({not_implemented, del_ns, 1}).
del_ns(_, _) -> erlang:error({not_implemented, del_ns, 2}).
del_rr(_, _, _, _) -> erlang:error({not_implemented, del_rr, 4}).
del_search(_) -> erlang:error({not_implemented, del_search, 1}).
del_socks_methods() -> erlang:error({not_implemented, del_socks_methods, 0}).
del_socks_methods(_) -> erlang:error({not_implemented, del_socks_methods, 1}).
del_socks_noproxy(_) -> erlang:error({not_implemented, del_socks_noproxy, 1}).
eq_domains(_, _) -> erlang:error({not_implemented, eq_domains, 2}).
get_hosts_file() -> erlang:error({not_implemented, get_hosts_file, 0}).
get_rc() -> erlang:error({not_implemented, get_rc, 0}).
get_searchlist() -> erlang:error({not_implemented, get_searchlist, 0}).
getbyname(_, _) -> erlang:error({not_implemented, getbyname, 2}).
gethostbyaddr(_, _) -> erlang:error({not_implemented, gethostbyaddr, 2}).
handle_call(_, _, _) -> erlang:error({not_implemented, handle_call, 3}).
handle_cast(_, _) -> erlang:error({not_implemented, handle_cast, 2}).
handle_info(_, _) -> erlang:error({not_implemented, handle_info, 2}).
init(_) -> erlang:error({not_implemented, init, 1}).
ins_alt_ns(_) -> erlang:error({not_implemented, ins_alt_ns, 1}).
ins_alt_ns(_, _) -> erlang:error({not_implemented, ins_alt_ns, 2}).
ins_ns(_) -> erlang:error({not_implemented, ins_ns, 1}).
ins_ns(_, _) -> erlang:error({not_implemented, ins_ns, 2}).
ins_search(_) -> erlang:error({not_implemented, ins_search, 1}).
lookup_socket(_) -> erlang:error({not_implemented, lookup_socket, 1}).
put_socket_type(_, _) -> erlang:error({not_implemented, put_socket_type, 2}).
register_socket(_, _) -> erlang:error({not_implemented, register_socket, 2}).
res_check_option(_, _) -> erlang:error({not_implemented, res_check_option, 2}).
res_gethostbyaddr(_, _, _) -> erlang:error({not_implemented, res_gethostbyaddr, 3}).
res_hostent_by_domain(_, _, _) -> erlang:error({not_implemented, res_hostent_by_domain, 3}).
res_option(_, _) -> erlang:error({not_implemented, res_option, 2}).
res_update_conf() -> erlang:error({not_implemented, res_update_conf, 0}).
res_update_hosts() -> erlang:error({not_implemented, res_update_hosts, 0}).
reset() -> erlang:error({not_implemented, reset, 0}).
sctp_module() -> erlang:error({not_implemented, sctp_module, 0}).
set_cache_refresh(_) -> erlang:error({not_implemented, set_cache_refresh, 1}).
set_cache_size(_) -> erlang:error({not_implemented, set_cache_size, 1}).
set_dnssec_ok(_) -> erlang:error({not_implemented, set_dnssec_ok, 1}).
set_domain(_) -> erlang:error({not_implemented, set_domain, 1}).
set_edns(_) -> erlang:error({not_implemented, set_edns, 1}).
set_hostname(_) -> erlang:error({not_implemented, set_hostname, 1}).
set_hosts_file(_) -> erlang:error({not_implemented, set_hosts_file, 1}).
set_inet6(_) -> erlang:error({not_implemented, set_inet6, 1}).
set_lookup(_) -> erlang:error({not_implemented, set_lookup, 1}).
set_recurse(_) -> erlang:error({not_implemented, set_recurse, 1}).
set_resolv_conf(_) -> erlang:error({not_implemented, set_resolv_conf, 1}).
set_retry(_) -> erlang:error({not_implemented, set_retry, 1}).
set_sctp_module(_) -> erlang:error({not_implemented, set_sctp_module, 1}).
set_servfail_retry_timeout(_) -> erlang:error({not_implemented, set_servfail_retry_timeout, 1}).
set_socks_port(_) -> erlang:error({not_implemented, set_socks_port, 1}).
set_socks_server(_) -> erlang:error({not_implemented, set_socks_server, 1}).
set_tcp_module(_) -> erlang:error({not_implemented, set_tcp_module, 1}).
set_timeout(_) -> erlang:error({not_implemented, set_timeout, 1}).
set_udp_module(_) -> erlang:error({not_implemented, set_udp_module, 1}).
set_udp_payload_size(_) -> erlang:error({not_implemented, set_udp_payload_size, 1}).
set_usevc(_) -> erlang:error({not_implemented, set_usevc, 1}).
socks_option(_) -> erlang:error({not_implemented, socks_option, 1}).
start() -> erlang:error({not_implemented, start, 0}).
start_link() -> erlang:error({not_implemented, start_link, 0}).
stop() -> erlang:error({not_implemented, stop, 0}).
take_socket_type(_) -> erlang:error({not_implemented, take_socket_type, 1}).
tcp_module() -> erlang:error({not_implemented, tcp_module, 0}).
terminate(_, _) -> erlang:error({not_implemented, terminate, 2}).
tolower(_) -> erlang:error({not_implemented, tolower, 1}).
udp_module() -> erlang:error({not_implemented, udp_module, 0}).
unregister_socket(_) -> erlang:error({not_implemented, unregister_socket, 1}).
