#pragma once
#include "status.hpp"
#include "v1.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace clause::abi::v1 {
// Dispatch synchronously through a live context; names/words borrow valid arrays (null args only at arity zero).
// Unknown signatures return unknown_builtin; recognized deferred BIFs report once and return not_implemented.
// Return explicit status and write result only on OK; errors never become words or escape as C++ exceptions.
Status dispatch_builtin(Context *context, const char *module, std::size_t module_size, const char *function,
                        std::size_t function_size, const TermWord *arguments, std::size_t arity,
                        TermWord *result) noexcept;

// One production builtin generated code reaches through the bridge (docs/builtins.md).
struct BuiltinName {
    std::string_view module;
    std::string_view function;
    std::size_t arity;
};

// Every bridge builtin. An entry's index is the `builtin` argument of CLAUSE_builtin_frame_v1, so entries are only
// ever appended. Group comments name the runtime translation unit implementing the group; module erlang's live in
// runtime/src/builtins/erlang/, one per family. The runtime registers an implementation for each; the compiler admits
// calls and funs of them.
inline constexpr std::array bridge_builtins{
    // Type tests (runtime/src/builtins/erlang/core.cpp).
    BuiltinName{"erlang", "is_atom", 1},
    BuiltinName{"erlang", "is_binary", 1},
    BuiltinName{"erlang", "is_bitstring", 1},
    BuiltinName{"erlang", "is_boolean", 1},
    BuiltinName{"erlang", "is_float", 1},
    BuiltinName{"erlang", "is_function", 1},
    BuiltinName{"erlang", "is_function", 2},
    BuiltinName{"erlang", "is_integer", 1},
    BuiltinName{"erlang", "is_list", 1},
    BuiltinName{"erlang", "is_map", 1},
    BuiltinName{"erlang", "is_number", 1},
    BuiltinName{"erlang", "is_pid", 1},
    BuiltinName{"erlang", "is_port", 1},
    BuiltinName{"erlang", "is_reference", 1},
    BuiltinName{"erlang", "is_tuple", 1},
    // Term queries and numeric conversions also legal in guards (erlang/core.cpp; length/1: erlang/lists.cpp).
    BuiltinName{"erlang", "abs", 1},
    BuiltinName{"erlang", "bit_size", 1},
    BuiltinName{"erlang", "byte_size", 1},
    BuiltinName{"erlang", "ceil", 1},
    BuiltinName{"erlang", "element", 2},
    BuiltinName{"erlang", "float", 1},
    BuiltinName{"erlang", "floor", 1},
    BuiltinName{"erlang", "hd", 1},
    BuiltinName{"erlang", "length", 1},
    BuiltinName{"erlang", "map_get", 2},
    BuiltinName{"erlang", "map_size", 1},
    BuiltinName{"erlang", "is_map_key", 2},
    BuiltinName{"erlang", "max", 2},
    BuiltinName{"erlang", "min", 2},
    BuiltinName{"erlang", "round", 1},
    BuiltinName{"erlang", "size", 1},
    BuiltinName{"erlang", "tl", 1},
    BuiltinName{"erlang", "trunc", 1},
    BuiltinName{"erlang", "tuple_size", 1},
    BuiltinName{"erlang", "binary_part", 2},
    BuiltinName{"erlang", "binary_part", 3},
    // Operators as functions (fun erlang:'+'/2, apply(erlang, '<', Args); erlang/core.cpp, '++'/'--':
    // erlang/lists.cpp).
    BuiltinName{"erlang", "=:=", 2},
    BuiltinName{"erlang", "=/=", 2},
    BuiltinName{"erlang", "==", 2},
    BuiltinName{"erlang", "/=", 2},
    BuiltinName{"erlang", "<", 2},
    BuiltinName{"erlang", "=<", 2},
    BuiltinName{"erlang", ">", 2},
    BuiltinName{"erlang", ">=", 2},
    BuiltinName{"erlang", "not", 1},
    BuiltinName{"erlang", "and", 2},
    BuiltinName{"erlang", "or", 2},
    BuiltinName{"erlang", "xor", 2},
    BuiltinName{"erlang", "+", 2},
    BuiltinName{"erlang", "-", 2},
    BuiltinName{"erlang", "*", 2},
    BuiltinName{"erlang", "/", 2},
    BuiltinName{"erlang", "div", 2},
    BuiltinName{"erlang", "rem", 2},
    BuiltinName{"erlang", "band", 2},
    BuiltinName{"erlang", "bor", 2},
    BuiltinName{"erlang", "bxor", 2},
    BuiltinName{"erlang", "bsl", 2},
    BuiltinName{"erlang", "bsr", 2},
    BuiltinName{"erlang", "+", 1},
    BuiltinName{"erlang", "-", 1},
    BuiltinName{"erlang", "bnot", 1},
    // Body-only builtins: output, halting, raising and code introspection (erlang/core.cpp).
    BuiltinName{"erlang", "display", 1},
    BuiltinName{"erlang", "halt", 0},
    BuiltinName{"erlang", "halt", 1},
    BuiltinName{"erlang", "error", 1},
    BuiltinName{"erlang", "error", 2},
    BuiltinName{"erlang", "error", 3},
    BuiltinName{"erlang", "exit", 1},
    BuiltinName{"erlang", "throw", 1},
    BuiltinName{"erlang", "raise", 3},
    BuiltinName{"erlang", "function_exported", 3},
    // Term access (plan step 37; erlang/term_access.cpp).
    BuiltinName{"erlang", "setelement", 3},
    BuiltinName{"erlang", "make_tuple", 2},
    BuiltinName{"erlang", "make_tuple", 3},
    BuiltinName{"erlang", "tuple_to_list", 1},
    BuiltinName{"erlang", "list_to_tuple", 1},
    BuiltinName{"erlang", "++", 2},
    BuiltinName{"erlang", "--", 2},
    // Conversions (plan step 38; erlang/conversions.cpp).
    BuiltinName{"erlang", "atom_to_list", 1},
    BuiltinName{"erlang", "list_to_atom", 1},
    BuiltinName{"erlang", "integer_to_list", 1},
    BuiltinName{"erlang", "integer_to_list", 2},
    BuiltinName{"erlang", "list_to_integer", 1},
    BuiltinName{"erlang", "list_to_integer", 2},
    BuiltinName{"erlang", "float_to_list", 1},
    BuiltinName{"erlang", "float_to_list", 2},
    BuiltinName{"erlang", "binary_to_list", 1},
    BuiltinName{"erlang", "list_to_binary", 1},
    BuiltinName{"erlang", "iolist_to_binary", 1},
    // Console output (plan step 40; runtime/src/builtins/io.cpp).
    BuiltinName{"io", "format", 1},
    BuiltinName{"io", "format", 2},
    BuiltinName{"io", "put_chars", 1},
    // Process identities (plan step 42; erlang/processes.cpp, the *_to_list conversions: erlang/conversions.cpp).
    BuiltinName{"erlang", "self", 0},
    BuiltinName{"erlang", "make_ref", 0},
    BuiltinName{"erlang", "pid_to_list", 1},
    BuiltinName{"erlang", "ref_to_list", 1},
    // Processes (plan step 43; erlang/processes.cpp, as are the groups down to registered names).
    BuiltinName{"erlang", "spawn", 1},
    BuiltinName{"erlang", "spawn", 3},
    BuiltinName{"erlang", "is_process_alive", 1},
    // Messages (plan step 45): the ! operator and erlang:send/2.
    BuiltinName{"erlang", "!", 2},
    BuiltinName{"erlang", "send", 2},
    // Links and exit signals (plan step 48).
    BuiltinName{"erlang", "spawn_link", 1},
    BuiltinName{"erlang", "spawn_link", 3},
    BuiltinName{"erlang", "link", 1},
    BuiltinName{"erlang", "unlink", 1},
    BuiltinName{"erlang", "exit", 2},
    BuiltinName{"erlang", "exit_signal", 2},
    BuiltinName{"erlang", "process_flag", 2},
    // Monitors (plan step 49).
    BuiltinName{"erlang", "spawn_monitor", 1},
    BuiltinName{"erlang", "spawn_monitor", 3},
    BuiltinName{"erlang", "monitor", 2},
    BuiltinName{"erlang", "demonitor", 1},
    BuiltinName{"erlang", "demonitor", 2},
    // Registered names (plan step 50).
    BuiltinName{"erlang", "register", 2},
    BuiltinName{"erlang", "unregister", 1},
    BuiltinName{"erlang", "whereis", 1},
    BuiltinName{"erlang", "registered", 0},
    // Identity-dependent guard BIFs (plan step 52; erlang/core.cpp); self/0 is above.
    BuiltinName{"erlang", "is_record", 1},
    BuiltinName{"erlang", "node", 0},
    BuiltinName{"erlang", "node", 1},
    // Ports (plan step 57B; erlang/ports.cpp).
    BuiltinName{"erlang", "open_port", 2},
    BuiltinName{"erlang", "port_close", 1},
    BuiltinName{"erlang", "port_command", 2},
    BuiltinName{"erlang", "port_command", 3},
    BuiltinName{"erlang", "port_connect", 2},
    BuiltinName{"erlang", "port_control", 3},
    BuiltinName{"erlang", "port_call", 2},
    BuiltinName{"erlang", "port_call", 3},
    BuiltinName{"erlang", "port_info", 1},
    BuiltinName{"erlang", "port_info", 2},
    BuiltinName{"erlang", "port_to_list", 1},
    BuiltinName{"erlang", "list_to_port", 1},
    BuiltinName{"erlang", "ports", 0},
    // Operating system queries (plan step 57D; runtime/src/builtins/os.cpp).
    BuiltinName{"os", "type", 0},
    BuiltinName{"os", "getenv", 1},
    // Process dictionary (plan step 41C; erlang/dictionary.cpp).
    BuiltinName{"erlang", "put", 2},
    BuiltinName{"erlang", "get", 1},
    BuiltinName{"erlang", "get", 0},
    BuiltinName{"erlang", "get_keys", 0},
    BuiltinName{"erlang", "get_keys", 1},
    BuiltinName{"erlang", "erase", 1},
    BuiltinName{"erlang", "erase", 0},
    // Binary, atom and number conversions (plan step 41D; erlang/binary_conversions.cpp).
    BuiltinName{"erlang", "atom_to_binary", 1},
    BuiltinName{"erlang", "atom_to_binary", 2},
    BuiltinName{"erlang", "binary_to_atom", 1},
    BuiltinName{"erlang", "binary_to_atom", 2},
    BuiltinName{"erlang", "binary_to_existing_atom", 1},
    BuiltinName{"erlang", "binary_to_existing_atom", 2},
    BuiltinName{"erlang", "list_to_existing_atom", 1},
    BuiltinName{"erlang", "integer_to_binary", 1},
    BuiltinName{"erlang", "integer_to_binary", 2},
    BuiltinName{"erlang", "binary_to_integer", 1},
    BuiltinName{"erlang", "binary_to_integer", 2},
    BuiltinName{"erlang", "float_to_binary", 1},
    BuiltinName{"erlang", "float_to_binary", 2},
    BuiltinName{"erlang", "binary_to_float", 1},
    BuiltinName{"erlang", "list_to_float", 1},
    BuiltinName{"erlang", "binary_to_list", 3},
    BuiltinName{"erlang", "bitstring_to_list", 1},
    BuiltinName{"erlang", "list_to_bitstring", 1},
    BuiltinName{"erlang", "split_binary", 2},
    BuiltinName{"erlang", "iolist_size", 1},
    BuiltinName{"erlang", "list_to_pid", 1},
    BuiltinName{"erlang", "list_to_ref", 1},
    // External term format (plan step 41E; erlang/term_encoding.cpp, erlang/term_decoding.cpp).
    BuiltinName{"erlang", "term_to_binary", 1},
    BuiltinName{"erlang", "term_to_binary", 2},
    BuiltinName{"erlang", "term_to_iovec", 1},
    BuiltinName{"erlang", "term_to_iovec", 2},
    BuiltinName{"erlang", "external_size", 1},
    BuiltinName{"erlang", "external_size", 2},
    BuiltinName{"erlang", "binary_to_term", 1},
    BuiltinName{"erlang", "binary_to_term", 2},
    // Time and unique values (plan step 41F; erlang/time.cpp).
    BuiltinName{"erlang", "monotonic_time", 0},
    BuiltinName{"erlang", "monotonic_time", 1},
    BuiltinName{"erlang", "system_time", 0},
    BuiltinName{"erlang", "system_time", 1},
    BuiltinName{"erlang", "time_offset", 0},
    BuiltinName{"erlang", "time_offset", 1},
    BuiltinName{"erlang", "convert_time_unit", 3},
    BuiltinName{"erlang", "timestamp", 0},
    BuiltinName{"erlang", "now", 0},
    BuiltinName{"erlang", "unique_integer", 0},
    BuiltinName{"erlang", "unique_integer", 1},
    BuiltinName{"erlang", "universaltime", 0},
    BuiltinName{"erlang", "localtime", 0},
    BuiltinName{"erlang", "date", 0},
    BuiltinName{"erlang", "time", 0},
    BuiltinName{"erlang", "universaltime_to_localtime", 1},
    BuiltinName{"erlang", "localtime_to_universaltime", 1},
    BuiltinName{"erlang", "localtime_to_universaltime", 2},
    BuiltinName{"erlang", "posixtime_to_universaltime", 1},
    BuiltinName{"erlang", "universaltime_to_posixtime", 1},
    // Timers (plan step 41F; erlang/timers.cpp, scheduler/timers.cpp).
    BuiltinName{"erlang", "start_timer", 3},
    BuiltinName{"erlang", "start_timer", 4},
    BuiltinName{"erlang", "send_after", 3},
    BuiltinName{"erlang", "send_after", 4},
    BuiltinName{"erlang", "cancel_timer", 1},
    BuiltinName{"erlang", "cancel_timer", 2},
    BuiltinName{"erlang", "read_timer", 1},
    BuiltinName{"erlang", "read_timer", 2},
    // Process and system introspection (plan step 41G1; erlang/introspection.cpp, erlang/system.cpp).
    BuiltinName{"erlang", "process_info", 1},
    BuiltinName{"erlang", "process_info", 2},
    BuiltinName{"erlang", "processes", 0},
    BuiltinName{"erlang", "group_leader", 0},
    BuiltinName{"erlang", "group_leader", 2},
    BuiltinName{"erlang", "garbage_collect", 0},
    BuiltinName{"erlang", "garbage_collect", 1},
    BuiltinName{"erlang", "garbage_collect", 2},
    BuiltinName{"erlang", "yield", 0},
    BuiltinName{"erlang", "statistics", 1},
    BuiltinName{"erlang", "system_info", 1},
    BuiltinName{"erlang", "memory", 0},
    BuiltinName{"erlang", "memory", 1},
    // Spawn options, spawn requests, aliases, monitor/3 and hibernation (plan step 41G2; erlang/spawning.cpp,
    // monitor/3 in erlang/processes.cpp).
    BuiltinName{"erlang", "monitor", 3},
    BuiltinName{"erlang", "spawn_opt", 2},
    BuiltinName{"erlang", "spawn_opt", 3},
    BuiltinName{"erlang", "spawn_opt", 4},
    BuiltinName{"erlang", "spawn_opt", 5},
    BuiltinName{"erlang", "spawn_request", 1},
    BuiltinName{"erlang", "spawn_request", 2},
    BuiltinName{"erlang", "spawn_request", 3},
    BuiltinName{"erlang", "spawn_request", 4},
    BuiltinName{"erlang", "spawn_request", 5},
    BuiltinName{"erlang", "spawn_request_abandon", 1},
    BuiltinName{"erlang", "alias", 0},
    BuiltinName{"erlang", "alias", 1},
    BuiltinName{"erlang", "unalias", 1},
    BuiltinName{"erlang", "hibernate", 3},
    // Fun information, hashing and MD5 (plan step 41G3; erlang/fun_info.cpp, erlang/hashing.cpp, erlang/md5.cpp).
    BuiltinName{"erlang", "fun_info", 1},
    BuiltinName{"erlang", "fun_info", 2},
    BuiltinName{"erlang", "fun_to_list", 1},
    BuiltinName{"erlang", "is_builtin", 3},
    BuiltinName{"erlang", "phash", 2},
    BuiltinName{"erlang", "phash2", 1},
    BuiltinName{"erlang", "phash2", 2},
    BuiltinName{"erlang", "md5", 1},
    BuiltinName{"erlang", "md5_init", 0},
    BuiltinName{"erlang", "md5_update", 2},
    BuiltinName{"erlang", "md5_final", 1},
    // Single-node distribution and code-loading answers (plan step 41H; erlang/node.cpp, erlang/code.cpp).
    BuiltinName{"erlang", "nodes", 0},
    BuiltinName{"erlang", "nodes", 1},
    BuiltinName{"erlang", "nodes", 2},
    BuiltinName{"erlang", "is_alive", 0},
    BuiltinName{"erlang", "monitor_node", 2},
    BuiltinName{"erlang", "monitor_node", 3},
    BuiltinName{"erlang", "disconnect_node", 1},
    BuiltinName{"erlang", "module_loaded", 1},
    BuiltinName{"erlang", "loaded", 0},
    BuiltinName{"erlang", "pre_loaded", 0},
    BuiltinName{"erlang", "delete_module", 1},
    BuiltinName{"erlang", "prepare_loading", 2},
    BuiltinName{"erlang", "finish_loading", 1},
    BuiltinName{"erlang", "get_module_info", 1},
    BuiltinName{"erlang", "get_module_info", 2},
    // The lists module's data functions (plan step 41T; runtime/src/builtins/lists/).
    BuiltinName{"lists", "sublist", 2},
    BuiltinName{"lists", "sublist", 3},
    BuiltinName{"lists", "droplast", 1},
    BuiltinName{"lists", "nthtail", 2},
    BuiltinName{"lists", "last", 1},
    BuiltinName{"lists", "duplicate", 2},
    BuiltinName{"lists", "join", 2},
    BuiltinName{"lists", "flatten", 1},
    BuiltinName{"lists", "flatten", 2},
    BuiltinName{"lists", "max", 1},
    BuiltinName{"lists", "min", 1},
    BuiltinName{"lists", "usort", 1},
    BuiltinName{"lists", "umerge", 1},
    BuiltinName{"lists", "umerge", 2},
    BuiltinName{"lists", "keymember", 3},
    BuiltinName{"lists", "keysearch", 3},
    BuiltinName{"lists", "keydelete", 3},
};

// The bridge index of Module:Function/Arity, if it is a bridge builtin.
constexpr std::optional<std::size_t> find_bridge_builtin(std::string_view module, std::string_view function,
                                                         std::size_t arity) noexcept {
    for (std::size_t index = 0; index < bridge_builtins.size(); ++index) {
        const auto &name = bridge_builtins[index];
        if (name.module == module && name.function == function && name.arity == arity) {
            return index;
        }
    }
    return std::nullopt;
}
} // namespace clause::abi::v1

// The FrameDescriptor of bridge builtin `builtin` (an index into bridge_builtins), which generated code enters like
// a function with the builtin's arguments in the registers; null after recording an unknown index as a failure.
const void *CLAUSE_builtin_frame_v1(void *context, std::size_t builtin) noexcept;
