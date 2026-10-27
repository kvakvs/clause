%% Clause's erl_boot_server (plan 21, docs/libraries/library.md#runtime-module-replacements): booting other nodes
%% over the network needs OTP's boot protocol and loader, which Clause programs do not have; every function is
%% declined.
-module(erl_boot_server).
-export([
    add_slave/1,
    add_subnet/2,
    boot_accept/3,
    boot_init/1,
    code_change/3,
    delete_slave/1,
    delete_subnet/2,
    handle_call/3,
    handle_cast/2,
    handle_info/2,
    init/1,
    start/1,
    start/2,
    start_link/1,
    start_link/2,
    terminate/2,
    which_slaves/0,
    would_be_booted/1
]).

add_slave(_) -> erlang:error({not_implemented, add_slave, 1}).
add_subnet(_, _) -> erlang:error({not_implemented, add_subnet, 2}).
boot_accept(_, _, _) -> erlang:error({not_implemented, boot_accept, 3}).
boot_init(_) -> erlang:error({not_implemented, boot_init, 1}).
code_change(_, _, _) -> erlang:error({not_implemented, code_change, 3}).
delete_slave(_) -> erlang:error({not_implemented, delete_slave, 1}).
delete_subnet(_, _) -> erlang:error({not_implemented, delete_subnet, 2}).
handle_call(_, _, _) -> erlang:error({not_implemented, handle_call, 3}).
handle_cast(_, _) -> erlang:error({not_implemented, handle_cast, 2}).
handle_info(_, _) -> erlang:error({not_implemented, handle_info, 2}).
init(_) -> erlang:error({not_implemented, init, 1}).
start(_) -> erlang:error({not_implemented, start, 1}).
start(_, _) -> erlang:error({not_implemented, start, 2}).
start_link(_) -> erlang:error({not_implemented, start_link, 1}).
start_link(_, _) -> erlang:error({not_implemented, start_link, 2}).
terminate(_, _) -> erlang:error({not_implemented, terminate, 2}).
which_slaves() -> erlang:error({not_implemented, which_slaves, 0}).
would_be_booted(_) -> erlang:error({not_implemented, would_be_booted, 1}).
