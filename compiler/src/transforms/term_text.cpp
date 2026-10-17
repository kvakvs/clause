// Added for parse transforms: consult-syntax text of terms, written and read without recursion.
#include "term_text.hpp"
#include "../preprocessor/value.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <clause/abi/keywords.hpp>
#include <clause/compiler/lexer.hpp>
#include <cmath>

namespace clause::transforms {
namespace {
// Latin-1 letters io_lib accepts unquoted: lowercase may start an atom, both may continue it.
bool latin1_lower(const char32_t code) {
    return (code >= U'a' && code <= U'z') || (code >= 0xDF && code <= 0xFF && code != 0xF7);
}

bool latin1_upper(const char32_t code) {
    return (code >= U'A' && code <= U'Z') || (code >= 0xC0 && code <= 0xDE && code != 0xD7);
}

// Whether an atom can be written without quotes.
bool bare_atom(const std::u32string_view name) {
    if (name.empty() || !latin1_lower(name.front()) || abi::v1::reserved_word(name)) {
        return false;
    }
    return std::ranges::all_of(name, [](const char32_t code) {
        return latin1_lower(code) || latin1_upper(code) || (code >= U'0' && code <= U'9') || code == U'_' ||
               code == U'@';
    });
}

// Append one character of a quoted atom or string, escaping the quote, backslash and control characters.
void quoted_char(std::string &out, const char32_t code, const char quote) {
    if (code == static_cast<char32_t>(quote) || code == U'\\') {
        out.push_back('\\');
        out.push_back(static_cast<char>(code));
    } else if (code < 0x20 || (code >= 0x7F && code < 0xA0)) {
        std::array<char, 16> digits{};
        const auto end =
            std::to_chars(digits.data(), digits.data() + digits.size(), static_cast<unsigned>(code), 16).ptr;
        out += "\\x{" + std::string(digits.data(), end) + "}";
    } else {
        out += utf8(std::u32string_view(&code, 1));
    }
}

void write_quoted(std::string &out, const std::u32string_view text, const char quote) {
    out.push_back(quote);
    for (const auto code : text) {
        quoted_char(out, code, quote);
    }
    out.push_back(quote);
}

void write_atom(std::string &out, const std::u32string &name) {
    if (bare_atom(name)) {
        out += utf8(name);
    } else {
        write_quoted(out, name, '\'');
    }
}

// Shortest exact float text in Erlang syntax: digits on both sides of the point, exponent without '+'.
void write_float(std::string &out, const double value) {
    std::array<char, 64> buffer{};
    const auto end =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, std::chars_format::scientific).ptr;
    std::string text(buffer.data(), end);
    const auto e = text.find('e');
    auto mantissa = text.substr(0, e);
    if (mantissa.find('.') == std::string::npos) {
        mantissa += ".0";
    }
    const auto exponent = std::stoi(text.substr(e + 1));
    out += exponent == 0 ? mantissa : mantissa + "e" + std::to_string(exponent);
}

// Characters printed inside strings: printable ASCII, common whitespace and Unicode beyond Latin-1 controls.
bool printable(const std::int64_t code) {
    return (code >= 0x20 && code < 0x7F) || code == '\n' || code == '\t' || code == '\r' ||
           (code >= 0xA0 && code <= 0x10FFFF && !(code >= 0xD800 && code <= 0xDFFF));
}

// One piece of output: a term still to write, or literal punctuation.
struct Piece {
    TermId id_ = 0;
    std::string_view literal_;

    static Piece term(const TermId id) { return {id, {}}; }

    static Piece text(const std::string_view literal) { return {0, literal}; }
};

// Brackets around a container's children and the separators before even and odd children.
struct Punctuation {
    std::string_view open_;
    std::string_view close_;
    std::string_view even_;
    std::string_view odd_;
};

class TextWriter {
  public:
    TextWriter(std::string &out, const Terms &terms) : out_(out), terms_(terms) {}

