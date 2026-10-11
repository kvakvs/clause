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
%% module registers; any other stub raises error(undef) when called.

'!'(_, _) -> erlang:error(undef).

'*'(_, _) -> erlang:error(undef).

'+'(_) -> erlang:error(undef).

'+'(_, _) -> erlang:error(undef).

'++'(_, _) -> erlang:error(undef).

'-'(_) -> erlang:error(undef).

'-'(_, _) -> erlang:error(undef).

'--'(_, _) -> erlang:error(undef).

'/'(_, _) -> erlang:error(undef).

'/='(_, _) -> erlang:error(undef).

'<'(_, _) -> erlang:error(undef).

'=/='(_, _) -> erlang:error(undef).

'=:='(_, _) -> erlang:error(undef).

'=<'(_, _) -> erlang:error(undef).

'=='(_, _) -> erlang:error(undef).

'>'(_, _) -> erlang:error(undef).

'>='(_, _) -> erlang:error(undef).

abs(_) -> erlang:error(undef).

adler32(_) -> erlang:error(undef).

adler32(_, _) -> erlang:error(undef).

adler32_combine(_, _, _) -> erlang:error(undef).

alias() -> erlang:error(undef).

alias(_) -> erlang:error(undef).

alloc_info(_) -> erlang:error(undef).

alloc_sizes(_) -> erlang:error(undef).

'and'(_, _) -> erlang:error(undef).

append(_, _) -> erlang:error(undef).

append_element(_, _) -> erlang:error(undef).

apply(_, _) -> erlang:error(undef).

apply(_, _, _) -> erlang:error(undef).

atom_to_binary(_) -> erlang:error(undef).

atom_to_binary(_, _) -> erlang:error(undef).

atom_to_list(_) -> erlang:error(undef).

'band'(_, _) -> erlang:error(undef).

binary_part(_, _) -> erlang:error(undef).

binary_part(_, _, _) -> erlang:error(undef).

binary_to_atom(_) -> erlang:error(undef).

binary_to_atom(_, _) -> erlang:error(undef).

binary_to_existing_atom(_) -> erlang:error(undef).

binary_to_existing_atom(_, _) -> erlang:error(undef).

binary_to_float(_) -> erlang:error(undef).

binary_to_integer(_) -> erlang:error(undef).

binary_to_integer(_, _) -> erlang:error(undef).

binary_to_list(_) -> erlang:error(undef).

binary_to_list(_, _, _) -> erlang:error(undef).

binary_to_term(_) -> erlang:error(undef).

binary_to_term(_, _) -> erlang:error(undef).

bit_size(_) -> erlang:error(undef).

bitstring_to_list(_) -> erlang:error(undef).

'bnot'(_) -> erlang:error(undef).

'bor'(_, _) -> erlang:error(undef).

'bsl'(_, _) -> erlang:error(undef).

'bsr'(_, _) -> erlang:error(undef).

bump_reductions(_) -> erlang:error(undef).

'bxor'(_, _) -> erlang:error(undef).

byte_size(_) -> erlang:error(undef).

call_on_load_function(_) -> erlang:error(undef).

cancel_timer(_) -> erlang:error(undef).

cancel_timer(_, _) -> erlang:error(undef).

ceil(_) -> erlang:error(undef).

check_old_code(_) -> erlang:error(undef).

check_process_code(_, _) -> erlang:error(undef).

check_process_code(_, _, _) -> erlang:error(undef).

convert_time_unit(_, _, _) -> erlang:error(undef).

crc32(_) -> erlang:error(undef).

crc32(_, _) -> erlang:error(undef).

crc32_combine(_, _, _) -> erlang:error(undef).

date() -> erlang:error(undef).

decode_packet(_, _, _) -> erlang:error(undef).

delay_trap(_, _) -> erlang:error(undef).

delete_element(_, _) -> erlang:error(undef).

delete_module(_) -> erlang:error(undef).

demonitor(_) -> erlang:error(undef).

demonitor(_, _) -> erlang:error(undef).

disconnect_node(_) -> erlang:error(undef).

display(_) -> erlang:error(undef).

display_string(_) -> erlang:error(undef).

display_string(_, _) -> erlang:error(undef).

dist_ctrl_get_data(_) -> erlang:error(undef).

dist_ctrl_get_data_notification(_) -> erlang:error(undef).

dist_ctrl_get_opt(_, _) -> erlang:error(undef).

dist_ctrl_input_handler(_, _) -> erlang:error(undef).

dist_ctrl_put_data(_, _) -> erlang:error(undef).

dist_ctrl_set_opt(_, _, _) -> erlang:error(undef).

