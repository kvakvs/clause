#include "../scheduler/executor.hpp"
#include "typed.hpp"
#include <array>

// The bundled epmd's builtin (plan 30 step DIST3b, docs/distribution.md#epmd): erl_epmd asks it when a registration
// finds no epmd listening.
namespace clause::runtime::builtins {
namespace {
// clause_dist:start_epmd(): under --epmd bundled, ok once the bundled epmd listens or another epmd won its port, error
// when it cannot listen; none under --epmd none.
Term start_epmd(ProcessContext &context) {
    auto &executor = detail::Executor::of(context);
    if (executor.epmd_mode() == EpmdMode::none) {
        return atom(context, "none"_atom);
    }
    return atom(context, executor.bundled_epmd().start() ? "ok"_atom : "error"_atom);
}

constexpr std::array EPMD_BUILTINS{
    typed_entry<start_epmd>("clause_dist", "start_epmd"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> epmd_builtins() noexcept { return builtins::EPMD_BUILTINS; }
} // namespace clause::runtime
