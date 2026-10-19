#include "../erlang/conversion_text.hpp"
#include "../lists/list_walk.hpp"
#include "../string/string.hpp"
#include "../text.hpp"
#include "scanner.hpp"
#include <array>
#include <utility>

// clause_erl_scan:scan/6 (plan 21 step LIB30, docs/builtins.md#erl-scan): the scanner over a character list, its
// tokens, errors and rests as OTP's terms. library/stdlib/erl_scan.erl drives it for string/1,2,3 and tokens/3,4.
namespace clause::runtime::builtins::front_end {
namespace {
using lists::no_clause;
using scanning::Location;
using scanning::Outcome;
using scanning::ScanError;
using scanning::Token;
using scanning::TokenKind;
using string::Terms;

// The roles of scan/6's arguments (typed.hpp's Named).
struct CharsRole;
struct EofRole;
struct LineRole;
struct ColumnRole;
struct WholeRole;
using CharsArg = Named<CharsRole>;
using EofArg = Named<EofRole>;
using LineArg = Named<LineRole>;
using ColumnArg = Named<ColumnRole>;
using WholeArg = Named<WholeRole>;

bool is_atom(const Term &term, std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

// The input: its code points, the list cell at each so rests are tails of the input, and its end.
struct Input {
    std::vector<char32_t> codes;
    std::vector<Term> cells;
    // [] or eof (OTP appends eof to the characters once the input has ended).
    Term tail;
    bool eof = false;
};

char32_t code_of(const Term &element) {
    const auto code = string::small_of(element);
    return code && *code >= 0 && *code <= 0x10FFFF ? static_cast<char32_t>(*code) : scanning::NOT_CHARACTER;
}

Input read_input(const Term &chars, bool eof) {
    Input input;
    auto rest = chars;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        input.cells.push_back(rest);
        input.codes.push_back(code_of(rest.head_unchecked()));
    }
    if (!rest.is_nil() && !is_atom(rest, "eof")) {
        no_clause();
    }
    input.tail = rest;
    input.eof = eof || rest.is_atom();
    return input;
}

// Builds the terms of one scan.
class Builder final {
  public:
    Builder(ProcessContext &context, const Input &input, bool text) : terms_(context), input_(input), text_(text) {}

    Term outcome(const Outcome &outcome) const {
        if (outcome.stop == scanning::Stop::error) {
            return error(outcome.error);
        }
        std::vector<Term> tokens;
        tokens.reserve(outcome.tokens.size());
        for (const auto &token : outcome.tokens) {
            tokens.push_back(this->token(token));
        }
        const auto column =
            outcome.location.column ? terms_.integer(*outcome.location.column) : terms_.atom("no_col"_atom);
        const std::array items{terms_.atom(outcome.stop == scanning::Stop::ok ? "ok"_atom : "more"_atom),
                               terms_.list(tokens), rest(outcome.rest), terms_.integer(outcome.location.line), column};
        return terms_.tuple(items);
    }

  private:
    Term token(const Token &token) const {
        const auto anno = annotation(token);
        switch (token.kind) {
        case TokenKind::symbol:
        case TokenKind::reserved:
            return pair(atom(token.chars), anno);
        case TokenKind::unquoted:
            return triple("$unquoted"_atom, anno, atom(token.chars));
        case TokenKind::atom:
            return triple("atom"_atom, anno, atom(token.chars));
        case TokenKind::var:
            return triple("var"_atom, anno, atom(token.chars));
        case TokenKind::sigil_prefix:
            return triple("sigil_prefix"_atom, anno, atom(token.chars));
        default:
            return valued(token, anno);
        }
    }

