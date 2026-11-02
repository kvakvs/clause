%% Clause's inet6_tls_dist (plan 30 step DIST12a, docs/distribution.md#tls-distribution): the IPv6 TLS carrier of
%% distribution, selected with --proto-dist inet6_tls. It is inet_tls_dist's carrier over the inet6 address family.
-module(inet6_tls_dist).
-export([accept/1, accept_connection/5, address/0, childspecs/0, close/1, listen/2, select/1, setup/5]).

-define(FAMILY, inet6).

childspecs() -> inet_tls_dist:childspecs().

select(Node) -> inet_tls_dist:fam_select(?FAMILY, Node).

address() -> inet_tls_dist:fam_address(?FAMILY).

listen(Name, Host) -> inet_tls_dist:fam_listen(?FAMILY, Name, Host).

accept(Listen) -> inet_tls_dist:fam_accept(?FAMILY, Listen).

accept_connection(Acceptor, Controller, MyNode, Allowed, SetupTime) ->
    inet_tls_dist:fam_accept_connection(?FAMILY, Acceptor, Controller, MyNode, Allowed, SetupTime).

setup(Node, Type, MyNode, LongOrShortNames, SetupTime) ->
    inet_tls_dist:fam_setup(?FAMILY, Node, Type, MyNode, LongOrShortNames, SetupTime).

close(Listen) -> inet_tls_dist:close(Listen).
