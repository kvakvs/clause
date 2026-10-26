#include "parser.hpp"

// Added for the ASN.1 compiler (plan 21 step LIB84): module definitions, imports and exports, assignments and
// information object class definitions (X.680 clause 13, X.681 clause 9, X.683 clause 8).
namespace clause::asn1 {
Location ModuleSyntax::location(const std::uint32_t token) const {
    const auto &found = token < tokens_.size() ? tokens_[token] : tokens_.back();
    return {file_, found.line_, found.column_};
}

// Parses one module definition starting at the cursor; owns the module being filled.
class ModuleParser {
  public:
    ModuleParser(ModuleSyntax &module, Parser &parser) : module_(module), parser_(parser) {}

    void run();

  private:
    void header();
    void tag_default();
    void exports();
    void imports();
    void skip_assigned_identifier();
    void assignment();
    void parameters(AssignmentSyntax &assignment);
    void type_or_class(AssignmentSyntax &assignment);
    void right_side(AssignmentSyntax &assignment);
    std::shared_ptr<ClassSyntax> class_body();
    FieldSpecSyntax field_spec();
    void field_tail(FieldSpecSyntax &field);
    std::string symbol();

    ModuleSyntax &module_;
    Parser &parser_;
};

void ModuleParser::run() {
    header();
    parser_.expect("BEGIN");
    exports();
    imports();
    while (!parser_.is("END")) {
        assignment();
    }
    parser_.expect("END");
}

// `Name [{oid}] DEFINITIONS [tag default] [EXTENSIBILITY IMPLIED] ::=`.
void ModuleParser::header() {
    module_.name_ = parser_.expect_kind(TokenKind::upper, "a module name");
    if (parser_.is("{")) {
        parser_.seek(parser_.block_end(parser_.position()));
    }
    parser_.expect("DEFINITIONS");
    tag_default();
    if (parser_.accept("EXTENSIBILITY")) {
        parser_.expect("IMPLIED");
        module_.extensibility_implied_ = true;
    }
    parser_.expect("::=");
}

void ModuleParser::tag_default() {
    if (parser_.accept("EXPLICIT")) {
        module_.tag_default_ = TagMode::explicit_;
    } else if (parser_.accept("IMPLICIT")) {
        module_.tag_default_ = TagMode::implicit;
    } else if (parser_.accept("AUTOMATIC")) {
        module_.tag_default_ = TagMode::implicit;
        module_.automatic_tags_ = true;
    } else {
        return;
    }
    parser_.expect("TAGS");
}

void ModuleParser::exports() {
    if (!parser_.accept("EXPORTS")) {
        return;
    }
    if (parser_.accept("ALL")) {
        parser_.expect(";");
        return;
    }
    module_.exports_all_ = false;
    while (!parser_.accept(";")) {
        module_.exports_.push_back(symbol());
        parser_.accept(",");
    }
}

// A symbol of an EXPORTS or IMPORTS list: a reference, optionally with the `{}` of a parameterized one.
std::string ModuleParser::symbol() {
    if (!parser_.is_kind(TokenKind::upper) && !parser_.is_kind(TokenKind::lower)) {
        parser_.fail("expected a symbol");
    }
    auto name = parser_.advance().text_;
    if (parser_.is("{") && parser_.is("}", 1)) {
        parser_.seek(parser_.position() + 2);
    }
    return name;
}

void ModuleParser::imports() {
    if (!parser_.accept("IMPORTS")) {
        return;
    }
    while (!parser_.accept(";")) {
        ImportSyntax import;
        import.token_ = parser_.position();
        while (!parser_.is("FROM")) {
            import.symbols_.push_back(symbol());
            parser_.accept(",");
        }
        parser_.expect("FROM");
        import.module_ = parser_.expect_kind(TokenKind::upper, "a module name");
        skip_assigned_identifier();
        module_.imports_.push_back(std::move(import));
    }
}

// `{oid}`, or a defined value not followed by `,` or FROM (which would make it the next list's symbol), and
// `WITH SUCCESSORS | DESCENDANTS`.
void ModuleParser::skip_assigned_identifier() {
    if (parser_.is("{")) {
        parser_.seek(parser_.block_end(parser_.position()));
    } else if (parser_.is_kind(TokenKind::lower) && !parser_.is(",", 1) && !parser_.is("FROM", 1)) {
        parser_.advance();
    }
    if (parser_.is("WITH") && (parser_.is("SUCCESSORS", 1) || parser_.is("DESCENDANTS", 1))) {
        parser_.seek(parser_.position() + 2);
    }
}

void ModuleParser::assignment() {
    AssignmentSyntax result;
    result.token_ = parser_.position();
    const bool upper = parser_.is_kind(TokenKind::upper);
    if (!upper && !parser_.is_kind(TokenKind::lower)) {
        parser_.fail("expected an assignment");
    }
    result.name_ = parser_.advance().text_;
    if (parser_.is("{")) {
        parameters(result);
    }
    if (upper && parser_.accept("::=")) {
        type_or_class(result);
    } else {
        result.kind_ = upper ? AssignmentSyntax::Kind::set : AssignmentSyntax::Kind::value;
        right_side(result);
    }
    module_.assignments_.push_back(std::move(result));
}

// The right side of `Name ::=`: a CLASS body or a type.
void ModuleParser::type_or_class(AssignmentSyntax &assignment) {
    if (parser_.accept("CLASS")) {
        assignment.kind_ = AssignmentSyntax::Kind::class_;
        assignment.class_ = class_body();
    } else {
        assignment.kind_ = AssignmentSyntax::Kind::type;
        assignment.type_ = parser_.type();
    }
}

// `Governor ::= Value` of a value, object, value set or object set assignment.
void ModuleParser::right_side(AssignmentSyntax &assignment) {
    if (parser_.is("MACRO")) {
        parser_.fail("MACRO definitions are not supported");
    }
    assignment.governor_ = parser_.type();
    parser_.expect("::=");
    assignment.value_ = parser_.value();
}

// `{ [Governor :] Name, ... }`.
void ModuleParser::parameters(AssignmentSyntax &assignment) {
    assignment.parameterized_ = true;
    parser_.expect("{");
    while (!parser_.accept("}")) {
        ParameterSyntax parameter;
        parameter.token_ = parser_.position();
        auto first = parser_.type();
        if (parser_.accept(":")) {
            parameter.governor_ = first;
            if (!parser_.is_kind(TokenKind::upper) && !parser_.is_kind(TokenKind::lower)) {
                parser_.fail("expected a parameter name");
            }
            parameter.name_ = parser_.advance().text_;
        } else if (first->kind_ == TypeKind::reference && first->reference_.module_.empty() && !first->parameterized_ &&
                   first->constraints_.empty() && first->tags_.empty()) {
            parameter.name_ = first->reference_.name_;
        } else {
            parser_.fail_at(parameter.token_, "malformed parameter");
        }
        assignment.parameters_.push_back(std::move(parameter));
        parser_.accept(",");
    }
}

// `{ field, ... } [WITH SYNTAX { ... }]`.
std::shared_ptr<ClassSyntax> ModuleParser::class_body() {
    auto result = std::make_shared<ClassSyntax>();
    parser_.expect("{");
    while (!parser_.accept("}")) {
        result->fields_.push_back(field_spec());
        if (!parser_.accept(",") && !parser_.is("}")) {
            parser_.fail("expected ',' or '}' in a class definition");
        }
    }
    if (parser_.is("WITH") && parser_.is("SYNTAX", 1)) {
        parser_.seek(parser_.position() + 2);
        parser_.expect("{");
        result->with_syntax_ = true;
        result->syntax_ = parser_.syntax_list();
        parser_.expect("}");
    }
    return result;
}

FieldSpecSyntax ModuleParser::field_spec() {
    FieldSpecSyntax field;
    field.token_ = parser_.position();
    const bool upper = parser_.is_kind(TokenKind::upper_field);
    if (!upper && !parser_.is_kind(TokenKind::lower_field)) {
        parser_.fail("expected a field name");
    }
    field.name_ = parser_.advance().text_;
    const bool bare = parser_.is(",") || parser_.is("}") || parser_.is("OPTIONAL") || parser_.is("DEFAULT");
    if (upper && bare) {
        field.type_field_ = true;
    } else if (parser_.is_kind(TokenKind::upper_field)) {
        // A variable-type value or value set field: the governor is another field of the class.
        field.type_ = std::make_shared<TypeSyntax>();
        field.type_->kind_ = TypeKind::class_field;
        field.type_->token_ = parser_.position();
        field.type_->fields_.push_back(parser_.advance().text_);
    } else {
        field.type_ = parser_.type();
    }
    field_tail(field);
    return field;
}

// `[UNIQUE] [OPTIONAL | DEFAULT setting]`; the setting of a type field is a type, kept as tokens like a value.
void ModuleParser::field_tail(FieldSpecSyntax &field) {
    field.unique_ = parser_.accept("UNIQUE");
    if (parser_.accept("OPTIONAL")) {
        field.presence_ = Presence::optional;
    } else if (parser_.accept("DEFAULT")) {
        field.presence_ = Presence::default_;
        if (field.type_field_) {
            const auto begin = parser_.position();
            parser_.type();
            field.default_ = {begin, parser_.position()};
        } else {
            field.default_ = parser_.value();
        }
    }
}

// The file's first module; as asn1ct, the text after its END is not read.
std::vector<std::shared_ptr<ModuleSyntax>> parse_file(const std::string_view text, const std::string &file) {
    auto module = std::make_shared<ModuleSyntax>();
    module->file_ = file;
    module->tokens_ = tokenize(text, file);
    if (module->tokens_.front().kind_ == TokenKind::end) {
        throw Error({file, 1, 1}, "no ASN.1 module found");
    }
    Parser parser(*module, {0, static_cast<std::uint32_t>(module->tokens_.size() - 1)});
    ModuleParser(*module, parser).run();
    return {module};
}
} // namespace clause::asn1
