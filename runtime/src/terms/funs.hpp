#pragma once
#include "term_text.hpp"
#include <clause/abi/calls.hpp>
#include <clause/runtime/code_server.hpp>
#include <clause/runtime/terms.hpp>
#include <span>

// Fun cells (docs/funs.md): a definition followed by the captured values of a local fun.
namespace clause::runtime::detail {
// A decoded fun, borrowing the cell's words while its Term is current.
struct FunView {
    // The definition the fun was created from; it lives as long as the runtime.
    const FunDefinition *definition;
    // Captured values in capture order; empty for an external fun.
    std::span<const Word> captures;
};

// The code a call of `fun` with `arity` arguments enters, read straight from the fun's cell, its captured values
// placed after the arguments; null when `fun` is no fun of that arity with code, for the checked path (cheap fun
// calls).
const void *word_apply(ProcessContext &context, Word fun, Word *arguments, std::size_t arity) noexcept;
// Decode a fun; wrong_type for any other term.
TermResult<FunView> fun_view(const Term &value) noexcept;
// Print `fun M:F/A` for an external fun and #Fun<M.Index.Uniq> for a local one.
TermResult<void> print_fun(const Term &value, TermStyle style, TextOutput &out);
// Whether a call service may run: an active invocation without a pending failure.
bool service_ready(ProcessContext &context);
// Record the Erlang error a failed call raises, with its payload if it has one.
void raise_call_error(ProcessContext &context, abi::v1::ErrorReason reason, std::optional<Term> value = std::nullopt);
// Check a called fun and append its captured values after the `arity` arguments; null when the call raised or failed.
const void *prepare_fun_call(ProcessContext &context, const Term &fun, std::size_t arity, Word *arguments);
// apply(Fun, List) and apply(M, F, List) (dynamic_calls.cpp): unpack List into the registers and return the frame the
// call enters, or null after recording the error or failure.
const void *apply_list_service(ProcessContext &context, Word fun, Word list, Word *registers) noexcept;
const void *call_list_service(ProcessContext &context, Word module, Word function, Word list, Word *registers) noexcept;
} // namespace clause::runtime::detail
