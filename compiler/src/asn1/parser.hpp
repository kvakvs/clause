#pragma once
// Added for the ASN.1 compiler (plan 21 step LIB84): recursive descent over a module's tokens. The checker reuses it
// to parse deferred token ranges once their governor is known.
#include "syntax.hpp"
#include <string_view>

namespace clause::asn1 {
class Parser {
  public:
    // Parse `range` of `module`'s tokens; the module supplies file names for diagnostics.
    Parser(const ModuleSyntax &module, TokenRange range);

    // Grammar entries the checker uses on deferred ranges.
    std::shared_ptr<TypeSyntax> type();
    std::shared_ptr<ConstraintSyntax> constraint(bool table);
    // The end of the value, object or object set notation starting here, which the cursor then skips.
    TokenRange value();
    std::vector<SyntaxItem> syntax_list();
    // `[Module.]name [{arguments}] [.&field...]` of an object or object set reference, of either case.
    std::shared_ptr<TypeSyntax> defined();

    // Token access.
    const Token &peek(std::uint32_t ahead = 0) const;

    std::uint32_t position() const { return at_; }

    void seek(std::uint32_t at) { at_ = at; }

    bool at_end() const { return at_ >= end_; }

    bool is(std::string_view text, std::uint32_t ahead = 0) const;
    bool is_kind(TokenKind kind, std::uint32_t ahead = 0) const;
    bool accept(std::string_view text);
    const Token &advance();
    void expect(std::string_view text);
    std::string expect_kind(TokenKind kind, std::string_view what);
    [[noreturn]] void fail(const std::string &message) const;
    [[noreturn]] void fail_at(std::uint32_t token, const std::string &message) const;
    // The end of the braces block opening at the cursor (exclusive), without moving.
    std::uint32_t block_end(std::uint32_t open) const;

  private:
    friend class ModuleParser;
    void tag(TypeSyntax &type);
    std::shared_ptr<TypeSyntax> untagged();
    bool builtin(TypeSyntax &type);
    bool simple_builtin(TypeSyntax &type);
    bool open_builtin(TypeSyntax &type);
    bool string_builtin(TypeSyntax &type);
    void constructed(TypeSyntax &type, TypeKind kind);
    void collection(TypeSyntax &type, TypeKind kind);
    void components(TypeSyntax &type);
    bool version_bracket(std::string_view bracket) const;
    void group_begin(TypeSyntax &type);
    ComponentSyntax component();
    void named_numbers(TypeSyntax &type, bool enumeration);
    NamedNumber named_number(bool enumeration);
    void referenced(TypeSyntax &type);
    void reference_name(TypeSyntax &type);
    void arguments(TypeSyntax &type);
    void fields(TypeSyntax &type);
    void constraints(TypeSyntax &type);
    std::shared_ptr<ConstraintSyntax> element_set(bool table);
    std::shared_ptr<ConstraintSyntax> unions(bool table);
    std::shared_ptr<ConstraintSyntax> intersections(bool table);
    std::shared_ptr<ConstraintSyntax> elements(bool table);
    std::shared_ptr<ConstraintSyntax> element(bool table);
    std::shared_ptr<ConstraintSyntax> keyword_element(bool table);
    std::shared_ptr<ConstraintSyntax> nested_element(ConstraintSyntax::Kind kind, std::uint32_t token, bool table);
    std::shared_ptr<ConstraintSyntax> size(std::uint32_t token);
    std::shared_ptr<ConstraintSyntax> from(std::uint32_t token);
    std::shared_ptr<ConstraintSyntax> with_components(std::uint32_t token);
    std::shared_ptr<ConstraintSyntax> containing(std::uint32_t token);
    std::shared_ptr<ConstraintSyntax> encoded_by(std::uint32_t token);
    std::shared_ptr<ConstraintSyntax> constrained_by(std::uint32_t token);
    std::shared_ptr<ConstraintSyntax> pattern(std::uint32_t token);
    std::shared_ptr<ConstraintSyntax> includes(std::uint32_t token);
    std::shared_ptr<ConstraintSyntax> inner_types();
    std::shared_ptr<ConstraintSyntax> table_element();
    std::shared_ptr<ConstraintSyntax> value_element();
    bool contained_type_ahead() const;
    std::uint32_t value_end(std::uint32_t at) const;
    // Token access by absolute index; past the range it is the final `end` token.
    const Token &token_at(std::uint32_t index) const;
    bool token_is(std::uint32_t index, std::string_view text) const;
    std::uint32_t reference_value_end(std::uint32_t at) const;
    std::uint32_t reference_suffix_end(std::uint32_t end) const;
    std::uint32_t qualified_end(std::uint32_t at) const;

    const ModuleSyntax &module_;
    std::uint32_t at_;
    std::uint32_t end_;
};

// True for the built-in type keywords that start a Type.
bool type_keyword(std::string_view word);
} // namespace clause::asn1
