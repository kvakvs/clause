#pragma once
#include "v1.hpp"
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace clause::abi::v1 {
struct ModuleDescriptor;
struct FrameDescriptor;

// One function value a module creates (docs/funs.md), immutable beside its module descriptor: a local fun F/A or an
// anonymous fun of this module, or an external fun M:F/A. Names are atom slots of the module that registers it.
struct FunDescriptor {
    // The module creating the value; its atom slots spell the names below.
    const ModuleDescriptor *module;
    // Atom slots of the module and function the value names: this module and the function (or lambda) for a local
    // fun, M and F for an external fun.
    std::size_t module_atom;
    std::size_t function_atom;
    // The fun's Erlang arity; a local fun's code takes its captured values as further arguments.
    std::size_t arity;
    // A local fun's index among the module's funs (printed in #Fun<M.Index.Uniq>); 0 for an external fun.
    std::size_t index;
    // Nonzero for an external fun M:F/A.
    std::size_t external;
    // The code a call enters: a local fun's function, an external fun's exported function within the program, or
    // null for an external fun nothing in the program exports (calling it raises undef) and for a fun only
    // unreachable code creates (plan step 62C).
    const FrameDescriptor *frame;
};

static_assert(std::is_standard_layout_v<FunDescriptor>);
static_assert(sizeof(FunDescriptor) == 7 * sizeof(TermWord));
} // namespace clause::abi::v1

// Build a fun of `descriptor` (a FunDescriptor of a registered module) capturing `count` rooted values in
// `output`; returns a Status byte.
std::uint8_t CLAUSE_make_fun_v1(void *context, const void *descriptor, const clause::abi::v1::TermWord *captures,
                                std::size_t count, clause::abi::v1::TermWord *output) noexcept;
// Prepare calling `fun` with the `arity` rooted arguments in `arguments` (the process registers): append its
// captured values after them and return the FrameDescriptor to enter. A non-function raises {badfun, Fun}, another
// arity {badarity, {Fun, Args}} and an external fun outside the program undef; the result is then null.
const void *CLAUSE_apply_v1(void *context, clause::abi::v1::TermWord fun, std::size_t arity,
                            clause::abi::v1::TermWord *arguments) noexcept;
// Prepare calling Module:Function with `arity` arguments already in `registers` (M:F(Args) with runtime operands):
// return the FrameDescriptor of the function a module of the program exports. A call into an interpreted module
// enters error_handler:undefined_function/3 with its three arguments written to the registers. A non-atom module or
// function raises badarg, anything else undef; the result is then null.
const void *CLAUSE_call_v1(void *context, clause::abi::v1::TermWord module, clause::abi::v1::TermWord function,
                           std::size_t arity, clause::abi::v1::TermWord *registers) noexcept;
// erlang:apply(Fun, Args): copy the proper list `list` into `registers` and prepare the call as CLAUSE_apply_v1
// does. An improper list raises badarg, before the fun is checked; badarity carries {Fun, Args}.
const void *CLAUSE_apply_list_v1(void *context, clause::abi::v1::TermWord fun, clause::abi::v1::TermWord list,
                                 clause::abi::v1::TermWord *registers) noexcept;
// erlang:apply(Module, Function, Args): copy the proper list `list` into `registers` and prepare the call as
// CLAUSE_call_v1 does; an improper list raises badarg.
const void *CLAUSE_call_list_v1(void *context, clause::abi::v1::TermWord module, clause::abi::v1::TermWord function,
                                clause::abi::v1::TermWord list, clause::abi::v1::TermWord *registers) noexcept;
// Build the external fun Module:Function/Arity from runtime operands (fun M:F/A with variables) in `output`. A
// non-atom module or function, or an arity outside 0..255, raises badarg; returns a Status byte.
std::uint8_t CLAUSE_make_external_fun_v1(void *context, clause::abi::v1::TermWord module,
                                         clause::abi::v1::TermWord function, clause::abi::v1::TermWord arity,
                                         clause::abi::v1::TermWord *output) noexcept;
