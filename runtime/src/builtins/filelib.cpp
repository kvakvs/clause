#include "../terms/integers.hpp"
#include "typed.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <utility>
#include <variant>
#include <vector>

// filelib's wildcard patterns (plan 21 step LIB51, docs/builtins.md#filelib): clause_filelib escapes a pattern so
// filename:split/1 keeps its escapes, compiles one part of the split pattern into match items and matches directory
// listings against them, with OTP's rules and quirks: "**" alone, "*" last and "{...}" ending a part, "]" first or
// as the low end of a range in a character set, alternatives folding in the rest of their part, an unfinished
// alternative raising {badpattern, missing_delimiter} and a nested one a case_clause. filelib.erl walks the
// directories.
namespace clause::runtime::builtins {
namespace {
struct PatternRole;
struct BaseRole;
struct NameRole;
using Pattern = Named<PatternRole>;
using Base = Named<BaseRole>;
using Name = Named<NameRole>;

// A small integer's value; none for any other term.
std::optional<std::int64_t> code_of(const Term &term) { return small(term.word()); }

bool is_character(const Term &term, char32_t c) {
    const auto code = code_of(term);
    return code && *code == static_cast<std::int64_t>(c);
}

// The word of a character.
Word character(char32_t c) { return need(Term::from_word(need(encode_integer(c)))).word(); }

// escape(Pattern): '@' doubled and '\' written "@e", OTP's escape spelling that filename:split/1 leaves alone.
Term escape(ProcessContext &context, const ListArgument &pattern) {
    std::vector<Word> escaped;
    for (const auto &element : pattern.elements) {
        if (is_character(element, '@')) {
            escaped.insert(escaped.end(), {character('@'), character('@')});
        } else if (is_character(element, '\\')) {
            escaped.insert(escaped.end(), {character('@'), character('e')});
        } else {
            escaped.push_back(element.word());
        }
    }
    TermFactory factory(context);
    return need(factory.list_words(escaped, need(factory.nil())));
}

// A part's element once escapes are read: the term, and whether an escape took its meaning away.
struct Item {
    Term term;
    bool escaped = false;

    // Whether this is the character `c` with its meaning.
    bool is(char32_t c) const { return !escaped && is_character(term, c); }
};

using Items = std::vector<Item>;

// Read the escape or element at `at` into `items`; how many elements it spans ("@@" is '@', "@e" escapes the next
// element, "@e" at the end is dropped).
std::size_t unescape_one(std::span<const Item> input, std::size_t at, Items &items) {
    const bool prefix = input[at].is('@') && at + 1 < input.size();
    if (prefix && input[at + 1].is('@')) {
        items.push_back(Item{input[at].term});
        return 2;
    }
    if (prefix && input[at + 1].is('e')) {
        if (at + 2 < input.size()) {
            items.push_back(Item{input[at + 2].term, true});
        }
        return 3;
    }
    items.push_back(input[at]);
    return 1;
}

// Read the escapes of a part.
Items unescape(std::span<const Item> input) {
    Items items;
    for (std::size_t at = 0; at < input.size();) {
        at += unescape_one(input, at, items);
    }
    return items;
}

// A compiled alternative up to its separator: the separator, its match items and where the rest of the part starts.
struct Piece {
    char32_t separator = 0;
    std::vector<Word> items;
    std::size_t rest = 0;
};

// Compiles the items of one part into match items: characters, question, star, accept, double_star,
// {one_of, [{Low, High}]} and {alt, [Items]}.
class PartCompiler {
  public:
    explicit PartCompiler(ProcessContext &context) : context_(context) {}

    // The match items of a whole part, its escapes read first.
    Term part(std::span<const Item> input) { return std::get<Term>(compile(unescape(input), 0, false)); }

  private:
    // The items from `at`: the list at the end of the part, or, when `upto` a separator, the piece before it. An
    // alternative ends the part, and a list then comes back even when `upto`, as OTP's compile_part/3 returns it.
    std::variant<Term, Piece> compile(const Items &items, std::size_t at, bool upto) {
        std::vector<Word> result;
        while (at < items.size()) {
            if (upto && separator(items[at])) {
                return Piece{items[at].is(',') ? U',' : U'}', std::move(result), at + 1};
            }
            if (items[at].is('{')) {
                result.push_back(alternatives(items, at + 1).word());
                return list(result);
            }
            at = step(items, at, result);
        }
        if (upto) {
            bad_pattern("missing_delimiter"_atom);
        }
        return list(result);
    }

