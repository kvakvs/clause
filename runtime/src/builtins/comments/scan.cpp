#include "../collections/collections.hpp"
#include "../syntax/syntax.hpp"

// erl_comment_scan's scanning as builtins (plan 21 LIB77E4b, docs/builtins.md#comments): the comment lines of a source
// text with their line, column and indentation, skipping strings, quoted atoms and character literals, and the
// joining of consecutive lines into comments. file/1 stays Erlang (reading, encodings, error reports).
namespace clause::runtime::builtins::comments {
namespace {
using syntax::Items;
using syntax::Trees;

std::int64_t tab(std::int64_t column) { return column - column % 8 + 8; }

// The scan of one text: OTP's scan_lines/5 with its column, margin (the column after the last token) and line.
class Scanner final {
  public:
    Scanner(ProcessContext &context, Items text) : context_(context), text_(std::move(text)) {}

    // The comment lines, the last one first (OTP's accumulator).
    Term run();

  private:
    bool is(std::size_t at, std::int64_t code) const {
        return at < text_.size() && small(text_[at].word()) == std::optional{code};
    }

    // The length of a line break at `at`: 2 for CR LF, 1 for CR or LF, 0 for none.
    std::size_t line_break() const {
        if (is(at_, '\r')) {
            return is(at_ + 1, '\n') ? 2 : 1;
        }
        return is(at_, '\n') ? 1 : 0;
    }

    void next_line() {
        ++line_;
        column_ = 0;
        margin_ = 0;
    }

    // The column after a token, which becomes the margin.
    void token(std::int64_t width) {
        column_ += width;
        margin_ = column_;
    }

    void step();
    void comment();
    void quoted(const Term &quote);
    void character();

    // Builds the comment line terms.
    ProcessContext &context_;
    // The characters of the text (any terms; integers are characters).
    Items text_;
    std::size_t at_ = 0;
    std::int64_t line_ = 1;
    std::int64_t column_ = 0;
    std::int64_t margin_ = 0;
    // The comment lines found, in text order.
    Items found_;
};

Term Scanner::run() {
    while (at_ < text_.size()) {
        step();
    }
    std::ranges::reverse(found_);
    return Trees(context_).list(found_);
}

void Scanner::step() {
    if (const auto width = line_break()) {
        at_ += width;
        next_line();
        return;
    }
    const auto item = text_[at_++];
    const auto code = small(item.word()).value_or(-1);
    if (code == ' ') {
        ++column_;
    } else if (code == '\t') {
        column_ = tab(column_);
    } else if (code == '%') {
        comment();
    } else if (code == '$') {
        ++column_;
        character();
    } else if (code == '"' || code == '\'') {
        ++column_;
        quoted(item);
    } else {
        token(1);
    }
}

// seen_comment/6: the text up to the line break, trailing white space trimmed as string:trim/2 trims it.
void Scanner::comment() {
    const auto start = at_;
    while (at_ < text_.size() && line_break() == 0) {
        ++at_;
    }
    Items reversed(text_.begin() + std::ptrdiff_t(start), text_.begin() + std::ptrdiff_t(at_));
    std::ranges::reverse(reversed);
    at_ += line_break();
    const Trees trees(context_);
    const auto trimmed = string::trim_leading_of(context_, trees.list(reversed));
    const auto text = need(TermFactory(context_).reverse(trimmed, trees.nil()));
    const std::array fields{trees.integer(line_), trees.integer(column_ + 1), trees.integer(column_ - margin_), text};
    found_.push_back(trees.tuple(fields));
    next_line();
}

// scan_string/5: a string or quoted atom up to its closing quote; escapes skip the next character.
void Scanner::quoted(const Term &quote) {
    while (at_ < text_.size()) {
        if (collections::same(text_[at_], quote)) {
            ++at_;
            token(1);
            return;
        }
        if (const auto width = line_break()) {
            at_ += width;
            ++line_;
            column_ = 0;
        } else if (is(at_, '\t')) {
            ++at_;
            column_ = tab(column_);
        } else if (is(at_, '\\') && at_ + 1 < text_.size()) {
            at_ += 2;
            column_ += 2;
        } else {
            ++at_;
            ++column_;
        }
    }
}

// scan_char/4: the character after $, an escape taking two.
void Scanner::character() {
    if (at_ == text_.size()) {
        return;
    }
    if (const auto width = line_break()) {
        at_ += width;
        next_line();
    } else if (is(at_, '\t')) {
        ++at_;
        column_ = tab(column_);
        margin_ = column_;
    } else if (is(at_, '\\') && at_ + 1 < text_.size()) {
        at_ += 2;
        token(2);
    } else {
        ++at_;
        token(1);
    }
}

// The characters of a text; function_clause for an improper list or another term, as scan_lines/5 raises.
Items characters(const Term &text) {
    Items items;
    auto rest = text;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        items.push_back(rest.head_unchecked());
    }
    if (!rest.is_nil()) {
        lists::no_clause();
    }
    return items;
}

Term scan_lines(ProcessContext &context, const Term &text) { return Scanner(context, characters(text)).run(); }

// A comment line {Line, Column, Indentation, Text}; function_clause for anything else.
Items comment_line(const Term &line) {
    if (!line.is_tuple() || line.tuple_size_unchecked() != 4) {
        lists::no_clause();
    }
    return {syntax::element(line, 0), syntax::element(line, 1), syntax::element(line, 2), syntax::element(line, 3)};
}

// join_lines/5's guard: the line is right above the comment, in its column, alone on its line; arithmetic on
// anything but numbers fails the guard.
bool joins(ProcessContext &context, const Items &above, const Items &comment) {
    const Trees trees(context);
    try {
        return collections::same(above[0], collections::add(context, comment[0], trees.integer(-1))) &&
               collections::same(above[1], comment[1]) &&
               collections::same(collections::add(context, comment[2], trees.integer(1)), comment[1]);
    } catch (const BuiltinFailure &failure) {
        if (failure.term) {
            throw;
        }
        return false;
    }
}

Term join_lines(ProcessContext &context, const Term &lines) {
    const Trees trees(context);
    Items comments;
    std::optional<Items> current;
    Items texts;
    const auto finish = [&] {
        comments.push_back(trees.tuple(std::array{(*current)[0], (*current)[1], (*current)[2], trees.list(texts)}));
    };
    for (const auto &line : syntax::elements(lines)) {
        auto fields = comment_line(line);
        if (current && joins(context, fields, *current)) {
            texts.insert(texts.begin(), fields[3]);
        } else {
            if (current) {
                finish();
            }
            texts = {fields[3]};
        }
        current = std::move(fields);
    }
    if (current) {
        finish();
    }
    return trees.list(comments);
}

// string/1: the comments of a text, first first.
Term string1(ProcessContext &context, const Term &text) {
    auto comments = syntax::elements(join_lines(context, scan_lines(context, text)));
    std::ranges::reverse(comments);
    return Trees(context).list(comments);
}

constexpr std::array SCAN{
    typed_entry<scan_lines>("erl_comment_scan", "scan_lines"),
    typed_entry<join_lines>("erl_comment_scan", "join_lines"),
    typed_entry<string1>("erl_comment_scan", "string"),
};
} // namespace
} // namespace clause::runtime::builtins::comments

namespace clause::runtime {
// Publish erl_comment_scan's scanning.
std::span<const BuiltinEntry> erl_comment_scan_builtins() noexcept { return builtins::comments::SCAN; }
} // namespace clause::runtime
