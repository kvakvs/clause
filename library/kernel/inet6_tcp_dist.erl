%% Clause's inet6_tcp_dist (plan 30 step DIST4b2c, docs/distribution.md#carriers): the IPv6 TCP carrier of
%% distribution, selected with --proto-dist inet6_tcp. It is inet_tcp_dist's carrier over the inet6_tcp driver.
-module(inet6_tcp_dist).
-export([
    accept/1,
    accept_connection/5,
    address/0,
    close/1,
    getopts/2,
    is_node_name/1,
    listen/2,
    select/1,
    setopts/2,
    setup/5
]).

listen(Name, Host) -> inet_tcp_dist:gen_listen(inet6_tcp, Name, Host).

accept(Listen) -> inet_tcp_dist:gen_accept(inet6_tcp, Listen).

accept_connection(Acceptor, Controller, MyNode, Allowed, SetupTime) ->
    inet_tcp_dist:gen_accept_connection(inet6_tcp, Acceptor, Controller, MyNode, Allowed, SetupTime).

setup(Node, Type, MyNode, LongOrShortNames, SetupTime) ->
    inet_tcp_dist:gen_setup(inet6_tcp, Node, Type, MyNode, LongOrShortNames, SetupTime).

close(Listen) -> inet_tcp_dist:close(Listen).

select(Node) -> inet_tcp_dist:gen_select(inet6_tcp, Node).

address() -> inet_tcp_dist:gen_address(inet6_tcp).

is_node_name(Node) -> inet_tcp_dist:is_node_name(Node).

setopts(Listen, Options) -> inet_tcp_dist:setopts(Listen, Options).

getopts(Listen, Options) -> inet_tcp_dist:getopts(Listen, Options).
