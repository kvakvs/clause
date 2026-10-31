%% Clause's inet_tcp_dist (plan 30 step DIST4b1a, docs/distribution.md#starting-distribution): the IPv4 TCP carrier of
%% distribution over gen_tcp. It listens on a port of the kernel parameters inet_dist_listen_min/max and registers
%% that port with epmd; accepting and setting up connections come with the handshake (DIST4b2).
-module(inet_tcp_dist).
-export([
    accept/1,
    accept_connection/5,
    accept_loop/3,
    address/0,
    close/1,
    do_accept/7,
    do_setup/7,
    fam_address/1,
    fam_listen/4,
    fam_select/2,
    fam_setup/4,
    gen_accept/2,
    gen_accept_connection/6,
    gen_address/1,
    gen_hs_data/2,
    gen_listen/3,
    gen_select/2,
    gen_setup/6,
    getopts/2,
    getstat/1,
    is_node_name/1,
    listen/1,
    listen/2,
    merge_options/2,
    merge_options/3,
    nodelay/0,
    select/1,
    setopts/2,
    setup/5,
    tick/2
]).

-include_lib("kernel/include/net_address.hrl").

% The backlog of the listen socket.
-define(BACKLOG, 128).

listen(Name) ->
    {ok, Host} = inet:gethostname(),
    listen(Name, Host).

% Listen for distribution on Host and register the port under the alive name Name with the epmd module:
% {ok, {Socket, Address, Creation}}, or the error of either.
listen(Name, Host) ->
    case listen_socket(listen_ports()) of
        {ok, Socket} ->
            {ok, {IP, Port}} = inet:sockname(Socket),
            Address = #net_address{address = {IP, Port}, host = Host, protocol = tcp, family = inet},
            case register_node(net_kernel:epmd_module(), Name, Port) of
                {ok, Creation} ->
                    {ok, {Socket, Address, Creation}};
                Error ->
                    gen_tcp:close(Socket),
                    Error
            end;
        Error ->
            Error
    end.

% erl_epmd is called by name, so that programs link it.
register_node(erl_epmd, Name, Port) -> erl_epmd:register_node(Name, Port, inet_tcp);
register_node(Module, Name, Port) -> Module:register_node(Name, Port, inet_tcp).

% The ports to try, in order: inet_dist_listen_min to inet_dist_listen_max (min alone is one port), else any.
listen_ports() ->
    case application:get_env(kernel, inet_dist_listen_min) of
        {ok, Min} when is_integer(Min) ->
            case application:get_env(kernel, inet_dist_listen_max) of
                {ok, Max} when is_integer(Max), Max >= Min -> lists:seq(Min, Max);
                _ -> [Min]
            end;
        _ ->
            [0]
    end.

% The first port of Ports a listen socket takes; a port in use moves to the next one.
listen_socket([Port | Ports]) ->
    case gen_tcp:listen(Port, listen_options()) of
        {error, eaddrinuse} when Ports =/= [] -> listen_socket(Ports);
        Result -> Result
    end.

% The kernel parameters inet_dist_use_interface and inet_dist_listen_options over the carrier's own options.
listen_options() ->
    Interface =
        case application:get_env(kernel, inet_dist_use_interface) of
            {ok, IP} -> [{ip, IP}];
            undefined -> []
        end,
    Extra =
        case application:get_env(kernel, inet_dist_listen_options) of
            {ok, Options} when is_list(Options) -> Options;
            _ -> []
        end,
    Interface ++ Extra ++ [inet, binary, {active, false}, {packet, 2}, {backlog, ?BACKLOG}].

% The address node names of this host take, before the carrier listens.
address() ->
    {ok, Host} = inet:gethostname(),
    #net_address{host = Host, protocol = tcp, family = inet}.

close(Socket) -> gen_tcp:close(Socket).

% Whether Node is an atom of a name part and a host part around one @.
is_node_name(Node) when is_atom(Node) -> length(string:split(atom_to_list(Node), "@", all)) =:= 2;
is_node_name(_) -> false.

% Whether this carrier reaches Node: both parts present and the host has an IPv4 address.
select(Node) ->
    case string:split(to_string(Node), "@", all) of
        [[_ | _], [_ | _] = Host] -> element(1, inet:getaddr(Host, inet)) =:= ok;
        _ -> false
    end.

to_string(Node) when is_atom(Node) -> atom_to_list(Node);
to_string(Node) -> Node.

%% Accepting and setting up connections, and the shared carrier functions, come with the handshake (DIST4b2).
accept(_) -> erlang:error({not_implemented, accept, 1}).
accept_connection(_, _, _, _, _) -> erlang:error({not_implemented, accept_connection, 5}).
accept_loop(_, _, _) -> erlang:error({not_implemented, accept_loop, 3}).
do_accept(_, _, _, _, _, _, _) -> erlang:error({not_implemented, do_accept, 7}).
do_setup(_, _, _, _, _, _, _) -> erlang:error({not_implemented, do_setup, 7}).
fam_address(_) -> erlang:error({not_implemented, fam_address, 1}).
fam_listen(_, _, _, _) -> erlang:error({not_implemented, fam_listen, 4}).
fam_select(_, _) -> erlang:error({not_implemented, fam_select, 2}).
fam_setup(_, _, _, _) -> erlang:error({not_implemented, fam_setup, 4}).
gen_accept(_, _) -> erlang:error({not_implemented, gen_accept, 2}).
gen_accept_connection(_, _, _, _, _, _) -> erlang:error({not_implemented, gen_accept_connection, 6}).
gen_address(_) -> erlang:error({not_implemented, gen_address, 1}).
gen_hs_data(_, _) -> erlang:error({not_implemented, gen_hs_data, 2}).
gen_listen(_, _, _) -> erlang:error({not_implemented, gen_listen, 3}).
gen_select(_, _) -> erlang:error({not_implemented, gen_select, 2}).
gen_setup(_, _, _, _, _, _) -> erlang:error({not_implemented, gen_setup, 6}).
getopts(_, _) -> erlang:error({not_implemented, getopts, 2}).
getstat(_) -> erlang:error({not_implemented, getstat, 1}).
merge_options(_, _) -> erlang:error({not_implemented, merge_options, 2}).
merge_options(_, _, _) -> erlang:error({not_implemented, merge_options, 3}).
nodelay() -> erlang:error({not_implemented, nodelay, 0}).
setopts(_, _) -> erlang:error({not_implemented, setopts, 2}).
setup(_, _, _, _, _) -> erlang:error({not_implemented, setup, 5}).
tick(_, _) -> erlang:error({not_implemented, tick, 2}).
