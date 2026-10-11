#pragma once
#include "callable.hpp"
#include <array>
#include <clause/abi/builtins.hpp>
#include <clause/abi/modules.hpp>
#include <cstddef>
#include <map>
#include <span>
#include <tuple>
#include <type_traits>

namespace clause::runtime {
// Native implementation of one builtin: reads exactly its arity of argument words, records an Erlang error or a
// failure in the context's checked channel and returns the result word (ignored after a failure).
using BuiltinBody = Word (*)(ProcessContext &context, std::span<const Word> arguments);

// One builtin to register. The names must outlive the registry (production tables use string literals).
struct BuiltinEntry final {
    std::string_view module;
    std::string_view function;
    std::size_t arity = 0;
    BuiltinBody body = nullptr;
};

// A registered builtin as calls enter it: a FrameDescriptor with a null body, which marks a builtin, then its
// implementation. Dynamic calls and funs enter it like generated code (docs/builtins.md).
struct BuiltinFrame final {
    abi::v1::FrameDescriptor frame{};
    BuiltinBody body = nullptr;
};

static_assert(std::is_standard_layout_v<BuiltinFrame>);
static_assert(offsetof(BuiltinFrame, frame) == 0);

// The production builtins of one runtime, by exact module, function and arity.
class BuiltinRegistry final {
  public:
    BuiltinRegistry() = default;
    BuiltinRegistry(const BuiltinRegistry &) = delete;
    BuiltinRegistry &operator=(const BuiltinRegistry &) = delete;
    ~BuiltinRegistry() = default;

    // Register every entry or none: an invalid entry or a name registered already (or twice in `entries`)
    // rejects the whole batch, as does an allocation failure.
    RegistryResult<void> add(std::span<const BuiltinEntry> entries);
    // The builtin registered under these names; null when none is.
    const BuiltinFrame *find(std::string_view module, std::string_view function, std::size_t arity) const noexcept;
    // The builtin of a bridge index (abi::v1::bridge_builtins); null when the index is unknown or unregistered.
    const BuiltinFrame *bridge(std::size_t index) const noexcept;

    // Count registered builtins.
    std::size_t size() const noexcept { return entries_.size(); }

