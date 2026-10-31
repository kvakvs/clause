#include "../epmd/launch.hpp"
#include "../scheduler/executor.hpp"
#include "typed.hpp"
#include <array>
#include <cstdio>
#include <mutex>

// The epmd builtins of registrations (plan 30 step DIST3b, docs/distribution.md#epmd): erl_epmd asks them when a
// registration finds no epmd listening.
namespace clause::runtime::builtins {
namespace {
// Says once that --epmd bundled acts as none in a program built without the bundled epmd. Added for programs
// without the bundled epmd (plan 30 step DIST3d).
void warn_not_carried() {
    static std::once_flag warned;
    std::call_once(warned, [] {
        std::fputs("clau: warning: this program was built without the bundled epmd (--no-bundled-epmd); "
                   "--epmd bundled acts as --epmd none\n",
                   stderr);
    });
}

// clause_dist:start_epmd(): under --epmd bundled, ok once the bundled epmd listens or another epmd won its port, error
// when it cannot listen; none under --epmd none or without the bundled epmd; external under --epmd external, whose
// program erl_epmd finds and starts (DIST3d).
Term start_epmd(ProcessContext &context) {
    auto &executor = detail::Executor::of(context);
    if (executor.epmd_mode() == EpmdMode::none) {
        return atom(context, "none"_atom);
    }
    if (executor.epmd_mode() == EpmdMode::external) {
        return atom(context, "external"_atom);
    }
    auto *bundled = executor.bundled_epmd();
    if (bundled == nullptr) {
        warn_not_carried();
        return atom(context, "none"_atom);
    }
    return atom(context, bundled->start() ? "ok"_atom : "error"_atom);
}

// clause_dist:start_daemon(Program): start the epmd program at the UTF-8 path with -daemon; ok when it started, else
// error. Added for --epmd external (plan 30 step DIST3d).
Term start_daemon(ProcessContext &context, const BinaryArgument &program) {
    const std::string path(reinterpret_cast<const char *>(program.bytes.data()), program.bytes.size());
    return atom(context, epmd::start_daemon(path) ? "ok"_atom : "error"_atom);
}

constexpr std::array EPMD_BUILTINS{
    typed_entry<start_epmd>("clause_dist", "start_epmd"),
    typed_entry<start_daemon>("clause_dist", "start_daemon"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> epmd_builtins() noexcept { return builtins::EPMD_BUILTINS; }
} // namespace clause::runtime