    // Whether an item ends an alternative.
    static bool separator(const Item &item) { return item.is(',') || item.is('}'); }

    // Compile the item at `at` that is no separator or alternative; where the next one starts.
    std::size_t step(const Items &items, std::size_t at, std::vector<Word> &result) {
        const auto &item = items[at];
        if (item.is('?')) {
            result.push_back(atom(context_, "question"_atom).word());
            return at + 1;
        }
        if (item.is('*')) {
            return stars(items, at, result);
        }
        if (item.is('[')) {
            if (auto set = character_set(items, at + 1)) {
                result.push_back(set->first.word());
                return set->second;
            }
        }
        result.push_back(item.term.word());
        return at + 1;
    }

    // "**" ending the part is double_star, a "*" ending it accept, any other "*" or "**" star.
    std::size_t stars(const Items &items, std::size_t at, std::vector<Word> &result) {
        const bool twice = at + 1 < items.size() && items[at + 1].is('*');
        const auto end = at + (twice ? 2 : 1);
        const bool last = end == items.size();
        if (!last) {
            result.push_back(atom(context_, "star"_atom).word());
        } else {
            result.push_back(atom(context_, twice ? "double_star"_atom : "accept"_atom).word());
        }
        return end;
    }

    // A character set after "[": {one_of, Ranges} and where the part goes on, or none when no "]" closes it. A "]"
    // first is a member; a range's low end may be "]".
    std::optional<std::pair<Term, std::size_t>> character_set(const Items &items, std::size_t at) {
        std::vector<std::pair<std::int64_t, std::int64_t>> ranges;
        if (at < items.size() && items[at].is(']')) {
            ranges.emplace_back(']', ']');
            ++at;
        }
        while (at < items.size()) {
            if (at + 2 < items.size() && items[at + 1].is('-') && range(items[at], items[at + 2], ranges)) {
                at += 3;
            } else if (items[at].is(']')) {
                return std::pair{one_of(ranges), at + 1};
            } else {
                member(items[at], ranges);
                ++at;
            }
        }
        return std::nullopt;
    }

    // Add `low`-`high` when both are characters in order; false when they are not a range. An escaped high end at or
    // above `low` is a badarith, as OTP's range walk subtracts from it.
    static bool range(const Item &low, const Item &high, std::vector<std::pair<std::int64_t, std::int64_t>> &ranges) {
        const auto from = code_of(low.term);
        const auto to = code_of(high.term);
        if (!from || !to) {
            return false;
        }
        if (!low.escaped && !high.escaped) {
            if (*from > *to) {
                return false;
            }
            ranges.emplace_back(*from, *to);
            return true;
        }
        if (high.escaped && (!low.escaped || *from <= *to)) {
            throw BuiltinFailure{.reason = abi::v1::ErrorReason::badarith};
        }
        return false;
    }

    // Add one character; another term never matches a name's character.
    static void member(const Item &item, std::vector<std::pair<std::int64_t, std::int64_t>> &ranges) {
        if (const auto code = code_of(item.term)) {
            ranges.emplace_back(*code, *code);
        }
    }

    Term one_of(std::span<const std::pair<std::int64_t, std::int64_t>> ranges) {
        TermFactory factory(context_);
        std::vector<Word> pairs;
        for (const auto &[low, high] : ranges) {
            pairs.push_back(need(factory.tuple_words(std::array{character(static_cast<char32_t>(low)),
                                                                character(static_cast<char32_t>(high))}))
                                .word());
        }
        const auto members = need(factory.list_words(pairs, need(factory.nil())));
        return need(factory.tuple_words(std::array{atom(context_, "one_of"_atom).word(), members.word()}));
    }

