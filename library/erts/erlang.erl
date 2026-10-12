-module(erlang).
-export([
    '!'/2,
    '*'/2,
    '+'/1,
    '+'/2,
    '++'/2,
    '-'/1,
    '-'/2,
    '--'/2,
    '/'/2,
    '/='/2,
    '<'/2,
    '=/='/2,
    '=:='/2,
    '=<'/2,
    '=='/2,
    '>'/2,
    '>='/2,
    abs/1,
    adler32/1,
    adler32/2,
    adler32_combine/3,
    alias/0,
    alias/1,
    alloc_info/1,
    alloc_sizes/1,
    'and'/2,
    append/2,
    append_element/2,
    apply/2,
    apply/3,
    atom_to_binary/1,
    atom_to_binary/2,
    atom_to_list/1,
    'band'/2,
    binary_part/2,
    binary_part/3,
    binary_to_atom/1,
    binary_to_atom/2,
    binary_to_existing_atom/1,
    binary_to_existing_atom/2,
    binary_to_float/1,
    binary_to_integer/1,
    binary_to_integer/2,
    binary_to_list/1,
    binary_to_list/3,
    binary_to_term/1,
    binary_to_term/2,
    bit_size/1,
    bitstring_to_list/1,
    'bnot'/1,
    'bor'/2,
    'bsl'/2,
    'bsr'/2,
    bump_reductions/1,
    'bxor'/2,
    byte_size/1,
    call_on_load_function/1,
    cancel_timer/1,
    cancel_timer/2,
    ceil/1,
    check_old_code/1,
    check_process_code/2,
    check_process_code/3,
    convert_time_unit/3,
    crc32/1,
    crc32/2,
    crc32_combine/3,
    date/0,
    decode_packet/3,
    delay_trap/2,
    delete_element/2,
    delete_module/1,
    demonitor/1,
    demonitor/2,
    disconnect_node/1,
    display/1,
    display_string/1,
    display_string/2,
    dist_ctrl_get_data/1,
    dist_ctrl_get_data_notification/1,
    dist_ctrl_get_opt/2,
    dist_ctrl_input_handler/2,
    dist_ctrl_put_data/2,
    dist_ctrl_set_opt/3,
    dist_get_stat/1,
    'div'/2,
    dmonitor_node/3,
    dt_append_vm_tag_data/1,
    dt_get_tag/0,
    dt_get_tag_data/0,
    dt_prepend_vm_tag_data/1,
    dt_put_tag/1,
    dt_restore_tag/1,
    dt_spread_tag/1,
    element/2,
    erase/0,
    erase/1,
    error/1,
    error/2,
    error/3,
    exit/1,
    exit/2,
    exit/3,
    exit_signal/2,
    exit_signal/3,
    external_size/1,
    external_size/2,
    finish_after_on_load/2,
    finish_loading/1,
    float/1,
    float_to_binary/1,
    float_to_binary/2,
    float_to_list/1,
    float_to_list/2,
    floor/1,
    format_cpu_topology/1,
    fun_info/1,
    fun_info/2,
    fun_info_mfa/1,
    fun_to_list/1,
    function_exported/3,
    garbage_collect/0,
    garbage_collect/1,
    garbage_collect/2,
    garbage_collect_message_area/0,
    gather_gc_info_result/1,
    get/0,
    get/1,
    get_cookie/0,
    get_cookie/1,
    get_keys/0,
    get_keys/1,
    get_module_info/1,
    get_module_info/2,
    group_leader/0,
    group_leader/2,
    halt/0,
    halt/1,
    halt/2,
    has_prepared_code_on_load/1,
    hd/1,
    hibernate/0,
    hibernate/3,
    insert_element/3,
    integer_to_binary/1,
    integer_to_binary/2,
    integer_to_list/1,
    integer_to_list/2,
    iolist_size/1,
    iolist_to_binary/1,
    iolist_to_iovec/1,
    is_alive/0,
    is_atom/1,
    is_binary/1,
    is_bitstring/1,
    is_boolean/1,
    is_builtin/3,
    is_float/1,
    is_function/1,
    is_function/2,
    is_integer/1,
    is_integer/3,
    is_list/1,
    is_map/1,
    is_map_key/2,
    is_number/1,
    is_pid/1,
    is_port/1,
    is_process_alive/1,
    is_record/1,
    is_record/2,
    is_record/3,
    is_reference/1,
    is_tuple/1,
    length/1,
    link/1,
    link/2,
    list_to_atom/1,
    list_to_binary/1,
    list_to_bitstring/1,
    list_to_existing_atom/1,
    list_to_float/1,
    list_to_integer/1,
    list_to_integer/2,
    list_to_pid/1,
    list_to_port/1,
    list_to_ref/1,
    list_to_tuple/1,
    load_module/2,
    load_nif/2,
    loaded/0,
    localtime/0,
    localtime_to_universaltime/1,
    localtime_to_universaltime/2,
    make_fun/3,
    make_ref/0,
    make_tuple/2,
    make_tuple/3,
    map_get/2,
    map_size/1,
    match_spec_test/3,
    max/2,
    md5/1,
    md5_final/1,
    md5_init/0,
    md5_update/2,
    memory/0,
    memory/1,
    min/2,
    module_loaded/1,
    monitor/2,
    monitor/3,
    monitor_node/2,
    monitor_node/3,
    monotonic_time/0,
    monotonic_time/1,
    nif_error/1,
    nif_error/2,
    node/0,
    node/1,
    nodes/0,
    nodes/1,
    nodes/2,
    'not'/1,
    now/0,
    open_port/2,
    'or'/2,
    phash/2,
    phash2/1,
    phash2/2,
    pid_to_list/1,
    port_call/2,
    port_call/3,
    port_close/1,
    port_command/2,
    port_command/3,
    port_connect/2,
    port_control/3,
    port_get_data/1,
    port_info/1,
    port_info/2,
    port_set_data/2,
    port_to_list/1,
    ports/0,
    posixtime_to_universaltime/1,
    pre_loaded/0,
    prepare_loading/2,
    process_display/2,
    process_flag/2,
    process_flag/3,
    process_info/1,
    process_info/2,
    processes/0,
    processes_iterator/0,
    processes_next/1,
    purge_module/1,
    put/2,
    raise/3,
    read_timer/1,
    read_timer/2,
    ref_to_list/1,
    register/2,
    registered/0,
    'rem'/2,
    resume_process/1,
    round/1,
    self/0,
    send/2,
    send/3,
    send_after/3,
    send_after/4,
    send_nosuspend/2,
    send_nosuspend/3,
    seq_trace/2,
    seq_trace_info/1,
    seq_trace_print/1,
    seq_trace_print/2,
    set_cookie/1,
    set_cookie/2,
    set_cpu_topology/1,
    setelement/3,
    setnode/2,
    setnode/3,
    size/1,
    spawn/1,
    spawn/2,
    spawn/3,
    spawn/4,
    spawn_link/1,
    spawn_link/2,
    spawn_link/3,
    spawn_link/4,
    spawn_monitor/1,
    spawn_monitor/2,
    spawn_monitor/3,
    spawn_monitor/4,
    spawn_opt/2,
    spawn_opt/3,
    spawn_opt/4,
    spawn_opt/5,
    spawn_request/1,
    spawn_request/2,
    spawn_request/3,
    spawn_request/4,
    spawn_request/5,
    spawn_request_abandon/1,
    split_binary/2,
    start_timer/3,
    start_timer/4,
    statistics/1,
    subtract/2,
    suspend_process/1,
    suspend_process/2,
    system_flag/2,
    system_info/1,
    system_monitor/0,
    system_monitor/1,
    system_monitor/2,
    system_profile/0,
    system_profile/2,
    system_time/0,
    system_time/1,
    term_to_binary/1,
    term_to_binary/2,
    term_to_iovec/1,
    term_to_iovec/2,
    throw/1,
    time/0,
    time_offset/0,
    time_offset/1,
    timestamp/0,
    tl/1,
    trace/3,
    trace_delivered/1,
    trace_info/2,
    trace_pattern/2,
    trace_pattern/3,
    trunc/1,
    tuple_size/1,
    tuple_to_list/1,
    unalias/1,
    unique_integer/0,
    unique_integer/1,
    universaltime/0,
    universaltime_to_localtime/1,
    universaltime_to_posixtime/1,
    unlink/1,
    unregister/1,
    whereis/1,
    'xor'/2,
    yield/0
]).

