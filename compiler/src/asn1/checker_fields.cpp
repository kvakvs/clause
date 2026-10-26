#include "checker_types.hpp"

// Added for the ASN.1 compiler (plan 21 step LIB84): types taken from information object classes and objects
// (X.681 clauses 14-15) and the object tables of open types under component relation constraints (X.682).
namespace clause::asn1::detail {
namespace {
TypeSyntax bare_reference(const TypeSyntax &syntax) {
    TypeSyntax reference;
    reference.kind_ = TypeKind::reference;
    reference.reference_ = syntax.reference_;
    reference.arguments_ = syntax.arguments_;
    reference.parameterized_ = syntax.parameterized_;
    reference.token_ = syntax.token_;
    return reference;
}

// The field a component's type names when it is `CLASS.&field`, following type references.
std::string class_field_name(Checker &checker, const TypeSyntax *type, Scope scope) {
    while (type->kind_ == TypeKind::reference && !type->parameterized_) {
        const auto found = checker.find(type->reference_, scope);
        if (found.assignment_ == nullptr || found.assignment_->kind_ != AssignmentSyntax::Kind::type) {
            break;
        }
        scope = found.scope_;
        type = found.assignment_->type_.get();
    }
    if (type->kind_ != TypeKind::class_field) {
        checker.fail(scope, type->token_, "a component relation must refer to a class field component");
    }
    return type->fields_.back();
}

// The SEQUENCE or SET body of a component type, following type references.
const TypeSyntax *component_body(Checker &checker, const TypeSyntax *type, Scope &scope) {
    while (type->kind_ == TypeKind::reference && !type->parameterized_) {
        const auto found = checker.find(type->reference_, scope);
        scope = found.scope_;
        type = found.assignment_->type_.get();
    }
    return type;
}

const TypeSyntax *component_type(Checker &checker, const TypeSyntax &body, const std::string &name,
                                 const Scope &scope) {
    for (const auto &component : body.components_) {
        if (component.kind_ == ComponentSyntax::Kind::named && component.name_ == name) {
            return component.type_.get();
        }
    }
    checker.fail(scope, body.token_, "no component " + name + " for a component relation");
}

// The key field of a component relation `{@a.b}`: the class field the referenced component's type names.
std::string relation_key(Checker &checker, const Frame &frame, const std::vector<std::string> &path) {
    const auto *body = frame.syntax_;
    auto scope = frame.scope_;
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        body = component_body(checker, component_type(checker, *body, path[i], scope), scope);
    }
    return class_field_name(checker, component_type(checker, *body, path.back(), scope), scope);
}

// The context that names types written inside an object (asn1ct: `ObjectName_PType` for parameterized ones).
Context object_type_context(const ObjectDef &object, const TypeSyntax &type) {
    Context context;
    const auto name = object.name_.empty() ? std::string("object") : object.name_;
    context.path_.push_back({name, false});
    if (type.kind_ == TypeKind::reference && type.parameterized_) {
        context.path_.push_back({type.reference_.name_, false});
    }
    return context;
}
} // namespace

Checker::FieldOf Checker::class_field_spec(const TypeSyntax &syntax, const Scope &scope) {
    auto definition = class_of(bare_reference(syntax), scope);
    if (!definition) {
        fail(scope, syntax.token_, syntax.reference_.name_ + " is not an information object class");
    }
    const FieldSpec *spec = nullptr;
    for (std::size_t i = 0; i < syntax.fields_.size(); ++i) {
        spec = definition->field(syntax.fields_[i]);
        if (spec == nullptr) {
            fail(scope, syntax.token_, "no field " + syntax.fields_[i] + " in class " + definition->name_);
        }
        if (i + 1 < syntax.fields_.size()) {
            definition = spec->class_;
        }
    }
    if (spec == nullptr) {
        fail(scope, syntax.token_, "expected a field of class " + definition->name_);
    }
    return {definition, spec};
}

// `CLASS.&Type` is an open type; `CLASS.&value` has the type of its fixed-type value field.
Resolved TypeChecker::class_field(const TypeSyntax &syntax) {
    const auto field = checker_.class_field_spec(syntax, scope_);
    switch (field.spec_->kind_) {
    case FieldKind::fixed_value:
    case FieldKind::fixed_value_set: {
        const auto &type = field.spec_->type_;
        return {TypeChecker(checker_, type.scope_, context_).resolve(*type.syntax_).id_, false, false};
    }
    case FieldKind::object:
    case FieldKind::object_set:
        checker_.fail(scope_, syntax.token_, syntax.fields_.back() + " is not a type field");
    default: {
        Node node;
        node.kind_ = NodeKind::open_type;
        return fresh(std::move(node));
    }
    }
}

// `object.&Type`: the type an object's type field is set to.
Resolved TypeChecker::object_field(const TypeSyntax &syntax) {
    auto object = checker_.named_object(bare_reference(syntax), nullptr, scope_);
    for (std::size_t i = 0; i + 1 < syntax.fields_.size(); ++i) {
        const auto setting = object->settings_.find(syntax.fields_[i]);
        if (setting == object->settings_.end() || !setting->second.object_) {
            checker_.fail(scope_, syntax.token_, "no object in field " + syntax.fields_[i]);
        }
        object = setting->second.object_;
    }
    const auto setting = object->settings_.find(syntax.fields_.back());
    if (setting == object->settings_.end() || setting->second.kind_ != FieldKind::type) {
        checker_.fail(scope_, syntax.token_, "no type in field " + syntax.fields_.back());
    }
    const auto &type = setting->second.type_;
    return {TypeChecker(checker_, type.scope_, context_).resolve(*type.syntax_).id_, false, false};
}

