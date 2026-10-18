#include "../../terms/structural_order.hpp"
#include "../erlang/conversion_text.hpp"
#include "../lists/list_walk.hpp"
#include "../string/string.hpp"
#include <array>
#include <utility>

// io_lib:fread/2,3 (plan 21 step LIB4E, docs/builtins.md#io-lib): OTP's format-directed scanner clause for clause,
// over the line as a list term, so rests are tails of the input and malformed input gets OTP's error terms.
namespace clause::runtime::builtins::io_lib {
namespace {
using lists::no_clause;
using string::is_char;
using string::small_of;
using string::Terms;

// The roles of fread's arguments (typed.hpp's Named).
struct FormatRole;
struct LineRole;
struct ContinuationRole;
using FormatArg = Named<FormatRole>;
using LineArg = Named<LineRole>;
// fread/3's continuation: [] or OTP's {Format, Stack, N, Results}.
using ContinuationArg = Named<ContinuationRole>;

// OTP's ?is_whitespace(C): space, tab, CR or LF.
bool is_white(const Term &term) {
    return is_char(term, ' ') || is_char(term, '\t') || is_char(term, '\r') || is_char(term, '\n');
}

bool head_is(const Term &list, char code) { return list.is_cons() && is_char(list.head_unchecked(), code); }

// A list whose head is an integer in [low, high] (OTP's is_integer(C, Low, High)).
bool head_in(const Term &list, std::int64_t low, std::int64_t high) {
    if (!list.is_cons()) {
        return false;
    }
    const auto code = small_of(list.head_unchecked());
    return code && *code >= low && *code <= high;
}

// The value of a decimal digit at the head of a list.
std::optional<std::int64_t> digit_head(const Term &list) {
    const auto code = list.is_cons() ? small_of(list.head_unchecked()) : std::nullopt;
    if (!code || *code < '0' || *code > '9') {
        return std::nullopt;
    }
    return *code - '0';
}

bool is_eof(const Term &term) { return term.is_atom() && term.atom_spelling_unchecked() == "eof"; }

// The prefix of one field: a width, ~* suppression and ~t Unicode.
struct Field {
    std::optional<std::int64_t> width;
    bool suppress = false;
    bool unicode = false;
};

// The characters one field collected, in input order; `failed` is OTP's `error` in their place.
struct Collected {
    std::vector<Term> chars;
    bool failed = false;
};

// Characters with leading whitespace dropped (OTP's fread_skip_white/1 on the field's characters).
std::span<const Term> skip_white(std::span<const Term> chars) {
    while (!chars.empty() && is_white(chars.front())) {
        chars = chars.subspan(1);
    }
    return chars;
}

// Characters with whitespace dropped at both ends (fread_string's double skip).
std::span<const Term> trim_white(std::span<const Term> chars) {
    chars = skip_white(chars);
    while (!chars.empty() && is_white(chars.back())) {
        chars = chars.first(chars.size() - 1);
    }
    return chars;
}

// The bytes of a list of Latin-1 characters; badarg for anything else, as list_to_integer and list_to_float raise.
std::vector<unsigned char> latin1_text(std::span<const Term> chars) {
    std::vector<unsigned char> text;
    for (const auto &term : chars) {
        const auto code = small_of(term);
        if (!code || *code < 0 || *code > 255) {
            bad_argument();
        }
        text.push_back(static_cast<unsigned char>(*code));
    }
    return text;
}

// What fread/4 ended with: {ok, Results, Rest}, {more, Format, N, Results}, eof or {error, {fread, What}}.
struct Outcome {
    enum class Kind : std::uint8_t { ok, more, eof, error };
    Kind kind = Kind::ok;
    // ok: the results in order; more: OTP's stack of results, most recent first.
    Term results;
    // ok: the rest of the line; more: the format still to scan.
    Term rest;
    std::int64_t count = 0;
    std::string_view error;

    static Outcome ok(const Term &results, const Term &rest) { return made(Kind::ok, results, rest); }

    static Outcome more(const Term &stack, const Term &format, std::int64_t count) {
        auto outcome = made(Kind::more, stack, format);
        outcome.count = count;
        return outcome;
    }

    static Outcome end() { return made(Kind::eof, {}, {}); }

