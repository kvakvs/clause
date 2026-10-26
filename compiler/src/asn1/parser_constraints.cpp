#include "parser.hpp"
#include <array>

// Added for the ASN.1 compiler (plan 21 step LIB84): constraint specifications (X.680 clauses 49-51, X.682) and the
// extent of deferred value notation.
namespace clause::asn1 {
namespace {
std::shared_ptr<ConstraintSyntax> node(const ConstraintSyntax::Kind kind, const std::uint32_t token) {
    auto result = std::make_shared<ConstraintSyntax>();
    result->kind_ = kind;
    result->token_ = token;
    return result;
}

// Upper-case reserved words that are values.
constexpr std::array VALUE_WORDS = {"TRUE", "FALSE",         "NULL",           "MIN",
                                    "MAX",  "PLUS-INFINITY", "MINUS-INFINITY", "NOT-A-NUMBER"};

bool numeric(const TokenKind kind) { return kind == TokenKind::number || kind == TokenKind::real; }

bool word(const TokenKind kind) { return kind == TokenKind::upper || kind == TokenKind::lower; }

bool field(const TokenKind kind) { return kind == TokenKind::upper_field || kind == TokenKind::lower_field; }

bool value_word(const std::string_view word) {
    for (const auto *candidate : VALUE_WORDS) {
        if (word == candidate) {
            return true;
        }
    }
    return false;
}
} // namespace

std::shared_ptr<ConstraintSyntax> Parser::constraint(const bool table) {
    auto result = element_set(table);
    if (is("!")) {
        const auto token = at_;
        advance();
        value();
        auto exception = node(ConstraintSyntax::Kind::exception, token);
        exception->children_.push_back(result);
        result = exception;
    }
    return result;
}

// Root [, ... [, additions]] or ... [, additions].
std::shared_ptr<ConstraintSyntax> Parser::element_set(const bool table) {
    const auto token = at_;
    std::shared_ptr<ConstraintSyntax> root;
    if (!is("...")) {
        root = unions(table);
        if (!(is(",") && is("...", 1))) {
            return root;
        }
        advance();
    }
    expect("...");
    auto result = node(ConstraintSyntax::Kind::extensible, token);
    result->children_.push_back(root);
    result->children_.push_back(accept(",") ? unions(table) : nullptr);
    return result;
}

std::shared_ptr<ConstraintSyntax> Parser::unions(const bool table) {
    auto first = intersections(table);
    if (!is("|") && !is("UNION")) {
        return first;
    }
    auto result = node(ConstraintSyntax::Kind::union_, first->token_);
    result->children_.push_back(first);
    while (accept("|") || accept("UNION")) {
        result->children_.push_back(intersections(table));
    }
    return result;
}

std::shared_ptr<ConstraintSyntax> Parser::intersections(const bool table) {
    auto first = elements(table);
    if (!is("^") && !is("INTERSECTION")) {
        return first;
    }
    auto result = node(ConstraintSyntax::Kind::intersection, first->token_);
    result->children_.push_back(first);
    while (accept("^") || accept("INTERSECTION")) {
        result->children_.push_back(elements(table));
    }
    return result;
}

// ALL EXCEPT x, or an element optionally followed by EXCEPT x.
std::shared_ptr<ConstraintSyntax> Parser::elements(const bool table) {
    const auto token = at_;
    if (is("ALL") && is("EXCEPT", 1)) {
        at_ += 2;
        auto result = node(ConstraintSyntax::Kind::all_except, token);
        result->children_.push_back(element(table));
        return result;
    }
    auto first = element(table);
    if (!accept("EXCEPT")) {
        return first;
    }
    auto result = node(ConstraintSyntax::Kind::except, token);
    result->children_.push_back(first);
    result->children_.push_back(element(table));
    return result;
}

std::shared_ptr<ConstraintSyntax> Parser::element(const bool table) {
    if (accept("(")) {
        auto inner = element_set(table);
        expect(")");
        return inner;
    }
    if (auto keyword = keyword_element(table)) {
        return keyword;
    }
    if (table && is("{")) {
        return table_element();
    }
    if (contained_type_ahead()) {
        auto result = node(ConstraintSyntax::Kind::contained_subtype, at_);
        result->type_ = type();
        return result;
    }
    return value_element();
}

// Elements introduced by a keyword: SIZE, FROM, WITH COMPONENT(S), CONTAINING, CONSTRAINED BY, PATTERN, INCLUDES.
std::shared_ptr<ConstraintSyntax> Parser::keyword_element(const bool table) {
    using Kind = ConstraintSyntax::Kind;
    const auto token = at_;
    if (is("WITH") && is("COMPONENT", 1)) {
        at_ += 2;
        return nested_element(Kind::inner_type, token, table);
    }
    using Handler = std::shared_ptr<ConstraintSyntax> (Parser::*)(std::uint32_t);

    struct Keyword {
        std::string_view first_;
        std::string_view second_;
        Handler handler_;
    };

    static constexpr std::array KEYWORDS = {Keyword{"SIZE", "", &Parser::size},
                                            Keyword{"FROM", "", &Parser::from},
                                            Keyword{"WITH", "COMPONENTS", &Parser::with_components},
                                            Keyword{"CONTAINING", "", &Parser::containing},
                                            Keyword{"ENCODED", "BY", &Parser::encoded_by},
                                            Keyword{"CONSTRAINED", "BY", &Parser::constrained_by},
                                            Keyword{"PATTERN", "", &Parser::pattern},
                                            Keyword{"INCLUDES", "", &Parser::includes}};
    for (const auto &keyword : KEYWORDS) {
        if (is(keyword.first_) && (keyword.second_.empty() || is(keyword.second_, 1))) {
            at_ += keyword.second_.empty() ? 1 : 2;
            return (this->*keyword.handler_)(token);
        }
    }
    return nullptr;
}

// `KEYWORD ( constraint )` of SIZE, FROM and WITH COMPONENT.
std::shared_ptr<ConstraintSyntax> Parser::nested_element(const ConstraintSyntax::Kind kind, const std::uint32_t token,
                                                         const bool table) {
    auto result = node(kind, token);
    expect("(");
    result->children_.push_back(constraint(table));
    expect(")");
    return result;
}

std::shared_ptr<ConstraintSyntax> Parser::size(const std::uint32_t token) {
    return nested_element(ConstraintSyntax::Kind::size, token, false);
}

std::shared_ptr<ConstraintSyntax> Parser::from(const std::uint32_t token) {
    return nested_element(ConstraintSyntax::Kind::from, token, false);
}

std::shared_ptr<ConstraintSyntax> Parser::with_components(const std::uint32_t /*token*/) { return inner_types(); }

std::shared_ptr<ConstraintSyntax> Parser::containing(const std::uint32_t token) {
    auto result = node(ConstraintSyntax::Kind::containing, token);
    result->type_ = type();
    if (is("ENCODED") && is("BY", 1)) {
        at_ += 2;
        result->value_ = value();
    }
    return result;
}

std::shared_ptr<ConstraintSyntax> Parser::encoded_by(const std::uint32_t token) {
    auto result = node(ConstraintSyntax::Kind::containing, token);
    result->value_ = value();
    return result;
}

// CONSTRAINED BY { ... }: a user-defined constraint, which no encoder checks.
std::shared_ptr<ConstraintSyntax> Parser::constrained_by(const std::uint32_t token) {
    at_ = block_end(at_);
    return node(ConstraintSyntax::Kind::user_defined, token);
}

std::shared_ptr<ConstraintSyntax> Parser::pattern(const std::uint32_t token) {
    auto result = node(ConstraintSyntax::Kind::pattern, token);
    result->value_ = value();
    return result;
}

std::shared_ptr<ConstraintSyntax> Parser::includes(const std::uint32_t token) {
    auto result = node(ConstraintSyntax::Kind::contained_subtype, token);
    result->type_ = type();
    return result;
}

// WITH COMPONENTS { [..., ] name [(constraint)] [PRESENT | ABSENT | OPTIONAL], ... }.
std::shared_ptr<ConstraintSyntax> Parser::inner_types() {
    auto result = node(ConstraintSyntax::Kind::inner_types, at_);
    expect("{");
    if (is("...")) {
        advance();
        result->partial_ = true;
        expect(",");
    }
    while (!is("}")) {
        ConstraintSyntax::Named named;
        named.name_ = expect_kind(TokenKind::lower, "a component name");
        if (accept("(")) {
            named.constraint_ = constraint(false);
            expect(")");
        }
        if (is("PRESENT") || is("ABSENT") || is("OPTIONAL")) {
            named.presence_ = advance().text_;
        }
        result->named_.push_back(std::move(named));
        if (!accept(",") && !is("}")) {
            fail("expected ',' or '}' in WITH COMPONENTS");
        }
    }
    expect("}");
    return result;
}

// {ObjectSet} or {ObjectSet}{@a, @.b}.
std::shared_ptr<ConstraintSyntax> Parser::table_element() {
    auto result = node(ConstraintSyntax::Kind::table, at_);
    const auto end = block_end(at_);
    result->value_ = {at_, end};
    at_ = end;
    if (!accept("{")) {
        return result;
    }
    while (!is("}")) {
        expect("@");
        ConstraintSyntax::AtNotation at;
        while (accept(".")) {
            ++at.level_;
        }
        at.path_.push_back(expect_kind(TokenKind::lower, "a component name"));
        while (accept(".")) {
            at.path_.push_back(expect_kind(TokenKind::lower, "a component name"));
        }
        result->at_.push_back(std::move(at));
        if (!accept(",") && !is("}")) {
            fail("expected ',' or '}' in a component relation");
        }
    }
    expect("}");
    return result;
}

// A single value or a value range `lower [<] .. [<] upper`.
std::shared_ptr<ConstraintSyntax> Parser::value_element() {
    auto result = node(ConstraintSyntax::Kind::single_value, at_);
    result->value_ = value();
    if (accept("<")) {
        result->lower_open_ = true;
    }
    if (!accept("..")) {
        if (result->lower_open_) {
            fail("expected '..' after '<'");
        }
        return result;
    }
    result->kind_ = ConstraintSyntax::Kind::value_range;
    result->upper_open_ = accept("<");
    result->upper_ = value();
    return result;
}

// A contained subtype starts with a type: a built-in type keyword or a type reference (never a value word).
bool Parser::contained_type_ahead() const {
    if (is("[")) {
        return true;
    }
    if (!is_kind(TokenKind::upper)) {
        return false;
    }
    const auto &word = peek().text_;
    if (value_word(word)) {
        return false;
    }
    // `Module.value` is a value; `Module.Type` and `Type` are types.
    return !(is(".", 1) && is_kind(TokenKind::lower, 2));
}

TokenRange Parser::value() {
    const auto begin = at_;
    at_ = value_end(at_);
    if (at_ == begin) {
        fail("expected a value");
    }
    return {begin, at_};
}

std::uint32_t Parser::value_end(const std::uint32_t at) const {
    const auto kind = token_at(at).kind_;
    if (token_is(at, "{")) {
        return block_end(at);
    }
    if (token_is(at, "-") && numeric(token_at(at + 1).kind_)) {
        return at + 2;
    }
    if (token_is(at, "CONTAINING")) {
        return value_end(at + 1);
    }
    if (numeric(kind) || kind == TokenKind::cstring || kind == TokenKind::bstring || kind == TokenKind::hstring) {
        return at + 1;
    }
    return word(kind) ? reference_value_end(at) : at;
}

// A value word, `[Module.]reference`, a choice value `id : value`, a glued parameter list or `obj.&field`.
std::uint32_t Parser::reference_value_end(const std::uint32_t at) const {
    const auto &token = token_at(at);
    // `Type : value` of an open type, or `name : value` of a CHOICE.
    if (word(token.kind_) && token_is(at + 1, ":")) {
        return value_end(at + 2);
    }
    if (token.kind_ == TokenKind::upper && value_word(token.text_)) {
        return at + 1;
    }
    return reference_suffix_end(qualified_end(at));
}

// The end of the actual parameters and `.&field` selections after a reference ending at `end`.
std::uint32_t Parser::reference_suffix_end(std::uint32_t end) const {
    if (token_is(end, "{") && token_at(end).glued_) {
        end = block_end(end);
    }
    while (token_is(end, ".") && field(token_at(end + 1).kind_)) {
        end += 2;
    }
    return end;
}

// The end of `name` or `Module.name`.
std::uint32_t Parser::qualified_end(const std::uint32_t at) const {
    return token_is(at + 1, ".") && word(token_at(at + 2).kind_) ? at + 3 : at + 1;
}
} // namespace clause::asn1
