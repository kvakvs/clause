%% Clause's replacement for OTP's erts_internal (docs/libraries/library.md#runtime-module-replacements), generated from
%% erts_internal.terms by tests/compiler/library/library_modules.py; edited by hand from here on. A stub raises
%% error({not_implemented, Name, Arity}). The functions stdlib needs are implemented (plan step 41N,
%% docs/builtins.md#erts-internal): cmp_term/2 and map_next/3 are runtime builtins, the rest Erlang; write-concurrency
%% counters are atomics arrays. A non-distributed node answers the distribution queries; the other functions serve
%% the emulator itself, .beam files or distribution and stay declined.
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
%% Helper of binary_to_integer/1,2: the integer, big beyond a small integer, or badarg.
binary_to_integer(Binary, Base) ->
    try erlang:binary_to_integer(Binary, Base) of
        Integer -> small_or_big(Integer)
    catch
        error:_ -> badarg
    end.
breakpoint(_, _, _, _) -> erlang:error({not_implemented, breakpoint, 4}).
check_dirty_process_code(_, _) -> erlang:error({not_implemented, check_dirty_process_code, 2}).
check_process_code(_) -> erlang:error({not_implemented, check_process_code, 1}).
check_process_code(_, _, _) -> erlang:error({not_implemented, check_process_code, 3}).
cmp_term(_, _) -> clause_builtin_placeholder.
%% Write-concurrency counters: a signed atomics array, as atomics counters are.
counters_add(Ref, Ix, Incr) -> atomics:add(Ref, Ix, Incr).
counters_get(Ref, Ix) -> atomics:get(Ref, Ix).
counters_info(Ref) ->
    #{size := Size, memory := Memory} = atomics:info(Ref),
    #{size => Size, memory => Memory}.
counters_new(Size) -> atomics:new(Size, [{signed, true}]).
counters_put(Ref, Ix, Value) -> atomics:put(Ref, Ix, Value).
crasher(_, _, _, _, _, _) -> erlang:error({not_implemented, crasher, 6}).
create_dist_channel(_, _, _) -> erlang:error({not_implemented, create_dist_channel, 3}).
dirty_process_handle_signals(_) -> erlang:error({not_implemented, dirty_process_handle_signals, 1}).
dist_ctrl_put_data(_, _) -> erlang:error({not_implemented, dist_ctrl_put_data, 2}).
dist_spawn_init(_) -> erlang:error({not_implemented, dist_spawn_init, 1}).
dist_spawn_request(_, _, _, _) -> erlang:error({not_implemented, dist_spawn_request, 4}).
dynamic_node_name() ->
    case persistent_term:get({?MODULE, dynamic_node_name}, false) of
        false -> false;
        _ -> true
    end.

dynamic_node_name(true) ->
    persistent_term:put({?MODULE, dynamic_node_name}, true);
dynamic_node_name(false) ->
    case dynamic_node_name() of
        false ->
            ok;
        _ ->
            _ = persistent_term:erase({?MODULE, dynamic_node_name}),
            ok
    end.
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
%% A node that is not distributed has no creation.
get_creation() -> undefined.
get_dflags() -> erlang:error({not_implemented, get_dflags, 0}).
get_internal_state_blocked(_) -> erlang:error({not_implemented, get_internal_state_blocked, 1}).
group_leader(_, _) -> erlang:error({not_implemented, group_leader, 2}).
group_leader(_, _, _) -> erlang:error({not_implemented, group_leader, 3}).
is_process_alive(_) -> erlang:error({not_implemented, is_process_alive, 1}).
is_process_alive(_, _) -> erlang:error({not_implemented, is_process_alive, 2}).
is_process_executing_dirty(_) -> erlang:error({not_implemented, is_process_executing_dirty, 1}).
is_system_process(_) -> erlang:error({not_implemented, is_system_process, 1}).
%% Helper of list_to_integer/1,2 and string:to_integer/1: {Integer, Rest} for an optional sign and at least one
%% digit, big beyond a small integer, no_integer without digits, not_a_list or badarg.
list_to_integer([], _) ->
    no_integer;
list_to_integer(List, _) when not is_list(List) ->
    not_a_list;
list_to_integer(_, Base) when not is_integer(Base); Base < 2; Base > 36 ->
    badarg;
list_to_integer([$- | Digits], Base) ->
    signed_digits(-1, Digits, Base);
list_to_integer([$+ | Digits], Base) ->
    signed_digits(1, Digits, Base);
list_to_integer(Digits, Base) ->
    signed_digits(1, Digits, Base).

signed_digits(Sign, Digits, Base) ->
    case digits(Digits, Base, 0, 0) of
        {_, _, 0} -> no_integer;
        {Value, Rest, _} -> small_or_big_rest(Sign * Value, Rest)
    end.

digits([C | Rest] = All, Base, Value, Count) when is_integer(C) ->
    case digit(C) of
        D when D < Base -> digits(Rest, Base, Value * Base + D, Count + 1);
        _ -> {Value, All, Count}
    end;
digits(Rest, _, Value, Count) ->
    {Value, Rest, Count}.

digit(C) when C >= $0, C =< $9 -> C - $0;
digit(C) when C >= $a, C =< $z -> C - $a + 10;
digit(C) when C >= $A, C =< $Z -> C - $A + 10;
digit(_) -> 36.

small_or_big_rest(Integer, Rest) ->
    case small_or_big(Integer) of
        big -> big;
        _ -> {Integer, Rest}
    end.

%% OTP's helpers answer big for integers outside a small integer of the word size.
small_or_big(Integer) ->
    Bits = 8 * erlang:system_info(wordsize) - 4,
    case Integer >= -(1 bsl (Bits - 1)) andalso Integer < 1 bsl (Bits - 1) of
        true -> Integer;
        false -> big
    end.

%% No map has hashmap nodes: every map is one sorted table.
map_hashmap_children(_) -> erlang:error(badarg).
map_next(_, _, _) -> clause_builtin_placeholder.
%% The sorted keys of any map, as of OTP's flatmaps.
map_to_tuple_keys(Map) when is_map(Map) -> list_to_tuple(maps:keys(Map)).
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
system_monitor(_) -> erlang:error(notsup).
system_monitor(_, _, _) -> erlang:error(notsup).
term_to_string(_) -> erlang:error({not_implemented, term_to_string, 1}).
term_to_string(_, _) -> erlang:error({not_implemented, term_to_string, 2}).
%% OTP's internal type names for Clause's terms: maps are flatmaps, bitstrings sub_binary as OTP's binary terms
%% usually are, integers fixnum within a small integer of the word size.
term_type(Term) when is_integer(Term) ->
    case small_or_big(Term) of
        big -> bignum;
        _ -> fixnum
    end;
term_type(Term) when is_float(Term) -> hfloat;
term_type([]) ->
    nil;
term_type(Term) when is_list(Term) -> list;
term_type(Term) when is_tuple(Term) -> tuple;
term_type(Term) when is_map(Term) -> flatmap;
term_type(Term) when is_bitstring(Term) -> sub_binary;
term_type(Term) when is_function(Term) ->
    case erlang:fun_info(Term, type) of
        {type, external} -> export;
        _ -> 'fun'
    end;
term_type(Term) when is_reference(Term) -> reference;
term_type(Term) when is_pid(Term) -> pid;
term_type(Term) when is_port(Term) -> port;
term_type(Term) when is_atom(Term) -> atom.
time_unit() -> erlang:error({not_implemented, time_unit, 0}).
trace(_, _, _) -> erlang:error(notsup).
trace(_, _, _, _) -> erlang:error(notsup).
trace_info(_, _, _) -> erlang:error(notsup).
trace_pattern(_, _, _) -> erlang:error(notsup).
trace_pattern(_, _, _, _) -> erlang:error(notsup).
trace_session_create(_, _, _) -> erlang:error(notsup).
trace_session_destroy(_) -> erlang:error(notsup).
wait_release_literal_area_switch(_) ->
    erlang:error({not_implemented, wait_release_literal_area_switch, 1}).
