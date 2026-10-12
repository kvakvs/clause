%% Clause's replacement for OTP's init (docs/library.md#runtime-module-replacements), generated from
%% init.terms by tests/compiler/library/library_modules.py; edited by hand from here on. A stub raises
%% error({not_implemented, Name, Arity}).
-module(init).
-export([
    archive_extension/0,
    boot/1,
    code_path_choice/0,
    ensure_loaded/1,
    fetch_loaded/0,
    get_argument/1,
    get_arguments/0,
    get_configfd/1,
    get_plain_arguments/0,
    get_status/0,
    make_permanent/2,
    notify_when_started/1,
    objfile_extension/0,
    reboot/0,
    restart/0,
    restart/1,
    run_on_load_handlers/0,
    script_id/0,
    script_name/0,
    set_configfd/2,
    stop/0,
    stop/1,
    wait_until_started/0
]).

archive_extension() -> erlang:error({not_implemented, archive_extension, 0}).
boot(_) -> erlang:error({not_implemented, boot, 1}).
code_path_choice() -> erlang:error({not_implemented, code_path_choice, 0}).
ensure_loaded(_) -> erlang:error({not_implemented, ensure_loaded, 1}).
fetch_loaded() -> erlang:error({not_implemented, fetch_loaded, 0}).
get_argument(_) -> erlang:error({not_implemented, get_argument, 1}).
get_arguments() -> erlang:error({not_implemented, get_arguments, 0}).
get_configfd(_) -> erlang:error({not_implemented, get_configfd, 1}).
get_plain_arguments() -> erlang:error({not_implemented, get_plain_arguments, 0}).
get_status() -> erlang:error({not_implemented, get_status, 0}).
make_permanent(_, _) -> erlang:error({not_implemented, make_permanent, 2}).
notify_when_started(_) -> erlang:error({not_implemented, notify_when_started, 1}).
objfile_extension() -> erlang:error({not_implemented, objfile_extension, 0}).
reboot() -> erlang:error({not_implemented, reboot, 0}).
restart() -> erlang:error({not_implemented, restart, 0}).
restart(_) -> erlang:error({not_implemented, restart, 1}).
run_on_load_handlers() -> erlang:error({not_implemented, run_on_load_handlers, 0}).
script_id() -> erlang:error({not_implemented, script_id, 0}).
script_name() -> erlang:error({not_implemented, script_name, 0}).
set_configfd(_, _) -> erlang:error({not_implemented, set_configfd, 2}).
stop() -> erlang:error({not_implemented, stop, 0}).
stop(_) -> erlang:error({not_implemented, stop, 1}).
wait_until_started() -> erlang:error({not_implemented, wait_until_started, 0}).