%% Generated by tests/compiler/builtins/generate_erlang.py from erlang.terms; do not edit
%% (docs/builtins.md#erlang-module). The runtime's builtins replace the stubs of implemented functions when this
%% module registers; any other stub raises error({not_implemented, Name, Arity}) when called.

'!'(_, _) -> erlang:error({not_implemented, '!', 2}).

'*'(_, _) -> erlang:error({not_implemented, '*', 2}).

'+'(_) -> erlang:error({not_implemented, '+', 1}).

'+'(_, _) -> erlang:error({not_implemented, '+', 2}).

'++'(_, _) -> erlang:error({not_implemented, '++', 2}).

'-'(_) -> erlang:error({not_implemented, '-', 1}).

'-'(_, _) -> erlang:error({not_implemented, '-', 2}).

'--'(_, _) -> erlang:error({not_implemented, '--', 2}).

'/'(_, _) -> erlang:error({not_implemented, '/', 2}).

'/='(_, _) -> erlang:error({not_implemented, '/=', 2}).

'<'(_, _) -> erlang:error({not_implemented, '<', 2}).

'=/='(_, _) -> erlang:error({not_implemented, '=/=', 2}).

'=:='(_, _) -> erlang:error({not_implemented, '=:=', 2}).

'=<'(_, _) -> erlang:error({not_implemented, '=<', 2}).

'=='(_, _) -> erlang:error({not_implemented, '==', 2}).

'>'(_, _) -> erlang:error({not_implemented, '>', 2}).

'>='(_, _) -> erlang:error({not_implemented, '>=', 2}).

abs(_) -> erlang:error({not_implemented, abs, 1}).

adler32(_) -> erlang:error({not_implemented, adler32, 1}).

adler32(_, _) -> erlang:error({not_implemented, adler32, 2}).

adler32_combine(_, _, _) -> erlang:error({not_implemented, adler32_combine, 3}).

alias() -> erlang:error({not_implemented, alias, 0}).

alias(_) -> erlang:error({not_implemented, alias, 1}).

alloc_info(_) -> erlang:error({not_implemented, alloc_info, 1}).

alloc_sizes(_) -> erlang:error({not_implemented, alloc_sizes, 1}).

'and'(_, _) -> erlang:error({not_implemented, 'and', 2}).

append(_, _) -> erlang:error({not_implemented, append, 2}).

append_element(_, _) -> erlang:error({not_implemented, append_element, 2}).

apply(_, _) -> erlang:error({not_implemented, apply, 2}).

apply(_, _, _) -> erlang:error({not_implemented, apply, 3}).

atom_to_binary(_) -> erlang:error({not_implemented, atom_to_binary, 1}).

atom_to_binary(_, _) -> erlang:error({not_implemented, atom_to_binary, 2}).

atom_to_list(_) -> erlang:error({not_implemented, atom_to_list, 1}).

'band'(_, _) -> erlang:error({not_implemented, 'band', 2}).

binary_part(_, _) -> erlang:error({not_implemented, binary_part, 2}).

binary_part(_, _, _) -> erlang:error({not_implemented, binary_part, 3}).

binary_to_atom(_) -> erlang:error({not_implemented, binary_to_atom, 1}).

binary_to_atom(_, _) -> erlang:error({not_implemented, binary_to_atom, 2}).

binary_to_existing_atom(_) -> erlang:error({not_implemented, binary_to_existing_atom, 1}).

binary_to_existing_atom(_, _) -> erlang:error({not_implemented, binary_to_existing_atom, 2}).

binary_to_float(_) -> erlang:error({not_implemented, binary_to_float, 1}).

binary_to_integer(_) -> erlang:error({not_implemented, binary_to_integer, 1}).

binary_to_integer(_, _) -> erlang:error({not_implemented, binary_to_integer, 2}).

binary_to_list(_) -> erlang:error({not_implemented, binary_to_list, 1}).

binary_to_list(_, _, _) -> erlang:error({not_implemented, binary_to_list, 3}).

binary_to_term(_) -> erlang:error({not_implemented, binary_to_term, 1}).

binary_to_term(_, _) -> erlang:error({not_implemented, binary_to_term, 2}).

bit_size(_) -> erlang:error({not_implemented, bit_size, 1}).

bitstring_to_list(_) -> erlang:error({not_implemented, bitstring_to_list, 1}).

'bnot'(_) -> erlang:error({not_implemented, 'bnot', 1}).

'bor'(_, _) -> erlang:error({not_implemented, 'bor', 2}).

'bsl'(_, _) -> erlang:error({not_implemented, 'bsl', 2}).

'bsr'(_, _) -> erlang:error({not_implemented, 'bsr', 2}).

bump_reductions(_) -> erlang:error({not_implemented, bump_reductions, 1}).

'bxor'(_, _) -> erlang:error({not_implemented, 'bxor', 2}).

byte_size(_) -> erlang:error({not_implemented, byte_size, 1}).

call_on_load_function(_) -> erlang:error({not_implemented, call_on_load_function, 1}).

cancel_timer(_) -> erlang:error({not_implemented, cancel_timer, 1}).

cancel_timer(_, _) -> erlang:error({not_implemented, cancel_timer, 2}).

ceil(_) -> erlang:error({not_implemented, ceil, 1}).

check_old_code(_) -> erlang:error({not_implemented, check_old_code, 1}).

check_process_code(_, _) -> erlang:error({not_implemented, check_process_code, 2}).

check_process_code(_, _, _) -> erlang:error({not_implemented, check_process_code, 3}).

convert_time_unit(_, _, _) -> erlang:error({not_implemented, convert_time_unit, 3}).

crc32(_) -> erlang:error({not_implemented, crc32, 1}).

crc32(_, _) -> erlang:error({not_implemented, crc32, 2}).

crc32_combine(_, _, _) -> erlang:error({not_implemented, crc32_combine, 3}).

date() -> erlang:error({not_implemented, date, 0}).

decode_packet(_, _, _) -> erlang:error({not_implemented, decode_packet, 3}).

delay_trap(_, _) -> erlang:error({not_implemented, delay_trap, 2}).

delete_element(_, _) -> erlang:error({not_implemented, delete_element, 2}).

delete_module(_) -> erlang:error({not_implemented, delete_module, 1}).

demonitor(_) -> erlang:error({not_implemented, demonitor, 1}).

demonitor(_, _) -> erlang:error({not_implemented, demonitor, 2}).

disconnect_node(_) -> erlang:error({not_implemented, disconnect_node, 1}).

display(_) -> erlang:error({not_implemented, display, 1}).

display_string(_) -> erlang:error({not_implemented, display_string, 1}).

display_string(_, _) -> erlang:error({not_implemented, display_string, 2}).

dist_ctrl_get_data(_) -> erlang:error({not_implemented, dist_ctrl_get_data, 1}).

dist_ctrl_get_data_notification(_) ->
    erlang:error({not_implemented, dist_ctrl_get_data_notification, 1}).

dist_ctrl_get_opt(_, _) -> erlang:error({not_implemented, dist_ctrl_get_opt, 2}).

dist_ctrl_input_handler(_, _) -> erlang:error({not_implemented, dist_ctrl_input_handler, 2}).

dist_ctrl_put_data(_, _) -> erlang:error({not_implemented, dist_ctrl_put_data, 2}).

dist_ctrl_set_opt(_, _, _) -> erlang:error({not_implemented, dist_ctrl_set_opt, 3}).

dist_get_stat(_) -> erlang:error({not_implemented, dist_get_stat, 1}).

'div'(_, _) -> erlang:error({not_implemented, 'div', 2}).

dmonitor_node(_, _, _) -> erlang:error({not_implemented, dmonitor_node, 3}).

dt_append_vm_tag_data(_) -> erlang:error({not_implemented, dt_append_vm_tag_data, 1}).

dt_get_tag() -> erlang:error({not_implemented, dt_get_tag, 0}).

dt_get_tag_data() -> erlang:error({not_implemented, dt_get_tag_data, 0}).

dt_prepend_vm_tag_data(_) -> erlang:error({not_implemented, dt_prepend_vm_tag_data, 1}).

dt_put_tag(_) -> erlang:error({not_implemented, dt_put_tag, 1}).

dt_restore_tag(_) -> erlang:error({not_implemented, dt_restore_tag, 1}).

dt_spread_tag(_) -> erlang:error({not_implemented, dt_spread_tag, 1}).

element(_, _) -> erlang:error({not_implemented, element, 2}).

erase() -> erlang:error({not_implemented, erase, 0}).

erase(_) -> erlang:error({not_implemented, erase, 1}).

error(_) -> erlang:error({not_implemented, error, 1}).

error(_, _) -> erlang:error({not_implemented, error, 2}).

error(_, _, _) -> erlang:error({not_implemented, error, 3}).

exit(_) -> erlang:error({not_implemented, exit, 1}).

exit(_, _) -> erlang:error({not_implemented, exit, 2}).

exit(_, _, _) -> erlang:error({not_implemented, exit, 3}).

exit_signal(_, _) -> erlang:error({not_implemented, exit_signal, 2}).

exit_signal(_, _, _) -> erlang:error({not_implemented, exit_signal, 3}).

external_size(_) -> erlang:error({not_implemented, external_size, 1}).

external_size(_, _) -> erlang:error({not_implemented, external_size, 2}).

finish_after_on_load(_, _) -> erlang:error({not_implemented, finish_after_on_load, 2}).

finish_loading(_) -> erlang:error({not_implemented, finish_loading, 1}).

float(_) -> erlang:error({not_implemented, float, 1}).

float_to_binary(_) -> erlang:error({not_implemented, float_to_binary, 1}).

float_to_binary(_, _) -> erlang:error({not_implemented, float_to_binary, 2}).

float_to_list(_) -> erlang:error({not_implemented, float_to_list, 1}).

float_to_list(_, _) -> erlang:error({not_implemented, float_to_list, 2}).

floor(_) -> erlang:error({not_implemented, floor, 1}).

format_cpu_topology(_) -> erlang:error({not_implemented, format_cpu_topology, 1}).

fun_info(_) -> erlang:error({not_implemented, fun_info, 1}).

fun_info(_, _) -> erlang:error({not_implemented, fun_info, 2}).

fun_info_mfa(_) -> erlang:error({not_implemented, fun_info_mfa, 1}).

fun_to_list(_) -> erlang:error({not_implemented, fun_to_list, 1}).

function_exported(_, _, _) -> erlang:error({not_implemented, function_exported, 3}).

garbage_collect() -> erlang:error({not_implemented, garbage_collect, 0}).

garbage_collect(_) -> erlang:error({not_implemented, garbage_collect, 1}).

garbage_collect(_, _) -> erlang:error({not_implemented, garbage_collect, 2}).

garbage_collect_message_area() -> erlang:error({not_implemented, garbage_collect_message_area, 0}).

gather_gc_info_result(_) -> erlang:error({not_implemented, gather_gc_info_result, 1}).

get() -> erlang:error({not_implemented, get, 0}).

get(_) -> erlang:error({not_implemented, get, 1}).

get_cookie() -> erlang:error({not_implemented, get_cookie, 0}).

get_cookie(_) -> erlang:error({not_implemented, get_cookie, 1}).

get_keys() -> erlang:error({not_implemented, get_keys, 0}).

get_keys(_) -> erlang:error({not_implemented, get_keys, 1}).

get_module_info(_) -> erlang:error({not_implemented, get_module_info, 1}).

get_module_info(_, _) -> erlang:error({not_implemented, get_module_info, 2}).

group_leader() -> erlang:error({not_implemented, group_leader, 0}).

group_leader(_, _) -> erlang:error({not_implemented, group_leader, 2}).

halt() -> erlang:error({not_implemented, halt, 0}).

halt(_) -> erlang:error({not_implemented, halt, 1}).

halt(_, _) -> erlang:error({not_implemented, halt, 2}).

has_prepared_code_on_load(_) -> erlang:error({not_implemented, has_prepared_code_on_load, 1}).

hd(_) -> erlang:error({not_implemented, hd, 1}).

hibernate() -> erlang:error({not_implemented, hibernate, 0}).

hibernate(_, _, _) -> erlang:error({not_implemented, hibernate, 3}).

insert_element(_, _, _) -> erlang:error({not_implemented, insert_element, 3}).

integer_to_binary(_) -> erlang:error({not_implemented, integer_to_binary, 1}).

integer_to_binary(_, _) -> erlang:error({not_implemented, integer_to_binary, 2}).

integer_to_list(_) -> erlang:error({not_implemented, integer_to_list, 1}).

integer_to_list(_, _) -> erlang:error({not_implemented, integer_to_list, 2}).

iolist_size(_) -> erlang:error({not_implemented, iolist_size, 1}).

iolist_to_binary(_) -> erlang:error({not_implemented, iolist_to_binary, 1}).

iolist_to_iovec(_) -> erlang:error({not_implemented, iolist_to_iovec, 1}).

is_alive() -> erlang:error({not_implemented, is_alive, 0}).

is_atom(_) -> erlang:error({not_implemented, is_atom, 1}).

is_binary(_) -> erlang:error({not_implemented, is_binary, 1}).

is_bitstring(_) -> erlang:error({not_implemented, is_bitstring, 1}).

is_boolean(_) -> erlang:error({not_implemented, is_boolean, 1}).

is_builtin(_, _, _) -> erlang:error({not_implemented, is_builtin, 3}).

is_float(_) -> erlang:error({not_implemented, is_float, 1}).

is_function(_) -> erlang:error({not_implemented, is_function, 1}).

is_function(_, _) -> erlang:error({not_implemented, is_function, 2}).

is_integer(_) -> erlang:error({not_implemented, is_integer, 1}).

is_integer(_, _, _) -> erlang:error({not_implemented, is_integer, 3}).

is_list(_) -> erlang:error({not_implemented, is_list, 1}).

is_map(_) -> erlang:error({not_implemented, is_map, 1}).

is_map_key(_, _) -> erlang:error({not_implemented, is_map_key, 2}).

is_number(_) -> erlang:error({not_implemented, is_number, 1}).

is_pid(_) -> erlang:error({not_implemented, is_pid, 1}).

is_port(_) -> erlang:error({not_implemented, is_port, 1}).

is_process_alive(_) -> erlang:error({not_implemented, is_process_alive, 1}).

is_record(_) -> erlang:error({not_implemented, is_record, 1}).

is_record(_, _) -> erlang:error({not_implemented, is_record, 2}).

is_record(_, _, _) -> erlang:error({not_implemented, is_record, 3}).

is_reference(_) -> erlang:error({not_implemented, is_reference, 1}).

is_tuple(_) -> erlang:error({not_implemented, is_tuple, 1}).

length(_) -> erlang:error({not_implemented, length, 1}).

link(_) -> erlang:error({not_implemented, link, 1}).

link(_, _) -> erlang:error({not_implemented, link, 2}).

list_to_atom(_) -> erlang:error({not_implemented, list_to_atom, 1}).

list_to_binary(_) -> erlang:error({not_implemented, list_to_binary, 1}).

list_to_bitstring(_) -> erlang:error({not_implemented, list_to_bitstring, 1}).

list_to_existing_atom(_) -> erlang:error({not_implemented, list_to_existing_atom, 1}).

list_to_float(_) -> erlang:error({not_implemented, list_to_float, 1}).

list_to_integer(_) -> erlang:error({not_implemented, list_to_integer, 1}).

list_to_integer(_, _) -> erlang:error({not_implemented, list_to_integer, 2}).

list_to_pid(_) -> erlang:error({not_implemented, list_to_pid, 1}).

list_to_port(_) -> erlang:error({not_implemented, list_to_port, 1}).

list_to_ref(_) -> erlang:error({not_implemented, list_to_ref, 1}).

list_to_tuple(_) -> erlang:error({not_implemented, list_to_tuple, 1}).

load_module(_, _) -> erlang:error({not_implemented, load_module, 2}).

load_nif(_, _) -> erlang:error({not_implemented, load_nif, 2}).

loaded() -> erlang:error({not_implemented, loaded, 0}).

localtime() -> erlang:error({not_implemented, localtime, 0}).

localtime_to_universaltime(_) -> erlang:error({not_implemented, localtime_to_universaltime, 1}).

localtime_to_universaltime(_, _) -> erlang:error({not_implemented, localtime_to_universaltime, 2}).

make_fun(_, _, _) -> erlang:error({not_implemented, make_fun, 3}).

make_ref() -> erlang:error({not_implemented, make_ref, 0}).

make_tuple(_, _) -> erlang:error({not_implemented, make_tuple, 2}).

make_tuple(_, _, _) -> erlang:error({not_implemented, make_tuple, 3}).

map_get(_, _) -> erlang:error({not_implemented, map_get, 2}).

map_size(_) -> erlang:error({not_implemented, map_size, 1}).

match_spec_test(_, _, _) -> erlang:error({not_implemented, match_spec_test, 3}).

max(_, _) -> erlang:error({not_implemented, max, 2}).

md5(_) -> erlang:error({not_implemented, md5, 1}).

md5_final(_) -> erlang:error({not_implemented, md5_final, 1}).

md5_init() -> erlang:error({not_implemented, md5_init, 0}).

md5_update(_, _) -> erlang:error({not_implemented, md5_update, 2}).

memory() -> erlang:error({not_implemented, memory, 0}).

memory(_) -> erlang:error({not_implemented, memory, 1}).

min(_, _) -> erlang:error({not_implemented, min, 2}).

module_loaded(_) -> erlang:error({not_implemented, module_loaded, 1}).

monitor(_, _) -> erlang:error({not_implemented, monitor, 2}).

monitor(_, _, _) -> erlang:error({not_implemented, monitor, 3}).

monitor_node(_, _) -> erlang:error({not_implemented, monitor_node, 2}).

monitor_node(_, _, _) -> erlang:error({not_implemented, monitor_node, 3}).

monotonic_time() -> erlang:error({not_implemented, monotonic_time, 0}).

monotonic_time(_) -> erlang:error({not_implemented, monotonic_time, 1}).

nif_error(Reason) -> erlang:error(Reason).

nif_error(Reason, Arguments) -> erlang:error(Reason, Arguments).

node() -> erlang:error({not_implemented, node, 0}).

node(_) -> erlang:error({not_implemented, node, 1}).

nodes() -> erlang:error({not_implemented, nodes, 0}).

nodes(_) -> erlang:error({not_implemented, nodes, 1}).

nodes(_, _) -> erlang:error({not_implemented, nodes, 2}).

'not'(_) -> erlang:error({not_implemented, 'not', 1}).

now() -> erlang:error({not_implemented, now, 0}).

open_port(_, _) -> erlang:error({not_implemented, open_port, 2}).

'or'(_, _) -> erlang:error({not_implemented, 'or', 2}).

phash(_, _) -> erlang:error({not_implemented, phash, 2}).

phash2(_) -> erlang:error({not_implemented, phash2, 1}).

phash2(_, _) -> erlang:error({not_implemented, phash2, 2}).

pid_to_list(_) -> erlang:error({not_implemented, pid_to_list, 1}).

port_call(_, _) -> erlang:error({not_implemented, port_call, 2}).

port_call(_, _, _) -> erlang:error({not_implemented, port_call, 3}).

port_close(_) -> erlang:error({not_implemented, port_close, 1}).

port_command(_, _) -> erlang:error({not_implemented, port_command, 2}).

port_command(_, _, _) -> erlang:error({not_implemented, port_command, 3}).

port_connect(_, _) -> erlang:error({not_implemented, port_connect, 2}).

port_control(_, _, _) -> erlang:error({not_implemented, port_control, 3}).

port_get_data(_) -> erlang:error({not_implemented, port_get_data, 1}).

port_info(_) -> erlang:error({not_implemented, port_info, 1}).

port_info(_, _) -> erlang:error({not_implemented, port_info, 2}).

port_set_data(_, _) -> erlang:error({not_implemented, port_set_data, 2}).

port_to_list(_) -> erlang:error({not_implemented, port_to_list, 1}).

ports() -> erlang:error({not_implemented, ports, 0}).

posixtime_to_universaltime(_) -> erlang:error({not_implemented, posixtime_to_universaltime, 1}).

pre_loaded() -> erlang:error({not_implemented, pre_loaded, 0}).

prepare_loading(_, _) -> erlang:error({not_implemented, prepare_loading, 2}).

process_display(_, _) -> erlang:error({not_implemented, process_display, 2}).

process_flag(_, _) -> erlang:error({not_implemented, process_flag, 2}).

process_flag(_, _, _) -> erlang:error({not_implemented, process_flag, 3}).

process_info(_) -> erlang:error({not_implemented, process_info, 1}).

process_info(_, _) -> erlang:error({not_implemented, process_info, 2}).

processes() -> erlang:error({not_implemented, processes, 0}).

processes_iterator() -> erlang:error({not_implemented, processes_iterator, 0}).

processes_next(_) -> erlang:error({not_implemented, processes_next, 1}).

purge_module(_) -> erlang:error({not_implemented, purge_module, 1}).

put(_, _) -> erlang:error({not_implemented, put, 2}).

raise(_, _, _) -> erlang:error({not_implemented, raise, 3}).

read_timer(_) -> erlang:error({not_implemented, read_timer, 1}).

read_timer(_, _) -> erlang:error({not_implemented, read_timer, 2}).

ref_to_list(_) -> erlang:error({not_implemented, ref_to_list, 1}).

register(_, _) -> erlang:error({not_implemented, register, 2}).

registered() -> erlang:error({not_implemented, registered, 0}).

'rem'(_, _) -> erlang:error({not_implemented, 'rem', 2}).

resume_process(_) -> erlang:error({not_implemented, resume_process, 1}).

round(_) -> erlang:error({not_implemented, round, 1}).

self() -> erlang:error({not_implemented, self, 0}).

send(_, _) -> erlang:error({not_implemented, send, 2}).

send(_, _, _) -> erlang:error({not_implemented, send, 3}).

send_after(_, _, _) -> erlang:error({not_implemented, send_after, 3}).

send_after(_, _, _, _) -> erlang:error({not_implemented, send_after, 4}).

send_nosuspend(_, _) -> erlang:error({not_implemented, send_nosuspend, 2}).

send_nosuspend(_, _, _) -> erlang:error({not_implemented, send_nosuspend, 3}).

seq_trace(_, _) -> erlang:error({not_implemented, seq_trace, 2}).

seq_trace_info(_) -> erlang:error({not_implemented, seq_trace_info, 1}).

seq_trace_print(_) -> erlang:error({not_implemented, seq_trace_print, 1}).

seq_trace_print(_, _) -> erlang:error({not_implemented, seq_trace_print, 2}).

set_cookie(_) -> erlang:error({not_implemented, set_cookie, 1}).

set_cookie(_, _) -> erlang:error({not_implemented, set_cookie, 2}).

set_cpu_topology(_) -> erlang:error({not_implemented, set_cpu_topology, 1}).

setelement(_, _, _) -> erlang:error({not_implemented, setelement, 3}).

setnode(_, _) -> erlang:error({not_implemented, setnode, 2}).

setnode(_, _, _) -> erlang:error({not_implemented, setnode, 3}).

size(_) -> erlang:error({not_implemented, size, 1}).

spawn(_) -> erlang:error({not_implemented, spawn, 1}).

spawn(_, _) -> erlang:error({not_implemented, spawn, 2}).

spawn(_, _, _) -> erlang:error({not_implemented, spawn, 3}).

spawn(_, _, _, _) -> erlang:error({not_implemented, spawn, 4}).

spawn_link(_) -> erlang:error({not_implemented, spawn_link, 1}).

spawn_link(_, _) -> erlang:error({not_implemented, spawn_link, 2}).

spawn_link(_, _, _) -> erlang:error({not_implemented, spawn_link, 3}).

spawn_link(_, _, _, _) -> erlang:error({not_implemented, spawn_link, 4}).

spawn_monitor(_) -> erlang:error({not_implemented, spawn_monitor, 1}).

spawn_monitor(_, _) -> erlang:error({not_implemented, spawn_monitor, 2}).

spawn_monitor(_, _, _) -> erlang:error({not_implemented, spawn_monitor, 3}).

spawn_monitor(_, _, _, _) -> erlang:error({not_implemented, spawn_monitor, 4}).

spawn_opt(_, _) -> erlang:error({not_implemented, spawn_opt, 2}).

spawn_opt(_, _, _) -> erlang:error({not_implemented, spawn_opt, 3}).

spawn_opt(_, _, _, _) -> erlang:error({not_implemented, spawn_opt, 4}).

spawn_opt(_, _, _, _, _) -> erlang:error({not_implemented, spawn_opt, 5}).

spawn_request(_) -> erlang:error({not_implemented, spawn_request, 1}).

spawn_request(_, _) -> erlang:error({not_implemented, spawn_request, 2}).

spawn_request(_, _, _) -> erlang:error({not_implemented, spawn_request, 3}).

spawn_request(_, _, _, _) -> erlang:error({not_implemented, spawn_request, 4}).

spawn_request(_, _, _, _, _) -> erlang:error({not_implemented, spawn_request, 5}).

spawn_request_abandon(_) -> erlang:error({not_implemented, spawn_request_abandon, 1}).

split_binary(_, _) -> erlang:error({not_implemented, split_binary, 2}).

start_timer(_, _, _) -> erlang:error({not_implemented, start_timer, 3}).

start_timer(_, _, _, _) -> erlang:error({not_implemented, start_timer, 4}).

statistics(_) -> erlang:error({not_implemented, statistics, 1}).

subtract(_, _) -> erlang:error({not_implemented, subtract, 2}).

suspend_process(_) -> erlang:error({not_implemented, suspend_process, 1}).

suspend_process(_, _) -> erlang:error({not_implemented, suspend_process, 2}).

system_flag(_, _) -> erlang:error({not_implemented, system_flag, 2}).

system_info(_) -> erlang:error({not_implemented, system_info, 1}).

system_monitor() -> erlang:error({not_implemented, system_monitor, 0}).

system_monitor(_) -> erlang:error({not_implemented, system_monitor, 1}).

system_monitor(_, _) -> erlang:error({not_implemented, system_monitor, 2}).

system_profile() -> erlang:error({not_implemented, system_profile, 0}).

system_profile(_, _) -> erlang:error({not_implemented, system_profile, 2}).

system_time() -> erlang:error({not_implemented, system_time, 0}).

system_time(_) -> erlang:error({not_implemented, system_time, 1}).

term_to_binary(_) -> erlang:error({not_implemented, term_to_binary, 1}).

term_to_binary(_, _) -> erlang:error({not_implemented, term_to_binary, 2}).

term_to_iovec(_) -> erlang:error({not_implemented, term_to_iovec, 1}).

term_to_iovec(_, _) -> erlang:error({not_implemented, term_to_iovec, 2}).

throw(_) -> erlang:error({not_implemented, throw, 1}).

time() -> erlang:error({not_implemented, time, 0}).

time_offset() -> erlang:error({not_implemented, time_offset, 0}).

time_offset(_) -> erlang:error({not_implemented, time_offset, 1}).

timestamp() -> erlang:error({not_implemented, timestamp, 0}).

tl(_) -> erlang:error({not_implemented, tl, 1}).

trace(_, _, _) -> erlang:error({not_implemented, trace, 3}).

trace_delivered(_) -> erlang:error({not_implemented, trace_delivered, 1}).

trace_info(_, _) -> erlang:error({not_implemented, trace_info, 2}).

trace_pattern(_, _) -> erlang:error({not_implemented, trace_pattern, 2}).

trace_pattern(_, _, _) -> erlang:error({not_implemented, trace_pattern, 3}).

trunc(_) -> erlang:error({not_implemented, trunc, 1}).

tuple_size(_) -> erlang:error({not_implemented, tuple_size, 1}).

tuple_to_list(_) -> erlang:error({not_implemented, tuple_to_list, 1}).

unalias(_) -> erlang:error({not_implemented, unalias, 1}).

unique_integer() -> erlang:error({not_implemented, unique_integer, 0}).

unique_integer(_) -> erlang:error({not_implemented, unique_integer, 1}).

universaltime() -> erlang:error({not_implemented, universaltime, 0}).

universaltime_to_localtime(_) -> erlang:error({not_implemented, universaltime_to_localtime, 1}).

universaltime_to_posixtime(_) -> erlang:error({not_implemented, universaltime_to_posixtime, 1}).

unlink(_) -> erlang:error({not_implemented, unlink, 1}).

unregister(_) -> erlang:error({not_implemented, unregister, 1}).

whereis(_) -> erlang:error({not_implemented, whereis, 1}).

'xor'(_, _) -> erlang:error({not_implemented, 'xor', 2}).

yield() -> erlang:error({not_implemented, yield, 0}).
