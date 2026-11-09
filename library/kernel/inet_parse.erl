%% OTP's inet_parse as libraries call it (plan 21 step MIM7, docs/libraries/library.md#undocumented-modules): address
%% parsing and formatting as inet's, and the domain name check. Its configuration file parsers are declined: Clause
%% reads the resolver configuration itself (clause_inet_config).
-module(inet_parse).
-export([
    address/1,
    domain/1,
    ipv4_address/1,
    ipv4strict_address/1,
    ipv6_address/1,
    ipv6strict_address/1,
    ntoa/1,
    strict_address/1
]).
%% OTP 29 exports declined: stubs raising error({not_implemented, F, A})
%% (docs/libraries/library.md#runtime-module-replacements).
-export([
    dots/1,
    host_conf_bsdos/1,
    host_conf_bsdos/2,
    host_conf_freebsd/1,
    host_conf_freebsd/2,
    host_conf_linux/1,
    host_conf_linux/2,
    hosts/1,
    hosts/2,
    netmasks/1,
    netmasks/2,
    networks/1,
    networks/2,
    nsswitch_conf/1,
    nsswitch_conf/2,
    protocols/1,
    protocols/2,
    resolv/1,
    resolv/2,
    rpc/1,
    rpc/2,
    services/1,
    services/2,
    split_line/1,
    visible_string/1
]).

-define(IS_ALNUM(C), ((C >= $a andalso C =< $z) orelse (C >= $A andalso C =< $Z) orelse (C >= $0 andalso C =< $9))).

address(Text) -> inet:parse_address(Text).

% Whether Name is a domain name: dot-separated labels of ASCII letters, digits, - and _, each starting
% with a letter or digit and, but for the last, ending with one; not ending in four numeric labels.
domain([C | _] = Name) when ?IS_ALNUM(C) ->
    Labels = string:split(Name, ".", all),
    labels(Labels) andalso not numeric_end(lists:reverse(Labels));
domain(_) ->
    false.

labels([Last]) ->
    label(Last);
labels([Label | Rest]) ->
    label(Label) andalso ?IS_ALNUM(lists:last(Label)) andalso labels(Rest).

label([C | Rest]) when ?IS_ALNUM(C) ->
    lists:all(fun(D) -> ?IS_ALNUM(D) orelse D =:= $- orelse D =:= $_ end, Rest);
label(_) ->
    false.

% The last four labels all digits: the name reads as an IPv4 address.
numeric_end([A, B, C, D | _]) ->
    lists:all(fun(Label) -> lists:all(fun(X) -> X >= $0 andalso X =< $9 end, Label) end, [A, B, C, D]);
numeric_end(_) ->
    false.

ipv4_address(Text) -> inet:parse_ipv4_address(Text).

ipv4strict_address(Text) -> inet:parse_ipv4strict_address(Text).

ipv6_address(Text) -> inet:parse_ipv6_address(Text).

ipv6strict_address(Text) -> inet:parse_ipv6strict_address(Text).

ntoa(Address) -> inet:ntoa(Address).

strict_address(Text) -> inet:parse_strict_address(Text).

dots(_) -> erlang:error({not_implemented, dots, 1}).
host_conf_bsdos(_) -> erlang:error({not_implemented, host_conf_bsdos, 1}).
host_conf_bsdos(_, _) -> erlang:error({not_implemented, host_conf_bsdos, 2}).
host_conf_freebsd(_) -> erlang:error({not_implemented, host_conf_freebsd, 1}).
host_conf_freebsd(_, _) -> erlang:error({not_implemented, host_conf_freebsd, 2}).
host_conf_linux(_) -> erlang:error({not_implemented, host_conf_linux, 1}).
host_conf_linux(_, _) -> erlang:error({not_implemented, host_conf_linux, 2}).
hosts(_) -> erlang:error({not_implemented, hosts, 1}).
hosts(_, _) -> erlang:error({not_implemented, hosts, 2}).
netmasks(_) -> erlang:error({not_implemented, netmasks, 1}).
netmasks(_, _) -> erlang:error({not_implemented, netmasks, 2}).
networks(_) -> erlang:error({not_implemented, networks, 1}).
networks(_, _) -> erlang:error({not_implemented, networks, 2}).
nsswitch_conf(_) -> erlang:error({not_implemented, nsswitch_conf, 1}).
nsswitch_conf(_, _) -> erlang:error({not_implemented, nsswitch_conf, 2}).
protocols(_) -> erlang:error({not_implemented, protocols, 1}).
protocols(_, _) -> erlang:error({not_implemented, protocols, 2}).
resolv(_) -> erlang:error({not_implemented, resolv, 1}).
resolv(_, _) -> erlang:error({not_implemented, resolv, 2}).
rpc(_) -> erlang:error({not_implemented, rpc, 1}).
rpc(_, _) -> erlang:error({not_implemented, rpc, 2}).
services(_) -> erlang:error({not_implemented, services, 1}).
services(_, _) -> erlang:error({not_implemented, services, 2}).
split_line(_) -> erlang:error({not_implemented, split_line, 1}).
visible_string(_) -> erlang:error({not_implemented, visible_string, 1}).