    static Outcome failure(std::string_view what) {
        auto outcome = made(Kind::error, {}, {});
        outcome.error = what;
        return outcome;
    }

  private:
    static Outcome made(Kind kind, const Term &results, const Term &rest) {
        Outcome outcome;
        outcome.kind = kind;
        outcome.results = results;
        outcome.rest = rest;
        return outcome;
    }
};

// fread/4 over one line: the format and the line as cursors, the characters read and the results so far.
class Scanner final {
  public:
    Scanner(ProcessContext &context, const FormatArg &format, const LineArg &line)
        : terms_(context), format_(format.term), line_(line.term) {}

    // Resume where a continuation stopped: `count` characters read and `results` collected, most recent first.
    void resume(std::int64_t count, const Term &results) {
        count_ = count;
        results_ = results;
    }

    // Scan to the end of the format or the line. Inside OTP's ~# try every later error becomes {fread, based}.
    Outcome run() {
        try {
            for (;;) {
                if (auto outcome = step()) {
                    return *outcome;
                }
            }
        } catch (const BuiltinFailure &failure) {
            if (!in_based_ || failure.term) {
                throw;
            }
            return error("based");
        }
    }

    const Terms &terms() const { return terms_; }

  private:
    using Step = std::optional<Outcome>;
    using Control = Step (Scanner::*)(const Field &);

    static Outcome error(std::string_view what) { return Outcome::failure(what); }

    // One clause of fread/4.
    Step step() {
        if (format_.is_nil()) {
            return finish();
        }
        if (!format_.is_cons()) {
            no_clause();
        }
        const auto code = format_.head_unchecked();
        if (is_char(code, '~')) {
            return field();
        }
        if (is_white(code)) {
            format_ = format_.tail_unchecked();
            skip_line_white();
            return std::nullopt;
        }
        return literal(code);
    }

    // A format character other than ~ and whitespace must be the next character of the line.
    Step literal(const Term &code) {
        if (!line_.is_cons()) {
            return line_ended(format_);
        }
        if (!code.is_integer() || !string::same(code, line_.head_unchecked())) {
            return error("input");
        }
        format_ = format_.tail_unchecked();
        advance();
        return std::nullopt;
    }

    // fread([], Line, N, Results): the line must end or go on with a character.
    Step finish() const {
        if (line_.is_nil() || (line_.is_cons() && line_.head_unchecked().is_integer())) {
            return Outcome::ok(reversed(results_), line_);
        }
        return error("input");
    }

    // The line ended before `format` did: more for [], eof when nothing was read from an eof line.
    Step line_ended(const Term &format) const {
        if (line_.is_nil()) {
            return Outcome::more(results_, format, count_);
        }
        if (!is_eof(line_)) {
            no_clause();
        }
        if (count_ == 0 && results_.is_nil()) {
            return Outcome::end();
        }
        return error("input");
    }

    // ~ starts a field: its prefix, then ~l, the end of the line, or one control.
    Step field() {
        const auto all = format_;
        format_ = format_.tail_unchecked();
        const auto prefix = field_prefix();
        if (head_is(format_, 'l')) {
            format_ = format_.tail_unchecked();
            push(prefix, terms_.integer(count_));
            return std::nullopt;
        }
        if (line_.is_nil() || is_eof(line_)) {
            return line_ended(all);
        }
        return control(prefix);
    }

    // fread_field: an optional *, digits of a width, an optional t.
    Field field_prefix() {
        Field prefix;
        if (head_is(format_, '*')) {
            prefix.suppress = true;
            format_ = format_.tail_unchecked();
        }
        while (const auto digit = digit_head(format_)) {
            prefix.width = prefix.width.value_or(0) * 10 + *digit;
            format_ = format_.tail_unchecked();
        }
        if (head_is(format_, 't')) {
            prefix.unicode = true;
            format_ = format_.tail_unchecked();
        }
        return prefix;
    }

    // fread1/7: the control character picks the field reader; an unknown one is a format error.
    Step control(const Field &prefix) {
        static constexpr std::array<std::pair<char, Control>, 9> CONTROLS{{
            {'f', &Scanner::float_field},
            {'d', &Scanner::integer_field},
            {'u', &Scanner::unsigned_field},
            {'-', &Scanner::sign_field},
            {'#', &Scanner::based_field},
            {'s', &Scanner::string_field},
            {'a', &Scanner::atom_field},
            {'c', &Scanner::chars_field},
            {'~', &Scanner::tilde_field},
        }};
        if (format_.is_cons()) {
            for (const auto &[code, reader] : CONTROLS) {
                if (is_char(format_.head_unchecked(), code)) {
                    format_ = format_.tail_unchecked();
                    return (this->*reader)(prefix);
                }
            }
        }
        return error("format");
    }