    // The tokens whose symbol is a number or a character list.
    Term valued(const Token &token, const Term &anno) const {
        switch (token.kind) {
        case TokenKind::integer:
            return triple("integer"_atom, anno, integer(token.digits, token.base));
        case TokenKind::floating:
            return triple("float"_atom, anno, need(TermFactory(terms_.context()).floating(token.number)));
        case TokenKind::character:
            return triple("char"_atom, anno, terms_.integer(token.code));
        case TokenKind::comment:
            return triple("comment"_atom, anno, chars(token.chars));
        case TokenKind::white_space:
            return triple("white_space"_atom, anno, chars(token.chars));
        case TokenKind::sigil_suffix:
            return triple("sigil_suffix"_atom, anno, chars(token.chars));
        default:
            return triple("string"_atom, anno, chars(token.chars));
        }
    }

    // erl_anno:new/1 of the location, with the token's text when tokens carry it and it is not empty (an empty sigil
    // suffix); badarg for a negative line, as erl_anno raises.
    Term annotation(const Token &token) const {
        if (token.location.line < 0) {
            bad_argument();
        }
        const auto place = location(token.location);
        if (!text_ || token.begin == token.end) {
            return place;
        }
        const std::u32string_view codes(input_.codes.data() + token.begin, token.end - token.begin);
        const std::array items{pair(terms_.atom("text"_atom), chars(codes)), pair(terms_.atom("location"_atom), place)};
        return terms_.list(items);
    }

    // {error, {Location, erl_scan, Descriptor}, EndLocation, Rest}.
    Term error(const ScanError &failure) const {
        const std::array info{location(failure.start), terms_.atom("erl_scan"_atom), descriptor(failure)};
        const std::array items{terms_.atom("error"_atom), terms_.tuple(info), location(failure.stop),
                               rest(failure.rest)};
        return terms_.tuple(items);
    }

    Term descriptor(const ScanError &failure) const {
        switch (failure.kind) {
        case scanning::ErrorKind::illegal:
            return pair(terms_.atom("illegal"_atom), terms_.atom(failure.what));
        case scanning::ErrorKind::base:
            return pair(terms_.atom("base"_atom), integer(failure.digits, 10));
        case scanning::ErrorKind::unterminated_char:
            return pair(terms_.atom("unterminated"_atom), terms_.atom("char"_atom));
        case scanning::ErrorKind::unterminated:
            return triple("unterminated"_atom, unterminated(failure),
                          string::prefix_of(terms_.context(), chars(failure.head), 16));
        case scanning::ErrorKind::indentation:
            return terms_.atom("indentation"_atom);
        case scanning::ErrorKind::white_space:
            return terms_.atom("white_space"_atom);
        case scanning::ErrorKind::string_concat:
            return terms_.atom("string_concat"_atom);
        }
        return terms_.nil();
    }

    // What was unterminated: atom, string, {string, N}, {sigil, Name, Q1, Q2} or {sigil, Name, N}.
    Term unterminated(const ScanError &failure) const {
        switch (failure.unterminated) {
        case scanning::Unterminated::atom:
            return terms_.atom("atom"_atom);
        case scanning::Unterminated::string:
            return terms_.atom("string"_atom);
        case scanning::Unterminated::triple:
            return pair(terms_.atom("string"_atom), terms_.integer(failure.quotes));
        case scanning::Unterminated::sigil: {
            const std::array items{terms_.atom("sigil"_atom), atom(failure.sigil), terms_.integer(failure.open),
                                   terms_.integer(failure.close)};
            return terms_.tuple(items);
        }
        case scanning::Unterminated::triple_sigil:
            return triple("sigil"_atom, atom(failure.sigil), terms_.integer(failure.quotes));
        }
        return terms_.nil();
    }

    // The input from `index` on; none is OTP's eof.
    Term rest(std::optional<std::size_t> index) const {
        if (!index) {
            return terms_.atom("eof"_atom);
        }
        return *index < input_.cells.size() ? input_.cells[*index] : input_.tail;
    }

    Term location(const Location &place) const {
        if (!place.column) {
            return terms_.integer(place.line);
        }
        return pair(terms_.integer(place.line), terms_.integer(*place.column));
    }

