#include "token_cursor.hpp"
#include <cassert>
#include <stdexcept>

namespace clause {
TokenCursor::TokenCursor(const std::span<const Token> tokens, Token end) : tokens_(tokens), end_(std::move(end)) {}

const Token *TokenCursor::peek(const std::size_t lookahead) const {
    return lookahead < tokens_.size() - offset_ ? &tokens_[offset_ + lookahead] : nullptr;
}

const Token *TokenCursor::consume() {
    const auto *token = peek();
    if (token) {
        ++offset_;
    }
    return token;
}

bool TokenCursor::take(const TokenKind kind, const std::u32string_view text) {
    const auto *token = peek();
    if (!token || token->kind != kind || token->text() != text) {
        return false;
    }
    ++offset_;
    return true;
}

bool TokenCursor::take_syntax(const std::u32string_view text) {
    const auto *token = peek();
    if (!token || !syntax(*token, text)) {
        return false;
    }
    ++offset_;
    return true;
}

std::size_t TokenCursor::checkpoint() const { return offset_; }

void TokenCursor::restore(const std::size_t offset) {
    // The parser restores only its own checkpoints (VAL13: an assert, not a test).
    assert(offset <= tokens_.size());
    if (offset > tokens_.size()) {
        throw std::out_of_range("token cursor checkpoint");
    }
    offset_ = offset;
}

std::size_t TokenCursor::offset() const { return offset_; }

bool TokenCursor::empty() const { return offset_ == tokens_.size(); }

std::span<const Token> TokenCursor::remaining() const { return tokens_.subspan(offset_); }

const Token &TokenCursor::anchor() const { return empty() ? end_ : tokens_[offset_]; }
} // namespace clause