    // Write the term and its children in order.
    void run(const TermId root) {
        pending_.push_back(Piece::term(root));
        while (!pending_.empty()) {
            const auto piece = pending_.back();
            pending_.pop_back();
            if (!piece.literal_.empty()) {
                out_ += piece.literal_;
            } else {
                write(piece.id_);
            }
        }
    }

  private:
    // Output, arena and pieces still to write (last one first).
    std::string &out_;
    const Terms &terms_;
    std::vector<Piece> pending_;

    // Queue children between `open` and `close`; the separator before child i is `even` or `odd` by i's parity.
    void queue(const std::vector<TermId> &children, const Punctuation &marks) {
        out_ += marks.open_;
        pending_.push_back(Piece::text(marks.close_));
        for (std::size_t index = children.size(); index-- > 0;) {
            pending_.push_back(Piece::term(children[index]));
            if (index != 0) {
                pending_.push_back(Piece::text(index % 2 == 0 ? marks.even_ : marks.odd_));
            }
        }
    }

    void write(const TermId id) {
        const auto &node = terms_.node(id);
        switch (node.kind_) {
        case TermKind::atom:
            write_atom(out_, node.atom_);
            return;
        case TermKind::integer:
            out_ += node.integer_.decimal;
            return;
        case TermKind::floating:
            write_float(out_, node.float_);
            return;
        case TermKind::bits:
            write_bits(node);
            return;
        case TermKind::tuple:
            queue(node.children_, {.open_ = "{", .close_ = "}", .even_ = ",", .odd_ = ","});
            return;
        case TermKind::map:
            queue(node.children_, {.open_ = "#{", .close_ = "}", .even_ = ",", .odd_ = "=>"});
            return;
        default:
            write_list(id, node);
        }
    }

    void write_list(const TermId id, const TermNode &node) {
        if (const auto text = node.children_.empty() ? std::nullopt : terms_.text(id);
            text && std::ranges::all_of(*text, [](const char32_t code) { return printable(code); })) {
            write_quoted(out_, *text, '"');
            return;
        }
        if (!node.improper_) {
            queue(node.children_, {.open_ = "[", .close_ = "]", .even_ = ",", .odd_ = ","});
            return;
        }
        // The tail follows `|` instead of the last comma.
        out_ += "[";
        pending_.push_back(Piece::text("]"));
        pending_.push_back(Piece::term(node.children_.back()));
        pending_.push_back(Piece::text("|"));
        for (std::size_t index = node.children_.size() - 1; index-- > 0;) {
            pending_.push_back(Piece::term(node.children_[index]));
            if (index != 0) {
                pending_.push_back(Piece::text(","));
            }
        }
    }

    // Bytes as integers; a partial last byte as Value:Bits.
    void write_bits(const TermNode &node) {
        out_ += "<<";
        const auto whole = node.bit_count_ / 8;
        for (std::size_t index = 0; index < node.bytes_.size(); ++index) {
            if (index != 0) {
                out_ += ",";
            }
            const auto byte = static_cast<std::uint8_t>(node.bytes_[index]);
            if (index < whole) {
                out_ += std::to_string(byte);
            } else {
                const auto bits = node.bit_count_ % 8;
                out_ += std::to_string(byte >> (8 - bits)) + ":" + std::to_string(bits);
            }
        }
        out_ += ">>";
    }
};

// ---- reading --------------------------------------------------------------------------------------------------------

// One bitstring segment: its value and how many of its low bits it contributes.
struct Segment {
    std::uint64_t value_;
    std::size_t size_;
};

// Append a segment's bits (most significant first) to a bitstring under construction.
void append_bits(std::string &bytes, std::size_t &count, const Segment segment) {
    for (std::size_t bit = segment.size_; bit-- > 0;) {
        if (count % 8 == 0) {
            bytes.push_back(0);
        }
        if (((segment.value_ >> bit) & 1U) != 0) {
            bytes.back() = static_cast<char>(static_cast<std::uint8_t>(bytes.back()) | (0x80U >> (count % 8)));
        }
        ++count;
    }
}

// A container whose closing token has not been read yet.
struct OpenText {
    TermKind kind_;
    std::vector<TermId> children_;
    // A list after `|` expects its tail and then `]`.
    bool tail_ = false;
};

class TextReader {
  public:
    TextReader(const SourcePtr &source, Terms &terms) : lexer_(source), terms_(terms) {}