    // ~f: a float, OTP's float characters or `width` characters.
    Step float_field(const Field &prefix) {
        if (prefix.unicode) {
            return error("format");
        }
        const auto chars = prefix.width ? fixed(*prefix.width, false) : float_chars();
        if (chars.failed) {
            return error("float");
        }
        try {
            const auto value = float_of_text(latin1_text(skip_white(chars.chars)));
            return pushed(prefix, need(TermFactory(terms_.context()).floating(value)));
        } catch (const BuiltinFailure &) {
            return error("float");
        }
    }

    // ~d: a decimal integer with an optional sign.
    Step integer_field(const Field &prefix) {
        if (prefix.unicode) {
            return error("format");
        }
        auto chars = prefix.width ? fixed(*prefix.width, false) : integer_chars();
        return number(prefix, chars, 10, "integer");
    }

    // ~u: digits of base 10, or of the base the width gives (2 to 36); no whitespace is skipped.
    Step unsigned_field(const Field &prefix) {
        const auto base = prefix.width.value_or(10);
        if (prefix.unicode || base < 2 || base > 36) {
            return error("format");
        }
        auto chars = digits(base);
        return number(prefix, chars, static_cast<unsigned>(base), "unsigned");
    }

    // list_to_integer(Chars, Base) after leading whitespace, its failure reported as `what`.
    Step number(const Field &prefix, const Collected &chars, unsigned base, std::string_view what) {
        if (chars.failed) {
            return error(what);
        }
        try {
            return pushed(prefix, need(integer_term(terms_.context(), latin1_text(skip_white(chars.chars)), base)));
        } catch (const BuiltinFailure &) {
            return error(what);
        }
    }

    // ~-: -1 or +1 for a sign character, which it consumes, else 1.
    Step sign_field(const Field &prefix) {
        if (prefix.unicode) {
            return error("format");
        }
        auto sign = 1;
        if (head_is(line_, '-') || head_is(line_, '+')) {
            sign = head_is(line_, '-') ? -1 : 1;
            advance();
        }
        return pushed(prefix, terms_.integer(sign));
    }

    // ~#: Base#Digits. OTP scans it inside a try that also covers the rest of the format, so every error from here on
    // is {fread, based}.
    Step based_field(const Field &prefix) {
        if (prefix.unicode) {
            return error("format");
        }
        in_based_ = true;
        if (!prefix.width) {
            const auto base = based_base();
            const auto magnitude = base < 0 ? -base : base;
            valid_base(magnitude);
            const auto chars = digits(magnitude);
            return pushed(prefix, based_value(signed_digits(base < 0, chars.chars), magnitude));
        }
        const auto field = fixed(*prefix.width, false);
        if (field.failed) {
            bad_argument();
        }
        Scanner inner(terms_.context(), {format_}, {terms_.list(field.chars)});
        const auto base = inner.based_base();
        valid_base(base);
        return pushed(prefix, based_value(inner.rest_chars(), base));
    }

    // fread_base: an integer, then # (else badmatch, caught as based); the base, with the line after #.
    std::int64_t based_base() {
        const auto chars = integer_chars();
        if (!head_is(line_, '#')) {
            bad_argument();
        }
        advance();
        const auto base = small_of(need(integer_term(terms_.context(), latin1_text(chars.chars), 10)));
        if (!base) {
            bad_argument();
        }
        return *base;
    }

    static void valid_base(std::int64_t base) {
        if (base < 2 || base > 36) {
            bad_argument();
        }
    }

    // The digits of a negative base follow a minus sign.
    std::vector<Term> signed_digits(bool negative, std::vector<Term> chars) const {
        if (negative) {
            chars.insert(chars.begin(), terms_.integer('-'));
        }
        return chars;
    }

