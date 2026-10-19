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
-include("erlang.types.hrl").

%% Generated by tests/compiler/builtins/generate_erlang.py from erlang.terms; do not edit
%% (docs/builtins.md#erlang-module). The runtime's builtins replace the clause_builtin_placeholder bodies of the
%% catalog builtins when this module registers; a stub raises error({not_implemented, Name, Arity}) when called.

'!'(_, _) -> clause_builtin_placeholder.

'*'(_, _) -> clause_builtin_placeholder.

'+'(_) -> clause_builtin_placeholder.

'+'(_, _) -> clause_builtin_placeholder.

'++'(_, _) -> clause_builtin_placeholder.

'-'(_) -> clause_builtin_placeholder.

'-'(_, _) -> clause_builtin_placeholder.

'--'(_, _) -> clause_builtin_placeholder.

'/'(_, _) -> clause_builtin_placeholder.

'/='(_, _) -> clause_builtin_placeholder.

'<'(_, _) -> clause_builtin_placeholder.

'=/='(_, _) -> clause_builtin_placeholder.

'=:='(_, _) -> clause_builtin_placeholder.

'=<'(_, _) -> clause_builtin_placeholder.

'=='(_, _) -> clause_builtin_placeholder.

'>'(_, _) -> clause_builtin_placeholder.

'>='(_, _) -> clause_builtin_placeholder.

abs(_) -> clause_builtin_placeholder.

adler32(_) -> clause_builtin_placeholder.

adler32(_, _) -> clause_builtin_placeholder.

adler32_combine(_, _, _) -> clause_builtin_placeholder.

alias() -> clause_builtin_placeholder.

alias(_) -> clause_builtin_placeholder.

alloc_info(_) -> erlang:error({not_implemented, alloc_info, 1}).

alloc_sizes(_) -> erlang:error({not_implemented, alloc_sizes, 1}).

'and'(_, _) -> clause_builtin_placeholder.

append(_, _) -> erlang:error({not_implemented, append, 2}).

append_element(_, _) -> clause_builtin_placeholder.

apply(_, _) -> erlang:error({not_implemented, apply, 2}).

apply(_, _, _) -> erlang:error({not_implemented, apply, 3}).

atom_to_binary(_) -> clause_builtin_placeholder.

atom_to_binary(_, _) -> clause_builtin_placeholder.

atom_to_list(_) -> clause_builtin_placeholder.

'band'(_, _) -> clause_builtin_placeholder.

binary_part(_, _) -> clause_builtin_placeholder.

binary_part(_, _, _) -> clause_builtin_placeholder.

binary_to_atom(_) -> clause_builtin_placeholder.

binary_to_atom(_, _) -> clause_builtin_placeholder.

binary_to_existing_atom(_) -> clause_builtin_placeholder.

binary_to_existing_atom(_, _) -> clause_builtin_placeholder.

binary_to_float(_) -> clause_builtin_placeholder.

binary_to_integer(_) -> clause_builtin_placeholder.

binary_to_integer(_, _) -> clause_builtin_placeholder.

binary_to_list(_) -> clause_builtin_placeholder.

binary_to_list(_, _, _) -> clause_builtin_placeholder.

binary_to_term(_) -> clause_builtin_placeholder.

binary_to_term(_, _) -> clause_builtin_placeholder.

bit_size(_) -> clause_builtin_placeholder.

bitstring_to_list(_) -> clause_builtin_placeholder.

'bnot'(_) -> clause_builtin_placeholder.

'bor'(_, _) -> clause_builtin_placeholder.

'bsl'(_, _) -> clause_builtin_placeholder.

'bsr'(_, _) -> clause_builtin_placeholder.

bump_reductions(_) -> clause_builtin_placeholder.

'bxor'(_, _) -> clause_builtin_placeholder.

byte_size(_) -> clause_builtin_placeholder.

call_on_load_function(_) -> erlang:error({not_implemented, call_on_load_function, 1}).

cancel_timer(_) -> clause_builtin_placeholder.

cancel_timer(_, _) -> clause_builtin_placeholder.

ceil(_) -> clause_builtin_placeholder.

check_old_code(_) -> erlang:error({not_implemented, check_old_code, 1}).

check_process_code(_, _) -> erlang:error({not_implemented, check_process_code, 2}).

check_process_code(_, _, _) -> erlang:error({not_implemented, check_process_code, 3}).

convert_time_unit(_, _, _) -> clause_builtin_placeholder.

crc32(_) -> clause_builtin_placeholder.

crc32(_, _) -> clause_builtin_placeholder.

crc32_combine(_, _, _) -> clause_builtin_placeholder.

date() -> clause_builtin_placeholder.

decode_packet(_, _, _) -> clause_builtin_placeholder.

delay_trap(_, _) -> erlang:error({not_implemented, delay_trap, 2}).

delete_element(_, _) -> clause_builtin_placeholder.

delete_module(_) -> clause_builtin_placeholder.

demonitor(_) -> clause_builtin_placeholder.

demonitor(_, _) -> clause_builtin_placeholder.

disconnect_node(_) -> clause_builtin_placeholder.

display(_) -> clause_builtin_placeholder.

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

'div'(_, _) -> clause_builtin_placeholder.

dmonitor_node(_, _, _) -> erlang:error({not_implemented, dmonitor_node, 3}).

dt_append_vm_tag_data(_) -> erlang:error({not_implemented, dt_append_vm_tag_data, 1}).

dt_get_tag() -> erlang:error({not_implemented, dt_get_tag, 0}).

dt_get_tag_data() -> erlang:error({not_implemented, dt_get_tag_data, 0}).

dt_prepend_vm_tag_data(_) -> erlang:error({not_implemented, dt_prepend_vm_tag_data, 1}).

dt_put_tag(_) -> erlang:error({not_implemented, dt_put_tag, 1}).

dt_restore_tag(_) -> erlang:error({not_implemented, dt_restore_tag, 1}).

dt_spread_tag(_) -> erlang:error({not_implemented, dt_spread_tag, 1}).

element(_, _) -> clause_builtin_placeholder.

erase() -> clause_builtin_placeholder.

erase(_) -> clause_builtin_placeholder.

error(_) -> clause_builtin_placeholder.

error(_, _) -> clause_builtin_placeholder.

error(_, _, _) -> clause_builtin_placeholder.

exit(_) -> clause_builtin_placeholder.

exit(_, _) -> clause_builtin_placeholder.

exit(_, _, _) -> clause_builtin_placeholder.

exit_signal(_, _) -> clause_builtin_placeholder.

exit_signal(_, _, _) -> clause_builtin_placeholder.

external_size(_) -> clause_builtin_placeholder.

external_size(_, _) -> clause_builtin_placeholder.

finish_after_on_load(_, _) -> erlang:error({not_implemented, finish_after_on_load, 2}).

finish_loading(_) -> clause_builtin_placeholder.

float(_) -> clause_builtin_placeholder.

float_to_binary(_) -> clause_builtin_placeholder.

float_to_binary(_, _) -> clause_builtin_placeholder.

float_to_list(_) -> clause_builtin_placeholder.

float_to_list(_, _) -> clause_builtin_placeholder.

floor(_) -> clause_builtin_placeholder.

format_cpu_topology(_) -> erlang:error({not_implemented, format_cpu_topology, 1}).

fun_info(_) -> clause_builtin_placeholder.

fun_info(_, _) -> clause_builtin_placeholder.

fun_info_mfa(_) -> erlang:error({not_implemented, fun_info_mfa, 1}).

fun_to_list(_) -> clause_builtin_placeholder.

function_exported(_, _, _) -> clause_builtin_placeholder.

garbage_collect() -> clause_builtin_placeholder.

garbage_collect(_) -> clause_builtin_placeholder.

garbage_collect(_, _) -> clause_builtin_placeholder.

garbage_collect_message_area() -> erlang:error({not_implemented, garbage_collect_message_area, 0}).

gather_gc_info_result(_) -> erlang:error({not_implemented, gather_gc_info_result, 1}).

get() -> clause_builtin_placeholder.

get(_) -> clause_builtin_placeholder.

get_cookie() -> clause_builtin_placeholder.

get_cookie(_) -> clause_builtin_placeholder.

get_keys() -> clause_builtin_placeholder.

get_keys(_) -> clause_builtin_placeholder.

get_module_info(_) -> clause_builtin_placeholder.

get_module_info(_, _) -> clause_builtin_placeholder.

group_leader() -> clause_builtin_placeholder.

group_leader(_, _) -> clause_builtin_placeholder.

halt() -> clause_builtin_placeholder.

halt(_) -> clause_builtin_placeholder.

halt(_, _) -> clause_builtin_placeholder.

has_prepared_code_on_load(_) -> erlang:error({not_implemented, has_prepared_code_on_load, 1}).

hd(_) -> clause_builtin_placeholder.

hibernate() -> _ = erlang:hibernate(erlang, self, []), ok.

hibernate(_, _, _) -> clause_builtin_placeholder.

insert_element(_, _, _) -> clause_builtin_placeholder.

integer_to_binary(_) -> clause_builtin_placeholder.

integer_to_binary(_, _) -> clause_builtin_placeholder.

integer_to_list(_) -> clause_builtin_placeholder.

integer_to_list(_, _) -> clause_builtin_placeholder.

iolist_size(_) -> clause_builtin_placeholder.

iolist_to_binary(_) -> clause_builtin_placeholder.

iolist_to_iovec(_) -> clause_builtin_placeholder.

is_alive() -> clause_builtin_placeholder.

is_atom(_) -> clause_builtin_placeholder.

is_binary(_) -> clause_builtin_placeholder.

is_bitstring(_) -> clause_builtin_placeholder.

is_boolean(_) -> clause_builtin_placeholder.

is_builtin(_, _, _) -> clause_builtin_placeholder.

is_float(_) -> clause_builtin_placeholder.

is_function(_) -> clause_builtin_placeholder.

is_function(_, _) -> clause_builtin_placeholder.

is_integer(_) -> clause_builtin_placeholder.

is_integer(_, _, _) -> clause_builtin_placeholder.

is_list(_) -> clause_builtin_placeholder.

is_map(_) -> clause_builtin_placeholder.

is_map_key(_, _) -> clause_builtin_placeholder.

is_number(_) -> clause_builtin_placeholder.

is_pid(_) -> clause_builtin_placeholder.

is_port(_) -> clause_builtin_placeholder.

is_process_alive(_) -> clause_builtin_placeholder.

is_record(_) -> clause_builtin_placeholder.

is_record(_, _) -> erlang:error({not_implemented, is_record, 2}).

is_record(_, _, _) -> erlang:error({not_implemented, is_record, 3}).

is_reference(_) -> clause_builtin_placeholder.

is_tuple(_) -> clause_builtin_placeholder.

length(_) -> clause_builtin_placeholder.

link(_) -> clause_builtin_placeholder.

link(_, _) -> clause_builtin_placeholder.

list_to_atom(_) -> clause_builtin_placeholder.

list_to_binary(_) -> clause_builtin_placeholder.

list_to_bitstring(_) -> clause_builtin_placeholder.

list_to_existing_atom(_) -> clause_builtin_placeholder.

list_to_float(_) -> clause_builtin_placeholder.

list_to_integer(_) -> clause_builtin_placeholder.

list_to_integer(_, _) -> clause_builtin_placeholder.

list_to_pid(_) -> clause_builtin_placeholder.

list_to_port(_) -> clause_builtin_placeholder.

list_to_ref(_) -> clause_builtin_placeholder.

list_to_tuple(_) -> clause_builtin_placeholder.

load_module(_, _) -> erlang:error({not_implemented, load_module, 2}).

load_nif(_, _) -> erlang:error({not_implemented, load_nif, 2}).

loaded() -> clause_builtin_placeholder.

localtime() -> clause_builtin_placeholder.

localtime_to_universaltime(_) -> clause_builtin_placeholder.

localtime_to_universaltime(_, _) -> clause_builtin_placeholder.

make_fun(_, _, _) -> erlang:error({not_implemented, make_fun, 3}).

make_ref() -> clause_builtin_placeholder.

make_tuple(_, _) -> clause_builtin_placeholder.

make_tuple(_, _, _) -> clause_builtin_placeholder.

map_get(_, _) -> clause_builtin_placeholder.

map_size(_) -> clause_builtin_placeholder.

match_spec_test(_, _, _) -> clause_builtin_placeholder.

max(_, _) -> clause_builtin_placeholder.

md5(_) -> clause_builtin_placeholder.

md5_final(_) -> clause_builtin_placeholder.

md5_init() -> clause_builtin_placeholder.

md5_update(_, _) -> clause_builtin_placeholder.

memory() -> clause_builtin_placeholder.

memory(_) -> clause_builtin_placeholder.

min(_, _) -> clause_builtin_placeholder.

module_loaded(_) -> clause_builtin_placeholder.

monitor(_, _) -> clause_builtin_placeholder.

monitor(_, _, _) -> clause_builtin_placeholder.

monitor_node(_, _) -> clause_builtin_placeholder.

monitor_node(_, _, _) -> clause_builtin_placeholder.

monotonic_time() -> clause_builtin_placeholder.

monotonic_time(_) -> clause_builtin_placeholder.

nif_error(Reason) -> erlang:error(Reason).

nif_error(Reason, Arguments) -> erlang:error(Reason, Arguments).

node() -> clause_builtin_placeholder.

node(_) -> clause_builtin_placeholder.

nodes() -> clause_builtin_placeholder.

nodes(_) -> clause_builtin_placeholder.

nodes(_, _) -> clause_builtin_placeholder.

'not'(_) -> clause_builtin_placeholder.

now() -> clause_builtin_placeholder.

open_port(_, _) -> clause_builtin_placeholder.

'or'(_, _) -> clause_builtin_placeholder.

phash(_, _) -> clause_builtin_placeholder.

phash2(_) -> clause_builtin_placeholder.

phash2(_, _) -> clause_builtin_placeholder.

pid_to_list(_) -> clause_builtin_placeholder.

port_call(_, _) -> clause_builtin_placeholder.

port_call(_, _, _) -> clause_builtin_placeholder.

port_close(_) -> clause_builtin_placeholder.

port_command(_, _) -> clause_builtin_placeholder.

port_command(_, _, _) -> clause_builtin_placeholder.

port_connect(_, _) -> clause_builtin_placeholder.

port_control(_, _, _) -> clause_builtin_placeholder.

port_get_data(_) -> erlang:error({not_implemented, port_get_data, 1}).

port_info(_) -> clause_builtin_placeholder.

port_info(_, _) -> clause_builtin_placeholder.

port_set_data(_, _) -> erlang:error({not_implemented, port_set_data, 2}).

port_to_list(_) -> clause_builtin_placeholder.

ports() -> clause_builtin_placeholder.

posixtime_to_universaltime(_) -> clause_builtin_placeholder.

pre_loaded() -> clause_builtin_placeholder.

prepare_loading(_, _) -> clause_builtin_placeholder.

process_display(_, _) -> clause_builtin_placeholder.

process_flag(_, _) -> clause_builtin_placeholder.

process_flag(_, _, _) -> clause_builtin_placeholder.

process_info(_) -> clause_builtin_placeholder.

process_info(_, _) -> clause_builtin_placeholder.

processes() -> clause_builtin_placeholder.

processes_iterator() -> clause_builtin_placeholder.

processes_next(_) -> clause_builtin_placeholder.

purge_module(_) -> erlang:error({not_implemented, purge_module, 1}).

put(_, _) -> clause_builtin_placeholder.

raise(_, _, _) -> clause_builtin_placeholder.

read_timer(_) -> clause_builtin_placeholder.

read_timer(_, _) -> clause_builtin_placeholder.

ref_to_list(_) -> clause_builtin_placeholder.

register(_, _) -> clause_builtin_placeholder.

registered() -> clause_builtin_placeholder.

'rem'(_, _) -> clause_builtin_placeholder.

resume_process(_) -> clause_builtin_placeholder.

round(_) -> clause_builtin_placeholder.

self() -> clause_builtin_placeholder.

send(_, _) -> clause_builtin_placeholder.

send(_, _, _) -> clause_builtin_placeholder.

send_after(_, _, _) -> clause_builtin_placeholder.

send_after(_, _, _, _) -> clause_builtin_placeholder.

send_nosuspend(_, _) -> clause_builtin_placeholder.

send_nosuspend(_, _, _) -> clause_builtin_placeholder.

seq_trace(_, _) -> erlang:error({not_implemented, seq_trace, 2}).

seq_trace_info(_) -> erlang:error({not_implemented, seq_trace_info, 1}).

seq_trace_print(_) -> erlang:error({not_implemented, seq_trace_print, 1}).

seq_trace_print(_, _) -> erlang:error({not_implemented, seq_trace_print, 2}).

set_cookie(_) -> clause_builtin_placeholder.

set_cookie(_, _) -> clause_builtin_placeholder.

set_cpu_topology(_) -> erlang:error({not_implemented, set_cpu_topology, 1}).

setelement(_, _, _) -> clause_builtin_placeholder.

setnode(_, _) -> erlang:error({not_implemented, setnode, 2}).

setnode(_, _, _) -> erlang:error({not_implemented, setnode, 3}).

size(_) -> clause_builtin_placeholder.

spawn(_) -> clause_builtin_placeholder.

spawn(_, _) -> clause_builtin_placeholder.

spawn(_, _, _) -> clause_builtin_placeholder.

spawn(_, _, _, _) -> clause_builtin_placeholder.

spawn_link(_) -> clause_builtin_placeholder.

spawn_link(_, _) -> clause_builtin_placeholder.

spawn_link(_, _, _) -> clause_builtin_placeholder.

spawn_link(_, _, _, _) -> clause_builtin_placeholder.

spawn_monitor(_) -> clause_builtin_placeholder.

spawn_monitor(_, _) -> clause_builtin_placeholder.

spawn_monitor(_, _, _) -> clause_builtin_placeholder.

spawn_monitor(_, _, _, _) -> clause_builtin_placeholder.

spawn_opt(_, _) -> clause_builtin_placeholder.

spawn_opt(_, _, _) -> clause_builtin_placeholder.

spawn_opt(_, _, _, _) -> clause_builtin_placeholder.

spawn_opt(_, _, _, _, _) -> clause_builtin_placeholder.

spawn_request(_) -> clause_builtin_placeholder.

spawn_request(_, _) -> clause_builtin_placeholder.

spawn_request(_, _, _) -> clause_builtin_placeholder.

spawn_request(_, _, _, _) -> clause_builtin_placeholder.

spawn_request(_, _, _, _, _) -> clause_builtin_placeholder.

spawn_request_abandon(_) -> clause_builtin_placeholder.

split_binary(_, _) -> clause_builtin_placeholder.

start_timer(_, _, _) -> clause_builtin_placeholder.

start_timer(_, _, _, _) -> clause_builtin_placeholder.

statistics(_) -> clause_builtin_placeholder.

subtract(_, _) -> erlang:error({not_implemented, subtract, 2}).

suspend_process(_) -> clause_builtin_placeholder.

suspend_process(_, _) -> clause_builtin_placeholder.

system_flag(_, _) -> clause_builtin_placeholder.

system_info(_) -> clause_builtin_placeholder.

system_monitor() -> erlang:error({not_implemented, system_monitor, 0}).

system_monitor(_) -> erlang:error({not_implemented, system_monitor, 1}).

system_monitor(_, _) -> erlang:error({not_implemented, system_monitor, 2}).

system_profile() -> erlang:error({not_implemented, system_profile, 0}).

system_profile(_, _) -> erlang:error({not_implemented, system_profile, 2}).

system_time() -> clause_builtin_placeholder.

system_time(_) -> clause_builtin_placeholder.

term_to_binary(_) -> clause_builtin_placeholder.

term_to_binary(_, _) -> clause_builtin_placeholder.

term_to_iovec(_) -> clause_builtin_placeholder.

term_to_iovec(_, _) -> clause_builtin_placeholder.

throw(_) -> clause_builtin_placeholder.

time() -> clause_builtin_placeholder.

time_offset() -> clause_builtin_placeholder.

time_offset(_) -> clause_builtin_placeholder.

timestamp() -> clause_builtin_placeholder.

tl(_) -> clause_builtin_placeholder.

trace(_, _, _) -> erlang:error({not_implemented, trace, 3}).

trace_delivered(_) -> erlang:error({not_implemented, trace_delivered, 1}).

trace_info(_, _) -> erlang:error({not_implemented, trace_info, 2}).

trace_pattern(_, _) -> erlang:error({not_implemented, trace_pattern, 2}).

trace_pattern(_, _, _) -> erlang:error({not_implemented, trace_pattern, 3}).

trunc(_) -> clause_builtin_placeholder.

tuple_size(_) -> clause_builtin_placeholder.

tuple_to_list(_) -> clause_builtin_placeholder.

unalias(_) -> clause_builtin_placeholder.

unique_integer() -> clause_builtin_placeholder.

unique_integer(_) -> clause_builtin_placeholder.

universaltime() -> clause_builtin_placeholder.

universaltime_to_localtime(_) -> clause_builtin_placeholder.

universaltime_to_posixtime(_) -> clause_builtin_placeholder.

unlink(_) -> clause_builtin_placeholder.

unregister(_) -> clause_builtin_placeholder.

whereis(_) -> clause_builtin_placeholder.

'xor'(_, _) -> clause_builtin_placeholder.

yield() -> clause_builtin_placeholder.
