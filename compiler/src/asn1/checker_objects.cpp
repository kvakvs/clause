#include "checker_internal.hpp"

// Added for the ASN.1 compiler (plan 21 step LIB84): information object classes, objects and object sets (X.681),
// including the user-defined syntax of WITH SYNTAX.
namespace clause::asn1::detail {
namespace {
// The classes X.681 Annex A and B define, checked like any other class.
constexpr std::string_view BUILTIN_CLASSES = R"(Clause-Builtin-Classes DEFINITIONS ::= BEGIN
TYPE-IDENTIFIER ::= CLASS { &id OBJECT IDENTIFIER UNIQUE, &Type } WITH SYNTAX { &Type IDENTIFIED BY &id }
ABSTRACT-SYNTAX ::= CLASS { &id OBJECT IDENTIFIER UNIQUE, &Type,
    &property BIT STRING { handles-invalid-encodings(0) } DEFAULT {} }
    WITH SYNTAX { &Type IDENTIFIED BY &id [HAS PROPERTY &property] }
END
)";

bool upper_name(const std::string_view name) {
    const auto first = name.starts_with('&') ? name.substr(1) : name;
    return !first.empty() && first.front() >= 'A' && first.front() <= 'Z';
}
} // namespace

// Parses object definitions in the default syntax or a class's WITH SYNTAX.
class ObjectChecker {
  public:
    ObjectChecker(Checker &checker, const ClassPtr &definition, const Scope &scope, Parser &parser)
        : checker_(checker), class_(definition), scope_(scope), parser_(parser) {}

    ObjectPtr definition(std::string name);

  private:
    void default_syntax();
    bool user_syntax(const std::vector<SyntaxItem> &items, bool optional);
    bool literal(const std::string &text, bool optional);
    void setting(const std::string &field);
    void variable_settings();
    void defaults();

    Checker &checker_;
    const ClassPtr &class_;
    const Scope &scope_;
    Parser &parser_;
    std::shared_ptr<ObjectDef> object_;
    // Variable-type value fields, checked once the type field naming their type is known.
    std::vector<std::pair<std::string, TokenRange>> variable_;
};

ObjectPtr ObjectChecker::definition(std::string name) {
    object_ = std::make_shared<ObjectDef>();
    object_->class_ = class_;
    object_->name_ = std::move(name);
    parser_.expect("{");
    if (class_->with_syntax_) {
        user_syntax(class_->syntax_, false);
    } else {
        default_syntax();
    }
    parser_.expect("}");
    variable_settings();
    defaults();
    return object_;
}

void ObjectChecker::default_syntax() {
    while (!parser_.is("}")) {
        if (!parser_.is_kind(TokenKind::upper_field) && !parser_.is_kind(TokenKind::lower_field)) {
            parser_.fail("expected a field name");
        }
        setting(parser_.advance().text_);
        if (!parser_.accept(",") && !parser_.is("}")) {
            parser_.fail("expected ',' or '}' in an object definition");
        }
    }
}

// Match the items of a WITH SYNTAX list; an optional group is present when its first literal is.
bool ObjectChecker::user_syntax(const std::vector<SyntaxItem> &items, const bool optional) {
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto &item = items[i];
        if (item.kind_ == SyntaxItem::Kind::literal && !literal(item.text_, optional && i == 0)) {
            return false;
        }
        if (item.kind_ == SyntaxItem::Kind::field) {
            setting(item.text_);
        } else if (item.kind_ == SyntaxItem::Kind::optional) {
            user_syntax(item.items_, true);
        }
    }
    return true;
}

// Accept a literal of the syntax list; false when an optional group's first literal is missing.
bool ObjectChecker::literal(const std::string &text, const bool optional) {
    if (parser_.accept(text)) {
        return true;
    }
    if (!optional) {
        parser_.fail("expected " + text + " in an object definition");
    }
    return false;
}

void ObjectChecker::setting(const std::string &field) {
    const auto *spec = class_->field(field);
    if (spec == nullptr) {
        parser_.fail("no field " + field + " in class " + class_->name_);
    }
    Setting result;
    result.kind_ = spec->kind_;
    switch (spec->kind_) {
    case FieldKind::type:
        result.type_ = {parser_.type(), scope_};
        if (!checker_.current_definition().empty() && result.type_.syntax_->kind_ == TypeKind::reference &&
            result.type_.syntax_->parameterized_) {
            checker_.object_type(*result.type_.syntax_, scope_);
        }
        break;
    case FieldKind::fixed_value: {
        const auto range = parser_.value();
        const auto governor = checker_.type(*spec->type_.syntax_, spec->type_.scope_, {});
        result.value_ = checker_.value(range, governor, scope_);
        break;
    }
    case FieldKind::variable_value:
        variable_.emplace_back(field, parser_.value());
        return;
    case FieldKind::object:
        result.object_ = checker_.object(parser_.value(), spec->class_, scope_);
        break;
    case FieldKind::object_set:
        result.set_ = checker_.object_set(parser_.value(), spec->class_, scope_);
        break;
    default:
        // Value set fields are kept unchecked: no encoder reads them.
        parser_.value();
        break;
    }
    object_->settings_.insert_or_assign(field, std::move(result));
}