    // The elements of the line left over, which must be a proper list.
    std::vector<Term> rest_chars() const {
        std::vector<Term> chars;
        auto rest = line_;
        for (; rest.is_cons(); rest = rest.tail_unchecked()) {
            chars.push_back(rest.head_unchecked());
        }
        if (!rest.is_nil()) {
            bad_argument();
        }
        return chars;
    }

    Term based_value(std::span<const Term> chars, std::int64_t base) const {
        return need(integer_term(terms_.context(), latin1_text(chars), static_cast<unsigned>(base)));
    }

    // ~s: characters up to whitespace (Latin-1 unless ~t), or `width` characters, trimmed.
    Step string_field(const Field &prefix) {
        const auto chars = prefix.width ? fixed(*prefix.width, prefix.unicode) : word(prefix.unicode);
        if (chars.failed) {
            return error("string");
        }
        return pushed(prefix, terms_.list(trim_white(chars.chars)));
    }

    // ~a: as ~s, then list_to_atom; a width allows Latin-1 only.
    Step atom_field(const Field &prefix) {
        if (prefix.width && prefix.unicode) {
            return error("format");
        }
        const auto chars = prefix.width ? fixed(*prefix.width, false) : word(prefix.unicode);
        if (chars.failed) {
            return error("atom");
        }
        const auto name = atom_name(terms_.list(trim_white(chars.chars)));
        if (!name) {
            name_failure(name.error());
        }
        return pushed(prefix, terms_.atom(*name));
    }

    // ~c: one character, or `width`, whitespace included.
    Step chars_field(const Field &prefix) {
        const auto chars = fixed(prefix.width.value_or(1), prefix.unicode);
        if (chars.failed) {
            return error("character");
        }
        return pushed(prefix, terms_.list(chars.chars));
    }

    // ~~: a ~ in the line.
    Step tilde_field(const Field & /*prefix*/) {
        if (!head_is(line_, '~')) {
            return error("format");
        }
        advance();
        return std::nullopt;
    }

    // fread_chars/3: `count` characters, failing at a newline or a non-character; the line moves either way.
    Collected fixed(std::int64_t count, bool unicode) {
        Collected chars;
        for (; count > 0; --count) {
            const bool fits = unicode ? line_.is_cons() && line_.head_unchecked().is_integer() : head_in(line_, 0, 255);
            if (head_is(line_, '\n') || !fits) {
                chars.failed = true;
                return chars;
            }
            chars.chars.push_back(line_.head_unchecked());
            line_ = line_.tail_unchecked();
        }
        count_ += static_cast<std::int64_t>(chars.chars.size());
        return chars;
    }

    // fread_int_cs: whitespace, an optional sign and decimal digits.
    Collected integer_chars() {
        skip_line_white();
        Collected chars;
        take_sign(chars);
        take_digits(chars, 10);
        return chars;
    }

    // fread_float_cs: as an integer, then . and digits, then e or E, a sign and digits.
    Collected float_chars() {
        auto chars = integer_chars();
        if (!head_is(line_, '.')) {
            return chars;
        }
        take(chars);
        take_digits(chars, 10);
        if (head_is(line_, 'e') || head_is(line_, 'E')) {
            take(chars);
            take_sign(chars);
            take_digits(chars, 10);
        }
        return chars;
    }

    // fread_digits/4: digits of `base`, letters in either case.
    Collected digits(std::int64_t base) {
        Collected chars;
        take_digits(chars, base);
        return chars;
    }

    // fread_string_cs: whitespace, then the characters up to the next whitespace.
    Collected word(bool unicode) {
        skip_line_white();
        Collected chars;
        while (line_.is_cons() && !is_white(line_.head_unchecked())) {
            const auto code = line_.head_unchecked();
            if (!unicode && above_latin1(code)) {
                chars.failed = chars.chars.empty();
                return chars;
            }
            if (!code.is_integer()) {
                chars.failed = true;
                return chars;
            }
            take(chars);
        }
        if (!line_.is_cons() && !line_.is_nil()) {
            no_clause();
        }
        return chars;
    }

    // OTP's guard C > 255 in Erlang's term order: atoms, tuples and lists are above every number.
    bool above_latin1(const Term &code) const { return need(detail::structural_order(code, terms_.integer(255))) > 0; }

    void take_sign(Collected &chars) {
        if (head_is(line_, '+') || head_is(line_, '-')) {
            take(chars);
        }
    }