dist_get_stat(_) -> erlang:error(undef).

'div'(_, _) -> erlang:error(undef).

dmonitor_node(_, _, _) -> erlang:error(undef).

dt_append_vm_tag_data(_) -> erlang:error(undef).

dt_get_tag() -> erlang:error(undef).

dt_get_tag_data() -> erlang:error(undef).

dt_prepend_vm_tag_data(_) -> erlang:error(undef).

dt_put_tag(_) -> erlang:error(undef).

dt_restore_tag(_) -> erlang:error(undef).

dt_spread_tag(_) -> erlang:error(undef).

element(_, _) -> erlang:error(undef).

erase() -> erlang:error(undef).

erase(_) -> erlang:error(undef).

error(_) -> erlang:error(undef).

error(_, _) -> erlang:error(undef).

error(_, _, _) -> erlang:error(undef).

exit(_) -> erlang:error(undef).

exit(_, _) -> erlang:error(undef).

exit(_, _, _) -> erlang:error(undef).

exit_signal(_, _) -> erlang:error(undef).

exit_signal(_, _, _) -> erlang:error(undef).

external_size(_) -> erlang:error(undef).

external_size(_, _) -> erlang:error(undef).

finish_after_on_load(_, _) -> erlang:error(undef).

finish_loading(_) -> erlang:error(undef).

float(_) -> erlang:error(undef).

float_to_binary(_) -> erlang:error(undef).

float_to_binary(_, _) -> erlang:error(undef).

float_to_list(_) -> erlang:error(undef).

float_to_list(_, _) -> erlang:error(undef).

floor(_) -> erlang:error(undef).

format_cpu_topology(_) -> erlang:error(undef).

fun_info(_) -> erlang:error(undef).

fun_info(_, _) -> erlang:error(undef).

fun_info_mfa(_) -> erlang:error(undef).

fun_to_list(_) -> erlang:error(undef).

function_exported(_, _, _) -> erlang:error(undef).

garbage_collect() -> erlang:error(undef).

garbage_collect(_) -> erlang:error(undef).

garbage_collect(_, _) -> erlang:error(undef).

garbage_collect_message_area() -> erlang:error(undef).

gather_gc_info_result(_) -> erlang:error(undef).

get() -> erlang:error(undef).

get(_) -> erlang:error(undef).

get_cookie() -> erlang:error(undef).

get_cookie(_) -> erlang:error(undef).

get_keys() -> erlang:error(undef).

get_keys(_) -> erlang:error(undef).

get_module_info(_) -> erlang:error(undef).

get_module_info(_, _) -> erlang:error(undef).

group_leader() -> erlang:error(undef).

group_leader(_, _) -> erlang:error(undef).

halt() -> erlang:error(undef).

halt(_) -> erlang:error(undef).

halt(_, _) -> erlang:error(undef).

has_prepared_code_on_load(_) -> erlang:error(undef).

hd(_) -> erlang:error(undef).

hibernate() -> erlang:error(undef).

hibernate(_, _, _) -> erlang:error(undef).

insert_element(_, _, _) -> erlang:error(undef).

integer_to_binary(_) -> erlang:error(undef).

integer_to_binary(_, _) -> erlang:error(undef).

integer_to_list(_) -> erlang:error(undef).

integer_to_list(_, _) -> erlang:error(undef).

iolist_size(_) -> erlang:error(undef).

iolist_to_binary(_) -> erlang:error(undef).

iolist_to_iovec(_) -> erlang:error(undef).

is_alive() -> erlang:error(undef).

is_atom(_) -> erlang:error(undef).

is_binary(_) -> erlang:error(undef).

is_bitstring(_) -> erlang:error(undef).

is_boolean(_) -> erlang:error(undef).

is_builtin(_, _, _) -> erlang:error(undef).

is_float(_) -> erlang:error(undef).

is_function(_) -> erlang:error(undef).

is_function(_, _) -> erlang:error(undef).

is_integer(_) -> erlang:error(undef).

is_integer(_, _, _) -> erlang:error(undef).

is_list(_) -> erlang:error(undef).

is_map(_) -> erlang:error(undef).

is_map_key(_, _) -> erlang:error(undef).

is_number(_) -> erlang:error(undef).

is_pid(_) -> erlang:error(undef).

is_port(_) -> erlang:error(undef).

is_process_alive(_) -> erlang:error(undef).

is_record(_) -> erlang:error(undef).

is_record(_, _) -> erlang:error(undef).

is_record(_, _, _) -> erlang:error(undef).

is_reference(_) -> erlang:error(undef).