    // Read `Term.` items until the end of the text.
    std::vector<TermId> run() {
        std::vector<TermId> result;
        while (peek()) {
            result.push_back(term());
            if (take().kind != TokenKind::dot) {
                fail(*last_, "expected '.' after a term");
            }
        }
        return result;
    }

  private:
    // Token source, arena, one token of lookahead, the last token taken and the open containers.
    Lexer lexer_;
    Terms &terms_;
    std::optional<Token> peeked_;
    std::optional<Token> last_;
    std::vector<OpenText> open_;

    [[noreturn]] static void fail(const Token &token, const std::string &message) {
        throw TermError(token.location.file + ":" + std::to_string(token.location.line) + ":" +
                        std::to_string(token.location.column) + ": " + message);
    }

    // The next token without consuming it; nothing at the end of the text.
    const Token *peek() {
        if (!peeked_) {
            try {
                peeked_ = lexer_.next();
            } catch (const LexicalError &error) {
                throw TermError(render(error.diagnostic));
            }
        }
        return peeked_ ? &*peeked_ : nullptr;
    }

    // Consume the next token; the end of the text is an error.
    Token take() {
        if (!peek()) {
            if (last_) {
                fail(*last_, "unexpected end of text");
            }
            throw TermError("unexpected end of text");
        }
        last_ = std::move(*peeked_);
        peeked_.reset();
        return *last_;
    }

    static bool symbol(const Token &token, const std::u32string_view text) {
        return token.kind == TokenKind::symbol && token.text() == text;
    }

    // Read one complete term, holding open containers on an explicit stack.
    TermId term() {
        const auto base = open_.size();
        while (true) {
            auto value = opening();
            while (value) {
                if (open_.size() == base) {
                    return *value;
                }
                value = continued(*value);
            }
        }
    }

    // Read a value position: a finished leaf or empty container, or nothing when a container opened.
    std::optional<TermId> opening() {
        const auto token = take();
        if (symbol(token, U"{")) {
            return open(TermKind::tuple, U"}");
        }
        if (symbol(token, U"[")) {
            return open(TermKind::list, U"]");
        }
        if (symbol(token, U"#")) {
            if (!symbol(take(), U"{")) {
                fail(*last_, "expected '{' after '#'");
            }
            return open(TermKind::map, U"}");
        }
        if (symbol(token, U"<<")) {
            return bits();
        }
        return leaf(token);
    }

    // Open a container unless it is closed at once.
    std::optional<TermId> open(const TermKind kind, const std::u32string_view close) {
        if (const auto *next = peek(); next && symbol(*next, close)) {
            take();
            if (kind == TermKind::list) {
                return terms_.nil();
            }
            return kind == TermKind::tuple ? terms_.tuple({}) : terms_.map({});
        }
        open_.push_back({.kind_ = kind, .children_ = {}, .tail_ = false});
        return std::nullopt;
    }

    // Add a finished value to the innermost container and read what follows it: the closed container, or nothing
    // when another child follows.
    std::optional<TermId> continued(const TermId value) {
        auto &top = open_.back();
        top.children_.push_back(value);
        const auto token = take();
        if (top.kind_ == TermKind::list) {
            return list_follow(top, token);
        }
        const bool key = top.kind_ == TermKind::map && top.children_.size() % 2 == 1;
        if (symbol(token, key ? U"=>" : U",")) {
            return std::nullopt;
        }
        if (key || !symbol(token, U"}")) {
            fail(token, key ? "expected '=>'" : "expected ',' or '}'");
        }
        auto children = std::move(top.children_);
        const auto kind = top.kind_;
        open_.pop_back();
        return kind == TermKind::tuple ? terms_.tuple(std::move(children)) : terms_.map(std::move(children));
    }