    void take_digits(Collected &chars, std::int64_t base) {
        while (head_in(line_, '0', '9') || head_in(line_, 'A', base + ('A' - 11)) ||
               head_in(line_, 'a', base + ('a' - 11))) {
            take(chars);
        }
    }

    void take(Collected &chars) {
        chars.chars.push_back(line_.head_unchecked());
        advance();
    }

    void skip_line_white() {
        while (line_.is_cons() && is_white(line_.head_unchecked())) {
            advance();
        }
    }

    void advance() {
        line_ = line_.tail_unchecked();
        ++count_;
    }

    void push(const Field &prefix, const Term &value) {
        if (!prefix.suppress) {
            results_ = terms_.cons(value, results_);
        }
    }

    Step pushed(const Field &prefix, const Term &value) {
        push(prefix, value);
        return std::nullopt;
    }

    Term reversed(const Term &stack) const {
        std::vector<Term> items;
        for (auto rest = stack; rest.is_cons(); rest = rest.tail_unchecked()) {
            items.push_back(rest.head_unchecked());
        }
        std::ranges::reverse(items);
        return terms_.list(items);
    }

    Terms terms_;
    // The format still to scan and the line still to read.
    Term format_;
    Term line_;
    // Characters read so far (OTP's N, ~l's value).
    std::int64_t count_ = 0;
    // The results so far, most recent first.
    Term results_ = terms_.nil();
    // Set once a ~# field starts: OTP's try around it covers the rest of the scan.
    bool in_based_ = false;
};

Term fread_error(const Terms &terms, std::string_view what) {
    const std::array inner{terms.atom("fread"), terms.atom(what)};
    const std::array outer{terms.atom("error"), terms.tuple(inner)};
    return terms.tuple(outer);
}

// fread/4's result as OTP's term.
Term outcome_term(const Terms &terms, const Outcome &outcome) {
    switch (outcome.kind) {
    case Outcome::Kind::ok: {
        const std::array items{terms.atom("ok"), outcome.results, outcome.rest};
        return terms.tuple(items);
    }
    case Outcome::Kind::more: {
        const std::array items{terms.atom("more"), outcome.rest, terms.integer(outcome.count), outcome.results};
        return terms.tuple(items);
    }
    case Outcome::Kind::eof:
        return terms.atom("eof");
    case Outcome::Kind::error:
        break;
    }
    return fread_error(terms, outcome.error);
}

// fread(Format, String): {ok, Results, Rest}, {more, RestFormat, N, Stack}, eof or {error, {fread, What}}.
Term fread2(ProcessContext &context, const FormatArg &format, const LineArg &line) {
    Scanner scanner(context, format, line);
    return outcome_term(scanner.terms(), scanner.run());
}

// fread/3's collection of one line at a time: OTP's {Format, Stack, N, Results} with the stack in input order.
class Collector final {
  public:
    explicit Collector(ProcessContext &context) : terms_(context) {}

    // Start from fread/3's continuation argument: [] or {Format, Stack, N, Results}.
    void start(const ContinuationArg &continuation, const FormatArg &format) {
        const auto &state = continuation.term;
        if (state.is_nil()) {
            format_ = format.term;
            return;
        }
        if (!state.is_tuple() || state.tuple_size_unchecked() != 4) {
            no_clause();
        }
        format_ = state.tuple_element_unchecked(0);
        stack_ = elements(state.tuple_element_unchecked(1));
        std::ranges::reverse(stack_);
        const auto count = small_of(state.tuple_element_unchecked(2));
        if (!count) {
            bad_argument();
        }
        count_ = *count;
        results_ = state.tuple_element_unchecked(3);
    }

    // fread_collect: gather characters up to a newline (LF, CRLF or a CR the input ends after), then scan the line.
    Term collect(Term chars) {
        for (;;) {
            if (auto done = collect_step(chars)) {
                return *done;
            }
        }
    }

  private:
    // One clause of fread_collect; `chars` moves past what it consumed.
    std::optional<Term> collect_step(Term &chars) {
        const bool after_cr = !stack_.empty() && is_char(stack_.back(), '\r');
        if (head_is(chars, '\n')) {
            drop_cr(after_cr);
            chars = chars.tail_unchecked();
            return line(chars, after_cr ? "\r\n" : "\n");
        }
        if (chars.is_nil()) {
            return more();
        }
        if (after_cr || !chars.is_cons()) {
            drop_cr(after_cr);
            return line(chars, after_cr ? "\r" : "");
        }
        stack_.push_back(chars.head_unchecked());
        chars = chars.tail_unchecked();
        return std::nullopt;
    }

