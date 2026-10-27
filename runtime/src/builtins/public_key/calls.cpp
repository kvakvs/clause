#include "calls.hpp"
#include <algorithm>
#include <array>
#include <clause/runtime/builtin_registry.hpp>
#include <clause/runtime/code_server.hpp>

// Added for public_key's PBKDF1 loop (plan 21 step LIB80E5): calls of other registered builtins.
namespace clause::runtime::builtins::public_key {
Term call(ProcessContext &context, const std::string_view module, const std::string_view function,
          const std::initializer_list<Term> args) {
    const auto *builtin = context.code_server().builtins().find(module, function, args.size());
    std::array<Word, 4> words{};
    if (builtin == nullptr || args.size() > words.size()) {
        bad_argument();
    }
    std::ranges::transform(args, words.begin(), [](const Term &arg) { return arg.word(); });
    const auto result = call_builtin(context, *builtin, words.data());
    if (result == 0) {
        throw Recorded{};
    }
    return need(Term::from_word(result, context));
}
} // namespace clause::runtime::builtins::public_key