    // After a list element: another element, the tail, or the end of the list.
    std::optional<TermId> list_follow(OpenText &top, const Token &token) {
        if (!top.tail_ && symbol(token, U",")) {
            return std::nullopt;
        }
        if (!top.tail_ && symbol(token, U"|")) {
            top.tail_ = true;
            return std::nullopt;
        }
        if (!symbol(token, U"]")) {
            fail(token, top.tail_ ? "expected ']' after a list tail" : "expected ',', '|' or ']'");
        }
        auto children = std::move(top.children_);
        std::optional<TermId> tail;
        if (top.tail_) {
            tail = children.back();
            children.pop_back();
        }
        open_.pop_back();
        return terms_.list(std::move(children), tail);
    }

    // Atoms, numbers, characters and strings, including a leading minus sign.
    TermId leaf(const Token &token) {
        switch (token.kind) {
        case TokenKind::atom:
        case TokenKind::keyword:
            return terms_.atom(token.text());
        case TokenKind::integer:
        case TokenKind::character:
            return terms_.integer(std::get<Integer>(token.value));
        case TokenKind::floating:
            return terms_.floating(std::get<double>(token.value));
        case TokenKind::string:
            return string(token);
        default:
            break;
        }
        if (!symbol(token, U"-")) {
            fail(token, "expected a term");
        }
        return negative(take());
    }

    TermId negative(const Token &token) {
        if (token.kind == TokenKind::floating) {
            return terms_.floating(-std::get<double>(token.value));
        }
        if (token.kind != TokenKind::integer) {
            fail(token, "expected a number after '-'");
        }
        return terms_.integer(Integer{decimal_integer(-decimal_number(std::get<Integer>(token.value).decimal))});
    }

    // Adjacent strings join into one list.
    TermId string(const Token &token) {
        std::u32string text(token.text());
        for (const auto *next = peek(); next && next->kind == TokenKind::string; next = peek()) {
            text += take().text();
        }
        return terms_.string(text);
    }

    // `<<...>>` of integers, Integer:Size segments and strings.
    TermId bits() {
        std::string bytes;
        std::size_t count = 0;
        if (const auto *next = peek(); next && symbol(*next, U">>")) {
            take();
            return terms_.bits({}, 0);
        }
        while (true) {
            segment(bytes, count);
            const auto token = take();
            if (symbol(token, U">>")) {
                return terms_.bits(std::move(bytes), count);
            }
            if (!symbol(token, U",")) {
                fail(token, "expected ',' or '>>'");
            }
        }
    }

    // One segment of a bitstring: a string of bytes, or an integer with an optional bit size.
    void segment(std::string &bytes, std::size_t &count) {
        const auto token = take();
        if (token.kind == TokenKind::string) {
            string_segment(token, bytes, count);
            return;
        }
        const auto value = small(token);
        std::size_t size = 8;
        if (const auto *next = peek(); next && symbol(*next, U":")) {
            take();
            size = width(take());
        }
        append_bits(bytes, count, {.value_ = static_cast<std::uint64_t>(value), .size_ = size});
    }

    // The bytes of a string segment.
    static void string_segment(const Token &token, std::string &bytes, std::size_t &count) {
        for (const auto code : token.text()) {
            if (code > 0xFF) {
                fail(token, "binary string character above 255");
            }
            append_bits(bytes, count, {.value_ = code, .size_ = 8});
        }
    }

    // A segment size of 1..64 bits.
    std::size_t width(const Token &token) {
        const auto value = small(token);
        if (value < 1 || value > 64) {
            fail(token, "segment size must be 1..64");
        }
        return static_cast<std::size_t>(value);
    }

    // A non-negative integer token that fits 64 bits.
    std::int64_t small(const Token &token) {
        if (token.kind != TokenKind::integer) {
            fail(token, "expected an integer");
        }
        const auto value = terms_.small_integer(terms_.integer(std::get<Integer>(token.value)));
        if (!value || *value < 0) {
            fail(token, "segment value out of range");
        }
        return *value;
    }
};
} // namespace

void write_text(std::string &out, const Terms &terms, const TermId id) { TextWriter(out, terms).run(id); }

std::vector<TermId> read_text(const SourcePtr &source, Terms &terms) { return TextReader(source, terms).run(); }
} // namespace clause::transforms