    void drop_cr(bool after_cr) {
        if (after_cr) {
            stack_.pop_back();
        }
    }

    // fread_line: scan the gathered line; done, or reset to collect the rest of the format from `more`.
    std::optional<Term> line(const Term &more, std::string_view newline) {
        auto outcome = scan(terms_.list(stack_));
        stack_.clear();
        if (outcome.kind != Outcome::Kind::more) {
            return done(outcome, more, newline);
        }
        continue_with(outcome, newline);
        if (more.is_list()) {
            return std::nullopt;
        }
        // The input ended: the rest of the format meets eof, with no newline.
        return done(scan(terms_.atom("eof")), more, "");
    }

    Outcome scan(const Term &line) const {
        Scanner scanner(terms_.context(), {format_}, {line});
        scanner.resume(count_, results_);
        return scanner.run();
    }

    void continue_with(const Outcome &outcome, std::string_view newline) {
        format_ = outcome.rest;
        count_ = outcome.count + static_cast<std::int64_t>(newline.size());
        results_ = outcome.results;
    }

    // {done, Result, LeftOver}: the rest of the line, the newline and what followed it after a result.
    Term done(const Outcome &outcome, const Term &more, std::string_view newline) const {
        if (outcome.kind != Outcome::Kind::ok) {
            return tagged_done(outcome_result(outcome), more);
        }
        const auto chars = more.is_list() ? more : terms_.nil();
        if (outcome.rest.is_nil()) {
            return tagged_done(outcome_result(outcome), chars);
        }
        auto left = elements(outcome.rest);
        for (const auto code : newline) {
            left.push_back(terms_.integer(code));
        }
        return tagged_done(outcome_result(outcome), terms_.list(left, chars));
    }

    // {ok, Results}, eof or {error, {fread, What}}.
    Term outcome_result(const Outcome &outcome) const {
        if (outcome.kind == Outcome::Kind::ok) {
            const std::array items{terms_.atom("ok"), outcome.results};
            return terms_.tuple(items);
        }
        return outcome_term(terms_, outcome);
    }

    Term tagged_done(const Term &result, const Term &left) const {
        const std::array items{terms_.atom("done"), result, left};
        return terms_.tuple(items);
    }

    // {more, {Format, Stack, N, Results}} with OTP's stack, most recent character first.
    Term more() const {
        std::vector<Term> stack(stack_.rbegin(), stack_.rend());
        const std::array state{format_, terms_.list(stack), terms_.integer(count_), results_};
        const std::array items{terms_.atom("more"), terms_.tuple(state)};
        return terms_.tuple(items);
    }

    // The elements of a proper list; badarg for an improper one, as lists:reverse/1 raises.
    static std::vector<Term> elements(const Term &list) {
        std::vector<Term> items;
        auto rest = list;
        for (; rest.is_cons(); rest = rest.tail_unchecked()) {
            items.push_back(rest.head_unchecked());
        }
        if (!rest.is_nil()) {
            bad_argument();
        }
        return items;
    }

    Terms terms_;
    Term format_ = terms_.nil();
    // The characters of the current line so far, in input order.
    std::vector<Term> stack_;
    std::int64_t count_ = 0;
    Term results_ = terms_.nil();
};

// fread(Continuation, CharSpec, Format): {more, Continuation1} or {done, Result, LeftOver}.
Term fread3(ProcessContext &context, const ContinuationArg &continuation, const LineArg &chars,
            const FormatArg &format) {
    Collector collector(context);
    collector.start(continuation, format);
    return collector.collect(chars.term);
}

constexpr std::array IO_LIB_FREAD_BUILTINS{
    typed_entry<fread2>("io_lib", "fread"),
    typed_entry<fread3>("io_lib", "fread"),
};
} // namespace
} // namespace clause::runtime::builtins::io_lib

namespace clause::runtime {
std::span<const BuiltinEntry> io_lib_fread_builtins() noexcept { return builtins::io_lib::IO_LIB_FREAD_BUILTINS; }
} // namespace clause::runtime
