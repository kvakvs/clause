%% Clause's erl_epmd (plan 21, docs/libraries/library.md#runtime-module-replacements): a Clause program is a node
%% without distribution and talks to no epmd daemon, so it answers as OTP's client does when epmd is not
%% operational: no names, no ports, and registration fails as no erl_epmd server runs. The server is declined.
-module(erl_epmd).
-export([
    address_please/3,
    code_change/3,
    handle_call/3,
    handle_cast/2,
    handle_info/2,
    init/1,
    listen_port_please/2,
    names/0,
    names/1,
    open/0,
    open/1,
    open/2,
    port_please/2,
    port_please/3,
    register_node/2,
    register_node/3,
    start/0,
    start_link/0,
    stop/0,
    terminate/2
]).

% The distribution protocol version a fixed listen port answers with (epmd's dist low).
-define(DIST_VERSION, 5).

names() ->
    {ok, Host} = inet:gethostname(),
    names(Host).

% The names epmd registered on a host: none, as without an operational epmd.
names(_Host) -> {error, address}.

port_please(Node, Host) -> port_please(Node, Host, infinity).

% A node's distribution port: the configured listen port, else noport (no epmd to ask).
port_please(Node, Host, _Timeout) ->
    case listen_port_please(Node, Host) of
        {ok, 0} -> noport;
        {ok, Port} -> {port, Port, ?DIST_VERSION}
    end.

% The listen port of kernel's erl_epmd_node_listen_port, 0 when unset.
listen_port_please(_Name, _Host) ->
    case application:get_env(kernel, erl_epmd_node_listen_port) of
        {ok, Port} when is_integer(Port), Port >= 0 -> {ok, Port};
        {ok, Invalid} -> erlang:error({invalid_parameter_value, erl_epmd_node_listen_port, Invalid});
        undefined -> {ok, 0}
    end.

address_please(_Name, Host, AddressFamily) -> inet:getaddr(Host, AddressFamily).

register_node(Name, Port) -> register_node(Name, Port, inet).

% Registration goes to the erl_epmd server, which never runs: exits noproc as OTP's call does then.
register_node(Name, Port, inet_tcp) -> register_node(Name, Port, inet);
register_node(Name, Port, inet6_tcp) -> register_node(Name, Port, inet6);
register_node(Name, Port, Family) -> gen_server:call(erl_epmd, {register, Name, Port, Family}, infinity).

code_change(_, _, _) -> erlang:error({not_implemented, code_change, 3}).
handle_call(_, _, _) -> erlang:error({not_implemented, handle_call, 3}).
handle_cast(_, _) -> erlang:error({not_implemented, handle_cast, 2}).
handle_info(_, _) -> erlang:error({not_implemented, handle_info, 2}).
init(_) -> erlang:error({not_implemented, init, 1}).
open() -> erlang:error({not_implemented, open, 0}).
open(_) -> erlang:error({not_implemented, open, 1}).
open(_, _) -> erlang:error({not_implemented, open, 2}).
start() -> erlang:error({not_implemented, start, 0}).
start_link() -> erlang:error({not_implemented, start_link, 0}).
stop() -> erlang:error({not_implemented, stop, 0}).
terminate(_, _) -> erlang:error({not_implemented, terminate, 2}).