    Term integer(const std::string &digits, unsigned base) const {
        const std::vector<unsigned char> text(digits.begin(), digits.end());
        return need(integer_term(terms_.context(), text, base));
    }

    Term chars(std::u32string_view codes) const { return need(char_list(terms_.context(), codes)); }

    Term atom(std::u32string_view name) const { return terms_.atom(utf8(name)); }

    Term pair(const Term &first, const Term &second) const {
        const std::array items{first, second};
        return terms_.tuple(items);
    }

    Term triple(AtomName tag, const Term &second, const Term &third) const {
        const std::array items{terms_.atom(tag), second, third};
        return terms_.tuple(items);
    }

    Terms terms_;
    const Input &input_;
    bool text_;
};

bool flag(const Term &term) {
    if (!term.is_boolean()) {
        bad_argument();
    }
    return is_atom(term, "true");
}

// The keywords of enabled features ({..., Keywords}), or `custom` for a reserved_word_fun.
void read_reserved(const Term &reserved, scanning::Options &options) {
    if (is_atom(reserved, "custom")) {
        options.custom_reserved = true;
        return;
    }
    for (auto rest = reserved; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto word = rest.head_unchecked();
        if (!word.is_atom()) {
            bad_argument();
        }
        options.keywords.push_back(code_points(word.atom_spelling_unchecked()));
    }
}

// Flags: {WhiteSpace, Comments, Text, Reserved}.
scanning::Options read_options(const TupleArgument &flags, bool eof, bool whole) {
    if (flags.elements.size() != 4) {
        bad_argument();
    }
    scanning::Options options;
    options.white_space = flag(flags.elements[0]);
    options.comments = flag(flags.elements[1]);
    options.text = flag(flags.elements[2]);
    read_reserved(flags.elements[3], options);
    options.whole_input = whole;
    options.eof = eof;
    return options;
}

Location read_location(const LineArg &line, const ColumnArg &column) {
    const auto row = string::small_of(line.term);
    if (!row) {
        bad_argument();
    }
    if (is_atom(column.term, "no_col")) {
        return {.line = *row, .column = std::nullopt};
    }
    const auto at = string::small_of(column.term);
    if (!at) {
        bad_argument();
    }
    return {.line = *row, .column = *at};
}

// error({not_character, C}).
[[noreturn]] void not_character(ProcessContext &context, const Term &element) {
    const Terms terms(context);
    const std::array items{terms.atom("not_character"_atom), element};
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error, .payload = terms.tuple(items).word()};
}

// scan(Chars, Eof, Line, Column, Flags, Whole): {ok, Tokens, Rest, Line, Column} at a form's dot (or the end of the
// input with eof), {more, Tokens, Rest, Line, Column} with the rest to scan again, or {error, ErrorInfo, EndLocation,
// Rest}.
Term scan(ProcessContext &context, const CharsArg &chars, const EofArg &eof, const LineArg &line,
          const ColumnArg &column, const TupleArgument &flags, const WholeArg &whole) {
    const auto input = read_input(chars.term, flag(eof.term));
    const auto options = read_options(flags, input.eof, flag(whole.term));
    try {
        const auto outcome = scanning::scan(input.codes, options, read_location(line, column));
        return Builder(context, input, options.text).outcome(outcome);
    } catch (const scanning::NotCharacter &failure) {
        not_character(context, input.cells[failure.index].head_unchecked());
    }
}

constexpr std::array ERL_SCAN_BUILTINS{
    typed_entry<scan>("clause_erl_scan", "scan"),
};
} // namespace
} // namespace clause::runtime::builtins::front_end

namespace clause::runtime {
std::span<const BuiltinEntry> erl_scan_builtins() noexcept { return builtins::front_end::ERL_SCAN_BUILTINS; }
} // namespace clause::runtime
