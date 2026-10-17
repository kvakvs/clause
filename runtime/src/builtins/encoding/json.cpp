#include "../../terms/bitstrings.hpp"
#include "../../terms/term_layout.hpp"
#include "../collections/collections.hpp"
#include "../erlang/conversion_text.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The json builtins (plan 21 step LIB5E, docs/builtins.md#encodings): decode/1, the two string escapers and the
// string and number scanners json.erl's callback decoders walk with. They follow OTP's decoding and escaping walks
// byte for byte, so values, the iodata the escapers build and the errors (unexpected_end, {invalid_byte, Byte},
// {unexpected_sequence, Bytes}) agree with OTP's for any input.
namespace clause::runtime::builtins::encoding {
namespace {
using collections::no_clause;

// A decoder's or escaper's input: a binary's bytes.
using Bytes = std::span<const std::byte>;

std::uint8_t byte_at(Bytes bytes, std::size_t at) { return std::to_integer<std::uint8_t>(bytes[at]); }

bool is_space(std::uint8_t byte) { return byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n'; }

bool is_digit(std::uint8_t byte) { return byte >= '0' && byte <= '9'; }

// ASCII a string holds as it is: printable characters but the quote and the backslash.
bool is_plain(std::uint8_t byte) { return byte >= 0x20 && byte < 0x80 && byte != '"' && byte != '\\'; }

// ASCII a string escapes: control characters, the quote and the backslash.
bool is_escaped(std::uint8_t byte) { return byte < 0x20 || byte == '"' || byte == '\\'; }

// A binary argument's bytes: read in place when they live off the heap, else copied, so that building terms (which
// may move the heap) cannot leave them dangling.
class Input final {
  public:
    explicit Input(const Term &binary) {
        const auto bytes = detail::trusted_binary_bytes(binary.word());
        if (!bytes) {
            bad_argument();
        }
        if (bytes->size() > detail::layout::heap_binary_bytes) {
            bytes_ = *bytes;
        } else {
            copy_.assign(bytes->begin(), bytes->end());
            bytes_ = copy_;
        }
    }

    Input(const Input &) = delete;
    Input &operator=(const Input &) = delete;
    Input(Input &&) = delete;
    Input &operator=(Input &&) = delete;
    ~Input() = default;

    Bytes bytes() const { return bytes_; }

  private:
    // The bytes of a heap binary.
    std::vector<std::byte> copy_;
    Bytes bytes_;
};

std::string_view text_of(Bytes bytes) { return {reinterpret_cast<const char *>(bytes.data()), bytes.size()}; }

[[noreturn]] void raise_reason(const Term &reason) {
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error, .payload = reason.word()};
}

// error({invalid_byte, Byte}) for the byte at `at`.
[[noreturn]] void invalid_byte(ProcessContext &context, Bytes bytes, std::size_t at) {
    TermFactory factory(context);
    const std::array reason{need(factory.atom("invalid_byte")), need(factory.integer(byte_at(bytes, at)))};
    raise_reason(need(factory.tuple(reason)));
}

[[noreturn]] void unexpected_end(ProcessContext &context) {
    raise_reason(need(TermFactory(context).atom("unexpected_end")));
}

// error({unexpected_sequence, Bytes}): an escape or number text OTP cannot turn into a value.
[[noreturn]] void unexpected_sequence(ProcessContext &context, Bytes sequence) {
    TermFactory factory(context);
    const std::array reason{need(factory.atom("unexpected_sequence")), need(factory.binary(sequence))};
    raise_reason(need(factory.tuple(reason)));
}

// The bytes a UTF-8 lead byte in `first..last` starts: the range of the second byte and the sequence length, as
// Hoehrmann's automaton OTP uses accepts them (no overlong forms, surrogates or code points past 10FFFF).
struct Lead {
    std::uint8_t first;
    std::uint8_t last;
    std::uint8_t low;
    std::uint8_t high;
    std::size_t length;
};

constexpr std::array LEADS{
    Lead{.first = 0xC2, .last = 0xDF, .low = 0x80, .high = 0xBF, .length = 2},
    Lead{.first = 0xE0, .last = 0xE0, .low = 0xA0, .high = 0xBF, .length = 3},
    Lead{.first = 0xE1, .last = 0xEC, .low = 0x80, .high = 0xBF, .length = 3},
    Lead{.first = 0xED, .last = 0xED, .low = 0x80, .high = 0x9F, .length = 3},
    Lead{.first = 0xEE, .last = 0xEF, .low = 0x80, .high = 0xBF, .length = 3},
    Lead{.first = 0xF0, .last = 0xF0, .low = 0x90, .high = 0xBF, .length = 4},
    Lead{.first = 0xF1, .last = 0xF3, .low = 0x80, .high = 0xBF, .length = 4},
    Lead{.first = 0xF4, .last = 0xF4, .low = 0x80, .high = 0x8F, .length = 4},
};

// One UTF-8 sequence: valid with its length, rejected at the byte the automaton fails on, or cut by the input's end.
struct Utf8 {
    enum class Kind : std::uint8_t { valid, rejected, truncated };
    Kind kind;
    // The sequence's length, or the position of the rejected byte.
    std::size_t at;
};

// Whether `byte` may stand at `index` (1 to 3) of a sequence `lead` starts.
bool continues(const Lead &lead, std::size_t index, std::uint8_t byte) {
    return index == 1 ? lead.low <= byte && byte <= lead.high : 0x80 <= byte && byte <= 0xBF;
}

Utf8 utf8_at(Bytes bytes, std::size_t at) {
    const auto lead = std::ranges::find_if(
        LEADS, [byte = byte_at(bytes, at)](const Lead &lead) { return lead.first <= byte && byte <= lead.last; });
    if (lead == LEADS.end()) {
        return {.kind = Utf8::Kind::rejected, .at = at};
    }
    for (std::size_t index = 1; index < lead->length; ++index) {
        if (at + index == bytes.size()) {
            return {.kind = Utf8::Kind::truncated, .at = at + index};
        }
        if (!continues(*lead, index, byte_at(bytes, at + index))) {
            return {.kind = Utf8::Kind::rejected, .at = at + index};
        }
    }
    return {.kind = Utf8::Kind::valid, .at = lead->length};
}

// The code point of a valid sequence.
char32_t code_point(Bytes bytes, std::size_t at, std::size_t length) {
    constexpr std::array<unsigned, 5> LEAD_BITS{0, 0x7F, 0x1F, 0x0F, 0x07};
    char32_t code = byte_at(bytes, at) & LEAD_BITS.at(length);
    for (std::size_t index = 1; index < length; ++index) {
        code = (code << 6U) | (byte_at(bytes, at + index) & 0x3FU);
    }
    return code;
}

void append_utf8(std::string &text, char32_t code) {
    if (code < 0x80) {
        text.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
        text.push_back(static_cast<char>(0xC0 | (code >> 6U)));
        text.push_back(static_cast<char>(0x80 | (code & 0x3FU)));
    } else if (code < 0x10000) {
        text.push_back(static_cast<char>(0xE0 | (code >> 12U)));
        text.push_back(static_cast<char>(0x80 | ((code >> 6U) & 0x3FU)));
        text.push_back(static_cast<char>(0x80 | (code & 0x3FU)));
    } else {
        text.push_back(static_cast<char>(0xF0 | (code >> 18U)));
        text.push_back(static_cast<char>(0x80 | ((code >> 12U) & 0x3FU)));
        text.push_back(static_cast<char>(0x80 | ((code >> 6U) & 0x3FU)));
        text.push_back(static_cast<char>(0x80 | (code & 0x3FU)));
    }
}

// OTP's `unexpected`: the walk stopped `pos` bytes past `skip` and needs `len` more; if the input ends before that
// it may continue with more input (true), else the byte at skip + pos is invalid.
bool needs_more(ProcessContext &context, Bytes bytes, std::size_t skip, std::size_t pos, std::size_t len) {
    if (bytes.size() <= skip + pos + len) {
        return true;
    }
    invalid_byte(context, bytes, skip + pos);
}

// The value of four hex digits at `at`; none if one is not a hex digit.
std::optional<char32_t> hex4(Bytes bytes, std::size_t at) {
    char32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        const auto byte = byte_at(bytes, at + index);
        const auto lower = static_cast<std::uint8_t>(byte | 0x20U);
        if (!is_digit(byte) && (lower < 'a' || lower > 'f')) {
            return std::nullopt;
        }
        value = value * 16 + (is_digit(byte) ? byte - '0' : lower - 'a' + 10);
    }
    return value;
}

// A JSON string's walk from the byte after its opening quote, as OTP's string and unescape states: the unescaped
// text and the position after the closing quote; none where the input ends first (OTP continues from the quote).
class StringScan final {
  public:
    StringScan(ProcessContext &context, Bytes bytes, std::size_t skip) : context_(context), bytes_(bytes), at_(skip) {}

