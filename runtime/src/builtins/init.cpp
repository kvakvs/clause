#include "text.hpp"
#include "typed.hpp"
#include <algorithm>
#include <array>
#include <terms.hpp>

// The init module's command-line answers (plan step 41L, docs/builtins.md#init-module): an executable has no
// emulator flags, so its plain arguments are the arguments after the runtime options and its flags are `home`, set
// at startup as OTP's erlexec sets it, and those of the node start options (DIST4a1). script_name/0 is the
// executable's absolute path.
namespace clause::runtime::builtins {
namespace {
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

// {Flag, Values} of get_arguments/0.
Term flag_entry(ProcessContext &context, std::string_view name, const Term &values) {
    const std::array flag{need(TermFactory(context).atom(name)), values};
    return need(TermFactory(context).tuple(flag));
}

// get_arguments(): [{home, [Home]} | Flags], home left out without a home directory; the flags of the node start
// options and --env follow it (DIST4a1).
TermResult<Term> get_arguments(ProcessContext &context) {
    std::vector<Term> flags;
    if (const auto values = home_values(context)) {
        flags.push_back(flag_entry(context, "home", *values));
    }
    for (const auto &flag : context.program_arguments().flags) {
        flags.push_back(flag_entry(context, flag.name, string_list(context, flag.values)));
    }
    return TermFactory(context).list(flags);
}

// The value lists of every occurrence of `name`, in order: home's, then those of the runtime options' flags.
std::vector<Term> flag_values(ProcessContext &context, std::string_view name) {
    std::vector<Term> occurrences;
    if (const auto values = name == "home" ? home_values(context) : std::nullopt) {
        occurrences.push_back(*values);
    }
    for (const auto &flag : context.program_arguments().flags) {
        if (flag.name == name) {
            occurrences.push_back(string_list(context, flag.values));
        }
    }
    return occurrences;
}

// get_argument(Flag): {ok, [Values]} with one value list per occurrence of Flag, error for a flag never given.
TermResult<Term> get_argument(ProcessContext &context, const Term &flag) {
    const auto occurrences =
        flag.is_atom() ? flag_values(context, flag.atom_spelling_unchecked()) : std::vector<Term>{};
    if (occurrences.empty()) {
        return atom(context, "error"_atom);
    }
    const std::array result{atom(context, "ok"_atom), need(TermFactory(context).list(occurrences))};
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
