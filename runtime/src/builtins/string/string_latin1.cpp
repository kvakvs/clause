#include "../lists/list_walk.hpp"
#include "search.hpp"
#include <algorithm>
#include <array>

// string's obsolete Latin-1 API (plan 21 step LIB4): plain lists of characters, each function as string.erl's
// clauses compute it, with their function_clause and badarg errors.
namespace clause::runtime::builtins::string {
namespace {
using lists::no_clause;

// The elements of a proper list; badarg otherwise (erlang:length/1, ++, lists:reverse/1).
std::vector<Term> items_or_badarg(const Term &list) {
    const auto items = proper_list(list);
    if (!items) {
        bad_argument();
    }
    return *items;
}

// The elements of a proper list; function_clause otherwise (a clause walking to []).
std::vector<Term> items_or_clause(const Term &list) {
    const auto items = proper_list(list);
    if (!items) {
        no_clause();
    }
    return *items;
}

std::int64_t integer_or_clause(const Term &term) {
    const auto value = small_of(term);
    if (!value) {
        no_clause();
    }
    return *value;
}

Term len1(ProcessContext &context, const Term &string) {
    return Terms(context).integer(static_cast<std::int64_t>(items_or_badarg(string).size()));
}

Term concat2(ProcessContext &context, const TextArg &left, const OtherArg &right) {
    return Terms(context).list(items_or_badarg(left.term), right.term);
}

// chr/2 and rchr/2: the first or last position of a character, 0 when absent.
template <bool Last> Term chr2(ProcessContext &context, const TextArg &string, const Term &character) {
    if (!character.is_integer()) {
        no_clause();
    }
    std::int64_t found = 0;
    std::int64_t index = 1;
    Term rest = string.term;
    for (; rest.is_cons(); rest = rest.tail_unchecked(), ++index) {
        if (same(rest.head_unchecked(), character)) {
            found = index;
            if (!Last) {
                break;
            }
        }
    }
    if (!rest.is_nil() && (Last || found == 0)) {
        no_clause();
    }
    return Terms(context).integer(found);
}

// l_prefix: whether the list starts with the prefix's elements (both proper lists).
bool list_prefix(Term prefix, Term string) {
    while (prefix.is_cons() && string.is_cons() && same(prefix.head_unchecked(), string.head_unchecked())) {
        prefix = prefix.tail_unchecked();
        string = string.tail_unchecked();
    }
    if (prefix.is_nil() && string.is_list()) {
        return true;
    }
    if (prefix.is_list() && string.is_list()) {
        return false;
    }
    no_clause();
}

// Whether the substring starts at the head of the string's rest.
bool starts_here(const PatternArg &sub, const Term &rest) {
    return sub.term.is_cons() && same(rest.head_unchecked(), sub.term.head_unchecked()) &&
           list_prefix(sub.term.tail_unchecked(), rest.tail_unchecked());
}

// str/2 and rstr/2: the first or last position of a substring, 0 when absent.
template <bool Last> Term str2(ProcessContext &context, const TextArg &string, const PatternArg &sub) {
    if (!sub.term.is_list()) {
        no_clause();
    }
    std::int64_t found = 0;
    std::int64_t index = 1;
    Term rest = string.term;
    for (; rest.is_cons() && (Last || found == 0); rest = rest.tail_unchecked(), ++index) {
        if (starts_here(sub, rest)) {
            found = index;
        }
    }
    if (!rest.is_nil() && (Last || found == 0)) {
        no_clause();
    }
    return Terms(context).integer(found);
}

bool member_of(const PatternArg &list, const Term &item) {
    Term rest = list.term;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        if (same(rest.head_unchecked(), item)) {
            return true;
        }
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return false;
}

// span/2 and cspan/2: the length of the prefix of characters in (or not in) the set.
template <bool Inside> Term span2(ProcessContext &context, const TextArg &string, const PatternArg &chars) {
    if (!chars.term.is_list()) {
        no_clause();
    }
    std::int64_t count = 0;
    Term rest = string.term;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        if (member_of(chars, rest.head_unchecked()) != Inside) {
            return Terms(context).integer(count);
        }
        ++count;
    }
    if (!rest.is_nil()) {
        no_clause();
    }
    return Terms(context).integer(count);
}

// substr2: the list from position Start (1-based); function_clause past its end.
Term from_position(const Term &string, std::int64_t start) {
    Term rest = string;
    for (; start > 1; --start) {
        if (!rest.is_cons()) {
            no_clause();
        }
        rest = rest.tail_unchecked();
    }
    if (!rest.is_list()) {
        no_clause();
    }
    return rest;
}

// substr1: the first Length elements, as many as there are.
Term first_elements(ProcessContext &context, Term string, std::int64_t length) {
    std::vector<Term> items;
    for (; length > 0 && string.is_cons(); --length) {
        items.push_back(string.head_unchecked());
        string = string.tail_unchecked();
    }
    if (!string.is_list()) {
        no_clause();
    }
    return Terms(context).list(items);
}

Term substr2(ProcessContext &, const TextArg &string, const Term &start) {
    const auto from = integer_or_clause(start);
    if (from < 1) {
        no_clause();
    }
    return from_position(string.term, from);
}

Term substr3(ProcessContext &context, const TextArg &string, const Term &start, const Term &length) {
    const auto from = integer_or_clause(start);
    const auto count = integer_or_clause(length);
    if (from < 1 || count < 0) {
        no_clause();
    }
    return first_elements(context, from_position(string.term, from), count);
}

// The non-empty runs of characters between separators.
Term tokens_of(const Terms &terms, const std::vector<Term> &chars, const PatternArg &separators) {
    std::vector<Term> tokens;
    std::vector<Term> token;
    const auto close = [&] {
        if (!token.empty()) {
            tokens.push_back(terms.list(token));
            token.clear();
        }
    };
    for (const auto &c : chars) {
        if (member_of(separators, c)) {
            close();
        } else {
            token.push_back(c);
        }
    }
    close();
    return terms.list(tokens);
}

// tokens/2: the non-empty runs between separator characters.
Term tokens2(ProcessContext &context, const TextArg &string, const PatternArg &separators) {
    const Terms terms(context);
    if (separators.term.is_nil()) {
        if (string.term.is_nil()) {
            return string.term;
        }
        if (!string.term.is_cons()) {
            case_clause(string.term);
        }
        return terms.list(std::array{string.term});
    }
    if (!separators.term.is_cons()) {
        case_clause(separators.term);
    }
    if (!string.term.is_list()) {
        no_clause();
    }
    return tokens_of(terms, items_or_badarg(string.term), separators);
}

// chars/3: Number copies of Character in front of Tail.
Term chars3(ProcessContext &context, const CharArg &char_arg, const Term &number, const OtherArg &tail) {
    const auto &character = char_arg.term;
    const auto count = integer_or_clause(number);
    if (count < 0 || (count == 0 && !character.is_integer())) {
        no_clause();
    }
    if (count > 0 && !character.is_integer()) {
        no_clause();
    }
    return Terms(context).list(std::vector<Term>(static_cast<std::size_t>(count), character), tail.term);
}

Term chars2(ProcessContext &context, const CharArg &character, const Term &number) {
    return chars3(context, character, number, {Terms(context).nil()});
}

Term copies2(ProcessContext &context, const TextArg &string, const Term &number) {
    const auto count = string.term.is_list() ? small_of(number) : std::nullopt;
    if (!count || *count < 0) {
        no_clause();
    }
    if (*count == 0) {
        return Terms(context).nil();
    }
    const auto items = items_or_badarg(string.term);
    std::vector<Term> all;
    for (std::int64_t copy = 0; copy < *count; ++copy) {
        all.insert(all.end(), items.begin(), items.end());
    }
    return Terms(context).list(all);
}

// strip_left: the list after its leading run of the character; function_clause for anything but a list.
Term strip_left(Term string, const Term &character) {
    while (string.is_cons() && same(string.head_unchecked(), character)) {
        string = string.tail_unchecked();
    }
    if (!string.is_list() || !character.is_integer()) {
        no_clause();
    }
    return string;
}

// strip_right: the list without its trailing run of the character, rebuilt.
Term strip_right(ProcessContext &context, const TextArg &string, const Term &character) {
    const auto items = items_or_clause(string.term);
    if (!character.is_integer()) {
        no_clause();
    }
    auto end = items.size();
    while (end > 0 && same(items[end - 1], character)) {
        --end;
    }
    return Terms(context).list(std::span(items).subspan(0, end));
}

Term strip3(ProcessContext &context, const TextArg &string, const DirArg &dir, const Term &character) {
    const auto direction = dir.term.is_atom() ? dir.term.atom_spelling_unchecked() : std::string_view{};
    if (direction == "left") {
        return strip_left(string.term, character);
    }
    if (direction == "right") {
        return strip_right(context, {string.term}, character);
    }
    if (direction != "both") {
        no_clause();
    }
    return strip_right(context, {strip_left(string.term, character)}, character);
}

Term strip2(ProcessContext &context, const TextArg &string, const DirArg &dir) {
    return strip3(context, string, dir, Terms(context).integer(' '));
}

Term strip1(ProcessContext &context, const TextArg &string) {
    return strip2(context, string, {Terms(context).atom("both"_atom)});
}

// words/2: the number of words separated by runs of the character.
std::int64_t word_count(ProcessContext &context, const Term &string, const Term &character) {
    const Terms terms(context);
    auto rest = strip3(context, {string}, {terms.atom("both"_atom)}, character);
    std::int64_t count = 0;
    while (rest.is_cons()) {
        if (same(rest.head_unchecked(), character)) {
            ++count;
            rest = strip_left(rest.tail_unchecked(), character);
        } else {
            rest = rest.tail_unchecked();
        }
    }
    return count + 1;
}

Term words2(ProcessContext &context, const TextArg &string, const Term &character) {
    if (!character.is_integer()) {
        no_clause();
    }
    return Terms(context).integer(word_count(context, string.term, character));
}

Term words1(ProcessContext &context, const TextArg &string) {
    return words2(context, string, Terms(context).integer(' '));
}

// s_word before the wanted word: the string moved past `stop` - 1 words, a separator run counting one.
Term skip_words(Term rest, std::int64_t stop, const Term &character) {
    std::int64_t index = 1;
    while (rest.is_cons() && index < stop) {
        if (same(rest.head_unchecked(), character)) {
            rest = strip_left(rest.tail_unchecked(), character);
            ++index;
        } else {
            rest = rest.tail_unchecked();
        }
    }
    return rest;
}

// s_word: the word at `stop` of a string whose leading separators are gone.
Term nth_word(ProcessContext &context, const Term &string, std::int64_t stop, const Term &character) {
    if (stop < 1 && string.is_cons()) {
        no_clause();
    }
    auto rest = skip_words(string, stop, character);
    std::vector<Term> word;
    while (rest.is_cons() && !same(rest.head_unchecked(), character)) {
        word.push_back(rest.head_unchecked());
        rest = rest.tail_unchecked();
    }
    if (!rest.is_list()) {
        no_clause();
    }
    return Terms(context).list(word);
}

Term sub_word3(ProcessContext &context, const TextArg &string, const Term &number, const Term &character) {
    const Terms terms(context);
    if (!number.is_integer() || !character.is_integer()) {
        no_clause();
    }
    const auto index = *small_of(number);
    if (word_count(context, string.term, character) < index) {
        return terms.nil();
    }
    return nth_word(context, strip_left(string.term, character), index, character);
}

Term sub_word2(ProcessContext &context, const TextArg &string, const Term &number) {
    return sub_word3(context, string, number, Terms(context).integer(' '));
}

// String ++ chars(Char, Num) and chars(Char, Num, String).
Term padded_right(ProcessContext &context, const std::vector<Term> &items, std::int64_t count, const Term &fill) {
    const Terms terms(context);
    return terms.list(items, terms.list(std::vector<Term>(static_cast<std::size_t>(count), fill)));
}

Term padded_left(ProcessContext &context, const Term &string, std::int64_t count, const Term &fill) {
    return Terms(context).list(std::vector<Term>(static_cast<std::size_t>(count), fill), string);
}

Term left3(ProcessContext &context, const TextArg &string, const Term &number, const Term &fill) {
    if (!number.is_integer() || !fill.is_integer()) {
        no_clause();
    }
    const auto wanted = *small_of(number);
    const auto items = items_or_badarg(string.term);
    const auto have = static_cast<std::int64_t>(items.size());
    if (have > wanted) {
        return substr3(context, string, Terms(context).integer(1), number);
    }
    return have < wanted ? padded_right(context, items, wanted - have, fill) : string.term;
}

Term left2(ProcessContext &context, const TextArg &string, const Term &number) {
    return left3(context, string, number, Terms(context).integer(' '));
}

Term right3(ProcessContext &context, const TextArg &string, const Term &number, const Term &fill) {
    if (!number.is_integer() || !fill.is_integer()) {
        no_clause();
    }
    const auto wanted = *small_of(number);
    const auto have = static_cast<std::int64_t>(items_or_badarg(string.term).size());
    if (have > wanted) {
        return substr2(context, string, Terms(context).integer(have - wanted + 1));
    }
    return have < wanted ? padded_left(context, string.term, wanted - have, fill) : string.term;
}

Term right2(ProcessContext &context, const TextArg &string, const Term &number) {
    return right3(context, string, number, Terms(context).integer(' '));
}

Term centre3(ProcessContext &context, const TextArg &string, const Term &number, const Term &fill) {
    const Terms terms(context);
    if (!number.is_integer() || !fill.is_integer()) {
        no_clause();
    }
    const auto wanted = *small_of(number);
    if (wanted == 0 && string.term.is_list()) {
        return terms.nil();
    }
    const auto items = items_or_badarg(string.term);
    const auto have = static_cast<std::int64_t>(items.size());
    if (have > wanted) {
        return substr3(context, string, terms.integer((have - wanted) / 2 + 1), number);
    }
    if (have == wanted) {
        return string.term;
    }
    const auto before = (wanted - have) / 2;
    return padded_left(context, padded_right(context, items, wanted - (have + before), fill), before, fill);
}

Term centre2(ProcessContext &context, const TextArg &string, const Term &number) {
    return centre3(context, string, number, Terms(context).integer(' '));
}

Term sub_string3(ProcessContext &context, const TextArg &string, const Term &start, const Term &stop) {
    if (!start.is_integer() || !stop.is_integer()) {
        no_clause();
    }
    return substr3(context, string, start, Terms(context).integer(*small_of(stop) - *small_of(start) + 1));
}

// join/2: the strings with the separator between them.
Term join2(ProcessContext &context, const TextArg &strings, const PatternArg &separator) {
    const Terms terms(context);
    if (strings.term.is_nil()) {
        if (!separator.term.is_list()) {
            no_clause();
        }
        return strings.term;
    }
    if (!strings.term.is_cons()) {
        no_clause();
    }
    std::vector<Term> all = items_or_badarg(strings.term.head_unchecked());
    for (const auto &string : items_or_badarg(strings.term.tail_unchecked())) {
        const auto sep = items_or_badarg(separator.term);
        all.insert(all.end(), sep.begin(), sep.end());
        const auto items = items_or_badarg(string);
        all.insert(all.end(), items.begin(), items.end());
    }
    return terms.list(all);
}

// The Latin-1 letters to_upper_char and to_lower_char change: lower-case ranges and upper-case ranges.
using Ranges = std::array<std::pair<std::int64_t, std::int64_t>, 3>;
constexpr Ranges LOWER_LETTERS{{{'a', 'z'}, {0xE0, 0xF6}, {0xF8, 0xFE}}};
constexpr Ranges UPPER_LETTERS{{{'A', 'Z'}, {0xC0, 0xD6}, {0xD8, 0xDE}}};

bool in_ranges(std::int64_t code, const Ranges &ranges) {
    return std::ranges::any_of(ranges,
                               [code](const auto &range) { return code >= range.first && code <= range.second; });
}

// to_lower_char / to_upper_char over Latin-1 letters.
Term cased_char(const Terms &terms, const Term &c, bool upper) {
    const auto code = small_of(c);
    if (!code || !in_ranges(*code, upper ? LOWER_LETTERS : UPPER_LETTERS)) {
        return c;
    }
    return terms.integer(*code + (upper ? -32 : 32));
}

template <bool Upper> Term case_latin1(ProcessContext &context, const Term &string) {
    const Terms terms(context);
    if (string.is_integer()) {
        return cased_char(terms, string, Upper);
    }
    if (!string.is_list()) {
        no_clause();
    }
    std::vector<Term> items;
    for (const auto &c : items_or_badarg(string)) {
        items.push_back(cased_char(terms, c, Upper));
    }
    return terms.list(items);
}

constexpr std::array STRING_LATIN1_BUILTINS{
    typed_entry<len1>("string", "len"),
    typed_entry<concat2>("string", "concat"),
    typed_entry<chr2<false>>("string", "chr"),
    typed_entry<chr2<true>>("string", "rchr"),
    typed_entry<str2<false>>("string", "str"),
    typed_entry<str2<true>>("string", "rstr"),
    typed_entry<span2<true>>("string", "span"),
    typed_entry<span2<false>>("string", "cspan"),
    typed_entry<substr2>("string", "substr"),
    typed_entry<substr3>("string", "substr"),
    typed_entry<tokens2>("string", "tokens"),
    typed_entry<chars2>("string", "chars"),
    typed_entry<chars3>("string", "chars"),
    typed_entry<copies2>("string", "copies"),
    typed_entry<words1>("string", "words"),
    typed_entry<words2>("string", "words"),
    typed_entry<sub_word2>("string", "sub_word"),
    typed_entry<sub_word3>("string", "sub_word"),
    typed_entry<strip1>("string", "strip"),
    typed_entry<strip2>("string", "strip"),
    typed_entry<strip3>("string", "strip"),
    typed_entry<left2>("string", "left"),
    typed_entry<left3>("string", "left"),
    typed_entry<right2>("string", "right"),
    typed_entry<right3>("string", "right"),
    typed_entry<substr2>("string", "sub_string"),
    typed_entry<sub_string3>("string", "sub_string"),
    typed_entry<centre2>("string", "centre"),
    typed_entry<centre3>("string", "centre"),
    typed_entry<join2>("string", "join"),
    typed_entry<case_latin1<true>>("string", "to_upper"),
    typed_entry<case_latin1<false>>("string", "to_lower"),
};
} // namespace
} // namespace clause::runtime::builtins::string

namespace clause::runtime {
std::span<const BuiltinEntry> string_latin1_builtins() noexcept { return builtins::string::STRING_LATIN1_BUILTINS; }
} // namespace clause::runtime
