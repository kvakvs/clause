#include "parser.hpp"
#include <algorithm>
#include <array>

// Added for the ASN.1 compiler (plan 21 step LIB84): token cursor and the Type grammar of X.680.
namespace clause::asn1 {
namespace {
constexpr std::array RESTRICTED_STRINGS = {"BMPString",    "GeneralString",   "GraphicString",   "IA5String",
                                           "ISO646String", "NumericString",   "PrintableString", "TeletexString",
                                           "T61String",    "UniversalString", "UTF8String",      "VideotexString",
                                           "VisibleString"};

constexpr std::array TYPE_KEYWORDS = {
    "BOOLEAN", "NULL",  "REAL",     "EXTERNAL",        "ObjectDescriptor", "UTCTime",        "INTEGER",  "ENUMERATED",
    "BIT",     "OCTET", "OBJECT",   "RELATIVE-OID",    "CHARACTER",        "EMBEDDED",       "SEQUENCE", "SET",
    "CHOICE",  "ANY",   "INSTANCE", "TYPE-IDENTIFIER", "ABSTRACT-SYNTAX",  "GeneralizedTime"};

bool restricted_string(const std::string_view word) {
    return std::ranges::find(RESTRICTED_STRINGS, word) != RESTRICTED_STRINGS.end();
}
} // namespace

bool type_keyword(const std::string_view word) {
    return restricted_string(word) || std::ranges::find(TYPE_KEYWORDS, word) != TYPE_KEYWORDS.end();
}

Parser::Parser(const ModuleSyntax &module, const TokenRange range)
    : module_(module), at_(range.begin_), end_(range.end_) {}

const Token &Parser::token_at(const std::uint32_t index) const {
    // The token vector always ends with an `end` token, which also stands for any position past `end_`.
    return index < end_ ? module_.tokens_[index] : module_.tokens_.back();
}

bool Parser::token_is(const std::uint32_t index, const std::string_view text) const {
    const auto &token = token_at(index);
    return token.kind_ != TokenKind::end && token.kind_ != TokenKind::cstring && token.text_ == text;
}

const Token &Parser::peek(const std::uint32_t ahead) const { return token_at(at_ + ahead); }

bool Parser::is(const std::string_view text, const std::uint32_t ahead) const { return token_is(at_ + ahead, text); }

bool Parser::is_kind(const TokenKind kind, const std::uint32_t ahead) const { return peek(ahead).kind_ == kind; }

bool Parser::accept(const std::string_view text) {
    if (!is(text)) {
        return false;
    }
    ++at_;
    return true;
}

const Token &Parser::advance() {
    if (at_end()) {
        fail("unexpected end of input");
    }
    return module_.tokens_[at_++];
}

void Parser::expect(const std::string_view text) {
    if (!accept(text)) {
        fail("expected '" + std::string(text) + "'");
    }
}

std::string Parser::expect_kind(const TokenKind kind, const std::string_view what) {
    if (!is_kind(kind)) {
        fail("expected " + std::string(what));
    }
    return advance().text_;
}

void Parser::fail(const std::string &message) const { fail_at(std::min(at_, end_), message); }

void Parser::fail_at(const std::uint32_t token, const std::string &message) const {
    const auto &found = token < module_.tokens_.size() ? module_.tokens_[token] : module_.tokens_.back();
    const auto near = found.kind_ == TokenKind::end ? std::string("end of input") : "'" + found.text_ + "'";
    throw Error(module_.location(token), message + " near " + near);
}

std::uint32_t Parser::block_end(const std::uint32_t open) const {
    int depth = 0;
    for (auto index = open; index < end_; ++index) {
        const auto &token = module_.tokens_[index];
        if (token.kind_ == TokenKind::punctuation && token.text_ == "{") {
            ++depth;
        } else if (token.kind_ == TokenKind::punctuation && token.text_ == "}" && --depth == 0) {
            return index + 1;
        }
    }
    fail_at(open, "unbalanced braces");
}

std::shared_ptr<TypeSyntax> Parser::type() {
    std::vector<TagSyntax> tags;
    while (is("[") && !is("[", 1)) {
        TypeSyntax holder;
        tag(holder);
        tags.push_back(holder.tags_.front());
    }
    auto result = untagged();
    result->tags_.insert(result->tags_.begin(), tags.begin(), tags.end());
    constraints(*result);
    return result;
}

// `[ [class] number ] [IMPLICIT | EXPLICIT]`.
void Parser::tag(TypeSyntax &type) {
    expect("[");
    TagSyntax result;
    if (accept("UNIVERSAL")) {
        result.class_ = TagClass::universal;
    } else if (accept("APPLICATION")) {
        result.class_ = TagClass::application;
    } else if (accept("PRIVATE")) {
        result.class_ = TagClass::private_;
    }
    const auto begin = at_;
    while (!is("]")) {
        advance();
    }
    result.number_ = {begin, at_};
    expect("]");
    if (accept("IMPLICIT")) {
        result.mode_ = TagMode::implicit;
    } else if (accept("EXPLICIT")) {
        result.mode_ = TagMode::explicit_;
    }
    type.tags_.push_back(result);
}

std::shared_ptr<TypeSyntax> Parser::untagged() {
    auto result = std::make_shared<TypeSyntax>();
    result->token_ = at_;
    if (!builtin(*result)) {
        referenced(*result);
    }
    return result;
}

bool Parser::builtin(TypeSyntax &type) {
    if (simple_builtin(type) || string_builtin(type)) {
        return true;
    }
    if (accept("SEQUENCE")) {
        constructed(type, TypeKind::sequence);
    } else if (accept("SET")) {
        constructed(type, TypeKind::set);
    } else if (accept("CHOICE")) {
        type.kind_ = TypeKind::choice;
        components(type);
    } else if (accept("INTEGER")) {
        type.kind_ = TypeKind::integer;
        named_numbers(type, false);
    } else if (accept("ENUMERATED")) {
        type.kind_ = TypeKind::enumerated;
        named_numbers(type, true);
    } else if (is("BIT") && is("STRING", 1)) {
        at_ += 2;
        type.kind_ = TypeKind::bit_string;
        named_numbers(type, false);
    } else {
        return false;
    }
    return true;
}

// Built-in types spelled by fixed keywords only.
bool Parser::simple_builtin(TypeSyntax &type) {
    struct Spelling {
        std::string_view first_;
        std::string_view second_;
        TypeKind kind_;
    };

    static constexpr std::array SPELLINGS = {
        Spelling{"BOOLEAN", "", TypeKind::boolean},
        Spelling{"NULL", "", TypeKind::null},
        Spelling{"REAL", "", TypeKind::real},
        Spelling{"EXTERNAL", "", TypeKind::external},
        Spelling{"ObjectDescriptor", "", TypeKind::object_descriptor},
        Spelling{"UTCTime", "", TypeKind::utc_time},
        Spelling{"GeneralizedTime", "", TypeKind::generalized_time},
        Spelling{"RELATIVE-OID", "", TypeKind::relative_oid},
        Spelling{"OCTET", "STRING", TypeKind::octet_string},
        Spelling{"OBJECT", "IDENTIFIER", TypeKind::object_identifier},
        Spelling{"CHARACTER", "STRING", TypeKind::character_string_type},
        Spelling{"EMBEDDED", "PDV", TypeKind::embedded_pdv},
    };
    for (const auto &spelling : SPELLINGS) {
        if (is(spelling.first_) && (spelling.second_.empty() || is(spelling.second_, 1))) {
            at_ += spelling.second_.empty() ? 1 : 2;
            type.kind_ = spelling.kind_;
            return true;
        }
    }
    return open_builtin(type);
}

// `ANY [DEFINED BY id]` and `INSTANCE OF Class`, whose values are open types.
bool Parser::open_builtin(TypeSyntax &type) {
    if (accept("ANY")) {
        type.kind_ = TypeKind::any;
        if (accept("DEFINED")) {
            expect("BY");
            type.identifier_ = expect_kind(TokenKind::lower, "an identifier");
        }
        return true;
    }
    if (is("INSTANCE") && is("OF", 1)) {
        at_ += 2;
        type.kind_ = TypeKind::instance_of;
        type.reference_ = {"", expect_kind(TokenKind::upper, "a class reference"), at_ - 1};
        return true;
    }
    return false;
}

bool Parser::string_builtin(TypeSyntax &type) {
    if (!is_kind(TokenKind::upper) || !restricted_string(peek().text_)) {
        return false;
    }
    type.kind_ = TypeKind::restricted_string;
    type.string_ = advance().text_;
    if (type.string_ == "ISO646String") {
        type.string_ = "VisibleString";
    } else if (type.string_ == "T61String") {
        type.string_ = "TeletexString";
    }
    return true;
}

// SEQUENCE/SET: a component list, or a collection `OF` with an optional size constraint.
void Parser::constructed(TypeSyntax &type, const TypeKind kind) {
    if (is("{")) {
        type.kind_ = kind;
        components(type);
        return;
    }
    collection(type, kind == TypeKind::sequence ? TypeKind::sequence_of : TypeKind::set_of);
}

void Parser::collection(TypeSyntax &type, const TypeKind kind) {
    type.kind_ = kind;
    if (accept("SIZE")) {
        auto size = std::make_shared<ConstraintSyntax>();
        size->kind_ = ConstraintSyntax::Kind::size;
        size->token_ = at_ - 1;
        expect("(");
        size->children_.push_back(constraint(false));
        expect(")");
        type.collection_constraint_ = size;
    } else if (accept("(")) {
        type.collection_constraint_ = constraint(false);
        expect(")");
    }
    expect("OF");
    if (is_kind(TokenKind::lower) && !is("<", 1)) {
        type.element_name_ = advance().text_;
    }
    type.element_ = this->type();
}

void Parser::components(TypeSyntax &type) {
    expect("{");
    while (!is("}")) {
        if (version_bracket("[")) {
            group_begin(type);
            continue;
        }
        if (version_bracket("]")) {
            at_ += 2;
            type.components_.push_back(ComponentSyntax::marker(ComponentSyntax::Kind::group_end, at_));
        } else {
            type.components_.push_back(component());
        }
        if (!accept(",") && !is("}") && !version_bracket("]")) {
            fail("expected ',' or '}' in a component list");
        }
    }
    expect("}");
}

// `[[` or `]]` of an extension addition group: two brackets with nothing between them.
bool Parser::version_bracket(const std::string_view bracket) const {
    return is(bracket) && is(bracket, 1) && peek(1).glued_;
}

// `[[` with an optional `n:` version number.
void Parser::group_begin(TypeSyntax &type) {
    at_ += 2;
    auto group = ComponentSyntax::marker(ComponentSyntax::Kind::group_begin, at_);
    if (is_kind(TokenKind::number) && is(":", 1)) {
        group.version_ = static_cast<std::uint32_t>(std::stoul(advance().text_));
        advance();
    }
    type.components_.push_back(std::move(group));
}

ComponentSyntax Parser::component() {
    ComponentSyntax result;
    result.token_ = at_;
    if (accept("...")) {
        result.kind_ = ComponentSyntax::Kind::ellipsis;
        if (accept("!")) {
            value();
        }
        return result;
    }
    if (is("COMPONENTS") && is("OF", 1)) {
        at_ += 2;
        result.kind_ = ComponentSyntax::Kind::components_of;
        result.type_ = type();
        return result;
    }
    result.name_ = expect_kind(TokenKind::lower, "a component name");
    result.type_ = type();
    if (accept("OPTIONAL")) {
        result.presence_ = Presence::optional;
    } else if (accept("DEFAULT")) {
        result.presence_ = Presence::default_;
        result.default_ = value();
    }
    return result;
}

// `{ name(value), ... }` of INTEGER and BIT STRING, or the items of ENUMERATED (values optional, `...` allowed).
void Parser::named_numbers(TypeSyntax &type, const bool enumeration) {
    if (!enumeration && !is("{")) {
        return;
    }
    expect("{");
    while (!is("}")) {
        type.named_.push_back(named_number(enumeration));
        if (!accept(",") && !is("}")) {
            fail("expected ',' or '}'");
        }
    }
    expect("}");
}

NamedNumber Parser::named_number(const bool enumeration) {
    NamedNumber item;
    item.token_ = at_;
    if (enumeration && accept("...")) {
        item.name_ = "...";
        if (accept("!")) {
            value();
        }
        return item;
    }
    item.name_ = expect_kind(TokenKind::lower, "a name");
    if (!accept("(")) {
        if (!enumeration) {
            fail("expected '(' after a named number");
        }
        return item;
    }
    const auto begin = at_;
    while (!is(")")) {
        advance();
    }
    item.value_ = {begin, at_};
    expect(")");
    return item;
}

// A defined type, parameterized type, class or object field type, or selection type.
void Parser::referenced(TypeSyntax &type) {
    if (is_kind(TokenKind::lower) && is("<", 1)) {
        type.kind_ = TypeKind::selection;
        type.identifier_ = advance().text_;
        advance();
        type.element_ = this->type();
        return;
    }
    reference_name(type);
    if (is("{")) {
        arguments(type);
    }
    fields(type);
    if (type.kind_ == TypeKind::reference && module_.tokens_[type.reference_.token_].kind_ == TokenKind::lower) {
        fail_at(type.reference_.token_, "expected a type");
    }
    // The classes X.681 defines are written as reserved words.
    if (type.kind_ == TypeKind::reference && type.reference_.name_ == "TYPE-IDENTIFIER") {
        type.kind_ = TypeKind::type_identifier;
    } else if (type.kind_ == TypeKind::reference && type.reference_.name_ == "ABSTRACT-SYNTAX") {
        type.kind_ = TypeKind::abstract_syntax;
    }
}

std::shared_ptr<TypeSyntax> Parser::defined() {
    auto result = std::make_shared<TypeSyntax>();
    result->token_ = at_;
    if (!is_kind(TokenKind::upper) && !is_kind(TokenKind::lower)) {
        fail("expected a reference");
    }
    result->kind_ = TypeKind::reference;
    result->reference_ = {"", advance().text_, at_ - 1};
    if (is(".") && (is_kind(TokenKind::upper, 1) || is_kind(TokenKind::lower, 1))) {
        advance();
        result->reference_.module_ = result->reference_.name_;
        result->reference_.name_ = advance().text_;
    }
    if (is("{")) {
        arguments(*result);
    }
    fields(*result);
    return result;
}

// `Name` or `Module.Name` of a reference (a lower-case name only before `.&field`).
void Parser::reference_name(TypeSyntax &type) {
    const bool word = is_kind(TokenKind::upper) || is_kind(TokenKind::lower);
    if (!word || (reserved_word(peek().text_) && !type_keyword(peek().text_))) {
        fail("expected a type");
    }
    type.kind_ = TypeKind::reference;
    type.reference_.token_ = at_;
    type.reference_.name_ = advance().text_;
    if (is(".") && is_kind(TokenKind::upper, 1)) {
        advance();
        type.reference_.module_ = type.reference_.name_;
        type.reference_.name_ = advance().text_;
    }
}

// `{ argument, ... }`: each argument's tokens, split at top-level commas.
void Parser::arguments(TypeSyntax &type) {
    const auto open = at_;
    const auto close = block_end(open) - 1;
    type.parameterized_ = true;
    auto begin = open + 1;
    int depth = 0;
    for (auto index = open + 1; index < close; ++index) {
        const auto &token = module_.tokens_[index];
        if (token.kind_ != TokenKind::punctuation) {
            continue;
        }
        if (token.text_ == "{" || token.text_ == "(") {
            ++depth;
        } else if (token.text_ == "}" || token.text_ == ")") {
            --depth;
        } else if (token.text_ == "," && depth == 0) {
            type.arguments_.push_back({begin, index});
            begin = index + 1;
        }
    }
    type.arguments_.push_back({begin, close});
    at_ = close + 1;
}

// `.&field.&field` after a class, object or object set reference.
void Parser::fields(TypeSyntax &type) {
    while (is(".") && (is_kind(TokenKind::upper_field, 1) || is_kind(TokenKind::lower_field, 1))) {
        advance();
        type.fields_.push_back(advance().text_);
    }
    if (type.fields_.empty()) {
        return;
    }
    const auto &first = module_.tokens_[type.reference_.token_];
    type.kind_ = first.kind_ == TokenKind::lower ? TypeKind::object_field : TypeKind::class_field;
}

void Parser::constraints(TypeSyntax &type) {
    const bool table = type.kind_ == TypeKind::class_field || type.kind_ == TypeKind::object_field ||
                       type.kind_ == TypeKind::instance_of;
    while (accept("(")) {
        type.constraints_.push_back(constraint(table));
        expect(")");
    }
}

std::vector<SyntaxItem> Parser::syntax_list() {
    std::vector<SyntaxItem> items;
    while (!is("}") && !is("]")) {
        SyntaxItem item;
        if (accept("[")) {
            item.kind_ = SyntaxItem::Kind::optional;
            item.items_ = syntax_list();
            expect("]");
        } else if (is_kind(TokenKind::upper_field) || is_kind(TokenKind::lower_field)) {
            item.kind_ = SyntaxItem::Kind::field;
            item.text_ = advance().text_;
        } else if (is_kind(TokenKind::upper) || is(",")) {
            item.text_ = advance().text_;
        } else {
            fail("unexpected token in WITH SYNTAX");
        }
        items.push_back(std::move(item));
    }
    return items;
}
} // namespace clause::asn1