is_tuple(_) -> erlang:error(undef).

length(_) -> erlang:error(undef).

link(_) -> erlang:error(undef).

link(_, _) -> erlang:error(undef).

list_to_atom(_) -> erlang:error(undef).

list_to_binary(_) -> erlang:error(undef).

list_to_bitstring(_) -> erlang:error(undef).

list_to_existing_atom(_) -> erlang:error(undef).

list_to_float(_) -> erlang:error(undef).

list_to_integer(_) -> erlang:error(undef).

list_to_integer(_, _) -> erlang:error(undef).

list_to_pid(_) -> erlang:error(undef).

list_to_port(_) -> erlang:error(undef).

list_to_ref(_) -> erlang:error(undef).

list_to_tuple(_) -> erlang:error(undef).

load_module(_, _) -> erlang:error(undef).

load_nif(_, _) -> erlang:error(undef).

loaded() -> erlang:error(undef).

localtime() -> erlang:error(undef).

localtime_to_universaltime(_) -> erlang:error(undef).

localtime_to_universaltime(_, _) -> erlang:error(undef).

make_fun(_, _, _) -> erlang:error(undef).

make_ref() -> erlang:error(undef).

make_tuple(_, _) -> erlang:error(undef).

make_tuple(_, _, _) -> erlang:error(undef).

map_get(_, _) -> erlang:error(undef).

map_size(_) -> erlang:error(undef).

match_spec_test(_, _, _) -> erlang:error(undef).

max(_, _) -> erlang:error(undef).

md5(_) -> erlang:error(undef).

md5_final(_) -> erlang:error(undef).

md5_init() -> erlang:error(undef).

md5_update(_, _) -> erlang:error(undef).

memory() -> erlang:error(undef).

memory(_) -> erlang:error(undef).

min(_, _) -> erlang:error(undef).

module_loaded(_) -> erlang:error(undef).

monitor(_, _) -> erlang:error(undef).

monitor(_, _, _) -> erlang:error(undef).

monitor_node(_, _) -> erlang:error(undef).

monitor_node(_, _, _) -> erlang:error(undef).

monotonic_time() -> erlang:error(undef).

monotonic_time(_) -> erlang:error(undef).

nif_error(Reason) -> erlang:error(Reason).

nif_error(Reason, Arguments) -> erlang:error(Reason, Arguments).

node() -> erlang:error(undef).

node(_) -> erlang:error(undef).

nodes() -> erlang:error(undef).

nodes(_) -> erlang:error(undef).

nodes(_, _) -> erlang:error(undef).

'not'(_) -> erlang:error(undef).

now() -> erlang:error(undef).

open_port(_, _) -> erlang:error(undef).

'or'(_, _) -> erlang:error(undef).

phash(_, _) -> erlang:error(undef).

phash2(_) -> erlang:error(undef).

phash2(_, _) -> erlang:error(undef).

pid_to_list(_) -> erlang:error(undef).

port_call(_, _) -> erlang:error(undef).

port_call(_, _, _) -> erlang:error(undef).

port_close(_) -> erlang:error(undef).

port_command(_, _) -> erlang:error(undef).

port_command(_, _, _) -> erlang:error(undef).

port_connect(_, _) -> erlang:error(undef).

port_control(_, _, _) -> erlang:error(undef).

port_get_data(_) -> erlang:error(undef).

port_info(_) -> erlang:error(undef).

port_info(_, _) -> erlang:error(undef).

port_set_data(_, _) -> erlang:error(undef).

port_to_list(_) -> erlang:error(undef).

ports() -> erlang:error(undef).

posixtime_to_universaltime(_) -> erlang:error(undef).

pre_loaded() -> erlang:error(undef).

prepare_loading(_, _) -> erlang:error(undef).

process_display(_, _) -> erlang:error(undef).

process_flag(_, _) -> erlang:error(undef).

process_flag(_, _, _) -> erlang:error(undef).

process_info(_) -> erlang:error(undef).

process_info(_, _) -> erlang:error(undef).

processes() -> erlang:error(undef).

processes_iterator() -> erlang:error(undef).

processes_next(_) -> erlang:error(undef).

purge_module(_) -> erlang:error(undef).

put(_, _) -> erlang:error(undef).

raise(_, _, _) -> erlang:error(undef).

read_timer(_) -> erlang:error(undef).

read_timer(_, _) -> erlang:error(undef).

ref_to_list(_) -> erlang:error(undef).

register(_, _) -> erlang:error(undef).

registered() -> erlang:error(undef).

'rem'(_, _) -> erlang:error(undef).

resume_process(_) -> erlang:error(undef).