    // Alternatives after "{": {alt, Alternatives}, each followed by the compiled rest of the part, latest first as
    // OTP lists them. A nested alternative ends a piece with a list: OTP's case on it fails with a case_clause.
    Term alternatives(const Items &items, std::size_t at) {
        std::vector<std::vector<Word>> alternatives;
        for (;;) {
            auto compiled = compile(items, at, true);
            if (auto *nested = std::get_if<Term>(&compiled)) {
                throw BuiltinFailure{.reason = abi::v1::ErrorReason::case_clause, .payload = nested->word()};
            }
            auto &piece = std::get<Piece>(compiled);
            alternatives.push_back(std::move(piece.items));
            if (piece.separator == U'}') {
                return alternation(alternatives, part(std::span(items).subspan(piece.rest)));
            }
            at = piece.rest;
        }
    }

    Term alternation(std::span<const std::vector<Word>> alternatives, const Term &rest) {
        TermFactory factory(context_);
        std::vector<Word> joined;
        for (const auto &alternative : std::ranges::reverse_view(alternatives)) {
            joined.push_back(need(factory.list_words(alternative, rest)).word());
        }
        const auto members = need(factory.list_words(joined, need(factory.nil())));
        return need(factory.tuple_words(std::array{atom(context_, "alt"_atom).word(), members.word()}));
    }

    Term list(std::span<const Word> words) {
        TermFactory factory(context_);
        return need(factory.list_words(words, need(factory.nil())));
    }

    [[noreturn]] void bad_pattern(AtomName reason) {
        const auto payload = need(TermFactory(context_).tuple_words(
            std::array{atom(context_, "badpattern"_atom).word(), atom(context_, reason).word()}));
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error, .payload = payload.word()};
    }

