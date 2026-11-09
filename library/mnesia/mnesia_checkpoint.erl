%% OTP's mnesia_checkpoint as other nodes call it (plan 21 step MIM1H11a,
%% docs/libraries/mnesia-protocol.md#checkpoints-over-nodes-mim1h11a): a node activating a checkpoint prepares
%% and activates it here, a sender joins this node to it, a node deactivates it, and nodes add and drop retainers
%% and retained records. Checkpoints are clause_mnesia_checkpoint's. OTP's other internal functions are
%% declined: no other node calls them.
-module(mnesia_checkpoint).
-export([call/2, cast/2, remote_deactivate/1, tm_prepare/1]).
%% OTP 29 exports implemented natively or not yet: catalog builtins, whose clause_builtin_placeholder body the runtime's
%% builtin replaces, and stubs raising error({not_implemented, F, A}) (docs/libraries/library.md#runtime-module-replacements).
-export([
    activate/1,
    checkpoints/0,
    deactivate/1,
    deactivate/2,
    init/1,
    iterate/6,
    most_local_node/2,
    really_retain/2,
    start/1,
    stop/0,
    stop_iteration/1,
    system_code_change/4,
    system_continue/3,
    system_terminate/4,
    tables_and_cookie/1,
    tm_add_copy/2,
    tm_change_table_copy_type/3,
    tm_del_copy/2,
    tm_enter_pending/1,
    tm_enter_pending/3,
    tm_exit_pending/1,
    tm_mnesia_down/1,
    tm_retain/4,
    tm_retain/5
]).

% A request to the checkpoint's process here, as OTP's: {error, {no_exists, Name}} without one.
call(Name, Request) ->
    clause_mnesia_checkpoint:call(Name, Request).

% A message to the checkpoint's process here: {ok, Pid}, or {error, {no_exists, Name}}.
cast(Name, Message) ->
    clause_mnesia_checkpoint:cast(Name, Message).

% The checkpoint's process here ended: deactivated.
remote_deactivate(Name) ->
    clause_mnesia_checkpoint:remote_deactivate(Name).

% This node's part of a checkpoint, OTP's #checkpoint_args{}: {ok, Name, IgnoreNew, node()}.
tm_prepare(Cp) ->
    clause_mnesia_checkpoint:tm_prepare(Cp).

activate(_) -> erlang:error({not_implemented, activate, 1}).
checkpoints() -> erlang:error({not_implemented, checkpoints, 0}).
deactivate(_) -> erlang:error({not_implemented, deactivate, 1}).
deactivate(_, _) -> erlang:error({not_implemented, deactivate, 2}).
init(_) -> erlang:error({not_implemented, init, 1}).
iterate(_, _, _, _, _, _) -> erlang:error({not_implemented, iterate, 6}).
most_local_node(_, _) -> erlang:error({not_implemented, most_local_node, 2}).
really_retain(_, _) -> erlang:error({not_implemented, really_retain, 2}).
start(_) -> erlang:error({not_implemented, start, 1}).
stop() -> erlang:error({not_implemented, stop, 0}).
stop_iteration(_) -> erlang:error({not_implemented, stop_iteration, 1}).
system_code_change(_, _, _, _) -> erlang:error({not_implemented, system_code_change, 4}).
system_continue(_, _, _) -> erlang:error({not_implemented, system_continue, 3}).
system_terminate(_, _, _, _) -> erlang:error({not_implemented, system_terminate, 4}).
tables_and_cookie(_) -> erlang:error({not_implemented, tables_and_cookie, 1}).
tm_add_copy(_, _) -> erlang:error({not_implemented, tm_add_copy, 2}).
tm_change_table_copy_type(_, _, _) -> erlang:error({not_implemented, tm_change_table_copy_type, 3}).
tm_del_copy(_, _) -> erlang:error({not_implemented, tm_del_copy, 2}).
tm_enter_pending(_) -> erlang:error({not_implemented, tm_enter_pending, 1}).
tm_enter_pending(_, _, _) -> erlang:error({not_implemented, tm_enter_pending, 3}).
tm_exit_pending(_) -> erlang:error({not_implemented, tm_exit_pending, 1}).
tm_mnesia_down(_) -> erlang:error({not_implemented, tm_mnesia_down, 1}).
tm_retain(_, _, _, _) -> erlang:error({not_implemented, tm_retain, 4}).
tm_retain(_, _, _, _, _) -> erlang:error({not_implemented, tm_retain, 5}).