void ObjectChecker::variable_settings() {
    for (const auto &[field, range] : variable_) {
        const auto *spec = class_->field(field);
        const auto type = object_->settings_.find(spec->type_field_);
        if (type == object_->settings_.end()) {
            checker_.fail(scope_, range.begin_, "no type for the variable-type field " + field);
        }
        Setting result;
        result.kind_ = FieldKind::variable_value;
        const auto governor = checker_.type(*type->second.type_.syntax_, type->second.type_.scope_, {});
        result.value_ = checker_.value(range, governor, scope_);
        object_->settings_.insert_or_assign(field, std::move(result));
    }
}

// The class's DEFAULT settings of fields the definition left out.
void ObjectChecker::defaults() {
    for (const auto &spec : class_->fields_) {
        if (spec.presence_ != Presence::default_ || object_->settings_.contains(spec.name_)) {
            continue;
        }
        Setting result;
        result.kind_ = spec.kind_;
        if (spec.kind_ == FieldKind::type) {
            result.type_ = {checker_.parser(class_->scope_, spec.default_).type(), class_->scope_};
        } else if (spec.kind_ == FieldKind::fixed_value) {
            const auto governor = checker_.type(*spec.type_.syntax_, spec.type_.scope_, {});
            result.value_ = checker_.value(spec.default_, governor, class_->scope_);
        } else {
            continue;
        }
        object_->settings_.insert_or_assign(spec.name_, std::move(result));
    }
}

// `object.&field` naming an object: the object an object field of `object` is set to.
ObjectPtr Checker::object_from_object(const TypeSyntax &reference, const Scope &scope) {
    auto base = reference;
    base.fields_.clear();
    auto object = named_object(base, nullptr, scope);
    for (const auto &field : reference.fields_) {
        const auto setting = object->settings_.find(field);
        if (setting == object->settings_.end() || !setting->second.object_) {
            fail(scope, reference.token_, "no object in field " + field);
        }
        object = setting->second.object_;
    }
    return object;
}

namespace {
// The name of the X.681 class a notation names, by keyword or by reference; none for anything else.
std::optional<std::string> builtin_class_name(const TypeSyntax &syntax) {
    if (syntax.kind_ == TypeKind::type_identifier || syntax.kind_ == TypeKind::abstract_syntax) {
        return syntax.kind_ == TypeKind::type_identifier ? "TYPE-IDENTIFIER" : "ABSTRACT-SYNTAX";
    }
    const auto &name = syntax.reference_.name_;
    const bool plain =
        syntax.kind_ == TypeKind::reference && syntax.reference_.module_.empty() && syntax.fields_.empty();
    if (plain && (name == "TYPE-IDENTIFIER" || name == "ABSTRACT-SYNTAX")) {
        return name;
    }
    return std::nullopt;
}
} // namespace

ClassPtr Checker::builtin_class(const std::string &name) {
    if (!modules_.contains("Clause-Builtin-Classes")) {
        add_module(parse_file(BUILTIN_CLASSES, "<builtin classes>").front());
    }
    auto &builtin = *modules_.find("Clause-Builtin-Classes")->second;
    return class_definition({Scope{&builtin, nullptr}, builtin.assignments_.find(name)->second, nullptr});
}

ClassPtr Checker::class_of(const TypeSyntax &syntax, const Scope &scope) {
    if (const auto name = builtin_class_name(syntax)) {
        return builtin_class(*name);
    }
    if (syntax.kind_ != TypeKind::reference || syntax.parameterized_ || !syntax.constraints_.empty()) {
        return nullptr;
    }
    const auto found = try_find(syntax.reference_, scope);
    return found ? found_class(*found) : nullptr;
}

// The class a name stands for: a class parameter, a class assignment, or a type assignment naming a class.
ClassPtr Checker::found_class(const Found &found) {
    if (found.binding_ != nullptr) {
        return found.binding_->kind_ == Binding::Kind::class_ ? found.binding_->class_ : nullptr;
    }
    const auto &assignment = *found.assignment_;
    if (assignment.kind_ == AssignmentSyntax::Kind::class_) {
        return class_definition(found);
    }
    if (assignment.kind_ == AssignmentSyntax::Kind::type && !assignment.parameterized_) {
        return class_of(*assignment.type_, found.scope_);
    }
    return nullptr;
}

