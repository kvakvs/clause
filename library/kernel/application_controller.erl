%% Clause's replacement for OTP's application_controller (docs/library.md#runtime-module-replacements), generated from
%% application_controller.terms by tests/compiler/library/library_modules.py; edited by hand from here on. A stub raises
%% error({not_implemented, Name, Arity}).
-module(application_controller).
-export([
    change_application_data/2,
    code_change/3,
    config_change/1,
    control_application/1,
    do_config_diff/2,
    format_log/1,
    format_log/2,
    get_all_env/1,
    get_all_key/1,
    get_application/1,
    get_application_module/1,
    get_env/2,
    get_env/3,
    get_key/2,
    get_loaded/1,
    get_master/1,
    get_pid_all_env/1,
    get_pid_all_key/1,
    get_pid_env/2,
    get_pid_key/2,
    handle_call/3,
    handle_cast/2,
    handle_info/2,
    info/0,
    is_running/1,
    load_application/1,
    loaded_applications/0,
    permit_application/2,
    prep_config_change/0,
    set_env/2,
    set_env/3,
    set_env/4,
    start/1,
    start_application/2,
    start_application_request/2,
    start_boot_application/2,
    start_type/1,
    stop_application/1,
    stop_application_request/1,
    terminate/2,
    test_change_apps/2,
    unload_application/1,
    unset_env/2,
    unset_env/3,
    which_applications/0,
    which_applications/1
]).

change_application_data(_, _) -> erlang:error({not_implemented, change_application_data, 2}).
code_change(_, _, _) -> erlang:error({not_implemented, code_change, 3}).
config_change(_) -> erlang:error({not_implemented, config_change, 1}).
control_application(_) -> erlang:error({not_implemented, control_application, 1}).
do_config_diff(_, _) -> erlang:error({not_implemented, do_config_diff, 2}).
format_log(_) -> erlang:error({not_implemented, format_log, 1}).
format_log(_, _) -> erlang:error({not_implemented, format_log, 2}).
get_all_env(_) -> erlang:error({not_implemented, get_all_env, 1}).
get_all_key(_) -> erlang:error({not_implemented, get_all_key, 1}).
get_application(_) -> erlang:error({not_implemented, get_application, 1}).
get_application_module(_) -> erlang:error({not_implemented, get_application_module, 1}).
get_env(_, _) -> erlang:error({not_implemented, get_env, 2}).
get_env(_, _, _) -> erlang:error({not_implemented, get_env, 3}).
get_key(_, _) -> erlang:error({not_implemented, get_key, 2}).
get_loaded(_) -> erlang:error({not_implemented, get_loaded, 1}).
get_master(_) -> erlang:error({not_implemented, get_master, 1}).
get_pid_all_env(_) -> erlang:error({not_implemented, get_pid_all_env, 1}).
get_pid_all_key(_) -> erlang:error({not_implemented, get_pid_all_key, 1}).
get_pid_env(_, _) -> erlang:error({not_implemented, get_pid_env, 2}).
get_pid_key(_, _) -> erlang:error({not_implemented, get_pid_key, 2}).
handle_call(_, _, _) -> erlang:error({not_implemented, handle_call, 3}).
handle_cast(_, _) -> erlang:error({not_implemented, handle_cast, 2}).
handle_info(_, _) -> erlang:error({not_implemented, handle_info, 2}).
info() -> erlang:error({not_implemented, info, 0}).
is_running(_) -> erlang:error({not_implemented, is_running, 1}).
load_application(_) -> erlang:error({not_implemented, load_application, 1}).
loaded_applications() -> erlang:error({not_implemented, loaded_applications, 0}).
permit_application(_, _) -> erlang:error({not_implemented, permit_application, 2}).
prep_config_change() -> erlang:error({not_implemented, prep_config_change, 0}).
set_env(_, _) -> erlang:error({not_implemented, set_env, 2}).
set_env(_, _, _) -> erlang:error({not_implemented, set_env, 3}).
set_env(_, _, _, _) -> erlang:error({not_implemented, set_env, 4}).
start(_) -> erlang:error({not_implemented, start, 1}).
start_application(_, _) -> erlang:error({not_implemented, start_application, 2}).
start_application_request(_, _) -> erlang:error({not_implemented, start_application_request, 2}).
start_boot_application(_, _) -> erlang:error({not_implemented, start_boot_application, 2}).
start_type(_) -> erlang:error({not_implemented, start_type, 1}).
stop_application(_) -> erlang:error({not_implemented, stop_application, 1}).
stop_application_request(_) -> erlang:error({not_implemented, stop_application_request, 1}).
terminate(_, _) -> erlang:error({not_implemented, terminate, 2}).
test_change_apps(_, _) -> erlang:error({not_implemented, test_change_apps, 2}).
unload_application(_) -> erlang:error({not_implemented, unload_application, 1}).
unset_env(_, _) -> erlang:error({not_implemented, unset_env, 2}).
unset_env(_, _, _) -> erlang:error({not_implemented, unset_env, 3}).
which_applications() -> erlang:error({not_implemented, which_applications, 0}).
which_applications(_) -> erlang:error({not_implemented, which_applications, 1}).
