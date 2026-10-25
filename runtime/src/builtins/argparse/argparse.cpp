#include "argparse.hpp"
#include "../collections/collections.hpp"
#include <algorithm>
#include <array>
#include <clause/runtime/builtin_registry.hpp>
#include <clause/runtime/code_server.hpp>

// The helpers the argparse walks share and the clause_argparse builtins (plan 21 step LIB70).
namespace clause::runtime::builtins::argparse {
Term Context::chars(std::string_view text) const {
    std::vector<Term> codes;
    codes.reserve(text.size());
    for (const char code : text) {
        codes.push_back(integer(static_cast<unsigned char>(code)));
    }
    return list(codes);
}

Term Context::binary(std::string_view text) const { return terms_.binary(std::as_bytes(std::span(text))); }

Term Context::map(std::span<const std::pair<Term, Term>> entries) const {
    return need(TermFactory(process()).map(entries));
}

Term Context::with(const Term &map, const std::pair<Term, Term> &entry) const {
    auto entries = need(map.map_entries());
    entries.push_back(entry);
    return this->map(entries);
}

Term Context::call(std::string_view module, std::string_view function, std::span<const Term> args) const {
    const auto *builtin = process().code_server().builtins().find(module, function, args.size());
    if (builtin == nullptr) {
        bad_argument();
    }
    std::array<Word, 4> words{};
    if (args.size() > words.size()) {
        bad_argument();
    }
    std::ranges::transform(args, words.begin(), [](const Term &arg) { return arg.word(); });
    const auto result = call_builtin(process(), *builtin, words.data());
    if (result == 0) {
        throw Recorded{};
    }
    return need(Term::from_word(result, process()));
}

std::optional<Term> Context::call_unless_badarg(std::string_view module, std::string_view function,
                                                std::initializer_list<Term> args) const {
    try {
        return call(module, function, args);
    } catch (const Recorded &) {
        auto &calls = process().generated_calls();
        if (calls.failure() && calls.failure()->reason == abi::v1::ErrorReason::badarg) {
            calls.clear();
            return std::nullopt;
        }
        throw;
    }
}

Term Context::format(std::string_view format, std::initializer_list<Term> args) const {
    return call("io_lib", "format", {chars(format), list(args)});
}

std::int64_t Context::length(const Term &data) const {
    const auto value = string::small_of(call("string", "length", {data}));
    if (!value) {
        bad_argument();
    }
    return *value;
}

std::optional<Term> find(const Term &map, const Term &key) {
    if (!map.is_map()) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badmap, .payload = map.word()};
    }
    return need(map.map_find(key));
}

Term get(const Term &map, const Term &key, const Term &fallback) { return find(map, key).value_or(fallback); }

Term get(const Context &, const Term &map, const Term &key) {
    const auto value = find(map, key);
    if (!value) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badkey, .payload = key.word()};
    }
    return *value;
}

bool has(const Term &map, const Term &key) { return map.is_map() && need(map.map_find(key)).has_value(); }

bool has_value(const Context &context, const Term &map, AtomName key, std::string_view value) {
    if (!map.is_map()) {
        return false;
    }
    const auto found = need(map.map_find(context.atom(key)));
    return found && is_atom(*found, value);
}

bool is_atom(const Term &term, std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

bool is_tagged(const Term &term, std::string_view tag, std::size_t size) {
    return term.is_tuple() && term.tuple_size_unchecked() == size && is_atom(term.tuple_element_unchecked(0), tag);
}

bool is_any(const Term &term, std::initializer_list<std::string_view> names) {
    return term.is_atom() && std::ranges::find(names, term.atom_spelling_unchecked()) != names.end();
}

bool is_listy(const Term &term) { return term.is_list() || term.is_binary(); }

bool is_pair(const Term &term) { return term.is_tuple() && term.tuple_size_unchecked() == 2; }

bool same(const Term &left, const Term &right) { return collections::same(left, right); }

bool is_text(const Term &term, std::string_view text) {
    Term rest = term;
    for (const char code : text) {
        if (!rest.is_cons() || !string::is_char(rest.head_unchecked(), static_cast<unsigned char>(code))) {
            return false;
        }
        rest = rest.tail_unchecked();
    }
    return rest.is_nil();
}

std::vector<Term> generated(const Context &, const Term &list) {
    std::vector<Term> items;
    Term rest = list;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        items.push_back(rest.head_unchecked());
    }
    if (!rest.is_nil()) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::bad_generator, .payload = rest.word()};
    }
    return items;
}

