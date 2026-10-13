#pragma once
#include "declarations.hpp"
#include <set>

namespace clause::semantic {
struct BindingEnvironment {
    // Only committed names are readable; conditional definitions remain explicitly unsafe.
    std::map<std::u32string, BindingId> names;
    std::set<std::u32string> unsafe;
    // Sibling expression definitions constrain matches but are not visible to sibling reads.
    std::map<std::u32string, BindingId> checks;
};

struct BindingCandidate {
    // Borrow the incoming scope without mutating it while a pattern/guard candidate is tentative.
    const BindingEnvironment &incoming;
    std::map<std::u32string, BindingId> tentative;
    // A binding error prevents publication of all definitions made by this candidate.
    bool valid = true;
    // Comprehension generator patterns shadow incoming names: every name they define is new.
    bool fresh = false;

    // Resolve incoming names before the candidate's new names; both require equality on reuse.
    [[nodiscard]] std::optional<BindingId> find(const std::u32string &name) const;
    // Publish only after successful pattern/guard selection; discarding a candidate is rollback.
    void commit(BindingEnvironment &destination) const;
    // Publish a fresh candidate, replacing the incoming names it shadows.
    void shadow(BindingEnvironment &destination) const;
};

// The default work budget of a module's binding and service walks: a floor, or so many steps per expression for
// large modules (a generated parser such as OTP's erl_parse needs about 10 per expression). Added for large
// generated modules.
inline constexpr std::size_t BINDING_WORK_FLOOR = 1'000'000;
inline constexpr std::size_t BINDING_WORK_PER_EXPRESSION = 32;

// The default walk budget for `module`'s size. Added for large generated modules.
std::size_t default_work_limit(const Module &module);

// Resolve all clause heads, read-only guards and sequential body-match scopes within a work budget; without one,
// the default budget for the module's size.
void bind_parameters(Module &module, const Reporter &out, std::optional<std::size_t> work_limit = {});
// Return projection provenance only for a validated read, never for an equality obligation.
std::optional<std::size_t> binding_argument(const Function &function, const ast::ExprId &expression);
} // namespace clause::semantic
