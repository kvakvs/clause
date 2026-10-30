#pragma once
#include <algorithm>
#include <array>
#include <clause/abi/builtins.hpp>
#include <cstddef>
#include <string_view>

// Builtins whose runtime implementation continues in Erlang code of the library (step 41U, plan 21 step LIB4F): a
// call of one keeps that code live and brings its module into the batch.
namespace clause::semantic {
struct BuiltinContinuation {
    // A library function the runtime implements as a builtin.
    abi::v1::BuiltinName builtin;
    // The exported function of the batch its implementation continues in.
    std::u32string_view target_module;
    std::u32string_view target_name;
    std::size_t target_arity;
};

// io output to a process device goes through clause_io:put_chars/2, an I/O protocol request (plan 21 step LIB63);
// writes of maps ordered by comparison funs sort their keys in clause_io_lib:sort_maps/6 (plan 21 step LIB4F); a
// passive node monitor asks net_kernel in clause_dist:passive_monitor/3 (plan 30 step DIST2c).
inline constexpr std::array BUILTIN_CONTINUATIONS{
    BuiltinContinuation{{"erlang", "monitor_node", 3}, U"clause_dist", U"passive_monitor", 3},
    BuiltinContinuation{{"erlang", "dmonitor_node", 3}, U"clause_dist", U"passive_monitor", 3},
    BuiltinContinuation{{"io", "format", 3}, U"clause_io", U"put_chars", 2},
    BuiltinContinuation{{"io", "fwrite", 3}, U"clause_io", U"put_chars", 2},
    BuiltinContinuation{{"io", "put_chars", 2}, U"clause_io", U"put_chars", 2},
    BuiltinContinuation{{"io", "nl", 1}, U"clause_io", U"put_chars", 2},
    BuiltinContinuation{{"io_lib", "format", 2}, U"clause_io_lib", U"sort_maps", 6},
    BuiltinContinuation{{"io_lib", "format", 3}, U"clause_io_lib", U"sort_maps", 6},
    BuiltinContinuation{{"io_lib", "fwrite", 2}, U"clause_io_lib", U"sort_maps", 6},
    BuiltinContinuation{{"io_lib", "fwrite", 3}, U"clause_io_lib", U"sort_maps", 6},
    BuiltinContinuation{{"io_lib", "bformat", 2}, U"clause_io_lib", U"sort_maps", 6},
    BuiltinContinuation{{"io_lib", "bformat", 3}, U"clause_io_lib", U"sort_maps", 6},
    BuiltinContinuation{{"io_lib", "bfwrite", 2}, U"clause_io_lib", U"sort_maps", 6},
    BuiltinContinuation{{"io_lib", "bfwrite", 3}, U"clause_io_lib", U"sort_maps", 6},
    BuiltinContinuation{{"io_lib", "build_text", 1}, U"clause_io_lib", U"sort_maps", 6},
    BuiltinContinuation{{"io_lib", "build_text", 2}, U"clause_io_lib", U"sort_maps", 6},
    BuiltinContinuation{{"io_lib", "write", 2}, U"clause_io_lib", U"sort_maps", 6},
    BuiltinContinuation{{"io_lib", "write", 5}, U"clause_io_lib", U"sort_maps", 6},
    BuiltinContinuation{{"io_lib", "bwrite", 2}, U"clause_io_lib", U"sort_maps", 6},
    BuiltinContinuation{{"io_lib", "write_bin", 5}, U"clause_io_lib", U"sort_maps", 6},
    BuiltinContinuation{{"io", "format", 1}, U"clause_io_lib", U"sort_maps", 6},
    BuiltinContinuation{{"io", "format", 2}, U"clause_io_lib", U"sort_maps", 6},
    BuiltinContinuation{{"io", "format", 3}, U"clause_io_lib", U"sort_maps", 6},
    BuiltinContinuation{{"io", "fwrite", 1}, U"clause_io_lib", U"sort_maps", 6},
    BuiltinContinuation{{"io", "fwrite", 2}, U"clause_io_lib", U"sort_maps", 6},
    BuiltinContinuation{{"io", "fwrite", 3}, U"clause_io_lib", U"sort_maps", 6},
};

// Visit the continuation of every row for builtin module:function/arity.
template <typename Visit>
void each_continuation(std::string_view module, std::string_view function, std::size_t arity, Visit visit) {
    for (const auto &item : BUILTIN_CONTINUATIONS) {
        if (item.builtin.module == module && item.builtin.function == function && item.builtin.arity == arity) {
            visit(item);
        }
    }
}
} // namespace clause::semantic
