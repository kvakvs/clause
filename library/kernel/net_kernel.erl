%% Clause's net_kernel (plan 21 step LIB25, docs/libraries/library.md): the answers of an OTP node whose distribution is not
%% started, which a Clause program always is. Most requests are ignored, node monitoring is accepted without any
%% node ever coming up, and distribution cannot be started.
-module(net_kernel).
-export([
    allow/1,
    allowed/0,
    async_disconnect/1,
    aux_ticker/4,
    code_change/3,
    connect_node/1,
    connecttime/0,
    dflag_unicode_io/1,
    disconnect/1,
    do_spawn/3,
    epmd_module/0,
    get_net_ticktime/0,
    get_state/0,
    getopts/2,
    handle_call/3,
    handle_cast/2,
    handle_info/2,
    hidden_connect_node/1,
    i/0,
    i/1,
    init/1,
    kernel_apply/3,
    longnames/0,
    monitor_nodes/1,
    monitor_nodes/2,
    node_info/1,
    node_info/2,
    nodename/0,
    nodes_info/0,
    passive_cnct/1,
    passive_connect_monitor/2,
    protocol_childspecs/0,
    publish_on_node/1,
    set_net_ticktime/1,
    set_net_ticktime/2,
    setopts/2,
    spawn_func/6,
    start/1,
    start/2,
    start_link/1,
    stop/0,
    terminate/2,
    ticker/2,
    ticker_loop/2,
    verbose/1
]).

get_state() -> #{started => no}.

%% Requests the net_kernel server would answer: without distribution they are ignored.
allow(_) -> ignored.
allowed() -> ignored.
connect_node(_) -> ignored.
hidden_connect_node(_) -> ignored.
disconnect(_) -> ignored.
get_net_ticktime() -> ignored.
set_net_ticktime(_) -> ignored.
set_net_ticktime(_, _) -> ignored.
getopts(_, _) -> ignored.
setopts(_, _) -> ignored.
nodename() -> ignored.
longnames() -> ignored.

async_disconnect(_) -> ok.

publish_on_node(_) -> false.

passive_cnct(_) -> false.

dflag_unicode_io(_) -> true.

protocol_childspecs() -> [].

epmd_module() -> erl_epmd.

connecttime() -> 7000.

stop() -> {error, not_found}.

%% Monitoring is accepted; no node ever comes up or goes down.
monitor_nodes(Flag) when is_boolean(Flag) -> ok;
monitor_nodes(_) -> error.

monitor_nodes(Flag, Options) when is_boolean(Flag), is_list(Options) ->
    case [Option || Option <- Options, not known_option(Option)] of
        [] -> ok;
        Unknown -> {error, {unknown_options, Unknown}}
    end;
monitor_nodes(_, _) ->
    error.

known_option(connection_id) -> true;
known_option(nodedown_reason) -> true;
known_option({node_type, Type}) -> lists:member(Type, [visible, hidden, all]);
known_option(_) -> false.

%% The distribution's own processes and tables are not there.
aux_ticker(_, _, _, _) -> erlang:error({not_implemented, aux_ticker, 4}).
code_change(_, _, _) -> erlang:error({not_implemented, code_change, 3}).
do_spawn(_, _, _) -> erlang:error({not_implemented, do_spawn, 3}).
handle_call(_, _, _) -> erlang:error({not_implemented, handle_call, 3}).
handle_cast(_, _) -> erlang:error({not_implemented, handle_cast, 2}).
handle_info(_, _) -> erlang:error({not_implemented, handle_info, 2}).
i() -> erlang:error({not_implemented, i, 0}).
i(_) -> erlang:error({not_implemented, i, 1}).
init(_) -> erlang:error({not_implemented, init, 1}).
kernel_apply(_, _, _) -> erlang:error({not_implemented, kernel_apply, 3}).
node_info(_) -> erlang:error({not_implemented, node_info, 1}).
node_info(_, _) -> erlang:error({not_implemented, node_info, 2}).
nodes_info() -> erlang:error({not_implemented, nodes_info, 0}).
passive_connect_monitor(_, _) -> erlang:error({not_implemented, passive_connect_monitor, 2}).
spawn_func(_, _, _, _, _, _) -> erlang:error({not_implemented, spawn_func, 6}).
start(_) -> erlang:error({not_implemented, start, 1}).
start(_, _) -> erlang:error({not_implemented, start, 2}).
start_link(_) -> erlang:error({not_implemented, start_link, 1}).
terminate(_, _) -> erlang:error({not_implemented, terminate, 2}).
ticker(_, _) -> erlang:error({not_implemented, ticker, 2}).
ticker_loop(_, _) -> erlang:error({not_implemented, ticker_loop, 2}).
verbose(_) -> erlang:error({not_implemented, verbose, 1}).