  private:
    using Key = std::tuple<std::string_view, std::string_view, std::size_t>;
    // Record a registered frame under its bridge index, if its name is in the catalog.
    void link_bridge(const Key &key, const BuiltinFrame &frame) noexcept;
    // Map nodes keep each frame's address stable: fun definitions and generated calls hold them.
    std::map<Key, BuiltinFrame> entries_;
    // Registered frames by bridge index, for CLAUSE_builtin_v1.
    std::array<const BuiltinFrame *, abi::v1::bridge_builtins.size()> bridge_{};
};

// The builtin of a FrameDescriptor with a null body, as the registry built it.
inline const BuiltinFrame &builtin_frame(const abi::v1::FrameDescriptor &frame) noexcept {
    return *reinterpret_cast<const BuiltinFrame *>(&frame);
}

// The frame of a trapping builtin's continuation (docs/builtins.md#portions), entered with `arity` state registers;
// it has no name in the registry.
constexpr BuiltinFrame continuation_frame(BuiltinBody body, std::size_t arity) noexcept {
    return {.frame = {.module = nullptr,
                      .module_atom = 0,
                      .function_atom = 0,
                      .arity = arity,
                      .body = nullptr,
                      .slots = 0,
                      .roots = 0},
            .body = body};
}

// Run one portion of `builtin` on its arguments; returns the result, or 0 with an Erlang error or failure recorded
// in the checked channel or after the builtin trapped (ProcessStack::take_trap). Host exceptions become failures.
Word call_builtin_portion(ProcessContext &context, const BuiltinFrame &builtin, const Word *arguments) noexcept;
// Run `builtin` to completion, continuing each of its traps at once without yielding.
Word call_builtin(ProcessContext &context, const BuiltinFrame &builtin, const Word *arguments) noexcept;

// The erlang builtins of the original bridge catalog: guard BIFs, operators, display, halt, raising and
// function_exported/3.
std::span<const BuiltinEntry> erlang_builtins() noexcept;
// The tuple builtins: setelement/3, make_tuple/2,3, tuple_to_list/1, list_to_tuple/1.
std::span<const BuiltinEntry> term_access_builtins() noexcept;
// The list builtins that run in portions: length/1, '++'/2, '--'/2.
std::span<const BuiltinEntry> list_builtins() noexcept;
// The conversion builtins: atom_to_list/1, list_to_atom/1, integer_to_list/1,2, list_to_integer/1,2,
// float_to_list/1,2, binary_to_list/1, list_to_binary/1, iolist_to_binary/1, pid_to_list/1, ref_to_list/1.
std::span<const BuiltinEntry> conversion_builtins() noexcept;
// The io builtins: io:format/1,2 and io:put_chars/1 on standard output.
std::span<const BuiltinEntry> io_builtins() noexcept;
// The process builtins: self/0, make_ref/0, spawn/1,3, spawn_link/1,3, is_process_alive/1, '!'/2, send/2, link/1,
// unlink/1, exit/2, exit_signal/2, process_flag/2, spawn_monitor/1,3, monitor/2, demonitor/1,2, register/2,
// unregister/1, whereis/1 and registered/0.
std::span<const BuiltinEntry> process_builtins() noexcept;
// The port builtins (docs/ports.md): open_port/2, port_close/1, port_command/2,3, port_connect/2, port_control/3,
// port_call/2,3, port_info/1,2, port_to_list/1, list_to_port/1 and ports/0.
std::span<const BuiltinEntry> port_builtins() noexcept;
// The os builtins: os:type/0 and os:getenv/1.
std::span<const BuiltinEntry> os_builtins() noexcept;
// The binary, atom and number conversions of step 41D: atom_to_binary/1,2, binary_to_atom/1,2,
// binary_to_existing_atom/1,2, list_to_existing_atom/1, integer_to_binary/1,2, binary_to_integer/1,2,
// float_to_binary/1,2, binary_to_float/1, list_to_float/1, binary_to_list/3, bitstring_to_list/1,
// list_to_bitstring/1, split_binary/2, iolist_size/1, list_to_pid/1 and list_to_ref/1.
std::span<const BuiltinEntry> binary_conversion_builtins() noexcept;
// The external term format of step 41E: term_to_binary/1,2, term_to_iovec/1,2, external_size/1,2, and
// binary_to_term/1,2.
std::span<const BuiltinEntry> term_encoding_builtins() noexcept;
std::span<const BuiltinEntry> term_decoding_builtins() noexcept;
// The time builtins of step 41F (monotonic, system and calendar time, units, unique integers) and its timers
// (start_timer/3,4, send_after/3,4, cancel_timer/1,2, read_timer/1,2).
std::span<const BuiltinEntry> time_builtins() noexcept;
std::span<const BuiltinEntry> timer_builtins() noexcept;
// Process and system introspection of step 41G1: process_info/1,2, processes/0, group_leader/0,2,
// garbage_collect/0,1,2, yield/0, and statistics/1, system_info/1, memory/0,1.
std::span<const BuiltinEntry> introspection_builtins() noexcept;
std::span<const BuiltinEntry> system_builtins() noexcept;
// Spawn options, spawn requests, aliases and hibernation of step 41G2: spawn_opt/2..5, spawn_request/1..5,
// spawn_request_abandon/1, alias/0,1, unalias/1, hibernate/3 (monitor/3 is among process_builtins()).
std::span<const BuiltinEntry> spawning_builtins() noexcept;
// Fun information, hashing and MD5 of step 41G3: fun_info/1,2, fun_to_list/1, is_builtin/3; phash/2,
// phash2/1,2; md5/1, md5_init/0, md5_update/2, md5_final/1.
std::span<const BuiltinEntry> fun_info_builtins() noexcept;
std::span<const BuiltinEntry> hashing_builtins() noexcept;
std::span<const BuiltinEntry> md5_builtins() noexcept;
// The process dictionary builtins: put/2, get/0,1, get_keys/0,1, erase/0,1. Added for step 41C.
std::span<const BuiltinEntry> dictionary_builtins() noexcept;
// Every production builtin family runtime startup registers, together covering abi::v1::bridge_builtins.
std::span<const std::span<const BuiltinEntry>> production_builtins() noexcept;
} // namespace clause::runtime
