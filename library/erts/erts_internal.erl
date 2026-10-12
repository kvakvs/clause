%% Clause's replacement for OTP's erts_internal (docs/library.md#runtime-module-replacements), generated from
%% erts_internal.terms by tests/compiler/library/library_modules.py; edited by hand from here on. A stub raises
%% error({not_implemented, Name, Arity}).
-module(erts_internal).
-export([
    '@flush_monitor_messages_refopt'/0,
    abort_pending_connection/2,
    atomics_new/2,
    await_microstate_accounting_modifications/3,
    await_port_send_result/3,
    await_result/1,
    await_sched_wall_time_modifications/2,
    beamfile_chunk/2,
    beamfile_module_md5/1,
    binary_to_integer/2,
    breakpoint/4,
    check_dirty_process_code/2,
    check_process_code/1,
    check_process_code/3,
    cmp_term/2,
    counters_add/3,
    counters_get/2,
    counters_info/1,
    counters_new/1,
    counters_put/3,
    crasher/6,
    create_dist_channel/3,
    dirty_process_handle_signals/1,
    dist_ctrl_put_data/2,
    dist_spawn_init/1,
    dist_spawn_request/4,
    dynamic_node_name/0,
    dynamic_node_name/1,
    erase_persistent_terms/0,
    ets_info_binary/1,
    ets_lookup_binary_info/2,
    ets_raw_first/1,
    ets_raw_next/2,
    ets_super_user/1,
    flush_monitor_messages/3,
    garbage_collect/1,
    gather_alloc_histograms/1,
    gather_carrier_info/1,
    gather_io_bytes/2,
    gather_microstate_accounting_result/2,
    gather_sched_wall_time_result/1,
    gather_system_check_result/1,
    get_creation/0,
    get_dflags/0,
    get_internal_state_blocked/1,
    group_leader/2,
    group_leader/3,
    is_process_alive/1,
    is_process_alive/2,
    is_process_executing_dirty/1,
    is_system_process/1,
    list_to_integer/2,
    map_hashmap_children/1,
    map_next/3,
    map_to_tuple_keys/1,
    mc_iterator/1,
    mc_refill/1,
    new_connection/1,
    no_aux_work_threads/0,
    notify_breakpoint_hit/3,
    open_port/2,
    perf_counter_unit/0,
    port_call/3,
    port_close/1,
    port_command/3,
    port_connect/2,
    port_control/3,
    port_info/1,
    port_info/2,
    prepare_loading/2,
    process_display/2,
    process_flag/3,
    processes_next/1,
    purge_module/2,
    request_system_task/3,
    request_system_task/4,
    scheduler_wall_time/1,
    set_code_and_literal_cleaner_prio/1,
    spawn_init/1,
    spawn_request/4,
    spawn_system_process/3,
    suspend_process/2,
    system_check/1,
    system_flag_scheduler_wall_time/1,
    system_monitor/1,
    system_monitor/3,
    term_to_string/1,
    term_to_string/2,
    term_type/1,
    time_unit/0,
    trace/3,
    trace/4,
    trace_info/3,
    trace_pattern/3,
    trace_pattern/4,
    trace_session_create/3,
    trace_session_destroy/1,
    wait_release_literal_area_switch/1
]).

'@flush_monitor_messages_refopt'() ->
    erlang:error({not_implemented, '@flush_monitor_messages_refopt', 0}).
abort_pending_connection(_, _) -> erlang:error({not_implemented, abort_pending_connection, 2}).
atomics_new(_, _) -> erlang:error({not_implemented, atomics_new, 2}).
await_microstate_accounting_modifications(_, _, _) ->
    erlang:error({not_implemented, await_microstate_accounting_modifications, 3}).
await_port_send_result(_, _, _) -> erlang:error({not_implemented, await_port_send_result, 3}).
await_result(_) -> erlang:error({not_implemented, await_result, 1}).
await_sched_wall_time_modifications(_, _) ->
    erlang:error({not_implemented, await_sched_wall_time_modifications, 2}).
beamfile_chunk(_, _) -> erlang:error({not_implemented, beamfile_chunk, 2}).
beamfile_module_md5(_) -> erlang:error({not_implemented, beamfile_module_md5, 1}).
binary_to_integer(_, _) -> erlang:error({not_implemented, binary_to_integer, 2}).
breakpoint(_, _, _, _) -> erlang:error({not_implemented, breakpoint, 4}).
check_dirty_process_code(_, _) -> erlang:error({not_implemented, check_dirty_process_code, 2}).
check_process_code(_) -> erlang:error({not_implemented, check_process_code, 1}).
check_process_code(_, _, _) -> erlang:error({not_implemented, check_process_code, 3}).
cmp_term(_, _) -> erlang:error({not_implemented, cmp_term, 2}).
counters_add(_, _, _) -> erlang:error({not_implemented, counters_add, 3}).
counters_get(_, _) -> erlang:error({not_implemented, counters_get, 2}).
counters_info(_) -> erlang:error({not_implemented, counters_info, 1}).
counters_new(_) -> erlang:error({not_implemented, counters_new, 1}).
counters_put(_, _, _) -> erlang:error({not_implemented, counters_put, 3}).
crasher(_, _, _, _, _, _) -> erlang:error({not_implemented, crasher, 6}).
create_dist_channel(_, _, _) -> erlang:error({not_implemented, create_dist_channel, 3}).
dirty_process_handle_signals(_) -> erlang:error({not_implemented, dirty_process_handle_signals, 1}).
dist_ctrl_put_data(_, _) -> erlang:error({not_implemented, dist_ctrl_put_data, 2}).
dist_spawn_init(_) -> erlang:error({not_implemented, dist_spawn_init, 1}).
dist_spawn_request(_, _, _, _) -> erlang:error({not_implemented, dist_spawn_request, 4}).
dynamic_node_name() -> erlang:error({not_implemented, dynamic_node_name, 0}).
dynamic_node_name(_) -> erlang:error({not_implemented, dynamic_node_name, 1}).
erase_persistent_terms() -> erlang:error({not_implemented, erase_persistent_terms, 0}).
ets_info_binary(_) -> erlang:error({not_implemented, ets_info_binary, 1}).
ets_lookup_binary_info(_, _) -> erlang:error({not_implemented, ets_lookup_binary_info, 2}).
ets_raw_first(_) -> erlang:error({not_implemented, ets_raw_first, 1}).
ets_raw_next(_, _) -> erlang:error({not_implemented, ets_raw_next, 2}).
ets_super_user(_) -> erlang:error({not_implemented, ets_super_user, 1}).
flush_monitor_messages(_, _, _) -> erlang:error({not_implemented, flush_monitor_messages, 3}).
garbage_collect(_) -> erlang:error({not_implemented, garbage_collect, 1}).
gather_alloc_histograms(_) -> erlang:error({not_implemented, gather_alloc_histograms, 1}).
gather_carrier_info(_) -> erlang:error({not_implemented, gather_carrier_info, 1}).
gather_io_bytes(_, _) -> erlang:error({not_implemented, gather_io_bytes, 2}).
gather_microstate_accounting_result(_, _) ->
    erlang:error({not_implemented, gather_microstate_accounting_result, 2}).
gather_sched_wall_time_result(_) ->
    erlang:error({not_implemented, gather_sched_wall_time_result, 1}).
gather_system_check_result(_) -> erlang:error({not_implemented, gather_system_check_result, 1}).
get_creation() -> erlang:error({not_implemented, get_creation, 0}).
get_dflags() -> erlang:error({not_implemented, get_dflags, 0}).
get_internal_state_blocked(_) -> erlang:error({not_implemented, get_internal_state_blocked, 1}).
group_leader(_, _) -> erlang:error({not_implemented, group_leader, 2}).
group_leader(_, _, _) -> erlang:error({not_implemented, group_leader, 3}).
is_process_alive(_) -> erlang:error({not_implemented, is_process_alive, 1}).
is_process_alive(_, _) -> erlang:error({not_implemented, is_process_alive, 2}).
is_process_executing_dirty(_) -> erlang:error({not_implemented, is_process_executing_dirty, 1}).
is_system_process(_) -> erlang:error({not_implemented, is_system_process, 1}).
list_to_integer(_, _) -> erlang:error({not_implemented, list_to_integer, 2}).
map_hashmap_children(_) -> erlang:error({not_implemented, map_hashmap_children, 1}).
map_next(_, _, _) -> erlang:error({not_implemented, map_next, 3}).
map_to_tuple_keys(_) -> erlang:error({not_implemented, map_to_tuple_keys, 1}).
mc_iterator(_) -> erlang:error({not_implemented, mc_iterator, 1}).
mc_refill(_) -> erlang:error({not_implemented, mc_refill, 1}).
new_connection(_) -> erlang:error({not_implemented, new_connection, 1}).
no_aux_work_threads() -> erlang:error({not_implemented, no_aux_work_threads, 0}).
notify_breakpoint_hit(_, _, _) -> erlang:error({not_implemented, notify_breakpoint_hit, 3}).
open_port(_, _) -> erlang:error({not_implemented, open_port, 2}).
perf_counter_unit() -> erlang:error({not_implemented, perf_counter_unit, 0}).
port_call(_, _, _) -> erlang:error({not_implemented, port_call, 3}).
port_close(_) -> erlang:error({not_implemented, port_close, 1}).
port_command(_, _, _) -> erlang:error({not_implemented, port_command, 3}).
port_connect(_, _) -> erlang:error({not_implemented, port_connect, 2}).
port_control(_, _, _) -> erlang:error({not_implemented, port_control, 3}).
port_info(_) -> erlang:error({not_implemented, port_info, 1}).
port_info(_, _) -> erlang:error({not_implemented, port_info, 2}).
prepare_loading(_, _) -> erlang:error({not_implemented, prepare_loading, 2}).
process_display(_, _) -> erlang:error({not_implemented, process_display, 2}).
process_flag(_, _, _) -> erlang:error({not_implemented, process_flag, 3}).
processes_next(_) -> erlang:error({not_implemented, processes_next, 1}).
purge_module(_, _) -> erlang:error({not_implemented, purge_module, 2}).
request_system_task(_, _, _) -> erlang:error({not_implemented, request_system_task, 3}).
request_system_task(_, _, _, _) -> erlang:error({not_implemented, request_system_task, 4}).
scheduler_wall_time(_) -> erlang:error({not_implemented, scheduler_wall_time, 1}).
set_code_and_literal_cleaner_prio(_) ->
    erlang:error({not_implemented, set_code_and_literal_cleaner_prio, 1}).
spawn_init(_) -> erlang:error({not_implemented, spawn_init, 1}).
spawn_request(_, _, _, _) -> erlang:error({not_implemented, spawn_request, 4}).
spawn_system_process(_, _, _) -> erlang:error({not_implemented, spawn_system_process, 3}).
suspend_process(_, _) -> erlang:error({not_implemented, suspend_process, 2}).
system_check(_) -> erlang:error({not_implemented, system_check, 1}).
system_flag_scheduler_wall_time(_) ->
    erlang:error({not_implemented, system_flag_scheduler_wall_time, 1}).
system_monitor(_) -> erlang:error({not_implemented, system_monitor, 1}).
system_monitor(_, _, _) -> erlang:error({not_implemented, system_monitor, 3}).
term_to_string(_) -> erlang:error({not_implemented, term_to_string, 1}).
term_to_string(_, _) -> erlang:error({not_implemented, term_to_string, 2}).
term_type(_) -> erlang:error({not_implemented, term_type, 1}).
time_unit() -> erlang:error({not_implemented, time_unit, 0}).
trace(_, _, _) -> erlang:error({not_implemented, trace, 3}).
trace(_, _, _, _) -> erlang:error({not_implemented, trace, 4}).
trace_info(_, _, _) -> erlang:error({not_implemented, trace_info, 3}).
trace_pattern(_, _, _) -> erlang:error({not_implemented, trace_pattern, 3}).
trace_pattern(_, _, _, _) -> erlang:error({not_implemented, trace_pattern, 4}).
trace_session_create(_, _, _) -> erlang:error({not_implemented, trace_session_create, 3}).
trace_session_destroy(_) -> erlang:error({not_implemented, trace_session_destroy, 1}).
wait_release_literal_area_switch(_) ->
    erlang:error({not_implemented, wait_release_literal_area_switch, 1}).
