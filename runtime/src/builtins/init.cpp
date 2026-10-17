#include "text.hpp"
#include "typed.hpp"
#include <algorithm>
#include <array>
#include <terms.hpp>

// The init module's command-line answers (plan step 41L, docs/builtins.md#init-module): an executable has no
// emulator flags, so its plain arguments are the arguments after the runtime options and its only flag is `home`,
// set at startup as OTP's erlexec sets it. script_name/0 is the executable's absolute path.
namespace clause::runtime::builtins {
namespace {
// The atom `name`.
Term atom(ProcessContext &context, std::string_view name) { return need(TermFactory(context).atom(name)); }

// UTF-8 text as an Erlang string.
Term string_term(ProcessContext &context, std::string_view text) {
    std::vector<Term> characters;
    for (const auto code : code_points(text)) {
        characters.push_back(need(Term::from_word(need(encode_integer(code)))));
    }
    return need(TermFactory(context).list(characters));
}

// A list of the strings of `texts`.
Term string_list(ProcessContext &context, std::span<const std::string> texts) {
    std::vector<Term> strings;
    for (const auto &text : texts) {
        strings.push_back(string_term(context, text));
    }
    return need(TermFactory(context).list(strings));
}

// The values of the `home` flag, [Home], or none when startup found no home directory.
std::optional<Term> home_values(ProcessContext &context) {
    const auto &home = context.program_arguments().home;
    return home ? std::optional<Term>(string_list(context, std::span(&*home, 1))) : std::nullopt;
}

// get_plain_arguments(): the arguments after the runtime options, as the arity-1 entry receives them.
TermResult<Term> get_plain_arguments(ProcessContext &context) {
    return string_list(context, context.program_arguments().plain);
}

// get_arguments(): [{home, [Home]}], or [] without a home directory.
TermResult<Term> get_arguments(ProcessContext &context) {
    std::vector<Term> flags;
    if (const auto values = home_values(context)) {
        const std::array flag{atom(context, "home"), *values};
        flags.push_back(need(TermFactory(context).tuple(flag)));
    }
    return TermFactory(context).list(flags);
}

// get_argument(Flag): {ok, [[Home]]} for home (one value list per occurrence), error for every other term.
TermResult<Term> get_argument(ProcessContext &context, const Term &flag) {
    const auto values =
        flag.is_atom() && flag.atom_spelling_unchecked() == "home" ? home_values(context) : std::nullopt;
    if (!values) {
        return atom(context, "error");
    }
    const std::array occurrences{*values};
    const std::array result{atom(context, "ok"), need(TermFactory(context).list(occurrences))};
    return TermFactory(context).tuple(result);
}

// script_name(): the executable's absolute path with forward slashes, as filename:absname/2 writes OTP's.
TermResult<Term> script_name(ProcessContext &context) {
    auto path = context.program_arguments().executable;
    std::ranges::replace(path, '\\', '/');
    return string_term(context, path);
}

constexpr std::array INIT_BUILTINS{
    typed_entry<get_plain_arguments>("init", "get_plain_arguments"),
    typed_entry<get_arguments>("init", "get_arguments"),
    typed_entry<get_argument>("init", "get_argument"),
    typed_entry<script_name>("init", "script_name"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> init_builtins() noexcept { return builtins::INIT_BUILTINS; }
} // namespace clause::runtime
