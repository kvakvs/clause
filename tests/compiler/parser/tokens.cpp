#include "parsing/operator_info.hpp"
#include "parsing/token_cursor.hpp"
#include "preprocessor/cursor.hpp"
#include <limits>
#include <stdexcept>

using namespace clause;

// Keep checks active in all build modes.
void require(bool condition) {
    if (!condition) {
        throw std::runtime_error("shared token parsing check failed");
    }
}

// Exercise rollback, overflow-safe lookahead, and explicit EOF ownership.
void cursor_bounds() {
    SourceManager sources;
    Lexer lexer(sources.add("cursor.erl", "a + 1."));
    const auto tokens = lexer.form();
    Token end = tokens.back();
    end.location.column = 7;
    TokenCursor cursor(tokens, end);
    require(cursor.peek(std::numeric_limits<std::size_t>::max()) == nullptr);
    const auto mark = cursor.checkpoint();
    require(cursor.consume()->text() == U"a");
    require(cursor.take(TokenKind::symbol, U"+"));
    cursor.restore(mark);
    require(cursor.offset() == 0 && cursor.remaining().size() == 4);
    cursor.restore(tokens.size());
    require(!cursor.consume() && cursor.empty() && cursor.anchor().location.column == 7);
    TokenCursor empty({}, end);
    require(empty.anchor().spelling.source == tokens.back().spelling.source);
}

// Quoted atoms must never become keywords or delimiters, even with identical text.
void categories() {
    SourceManager sources;
    Lexer lexer(sources.add("categories.erl", "'end' end #r.f."));
    const auto tokens = lexer.form();
    TokenCursor cursor(tokens, tokens.back());
    require(!cursor.take_syntax(U"end"));
    require(cursor.take(TokenKind::atom, U"end"));
    require(cursor.take(TokenKind::keyword, U"end"));
    cursor.restore(4);
    require(cursor.take(TokenKind::symbol, U"."));
    require(cursor.consume()->text() == U"f");
    require(!cursor.take(TokenKind::symbol, U"."));
    require(cursor.take(TokenKind::dot, U"."));
    DirectiveCursor directive(std::span(tokens).last(1), tokens.back().spelling);
    require(!directive.take(U"."));
}

// Audit new grammar terminals with the original lexer and longest-match rules.
void terminals() {
    SourceManager sources;
    Lexer lexer(sources.add("terminals.erl", "#_ ?= && <:- <:= :: .. ... <- <= ."));
    const auto tokens = lexer.form();
    constexpr std::u32string_view names[]{U"#_", U"?=", U"&&", U"<:-", U"<:=", U"::", U"..", U"...", U"<-", U"<="};
    require(tokens.size() == std::size(names) + 1);
    for (std::size_t i = 0; i < std::size(names); ++i) {
        require(tokens[i].kind == TokenKind::symbol && tokens[i].text() == names[i]);
    }
    require(tokens.back().kind == TokenKind::dot);
}

// Verify distinct grammar contexts and grouping metadata against the pinned grammar.
void operators() {
    SourceManager sources;
    Lexer lexer(sources.add("operators.erl", "= ! ++ + * == :: | .. 'andalso'"));
    const auto tokens = lexer.form();
    require(infix_operator(tokens[0], OperatorContext::expression)->precedence == 100);
    require(!infix_operator(tokens[0], OperatorContext::condition));
    require(infix_operator(tokens[2], OperatorContext::condition)->associativity == Associativity::right);
    require(infix_operator(tokens[3], OperatorContext::expression)->precedence == 400);
    require(infix_operator(tokens[4], OperatorContext::type)->precedence == 500);
    require(infix_operator(tokens[5], OperatorContext::condition)->associativity == Associativity::none);
    require(!infix_operator(tokens[5], OperatorContext::type));
    require(infix_operator(tokens[6], OperatorContext::type)->associativity == Associativity::right);
    require(!infix_operator(tokens[6], OperatorContext::expression));
    require(infix_operator(tokens[7], OperatorContext::type)->precedence == 170);
    require(infix_operator(tokens[8], OperatorContext::type)->associativity == Associativity::none);
    require(!infix_operator(tokens[9], OperatorContext::condition));
}

// Preserve logical and physical macro origins in the shared diagnostic constructor.
void diagnostics() {
    SourceManager sources;
    Lexer lexer(sources.add("physical.erl", "+"));
    auto token = *lexer.next();
    token.location = {"logical.erl", 20, 5};
    token.origins.push_back(token.spelling);
    const auto error = token_diagnostic(DiagnosticCode::invalid_condition, "test", token);
    require(error.location->file == "logical.erl" && error.location->line == 20);
    require(error.related.size() == 1 && error.primary.source->name == "physical.erl");
}

// Directive failures reach standard handlers without losing structured source context.
void diagnostic_exception() {
    SourceManager sources;
    Lexer lexer(sources.add("invalid.erl", "+"));
    const auto token = lexer.next().value();
    DirectiveCursor cursor(std::span(&token, 1), token.spelling);
    bool caught = false;
    try {
        cursor.expect(U"(");
    } catch (const std::exception &error) {
        caught = true;
        require(std::string_view(error.what()) == "expected '('");
        const auto *failure = dynamic_cast<const DiagnosticError *>(&error);
        require(failure != nullptr);
        require(failure->diagnostic.code == DiagnosticCode::malformed_directive);
        require(failure->diagnostic.primary.source == token.spelling.source);
        require(failure->diagnostic.location.value().file == "invalid.erl");
    }
    require(caught);
}

// Physical byte offsets are an embedding contract not printed by CLI diagnostics.
void source_offsets() {
    SourceManager sources;
    const auto source = sources.add("unicode.erl", "% λ\r\natom.");
    require(source->position(6).line == 2 && source->position(6).byte == 7);
    Lexer lexer(source);
    const auto token = lexer.next().value();
    require(token.spelling.source->spelling(token.spelling.begin, token.spelling.end) == "atom");
    try {
        sources.add("invalid.erl", "\xc0\x80");
        require(false);
    } catch (const EncodingError &error) {
        require(error.byte == 0);
    }
}

// Run shared cursor, lexical-boundary, operator, and provenance regressions.
int main() {
    source_offsets();
    cursor_bounds();
    categories();
    terminals();
    operators();
    diagnostics();
    diagnostic_exception();
}
