#pragma once
#include "../memory/heap_object.hpp"
#include "support.hpp"
#include <clause/abi/v1.hpp>
#include <cstddef>
#include <memory>
#include <stdexcept>

// Helpers of builtins that run in bounded portions (docs/builtins.md#portions): a portion does at most the process
// stack's budget of work, then traps to a continuation frame with its state in registers and a TrapState, which
// collections between portions rewrite like other roots.
namespace clause::runtime::builtins {
// Run a raw builtin body, recording the BuiltinFailure it throws.
template <Word (*Body)(ProcessContext &, Arguments)> Word guarded(ProcessContext &context, Arguments arguments) {
    try {
        return Body(context, arguments);
    } catch (const BuiltinFailure &failure) {
        return fail(context, failure);
    }
}

// A word of this process kept in a register or state between portions, as a term.
inline Term term_of(ProcessContext &context, Word word) { return need(Term::from_word(word, context)); }

// A count kept in a register between portions, and its value.
Word count_word(ProcessContext &context, std::size_t count);
std::size_t count_of(ProcessContext &context, Word word);

// Walk at most `budget` cells of `list`, passing each head word to `visit`; `list` stops at the first cell not
// walked. Returns the cells walked. The list is validated once and its cells are read as words, with no Term per
// cell (word-level walks).
template <typename Visit> std::size_t walk(Term &list, std::size_t budget, Visit visit) {
    if (!list.is_cons()) {
        return 0;
    }
    if (const auto valid = detail::TermAccess::validate(list); !valid) {
        throw BuiltinFailure{.term = valid.error()};
    }
    auto &storage = *detail::TermAccess::storage(list);
    auto rest = list.word();
    std::size_t walked = 0;
    for (; walked < budget && (rest & abi::v1::primary_mask) == abi::v1::list_tag; ++walked) {
        const auto cell = detail::TermAccess::cons_cell(storage, rest);
        if (cell.empty()) {
            throw BuiltinFailure{.term = TermError::wrong_owner};
        }
        visit(cell[0]);
        rest = cell[1];
    }
    list = need(detail::TermAccess::admit(rest, storage));
    return walked;
}

// The running builtin's state of type `State`; a continuation without one is an internal error.
template <typename State> State &trap_state(ProcessContext &context) {
    auto *state = context.stack().trap_state<State>();
    if (!state) {
        throw std::logic_error("builtin continuation without its state");
    }
    return *state;
}

// The elements of a list being built, as words from `base` on in input order.
class ListState : public TrapState {
  public:
    // Words below `base` belong to the builtin, not to the list.
    std::size_t base = 0;
};

// Build the list of the ListState's elements onto `tail` in portions, from the last element; the state is released
// with the result.
Word build_list(ProcessContext &context, const Term &tail);
} // namespace clause::runtime::builtins