namespace {
// A value or object field: its kind follows its type (`&field`, a class, else a type) and its name's case.
void value_field_kind(Checker &checker, const FieldSpecSyntax &syntax, const Scope &scope, FieldSpec &spec) {
    const bool upper = upper_name(syntax.name_);
    if (syntax.type_->kind_ == TypeKind::class_field && syntax.type_->reference_.name_.empty()) {
        spec.kind_ = upper ? FieldKind::variable_value_set : FieldKind::variable_value;
        spec.type_field_ = syntax.type_->fields_.front();
    } else if (auto governor = checker.class_of(*syntax.type_, scope)) {
        spec.kind_ = upper ? FieldKind::object_set : FieldKind::object;
        spec.class_ = std::move(governor);
    } else {
        spec.kind_ = upper ? FieldKind::fixed_value_set : FieldKind::fixed_value;
        spec.type_ = {syntax.type_, scope};
    }
}

FieldSpec field_spec(Checker &checker, const FieldSpecSyntax &syntax, const Scope &scope) {
    FieldSpec spec;
    spec.name_ = syntax.name_;
    spec.unique_ = syntax.unique_;
    spec.presence_ = syntax.presence_;
    spec.default_ = syntax.default_;
    if (syntax.type_field_) {
        spec.kind_ = FieldKind::type;
    } else {
        value_field_kind(checker, syntax, scope, spec);
    }
    return spec;
}
} // namespace

ClassPtr Checker::class_definition(const Found &found) {
    const std::pair key{found.scope_.module_->syntax_->name_, found.assignment_->name_};
    if (const auto known = classes_.find(key); known != classes_.end()) {
        return known->second;
    }
    auto definition = std::make_shared<ClassDef>();
    definition->name_ = found.assignment_->name_;
    definition->scope_ = found.scope_;
    const auto &syntax = *found.assignment_->class_;
    definition->with_syntax_ = syntax.with_syntax_;
    definition->syntax_ = syntax.syntax_;
    // Registered before its fields: a field may name its own class (recursive classes).
    classes_.emplace(key, definition);
    for (const auto &field : syntax.fields_) {
        definition->fields_.push_back(field_spec(*this, field, found.scope_));
    }
    return definition;
}

ObjectPtr Checker::object(const TokenRange range, const ClassPtr &definition, const Scope &scope) {
    auto parser = this->parser(scope, range);
    if (parser.is("{")) {
        auto result = ObjectChecker(*this, definition, scope, parser).definition("");
        if (!parser.at_end()) {
            parser.fail("unexpected tokens after an object");
        }
        return result;
    }
    auto reference = parser.defined();
    if (!parser.at_end()) {
        parser.fail("unexpected tokens after an object reference");
    }
    return named_object(*reference, definition, scope);
}

ObjectPtr Checker::named_object(const TypeSyntax &reference, const ClassPtr &definition, const Scope &scope) {
    if (!reference.fields_.empty()) {
        return object_from_object(reference, scope);
    }
    const auto found = find(reference.reference_, scope);
    if (found.binding_ != nullptr) {
        if (found.binding_->kind_ != Binding::Kind::object) {
            fail(scope, reference.token_, reference.reference_.name_ + " is not an object");
        }
        return object(found.binding_->range_, found.binding_->class_, found.binding_->scope_);
    }
    const auto &assignment = *found.assignment_;
    if (assignment.kind_ != AssignmentSyntax::Kind::value) {
        fail(scope, reference.token_, reference.reference_.name_ + " is not an object");
    }
    if (assignment.parameterized_) {
        const auto inner = instantiate(found, reference.arguments_, scope, reference.token_);
        return object(assignment.value_, class_of(*assignment.governor_, inner), inner);
    }
    const std::pair key{found.scope_.module_->syntax_->name_, assignment.name_};
    if (const auto known = objects_.find(key); known != objects_.end()) {
        return known->second;
    }
    auto governor = class_of(*assignment.governor_, found.scope_);
    if (!governor) {
        fail(scope, reference.token_, reference.reference_.name_ + " is not an object");
    }
    auto parser = this->parser(found.scope_, assignment.value_);
    ObjectPtr result;
    if (parser.is("{")) {
        result = ObjectChecker(*this, governor, found.scope_, parser).definition(assignment.name_);
    } else {
        result = object(assignment.value_, governor, found.scope_);
    }
    objects_.emplace(key, result);
    (void)definition;
    return result;
}
} // namespace clause::asn1::detail