std::vector<Term> elements(const Term &list) { return lists::proper(list); }

bool member(const Term &element, const ListOf &list) {
    Term rest = list.term;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        if (same(rest.head_unchecked(), element)) {
            return true;
        }
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return false;
}

Term append(const Context &context, const Term &left, const Term &right) {
    return context.call("erlang", "++", {left, right});
}

Term append_all(const Context &context, std::span<const Term> lists) {
    Term result = context.nil();
    for (auto list = lists.rbegin(); list != lists.rend(); ++list) {
        result = list == lists.rbegin() ? *list : append(context, *list, result);
    }
    return result;
}

Term join(const Context &context, const Term &separator, std::span<const Term> items) {
    std::vector<Term> joined;
    for (const auto &item : items) {
        if (!joined.empty()) {
            joined.push_back(separator);
        }
        joined.push_back(item);
    }
    return context.list(joined);
}

void raise_throw(const Term &reason) {
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_throw, .payload = reason.word()};
}

void raise_error(const Term &reason) {
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error, .payload = reason.word()};
}

void no_clause() { throw BuiltinFailure{.reason = abi::v1::ErrorReason::function_clause}; }

bool is_option(const Context &context, const Term &argument) {
    return has(argument, context.atom("short"_atom)) || has(argument, context.atom("long"_atom));
}

bool requires_argument(const Context &context, const Term &option) {
    if (const auto nargs = find(option, context.atom("nargs"_atom))) {
        return !is_tagged(*nargs, "maybe") && !is_atom(*nargs, "maybe");
    }
    const auto action = get(option, context.atom("action"_atom), context.atom("store"_atom));
    if (is_atom(action, "store") || is_atom(action, "append")) {
        return !is_atom(get(option, context.atom("type"_atom), context.atom("string"_atom)), "boolean");
    }
    return false;
}

Replay::Replay(const Term &answers) : answers_(elements(answers)) { std::ranges::reverse(answers_); }

Term Replay::next(const Term &request) {
    if (used_ == answers_.size()) {
        throw Request{request};
    }
    return answers_[used_++];
}

namespace {
// Run one builtin's walk: its result, the request it stopped at, or 0 after a failure a called builtin recorded.
template <typename Body> Word answered(Body &&body) {
    try {
        return body().word();
    } catch (const Request &request) {
        return request.term.word();
    } catch (const Recorded &) {
        return 0;
    }
}

// validate(Prog, Command, Prefixes): ok, or {Kind, Entity, Path, Field, Text} for argparse.erl to raise.
Word validate_builtin(ProcessContext &process, const ProgArg &prog, const CommandArg &command,
                      const PrefixesArg &prefixes) {
    const Context context(process);
    return answered([&] { return validate(context, {prog.term, command.term, prefixes.term}); });
}

// parse(Args, Prog, Command, Prefixes, Default, Answers): {ok, ArgMap, Path, Command} or a request; parser errors
// are thrown as OTP throws them.
Word parse_builtin(ProcessContext &process, const ArgsArg &args, const ProgArg &prog, const CommandArg &command,
                   const PrefixesArg &prefixes, const DefaultArg &fallback, const AnswersArg &answers) {
    const Context context(process);
    return answered([&] {
        Replay replay(answers.term);
        return parse(context, replay, {args.term, prog.term, command.term, prefixes.term, fallback.term});
    });
}

// help(Prog, Command, Options, Answers): {ok, Text} or a request.
Word help_builtin(ProcessContext &process, const ProgArg &prog, const CommandArg &command, const OptionsArg &options,
                  const AnswersArg &answers) {
    const Context context(process);
    return answered([&] {
        Replay replay(answers.term);
        return help(context, replay, {prog.term, command.term, options.term});
    });
}

constexpr std::array ARGPARSE_BUILTINS{
    typed_entry<validate_builtin>("clause_argparse", "validate"),
    typed_entry<parse_builtin>("clause_argparse", "parse"),
    typed_entry<help_builtin>("clause_argparse", "help"),
};
} // namespace
} // namespace clause::runtime::builtins::argparse

namespace clause::runtime {
std::span<const BuiltinEntry> argparse_builtins() noexcept { return builtins::argparse::ARGPARSE_BUILTINS; }
} // namespace clause::runtime