    std::optional<std::pair<std::string, std::size_t>> run() {
        while (at_ < bytes_.size()) {
            const auto byte = byte_at(bytes_, at_);
            if (byte == '"') {
                return std::pair{std::move(text_), at_ + 1};
            }
            if (!(byte == '\\' ? unescape() : character(byte))) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

  private:
    // A character copied as it is; false where the input ends inside its UTF-8 sequence.
    bool character(std::uint8_t byte) {
        if (is_plain(byte)) {
            text_.push_back(static_cast<char>(byte));
            ++at_;
            return true;
        }
        if (is_escaped(byte)) {
            invalid_byte(context_, bytes_, at_);
        }
        const auto sequence = utf8_at(bytes_, at_);
        if (sequence.kind == Utf8::Kind::rejected) {
            invalid_byte(context_, bytes_, sequence.at);
        }
        if (sequence.kind == Utf8::Kind::truncated) {
            return false;
        }
        text_.append(text_of(bytes_.subspan(at_, sequence.at)));
        at_ += sequence.at;
        return true;
    }

    // The escape at the backslash `at_`; false where the input ends inside it.
    bool unescape() {
        if (at_ + 1 == bytes_.size()) {
            return false;
        }
        constexpr std::string_view FROM = "bfnrt\"\\/";
        constexpr std::string_view TO = "\b\f\n\r\t\"\\/";
        const auto byte = byte_at(bytes_, at_ + 1);
        if (byte == 'u') {
            return unicode();
        }
        const auto index = FROM.find(static_cast<char>(byte));
        if (index == std::string_view::npos) {
            invalid_byte(context_, bytes_, at_ + 1);
        }
        text_.push_back(TO[index]);
        at_ += 2;
        return true;
    }

    // \uXXXX, a high surrogate needing its low surrogate escape after it.
    bool unicode() {
        if (bytes_.size() < at_ + 6) {
            return false;
        }
        const auto code = hex4(bytes_, at_ + 2);
        if (!code || (*code >= 0xDC00 && *code <= 0xDFFF)) {
            unexpected_sequence(context_, bytes_.subspan(at_, 6));
        }
        if (*code >= 0xD800 && *code <= 0xDBFF) {
            return surrogate(*code);
        }
        append_utf8(text_, *code);
        at_ += 6;
        return true;
    }

    bool surrogate(char32_t high) {
        const bool escaped =
            bytes_.size() >= at_ + 12 && byte_at(bytes_, at_ + 6) == '\\' && byte_at(bytes_, at_ + 7) == 'u';
        if (!escaped) {
            return !needs_more(context_, bytes_, at_, 6, 5);
        }
        const auto low = hex4(bytes_, at_ + 8);
        if (!low || *low < 0xDC00 || *low > 0xDFFF) {
            unexpected_sequence(context_, bytes_.subspan(at_, 12));
        }
        append_utf8(text_, 0x10000 + ((high & 0x3FFU) << 10U) + (*low & 0x3FFU));
        at_ += 12;
        return true;
    }

    ProcessContext &context_;
    Bytes bytes_;
    // The next byte to read.
    std::size_t at_;
    // The string's text so far.
    std::string text_;
};

// A JSON number's text as OTP hands it to the float or integer decoder (an exponent without a fraction is rewritten
// to ".0e", a zero's sign is dropped) and the position after it.
struct Number {
    bool floating = false;
    std::string token;
    std::size_t next = 0;
};

// OTP's number states from the sign or first digit at `skip`; none where the input ends before the number is
// complete (OTP continues from `skip`).
class NumberScan final {
  public:
    NumberScan(ProcessContext &context, Bytes bytes, std::size_t skip)
        : context_(context), bytes_(bytes), skip_(skip) {}

    std::optional<Number> run() {
        std::size_t length = byte_at(bytes_, skip_) == '-' ? 1 : 0;
        if (length == 1 && (at_end(skip_ + 1) || !is_digit(byte_at(bytes_, skip_ + 1)))) {
            return stop(1);
        }
        if (byte_at(bytes_, skip_ + length) == '0') {
            return after_integer(length + 1, "0");
        }
        length = digits(length);
        return after_integer(length, std::string(text(skip_, length)));
    }

  private:
    bool at_end(std::size_t at) const { return at >= bytes_.size(); }

    bool is(std::size_t at, std::string_view any) const {
        return !at_end(at) && any.find(static_cast<char>(byte_at(bytes_, at))) != std::string_view::npos;
    }

    std::string_view text(std::size_t at, std::size_t length) const { return text_of(bytes_.subspan(at, length)); }

    // The number's length after the digits from skip_ + length.
    std::size_t digits(std::size_t length) const {
        while (!at_end(skip_ + length) && is_digit(byte_at(bytes_, skip_ + length))) {
            ++length;
        }
        return length;
    }

    // OTP's unexpected at skip_ + pos: none, or invalid_byte.
    std::optional<Number> stop(std::size_t pos) const {
        needs_more(context_, bytes_, skip_, pos, 0);
        return std::nullopt;
    }

    // An integer part of `length` bytes, `prefix` its text for an exponent.
    std::optional<Number> after_integer(std::size_t length, std::string prefix) const {
        if (is(skip_ + length, ".")) {
            return fraction(length + 1);
        }
        if (is(skip_ + length, "eE")) {
            return exponent_of_integer(length + 1, prefix);
        }
        return Number{.floating = false, .token = std::move(prefix), .next = skip_ + length};
    }

    std::optional<Number> fraction(std::size_t length) const {
        if (!is(skip_ + length, "0123456789")) {
            return stop(length);
        }
        length = digits(length);
        if (is(skip_ + length, "eE")) {
            return exponent(length + 1);
        }
        return Number{.floating = true, .token = std::string(text(skip_, length)), .next = skip_ + length};
    }

    // The exponent after a fraction: the token is the number's text.
    std::optional<Number> exponent(std::size_t length) const {
        if (is(skip_ + length, "+-")) {
            ++length;
            if (!is(skip_ + length, "0123456789")) {
                return stop(length);
            }
        } else if (!is(skip_ + length, "0123456789")) {
            return stop(length);
        }
        length = digits(length);
        return Number{.floating = true, .token = std::string(text(skip_, length)), .next = skip_ + length};
    }

    // The exponent right after the integer part: the token is the prefix, ".0e" and the exponent's text.
    std::optional<Number> exponent_of_integer(std::size_t length, const std::string &prefix) const {
        std::size_t exponent = 0;
        if (is(skip_ + length, "+-")) {
            exponent = 1;
            if (!is(skip_ + length + 1, "0123456789")) {
                return stop(length + 1);
            }
        } else if (!is(skip_ + length, "0123456789")) {
            return stop(length);
        }
        exponent = digits(length + exponent) - length;
        return Number{.floating = true,
                      .token = prefix + ".0e" + std::string(text(skip_ + length, exponent)),
                      .next = skip_ + length + exponent};
    }

    ProcessContext &context_;
    Bytes bytes_;
    // The position of the number's first byte.
    std::size_t skip_;
};

// The value of a number token, as the default decoders binary_to_integer/1 and binary_to_float/1 make it; a float
// they reject is {unexpected_sequence, Token}.
Term number_value(ProcessContext &context, const Number &number) {
    const auto text = std::span(reinterpret_cast<const unsigned char *>(number.token.data()), number.token.size());
    if (!number.floating) {
        return need(integer_term(context, text, 10));
    }
    double value = 0;
    try {
        value = float_of_text(text);
    } catch (const BuiltinFailure &) {
        unexpected_sequence(context, std::as_bytes(std::span(number.token)));
    }
    return need(TermFactory(context).floating(value));
}

Term binary_of(ProcessContext &context, std::string_view text) {
    return need(TermFactory(context).binary(std::as_bytes(std::span(text))));
}

// An array or object decode/1 has opened: its values so far, and for an object the key read before its ':'.
struct Frame {
    bool object = false;
    std::vector<Term> values;
    std::vector<std::pair<Term, Term>> entries;
    std::optional<Term> key;
};

// decode/1: OTP's walk with the default decoders, as a loop over an explicit stack of open containers.
class Decoder final {
  public:
    Decoder(ProcessContext &context, Bytes bytes) : context_(context), bytes_(bytes) {}

    Term run() {
        auto step = read(0);
        while (step.kind != Step::Kind::done) {
            step = step.kind == Step::Kind::read ? read(step.at) : deliver(*step.value, step.at);
        }
        return *step.value;
    }

  private:
    // What the walk does next: read a value at `at`, hand `value` to the innermost container, or end with it.
    struct Step {
        enum class Kind : std::uint8_t { read, deliver, done };
        Kind kind;
        std::size_t at = 0;
        std::optional<Term> value = std::nullopt;
    };

    static Step read_at(std::size_t at) { return {.kind = Step::Kind::read, .at = at}; }

    static Step deliver_at(const Term &value, std::size_t at) {
        return {.kind = Step::Kind::deliver, .at = at, .value = value};
    }

    // The input ended inside the value: decode/1 has nothing to continue with.
    [[noreturn]] void more() { unexpected_end(context_); }

    std::size_t space(std::size_t at) const {
        while (at < bytes_.size() && is_space(byte_at(bytes_, at))) {
            ++at;
        }
        return at;
    }

    // The next non-space byte from `at`, or the end of the input.
    std::size_t token(std::size_t at) {
        at = space(at);
        if (at == bytes_.size()) {
            more();
        }
        return at;
    }

    Step read(std::size_t at) {
        at = token(at);
        switch (byte_at(bytes_, at)) {
        case '"':
            return string(at + 1);
        case '[':
            return open(at, false);
        case '{':
            return open(at, true);
        case 't':
            return literal(at, "rue", need(TermFactory(context_).atom("true")));
        case 'f':
            return literal(at, "alse", need(TermFactory(context_).atom("false")));
        case 'n':
            return literal(at, "ull", need(TermFactory(context_).atom("null")));
        default:
            break;
        }
        if (byte_at(bytes_, at) != '-' && !is_digit(byte_at(bytes_, at))) {
            invalid_byte(context_, bytes_, at);
        }
        return number(at);
    }

    Step literal(std::size_t at, std::string_view rest, const Term &value) {
        if (text_of(bytes_.subspan(at + 1)).starts_with(rest)) {
            return deliver_at(value, at + 1 + rest.size());
        }
        needs_more(context_, bytes_, at, 1, rest.size());
        more();
    }

    Step string(std::size_t at) {
        auto scanned = StringScan(context_, bytes_, at).run();
        if (!scanned) {
            more();
        }
        return deliver_at(binary_of(context_, scanned->first), scanned->second);
    }

    Step number(std::size_t at) {
        const auto scanned = NumberScan(context_, bytes_, at).run();
        if (!scanned) {
            more();
        }
        const auto value = number_value(context_, *scanned);
        if (scanned->next < bytes_.size()) {
            return deliver_at(value, scanned->next);
        }
        if (!frames_.empty()) {
            more();
        }
        return {.kind = Step::Kind::done, .value = value};
    }

    // An array or object at `at`: an empty one is a value, else a container to fill (an object from its first key).
    Step open(std::size_t at, bool object) {
        at = token(at + 1);
        const auto byte = byte_at(bytes_, at);
        if (byte == static_cast<std::uint8_t>(object ? '}' : ']')) {
            return deliver_at(object ? need(TermFactory(context_).map({})) : need(TermFactory(context_).nil()), at + 1);
        }
        if (object && byte != '"') {
            invalid_byte(context_, bytes_, at);
        }
        frames_.emplace_back().object = object;
        return object ? string(at + 1) : read_at(at);
    }

    // OTP's continue: a value for the innermost container, or the decoded value once none is open.
    Step deliver(const Term &value, std::size_t at) {
        if (frames_.empty()) {
            return finish(value, at);
        }
        auto &frame = frames_.back();
        if (!frame.object) {
            return element(frame, value, at);
        }
        if (!frame.key) {
            return key(frame, value, at);
        }
        return member(frame, value, at);
    }

    Step element(Frame &frame, const Term &value, std::size_t at) {
        at = token(at);
        const auto byte = byte_at(bytes_, at);
        if (byte != ']' && byte != ',') {
            invalid_byte(context_, bytes_, at);
        }
        frame.values.push_back(value);
        if (byte == ',') {
            return read_at(at + 1);
        }
        const auto list = need(TermFactory(context_).list(frame.values));
        frames_.pop_back();
        return deliver_at(list, at + 1);
    }

    Step key(Frame &frame, const Term &key, std::size_t at) {
        at = token(at);
        if (byte_at(bytes_, at) != ':') {
            invalid_byte(context_, bytes_, at);
        }
        frame.key = key;
        return read_at(at + 1);
    }

    // An object's member value: default object_push and object_finish keep a key's first value (maps:from_list of
    // the reversed members).
    Step member(Frame &frame, const Term &value, std::size_t at) {
        at = token(at);
        const auto byte = byte_at(bytes_, at);
        if (byte != '}' && byte != ',') {
            invalid_byte(context_, bytes_, at);
        }
        frame.entries.emplace_back(*frame.key, value);
        frame.key.reset();
        if (byte == ',') {
            const auto next = token(at + 1);
            if (byte_at(bytes_, next) != '"') {
                invalid_byte(context_, bytes_, next);
            }
            return string(next + 1);
        }
        std::ranges::reverse(frame.entries);
        const auto map = need(TermFactory(context_).map(frame.entries));
        frames_.pop_back();
        return deliver_at(map, at + 1);
    }

    // OTP's terminate: only spaces may follow the value; decode/1 rejects the first byte after it otherwise.
    Step finish(const Term &value, std::size_t at) {
        if (space(at) != bytes_.size()) {
            invalid_byte(context_, bytes_, at);
        }
        return {.kind = Step::Kind::done, .value = value};
    }

    ProcessContext &context_;
    Bytes bytes_;
    // The open containers, innermost last.
    std::vector<Frame> frames_;
};

// The iodata OTP's escapers build for a binary: an accumulator of nested lists, the plain run since the last
// escape, and the original binary itself when nothing needs escaping.
class Escaper final {
  public:
    Escaper(ProcessContext &context, const Term &original)
        : context_(context), original_(original), input_(original), bytes_(input_.bytes()), acc_(chars("\"")) {}

    // escape_binary (all = false) keeps valid UTF-8; escape_all writes \uXXXX for every character past ASCII.
    Term run(bool all) {
        while (at_ < bytes_.size()) {
            const auto byte = byte_at(bytes_, at_);
            if (is_plain(byte)) {
                ++at_;
            } else if (is_escaped(byte)) {
                ascii(byte);
            } else if (all) {
                escape_character();
            } else {
                keep_character();
            }
        }
        return finish();
    }

  private:
    Term chars(std::string_view text) const {
        std::vector<Term> codes;
        for (const auto c : text) {
            codes.push_back(need(TermFactory(context_).integer(static_cast<unsigned char>(c))));
        }
        return need(TermFactory(context_).list(codes));
    }

    Term integer(std::int64_t value) const { return need(TermFactory(context_).integer(value)); }

    Term cons(const Term &head, const Term &tail) const { return need(TermFactory(context_).cons(head, tail)); }

    // The run of bytes since the last escape.
    Term run_part() const { return need(TermFactory(context_).binary(bytes_.subspan(skip_, at_ - skip_))); }

    static std::string ascii_escape(std::uint8_t byte) {
        constexpr std::string_view SHORT = "\b\t\n\f\r\"\\";
        constexpr std::string_view LETTER = "btnfr\"\\";
        const auto index = SHORT.find(static_cast<char>(byte));
        if (index != std::string_view::npos) {
            return {'\\', LETTER[index]};
        }
        constexpr std::string_view HEX = "0123456789ABCDEF";
        return {'\\', 'u', '0', '0', HEX[byte >> 4U], HEX[byte & 0xFU]};
    }

    // An escaped ASCII byte: [Acc | Escape], or [Acc, Run | Escape] after a run.
    void ascii(std::uint8_t byte) {
        const auto escape = binary_of(context_, ascii_escape(byte));
        acc_ = at_ == skip_ ? cons(acc_, escape) : cons(acc_, cons(run_part(), escape));
        skip_ = ++at_;
    }

    // escape_binary's UTF-8: a valid sequence joins the run; a bad byte is invalid_byte, a cut one unexpected_end.
    void keep_character() {
        const auto sequence = utf8_at(bytes_, at_);
        if (sequence.kind == Utf8::Kind::rejected) {
            invalid_byte(context_, bytes_, sequence.at);
        }
        if (sequence.kind == Utf8::Kind::truncated) {
            unexpected_end(context_);
        }
        at_ += sequence.at;
    }

    // escape_all's character: [Acc, "\\u00" | Hex] (after [Acc | Run]), or a surrogate pair past FFFF.
    void escape_character() {
        const auto sequence = utf8_at(bytes_, at_);
        if (sequence.kind != Utf8::Kind::valid) {
            invalid_byte(context_, bytes_, at_);
        }
        const auto code = code_point(bytes_, at_, sequence.at);
        const auto base = at_ == skip_ ? acc_ : cons(acc_, run_part());
        acc_ = code > 0xFFFF ? cons(base, pair(code - 0x10000)) : cons(base, cons(chars(prefix(code)), hex(code)));
        at_ += sequence.at;
        skip_ = at_;
    }

    static std::string_view prefix(char32_t code) {
        if (code <= 0xFF) {
            return "\\u00";
        }
        return code <= 0xFFF ? "\\u0" : "\\u";
    }

    // integer_to_binary(Value, 16): upper-case digits.
    Term hex(char32_t value) const {
        constexpr std::string_view DIGITS = "0123456789ABCDEF";
        std::string text;
        do {
            text.insert(text.begin(), DIGITS[value & 0xFU]);
            value >>= 4U;
        } while (value != 0);
        return binary_of(context_, text);
    }

    // ["\\uD", First, "\\uD" | Second] for a code point past FFFF less 10000.
    Term pair(char32_t code) const {
        const auto second = cons(chars("\\uD"), hex(0xC00U | (code & 0x3FFU)));
        return cons(chars("\\uD"), cons(hex(0x800U | (code >> 10U)), second));
    }

    // ["\"", Original, "\""] when nothing was escaped, else [Acc, "\""] or [Acc, Run, "\""].
    Term finish() const {
        const auto quote = integer('"');
        if (skip_ == 0) {
            const std::array whole{quote, original_, quote};
            return need(TermFactory(context_).list(whole));
        }
        if (at_ == skip_) {
            const std::array closed{acc_, quote};
            return need(TermFactory(context_).list(closed));
        }
        const std::array closed{acc_, run_part(), quote};
        return need(TermFactory(context_).list(closed));
    }

    ProcessContext &context_;
    Term original_;
    Input input_;
    Bytes bytes_;
    // The iodata built so far.
    Term acc_;
    // The start of the current run and the next byte.
    std::size_t skip_ = 0;
    std::size_t at_ = 0;
};

// decode(Binary).
Term decode1(ProcessContext &context, const Term &binary) {
    if (!binary.is_binary()) {
        no_clause();
    }
    const Input input(binary);
    return Decoder(context, input.bytes()).run();
}

template <bool All> Term escape1(ProcessContext &context, const Term &binary) {
    if (!binary.is_binary()) {
        no_clause();
    }
    return Escaper(context, binary).run(All);
}

// clause_json:string(Binary, Skip): {Text, Next} for the string from `Skip` (after its quote), or `more`.
Term scan_string(ProcessContext &context, const Term &binary, std::int64_t skip) {
    const Input input(binary);
    const auto scanned = StringScan(context, input.bytes(), static_cast<std::size_t>(skip)).run();
    TermFactory factory(context);
    if (!scanned) {
        return need(factory.atom("more"));
    }
    const std::array result{binary_of(context, scanned->first),
                            need(factory.integer(static_cast<std::int64_t>(scanned->second)))};
    return need(factory.tuple(result));
}

// clause_json:number(Binary, Skip): {integer | float, Token, Next} for the number at `Skip`, or `more`.
Term scan_number(ProcessContext &context, const Term &binary, std::int64_t skip) {
    const Input input(binary);
    const auto scanned = NumberScan(context, input.bytes(), static_cast<std::size_t>(skip)).run();
    TermFactory factory(context);
    if (!scanned) {
        return need(factory.atom("more"));
    }
    const std::array result{need(factory.atom(scanned->floating ? "float" : "integer")),
                            binary_of(context, scanned->token),
                            need(factory.integer(static_cast<std::int64_t>(scanned->next)))};
    return need(factory.tuple(result));
}

constexpr std::array JSON_BUILTINS{
    typed_entry<decode1>("json", "decode"),
    typed_entry<escape1<false>>("json", "encode_binary"),
    typed_entry<escape1<true>>("json", "encode_binary_escape_all"),
    typed_entry<scan_string>("clause_json", "string"),
    typed_entry<scan_number>("clause_json", "number"),
};
} // namespace
} // namespace clause::runtime::builtins::encoding

namespace clause::runtime {
std::span<const BuiltinEntry> json_builtins() noexcept { return builtins::encoding::JSON_BUILTINS; }
} // namespace clause::runtime