namespace {
// One entry per object with a key: the key's value and the type of `type_field` (none when the object has none).
void table_entries(Checker &checker, Table &table, const ObjectSetDef &set, const std::string &type_field) {
    for (const auto &object : set.objects_) {
        const auto key = object->settings_.find(table.key_);
        if (key == object->settings_.end()) {
            continue;
        }
        NodeId type = NO_NODE;
        if (const auto setting = object->settings_.find(type_field); setting != object->settings_.end()) {
            const auto &ref = setting->second.type_;
            type = checker.type(*ref.syntax_, ref.scope_, object_type_context(*object, *ref.syntax_));
        }
        table.entries_.emplace_back(key->second.value_, type);
    }
}
} // namespace

// `CLASS.&Type({Set}{@id})`: the open type's table, keyed by the referenced component's field in each object.
void Checker::table_constraint(const NodeId id, const TypeSyntax &syntax, const ConstraintSyntax &table,
                               const Scope &scope, const Context &context) {
    if (table.at_.empty() || syntax.kind_ != TypeKind::class_field) {
        return;
    }
    const auto field = class_field_spec(syntax, scope);
    const auto kind = field.spec_->kind_;
    if (kind != FieldKind::type && kind != FieldKind::variable_value) {
        return;
    }
    const auto set = object_set(table.value_, field.class_, scope);
    const auto &at = table.at_.front();
    const auto frames = context.frames_.size();
    if (frames == 0 || at.level_ >= frames + 1) {
        fail(scope, table.token_, "component relation outside a SEQUENCE or SET");
    }
    const auto index = at.level_ == 0 ? 0 : frames - at.level_;
    const auto key = relation_key(*this, context.frames_[index], at.path_);
    const auto type_field = kind == FieldKind::type ? field.spec_->name_ : field.spec_->type_field_;
    auto result = std::make_shared<Table>();
    result->levels_up_ = static_cast<std::uint32_t>(frames - 1 - index);
    result->path_ = at.path_;
    result->extensible_ = set->extensible_;
    result->field_ = field.spec_->name_;
    result->key_ = key;
    table_entries(*this, *result, *set, type_field);
    nodes_[id].table_ = std::move(result);
}

namespace {
// X.681 Annex C: SEQUENCE { type-id CLASS.&id, value [0] CLASS.&Type }, tagged [UNIVERSAL 8].
Node associated_sequence(Checker &checker, std::string record, std::shared_ptr<Table> table) {
    Node oid;
    oid.kind_ = NodeKind::object_identifier;
    oid.tags_.push_back({TagClass::universal, 6});
    Node open;
    open.kind_ = NodeKind::open_type;
    open.tags_.push_back({TagClass::context, 0});
    open.table_ = std::move(table);
    Node sequence;
    sequence.kind_ = NodeKind::sequence;
    sequence.tags_.push_back({TagClass::universal, 8});
    sequence.record_ = std::move(record);
    Component type_id;
    type_id.name_ = "type-id";
    type_id.type_ = checker.add(std::move(oid));
    Component value;
    value.name_ = "value";
    value.type_ = checker.add(std::move(open));
    value.textual_ = 1;
    sequence.components_ = {type_id, value};
    return sequence;
}
} // namespace

NodeId Checker::instance_of_type(const Scope &scope) {
    instance_of_modules_.insert(scope.module_->syntax_->name_);
    if (instance_of_ == NO_NODE) {
        auto node = associated_sequence(*this, "INSTANCE OF", nullptr);
        node.named_ = "INSTANCE OF";
        instance_of_ = add(std::move(node));
    }
    return instance_of_;
}

// INSTANCE OF CLASS: asn1ct's shared 'INSTANCE OF' type; with a table constraint, an inline associated type whose
// value is looked up by its type-id.
Resolved TypeChecker::instance_of(const TypeSyntax &syntax) {
    if (syntax.constraints_.empty()) {
        return {checker_.instance_of_type(scope_), false, false};
    }
    const auto *table = &*syntax.constraints_.front();
    if (table->kind_ != ConstraintSyntax::Kind::table) {
        checker_.fail(scope_, syntax.token_, "INSTANCE OF takes a table constraint");
    }
    TypeSyntax reference;
    reference.kind_ = TypeKind::reference;
    reference.reference_ = syntax.reference_;
    reference.token_ = syntax.token_;
    const auto definition = checker_.class_of(reference, scope_);
    if (!definition) {
        checker_.fail(scope_, syntax.token_, syntax.reference_.name_ + " is not an information object class");
    }
    const auto set = checker_.object_set(table->value_, definition, scope_);
    auto result = std::make_shared<Table>();
    result->path_ = {"type-id"};
    result->extensible_ = set->extensible_;
    result->field_ = "Type";
    result->key_ = "id";
    table_entries(checker_, *result, *set, "Type");
    return fresh(associated_sequence(checker_, record_name(context_.path_), std::move(result)));
}
} // namespace clause::asn1::detail
