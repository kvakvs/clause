%% Clause's global_group (plan 21 step LIB25, docs/libraries/library.md): the answers of an OTP node without global groups,
%% which a Clause program always is: there are no groups, the node's own group is all of it (the node itself), and
%% names resolve through global.
-module(global_group).
-export([
    global_groups/0,
    monitor_nodes/1,
    own_nodes/0,
    registered_names/1,
    send/2,
    send/3,
    whereis_name/1,
    whereis_name/2,
    sync/0,
    info/0,
    member/1,
    participant/1,
    publish/2,
    group_configured/0,
    get_own_nodes/0,
    get_own_nodes_with_errors/0,
    global_groups_changed/1,
    global_groups_added/1,
    global_groups_removed/1,
    ng_add_check/2,
    ng_add_check/3,
    registered_names_test/1,
    send_test/2,
    whereis_name_test/1,
    start/0,
    start_link/0,
    stop/0,
    init/1,
    handle_call/3,
    handle_cast/2,
    handle_info/2,
    terminate/2,
    code_change/3
]).

global_groups() -> undefined.

own_nodes() -> [node()].

info() ->
    [
        {state, no_conf},
        {own_group_name, []},
        {own_group_nodes, []},
        {synced_nodes, []},
        {sync_error, []},
        {no_contact, []},
        {other_groups, []},
        {monitoring, []}
    ].

sync() -> ok.

monitor_nodes(Flag) when is_boolean(Flag) -> ok.

member(_) -> false.

participant(_) -> true.

group_configured() -> false.

get_own_nodes() -> {ok, all}.

get_own_nodes_with_errors() -> {ok, all}.

%% Names: the node's own names are global's; groups and other nodes have none.
registered_names({node, Node}) when Node =:= node() -> global:registered_names();
registered_names({node, _}) -> [];
registered_names({group, _}) -> [].

whereis_name(Name) -> global:whereis_name(Name).

%% A search on a node or in a group other than the own one: without distribution it finds nothing.
whereis_name({_, _}, _) -> undefined.

send(Name, Message) ->
    case global:whereis_name(Name) of
        Pid when is_pid(Pid) ->
            Pid ! Message,
            Pid;
        undefined ->
            {badarg, {Name, Message}}
    end.

send({node, Node}, Name, Message) when Node =:= node() -> send(Name, Message);
send({_, _}, Name, Message) -> {badarg, {Name, Message}}.

%% Group configuration changes, distributed checks, the server and test hooks need configured groups.
publish(_, _) -> erlang:error({not_implemented, publish, 2}).
global_groups_changed(_) -> erlang:error({not_implemented, global_groups_changed, 1}).
global_groups_added(_) -> erlang:error({not_implemented, global_groups_added, 1}).
global_groups_removed(_) -> erlang:error({not_implemented, global_groups_removed, 1}).
ng_add_check(_, _) -> erlang:error({not_implemented, ng_add_check, 2}).
ng_add_check(_, _, _) -> erlang:error({not_implemented, ng_add_check, 3}).
registered_names_test(_) -> erlang:error({not_implemented, registered_names_test, 1}).
send_test(_, _) -> erlang:error({not_implemented, send_test, 2}).
whereis_name_test(_) -> erlang:error({not_implemented, whereis_name_test, 1}).
start() -> erlang:error({not_implemented, start, 0}).
start_link() -> erlang:error({not_implemented, start_link, 0}).
stop() -> erlang:error({not_implemented, stop, 0}).
init(_) -> erlang:error({not_implemented, init, 1}).
handle_call(_, _, _) -> erlang:error({not_implemented, handle_call, 3}).
handle_cast(_, _) -> erlang:error({not_implemented, handle_cast, 2}).
handle_info(_, _) -> erlang:error({not_implemented, handle_info, 2}).
terminate(_, _) -> erlang:error({not_implemented, terminate, 2}).
code_change(_, _, _) -> erlang:error({not_implemented, code_change, 3}).
