#include "parsing/boost_parser.hpp"
#include <algorithm>
#include <array>
#include <clause/abi/keywords.hpp>
#include <clause/compiler/lexer.hpp>
#include <utility>

namespace clause {
namespace bp = boost::parser;

bool atom_start(const char32_t value) {
    return (value >= U'a' && value <= U'z') || (value >= U'ß' && value <= U'ÿ' && value != U'÷');
}

bool variable_start(const char32_t value) {
    return value == U'_' || (value >= U'A' && value <= U'Z') || (value >= U'À' && value <= U'Þ' && value != U'×');
}

bool whitespace(const char32_t value) { return value <= 32 || (value >= 128 && value <= 160); }

std::size_t word_length(const std::u32string_view input) {
    constexpr auto latin = (bp::char_(U'À', U'ÿ') - bp::char_(U"×÷"));
    constexpr auto part =
        bp::char_(U'a', U'z') | bp::char_(U'A', U'Z') | bp::char_(U'0', U'9') | bp::char_(U"_@") | latin;
    auto current = input.begin();
    bp::prefix_parse(current, input.end(), bp::omit[*part]);
    return static_cast<std::size_t>(current - input.begin());
}

std::u32string_view Token::text() const {
    const auto *text = std::get_if<std::u32string>(&value);
    return text ? std::u32string_view(*text) : std::u32string_view();
}

namespace {
// The reserved words, the lexer's keywords until features change them.
std::vector<std::u32string> default_keywords() {
    std::vector<std::u32string> keywords;
    keywords.reserve(abi::v1::RESERVED_WORDS.size());
    for (const auto word : abi::v1::RESERVED_WORDS) {
        keywords.emplace_back(word.begin(), word.end());
    }
    return keywords;
}
} // namespace

Lexer::Lexer(SourcePtr source, const bool comments)
    : source_(std::move(source)), comments_(comments), keywords_(default_keywords()), logical_file_(source_->name) {}

std::u32string_view Lexer::rest() const { return std::u32string_view(source_->text).substr(cursor_); }

std::size_t Lexer::offset() const { return cursor_; }

void Lexer::set_keywords(const std::set<std::u32string> &keywords) {
    keywords_.assign(keywords.begin(), keywords.end());
}

void Lexer::set_keyword(std::u32string keyword, const bool enabled) {
    std::erase(keywords_, keyword);
    if (enabled) {
        keywords_.push_back(std::move(keyword));
    }
}

void Lexer::set_location(std::string file, const std::size_t line) {
    logical_file_ = std::move(file);
    logical_base_ = line;
    physical_base_ = source_->position(cursor_).line;
}

LogicalLocation Lexer::logical_location(const std::size_t offset) const {
    const auto position = source_->position(offset);
    return {.file = logical_file_, .line = logical_base_ + position.line - physical_base_, .column = position.column};
}

Token Lexer::token(const TokenKind kind, TokenValue value, const std::size_t begin, const std::size_t end) const {
    const auto position = source_->position(begin);
    return {.kind = kind,
            .value = std::move(value),
            .spelling = {.source = source_, .begin = begin, .end = end},
            .location = {.file = logical_file_,
                         .line = logical_base_ + position.line - physical_base_,
                         .column = position.column},
            .origins = {}};
}

void Lexer::fail(const DiagnosticCode code, std::string message, const std::size_t begin) const {
    const auto position = source_->position(begin);
    throw LexicalError({.code = code,
                        .message = std::move(message),
                        .primary = {.source = source_, .begin = begin, .end = cursor_},
                        .related = {},
                        .severity = Severity::error,
                        .location = LogicalLocation{.file = logical_file_,
                                                    .line = logical_base_ + position.line - physical_base_,
                                                    .column = position.column}});
}

std::optional<Token> Lexer::trivia() {
    while (!rest().empty()) {
        if (whitespace(rest().front())) {
            ++cursor_;
            previous_string_ = false;
            continue;
        }
        if (rest().front() != U'%') {
            break;
        }
        const auto begin = cursor_;
        const auto end = rest().find(U'\n');
        cursor_ += end == std::u32string_view::npos ? rest().size() : end;
        previous_string_ = false;
        if (comments_) {
            return token(TokenKind::comment, source_->text.substr(begin, cursor_ - begin), begin, cursor_);
        }
    }
    return std::nullopt;
}

Token Lexer::word() {
    const auto begin = cursor_;
    cursor_ += word_length(rest());
    auto text = source_->text.substr(begin, cursor_ - begin);
    auto kind = variable_start(text.front()) ? TokenKind::variable : TokenKind::atom;
    if (kind == TokenKind::atom && std::ranges::contains(keywords_, text)) {
        kind = TokenKind::keyword;
    }
    if (text.size() > 255) {
        fail(DiagnosticCode::invalid_character, "name exceeds 255 characters", begin);
    }
    return token(kind, std::move(text), begin, cursor_);
}

Token Lexer::punctuation() {
    constexpr std::array operators{U"=:=", U"=/=", U"<:-", U"<:=", U"...", U"..", U"&&", U"?=",
                                   U"<<",  U"<-",  U"<=",  U">>",  U">=",  U"->", U"--", U"++",
                                   U"=<",  U"=>",  U"==",  U"/=",  U"||",  U":=", U"::", U"#_"};
    const auto begin = cursor_;
    const auto found = std::ranges::find_if(operators, [this](auto value) { return rest().starts_with(value); });
    if (found != operators.end()) {
        const std::u32string value(*found);
        cursor_ += value.size();
        return token(TokenKind::symbol, value, begin, cursor_);
    }
    const auto value = rest().front();
    ++cursor_;
    if (value > 255) {
        fail(DiagnosticCode::invalid_character, "invalid source character", begin);
    }
    const bool dot = value == U'.' && (rest().empty() || whitespace(rest().front()) || rest().front() == U'%');
    return token(dot ? TokenKind::dot : TokenKind::symbol, std::u32string(1, value), begin, cursor_);
}

std::optional<Token> Lexer::next() {
    if (!pending_.empty()) {
        auto result = std::move(pending_.front());
        pending_.erase(pending_.begin());
        return result;
    }
    if (auto comment = trivia()) {
        return comment;
    }
    if (rest().empty()) {
        return std::nullopt;
    }
    const auto first = rest().front();
    if (atom_start(first) || variable_start(first)) {
        previous_string_ = false;
        return word();
    }
    if (first >= U'0' && first <= U'9') {
        previous_string_ = false;
        return number();
    }
    return literal();
}

std::vector<Token> Lexer::form() {
    std::vector<Token> result;
    while (auto next_token = next()) {
        const auto kind = next_token->kind;
        result.push_back(std::move(*next_token));
        if (kind == TokenKind::dot) {
            break;
        }
    }
    return result;
}

void Lexer::recover_form() {
    pending_.clear();
    previous_string_ = false;
    while (!rest().empty() || !pending_.empty()) {
        const auto begin = cursor_;
        try {
            const auto item = next();
            if (!item || item->kind == TokenKind::dot) {
                return;
            }
        } catch (const LexicalError &) {
            // A failed scanner must not repeatedly attempt the same character.
            if (cursor_ == begin) {
                ++cursor_;
            }
        }
    }
}
} // namespace clause
