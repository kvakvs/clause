#pragma once
#include "../lists/list_walk.hpp"
#include "../string/string.hpp"
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

// The argparse builtins (plan 21 step LIB70, docs/builtins.md#argparse): OTP's command validation, parser and help
// formatter over the command maps, with OTP's checks, errors and result shapes. Funs and re:run/2,3 are called by
// library/stdlib/argparse.erl: a builtin asks for them one at a time and replays its walk with the answers so far.
namespace clause::runtime::builtins::argparse {
// A failure a called builtin already recorded; the argparse builtin unwinds and returns 0 with it.
struct Recorded {};

// A call argparse.erl makes for the builtin ({custom, Fun, Arg}, {re, Arg, Re}, {describe, Fun}, ...), returned in
// place of the result.
struct Request {
    Term term;
};

// The roles of the builtins' term arguments (typed.hpp's Named).
struct ArgsRole;
struct ProgRole;
struct CommandRole;
struct PrefixesRole;
struct OptionsRole;
struct DefaultRole;
struct AnswersRole;
using ArgsArg = Named<ArgsRole>;
using ProgArg = Named<ProgRole>;
using CommandArg = Named<CommandRole>;
using PrefixesArg = Named<PrefixesRole>;
using OptionsArg = Named<OptionsRole>;
using DefaultArg = Named<DefaultRole>;
using AnswersArg = Named<AnswersRole>;

// Term building and calls of other registered builtins over one process.
class Context final {
  public:
    explicit Context(ProcessContext &context) : terms_(context) {}

    ProcessContext &process() const { return terms_.context(); }

    const string::Terms &terms() const { return terms_; }

    Term atom(AtomName name) const { return terms_.atom(name); }

    Term nil() const { return terms_.nil(); }

    Term integer(std::int64_t value) const { return terms_.integer(value); }

    Term cons(const Term &head, const Term &tail) const { return terms_.cons(head, tail); }

    Term list(std::span<const Term> items) const { return terms_.list(items); }

    Term list(std::initializer_list<Term> items) const { return terms_.list(std::span(items.begin(), items.size())); }

    Term tuple(std::initializer_list<Term> items) const { return terms_.tuple(std::span(items.begin(), items.size())); }

    // An ASCII text as a character list, and as a binary.
    Term chars(std::string_view text) const;
    Term binary(std::string_view text) const;
    // A map of these entries; the last one wins for a key.
    Term map(std::span<const std::pair<Term, Term>> entries) const;
    // A copy of `map` with the entry's key set to its value.
    Term with(const Term &map, const std::pair<Term, Term> &entry) const;

    // Module:Function(Args) of a registered builtin; Recorded when it fails.
    Term call(std::string_view module, std::string_view function, std::span<const Term> args) const;

    Term call(std::string_view module, std::string_view function, std::initializer_list<Term> args) const {
        return call(module, function, std::span(args.begin(), args.size()));
    }

    // The same, none when it raised error:badarg (the failure is cleared, as `catch error:badarg` does).
    std::optional<Term> call_unless_badarg(std::string_view module, std::string_view function,
                                           std::initializer_list<Term> args) const;
    // io_lib:format(Format, Args) with an ASCII format.
    Term format(std::string_view format, std::initializer_list<Term> args) const;
    // string:length/1, as a small integer.
    std::int64_t length(const Term &data) const;

  private:
    string::Terms terms_;
};

// The value of `key` in a map; none when the key is missing. A non-map raises {badmap, Map}.
std::optional<Term> find(const Term &map, const Term &key);
// maps:get(Key, Map, Fallback).
Term get(const Term &map, const Term &key, const Term &fallback);
// maps:get(Key, Map): {badkey, Key} when it is missing.
Term get(const Context &context, const Term &map, const Term &key);
bool has(const Term &map, const Term &key);
// Whether a map binds `key` to the atom `value` (a #{Key := value} pattern).
bool has_value(const Context &context, const Term &map, AtomName key, std::string_view value);
// Whether a term is the atom `name`.
bool is_atom(const Term &term, std::string_view name);
// Whether a term is a 2-tuple tagged with the atom `tag` ({Tag, _}), or a tuple of `size` so tagged.
bool is_tagged(const Term &term, std::string_view tag, std::size_t size = 2);
// Whether a term is one of these atoms.
bool is_any(const Term &term, std::initializer_list<std::string_view> names);
// Whether a term is a list or a binary.
bool is_listy(const Term &term);

// lists:all/2 over a list: false at the first item `test` rejects, function_clause for an improper tail.
template <typename Test> bool all_items(const Term &list, const Test &test) {
    Term rest = list;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        if (!test(rest.head_unchecked())) {
            return false;
        }
    }
    if (!rest.is_nil()) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::function_clause};
    }
    return true;
}

// Whether a term is a 2-tuple ({_, _}).
bool is_pair(const Term &term);
// =:=.
bool same(const Term &left, const Term &right);
// Whether `term` is the character list `text`.
bool is_text(const Term &term, std::string_view text);
// The elements of a list comprehension's generator: {bad_generator, Tail} for an improper or non-list term.
std::vector<Term> generated(const Context &context, const Term &list);
// The elements of a proper list; function_clause for anything else.
std::vector<Term> elements(const Term &list);
// A list argument beside another term argument (a Named role, so the two are not swapped).
struct ListOfRole;
using ListOf = Named<ListOfRole>;
// lists:member(Element, List).
bool member(const Term &element, const ListOf &list);
// Left ++ Right; badarg for an improper Left.
Term append(const Context &context, const Term &left, const Term &right);
// lists:append/1 of these lists.
Term append_all(const Context &context, std::span<const Term> lists);
// lists:join(Separator, Items).
Term join(const Context &context, const Term &separator, std::span<const Term> items);
// Raise throw:Reason, error:Reason and error:function_clause.
[[noreturn]] void raise_throw(const Term &reason);
[[noreturn]] void raise_error(const Term &reason);
[[noreturn]] void no_clause();

// OTP's ?IS_OPTION: an argument with a short or a long form.
bool is_option(const Context &context, const Term &argument);
// requires_argument/1: whether an option takes a value (and so cannot be combined with other flags).
bool requires_argument(const Context &context, const Term &option);

// Answers of earlier requests, in the order they were asked (argparse.erl passes them newest first).
class Replay final {
  public:
    explicit Replay(const Term &answers);

    // The next answer, or Request thrown with `request` once every answer is used.
    Term next(const Term &request);

  private:
    std::vector<Term> answers_;
    std::size_t used_ = 0;
};

// What parse/3 hands the parser: the arguments as character lists, the program name, the command, the prefix map
// and the global default (maps:find(default, Options)).
struct ParseInputs {
    Term args;
    Term prog;
    Term command;
    Term prefixes;
    Term fallback;
};

// What validate/2 checks: the program name, the command and the prefix map.
struct ValidateInputs {
    Term prog;
    Term command;
    Term prefixes;
};

// What help/2 formats: the program name, the command and help/2's options.
struct HelpInputs {
    Term prog;
    Term command;
    Term options;
};

// clause_argparse:validate/3, parse/6 and help/4 (validate.cpp, parse.cpp, help.cpp).
Term validate(const Context &context, const ValidateInputs &inputs);
Term parse(const Context &context, Replay &replay, const ParseInputs &inputs);
Term help(const Context &context, Replay &replay, const HelpInputs &inputs);
} // namespace clause::runtime::builtins::argparse