    ProcessContext &context_;
};

// compile_part(Part): the match items of one part of a split pattern.
Term compile_part(ProcessContext &context, const ListArgument &part) {
    Items items;
    items.reserve(part.elements.size());
    for (const auto &element : part.elements) {
        items.push_back(Item{element});
    }
    return PartCompiler(context).part(items);
}

// One compiled match item, read back for matching.
struct Token {
    enum class Kind : std::uint8_t { character, question, star, accept, double_star, one_of, alternatives };
    Kind kind = Kind::character;
    // A character's code, or -1 for a term no name character equals.
    std::int64_t code = -1;
    std::vector<std::pair<std::int64_t, std::int64_t>> ranges;
    std::vector<std::vector<Token>> alternatives;
};

using Tokens = std::vector<Token>;

// A token of `kind` with a character code.
Token token(Token::Kind kind, std::int64_t code = -1) {
    Token made;
    made.kind = kind;
    made.code = code;
    return made;
}

Tokens tokens_of(const Term &pattern);

// The token of an atom item: question, star, accept or double_star.
Token word_token(const Term &item) {
    static constexpr std::array<std::pair<std::string_view, Token::Kind>, 4> WORDS{{
        {"question", Token::Kind::question},
        {"star", Token::Kind::star},
        {"accept", Token::Kind::accept},
        {"double_star", Token::Kind::double_star},
    }};
    const auto found =
        std::ranges::find(WORDS, item.atom_spelling_unchecked(), &std::pair<std::string_view, Token::Kind>::first);
    return token(found == WORDS.end() ? Token::Kind::character : found->second);
}

// The token of {one_of, Ranges} or {alt, Alternatives}.
Token group_token(const Term &item, Token::Kind kind) {
    auto compiled = token(kind);
    for (auto members = item.tuple_element_unchecked(1); members.is_cons(); members = members.tail_unchecked()) {
        const auto member = members.head_unchecked();
        if (kind == Token::Kind::one_of) {
            compiled.ranges.emplace_back(code_of(member.tuple_element_unchecked(0)).value_or(-1),
                                         code_of(member.tuple_element_unchecked(1)).value_or(-1));
        } else {
            compiled.alternatives.push_back(tokens_of(member));
        }
    }
    return compiled;
}

// The token of one compiled item.
Token token_of(const Term &item) {
    if (item.is_atom()) {
        return word_token(item);
    }
    const auto tag = item.is_tuple() && item.tuple_size_unchecked() == 2 ? item.tuple_element_unchecked(0) : item;
    const auto spelling = tag.is_atom() ? tag.atom_spelling_unchecked() : std::string_view{};
    if (spelling == "one_of") {
        return group_token(item, Token::Kind::one_of);
    }
    if (spelling == "alt") {
        return group_token(item, Token::Kind::alternatives);
    }
    return token(Token::Kind::character, code_of(item).value_or(-1));
}

// The tokens of a compiled part; badarg for anything but a proper list.
Tokens tokens_of(const Term &pattern) {
    Tokens tokens;
    auto items = pattern;
    for (; items.is_cons(); items = items.tail_unchecked()) {
        tokens.push_back(token_of(items.head_unchecked()));
    }
    if (!items.is_nil()) {
        bad_argument();
    }
    return tokens;
}

// The character codes of a name (a string); badarg for anything else.
std::vector<std::int64_t> codes_of(const Term &name) {
    std::vector<std::int64_t> codes;
    auto chars = name;
    for (; chars.is_cons(); chars = chars.tail_unchecked()) {
        codes.push_back(code_of(chars.head_unchecked()).value_or(-1));
    }
    if (!chars.is_nil()) {
        bad_argument();
    }
    return codes;
}

bool matches(std::span<const Token> pattern, std::span<const std::int64_t> name);

bool any_star(std::span<const Token> rest, std::span<const std::int64_t> name) {
    for (std::size_t skip = 0; skip <= name.size(); ++skip) {
        if (matches(rest, name.subspan(skip))) {
            return true;
        }
    }
    return false;
}

bool in_set(const Token &token, std::int64_t code) {
    return std::ranges::any_of(token.ranges,
                               [&](const auto &range) { return code >= range.first && code <= range.second; });
}

// Whether a token that stands for one character matches `code`.
bool matches_one(const Token &token, std::int64_t code) {
    switch (token.kind) {
    case Token::Kind::question:
        return true;
    case Token::Kind::one_of:
        return in_set(token, code);
    default:
        return token.code == code;
    }
}

// Whether a name matches the tokens, as OTP's match_part/2 walks them.
bool matches(std::span<const Token> pattern, std::span<const std::int64_t> name) {
    if (pattern.empty()) {
        return name.empty();
    }
    const auto &token = pattern.front();
    const auto rest = pattern.subspan(1);
    switch (token.kind) {
    case Token::Kind::accept:
    case Token::Kind::double_star:
        return true;
    case Token::Kind::star:
        return any_star(rest, name);
    case Token::Kind::alternatives:
        return std::ranges::any_of(token.alternatives,
                                   [&](const Tokens &alternative) { return matches(alternative, name); });
    default:
        break;
    }
    return !name.empty() && matches_one(token, name.front()) && matches(rest, name.subspan(1));
}

// matches(Pattern, Base, Names): Base ++ Name for every name the compiled part matches, in order.
Term matching(ProcessContext &context, const Pattern &pattern, const Base &base, const ListArgument &names) {
    const auto tokens = tokens_of(pattern.term);
    const bool all = tokens.size() == 1 &&
                     (tokens.front().kind == Token::Kind::accept || tokens.front().kind == Token::Kind::double_star);
    std::vector<Word> prefix;
    auto chars = base.term;
    for (; chars.is_cons(); chars = chars.tail_unchecked()) {
        prefix.push_back(chars.head_unchecked().word());
    }
    if (!chars.is_nil()) {
        bad_argument();
    }
    TermFactory factory(context);
    std::vector<Word> found;
    for (const auto &name : names.elements) {
        if (all || matches(tokens, codes_of(name))) {
            found.push_back(need(factory.list_words(prefix, name)).word());
        }
    }
    return need(factory.list_words(found, need(factory.nil())));
}

// match(Pattern, Name): whether the compiled part matches one name.
Term match(ProcessContext &context, const Pattern &pattern, const Name &name) {
    return atom(context, matches(tokens_of(pattern.term), codes_of(name.term)) ? "true"_atom : "false"_atom);
}

constexpr std::array FILELIB_BUILTINS{
    typed_entry<escape>("clause_filelib", "escape"),
    typed_entry<compile_part>("clause_filelib", "compile_part"),
    typed_entry<matching>("clause_filelib", "matches"),
    typed_entry<match>("clause_filelib", "match"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> filelib_builtins() noexcept { return builtins::FILELIB_BUILTINS; }
} // namespace clause::runtime