round(_) -> erlang:error(undef).

self() -> erlang:error(undef).

send(_, _) -> erlang:error(undef).

send(_, _, _) -> erlang:error(undef).

send_after(_, _, _) -> erlang:error(undef).

send_after(_, _, _, _) -> erlang:error(undef).

send_nosuspend(_, _) -> erlang:error(undef).

send_nosuspend(_, _, _) -> erlang:error(undef).

seq_trace(_, _) -> erlang:error(undef).

seq_trace_info(_) -> erlang:error(undef).

seq_trace_print(_) -> erlang:error(undef).

seq_trace_print(_, _) -> erlang:error(undef).

set_cookie(_) -> erlang:error(undef).

set_cookie(_, _) -> erlang:error(undef).

set_cpu_topology(_) -> erlang:error(undef).

setelement(_, _, _) -> erlang:error(undef).

setnode(_, _) -> erlang:error(undef).

setnode(_, _, _) -> erlang:error(undef).

size(_) -> erlang:error(undef).

spawn(_) -> erlang:error(undef).

spawn(_, _) -> erlang:error(undef).

spawn(_, _, _) -> erlang:error(undef).

spawn(_, _, _, _) -> erlang:error(undef).

spawn_link(_) -> erlang:error(undef).

spawn_link(_, _) -> erlang:error(undef).

spawn_link(_, _, _) -> erlang:error(undef).

spawn_link(_, _, _, _) -> erlang:error(undef).

spawn_monitor(_) -> erlang:error(undef).

spawn_monitor(_, _) -> erlang:error(undef).

spawn_monitor(_, _, _) -> erlang:error(undef).

spawn_monitor(_, _, _, _) -> erlang:error(undef).

spawn_opt(_, _) -> erlang:error(undef).

spawn_opt(_, _, _) -> erlang:error(undef).

spawn_opt(_, _, _, _) -> erlang:error(undef).

spawn_opt(_, _, _, _, _) -> erlang:error(undef).

spawn_request(_) -> erlang:error(undef).

spawn_request(_, _) -> erlang:error(undef).

spawn_request(_, _, _) -> erlang:error(undef).

spawn_request(_, _, _, _) -> erlang:error(undef).

spawn_request(_, _, _, _, _) -> erlang:error(undef).

spawn_request_abandon(_) -> erlang:error(undef).

split_binary(_, _) -> erlang:error(undef).

start_timer(_, _, _) -> erlang:error(undef).

start_timer(_, _, _, _) -> erlang:error(undef).

statistics(_) -> erlang:error(undef).

subtract(_, _) -> erlang:error(undef).

suspend_process(_) -> erlang:error(undef).

suspend_process(_, _) -> erlang:error(undef).

system_flag(_, _) -> erlang:error(undef).

system_info(_) -> erlang:error(undef).

system_monitor() -> erlang:error(undef).

system_monitor(_) -> erlang:error(undef).

system_monitor(_, _) -> erlang:error(undef).

system_profile() -> erlang:error(undef).

system_profile(_, _) -> erlang:error(undef).

system_time() -> erlang:error(undef).

system_time(_) -> erlang:error(undef).

term_to_binary(_) -> erlang:error(undef).

term_to_binary(_, _) -> erlang:error(undef).

term_to_iovec(_) -> erlang:error(undef).

term_to_iovec(_, _) -> erlang:error(undef).

throw(_) -> erlang:error(undef).

time() -> erlang:error(undef).

time_offset() -> erlang:error(undef).

time_offset(_) -> erlang:error(undef).

timestamp() -> erlang:error(undef).

tl(_) -> erlang:error(undef).

trace(_, _, _) -> erlang:error(undef).

trace_delivered(_) -> erlang:error(undef).

trace_info(_, _) -> erlang:error(undef).

trace_pattern(_, _) -> erlang:error(undef).

trace_pattern(_, _, _) -> erlang:error(undef).

trunc(_) -> erlang:error(undef).

tuple_size(_) -> erlang:error(undef).

tuple_to_list(_) -> erlang:error(undef).

unalias(_) -> erlang:error(undef).

unique_integer() -> erlang:error(undef).

unique_integer(_) -> erlang:error(undef).

universaltime() -> erlang:error(undef).

universaltime_to_localtime(_) -> erlang:error(undef).

universaltime_to_posixtime(_) -> erlang:error(undef).

unlink(_) -> erlang:error(undef).

unregister(_) -> erlang:error(undef).

whereis(_) -> erlang:error(undef).

'xor'(_, _) -> erlang:error(undef).

yield() -> erlang:error(undef).
