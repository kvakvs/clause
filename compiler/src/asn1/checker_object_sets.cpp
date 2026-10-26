#include "checker_internal.hpp"

// Added for the ASN.1 compiler (plan 21 step LIB84): object sets (X.681 clause 12) built from their element set
// specifications, which share the constraint notation.
namespace clause::asn1::detail {
namespace {
using Kind = ConstraintSyntax::Kind;

std::shared_ptr<ObjectSetDef> empty_set(const ClassPtr &definition) {
    auto result = std::make_shared<ObjectSetDef>();
    result->class_ = definition;
    return result;
}

void append(ObjectSetDef &into, const ObjectSetDef &from) {
    into.objects_.insert(into.objects_.end(), from.objects_.begin(), from.objects_.end());
    into.extensible_ = into.extensible_ || from.extensible_;
}
} // namespace

ObjectSetPtr Checker::object_set(const TokenRange range, const ClassPtr &definition, const Scope &scope) {
    auto parser = this->parser(scope, range);
    if (!parser.is("{")) {
        auto reference = parser.defined();
        return named_object_set(*reference, definition, scope);
    }
    const auto end = parser.block_end(range.begin_);
    auto inner = this->parser(scope, {range.begin_ + 1, end - 1});
    if (inner.at_end()) {
        return empty_set(definition);
    }
    auto spec = inner.constraint(false);
    if (!inner.at_end()) {
        inner.fail("unexpected tokens in an object set");
    }
    return object_set(*spec, definition, scope);
}

ObjectSetPtr Checker::object_set(const ConstraintSyntax &spec, const ClassPtr &definition, const Scope &scope) {
    switch (spec.kind_) {
    case Kind::single_value:
        return object_set_element(spec.value_, definition, scope);
    case Kind::contained_subtype:
        return named_object_set(*spec.type_, definition, scope);
    case Kind::union_:
    case Kind::extensible:
        return combined_set(spec, definition, scope);
    case Kind::intersection:
    case Kind::except:
    case Kind::exception:
        // Only the first operand: asn1ct also keeps the objects of the left-hand set.
        return object_set(*spec.children_.front(), definition, scope);
    default:
        fail(scope, spec.token_, "unsupported object set notation");
    }
}

// A union's objects in order; `root, ..., additions` the same, marked extensible.
ObjectSetPtr Checker::combined_set(const ConstraintSyntax &spec, const ClassPtr &definition, const Scope &scope) {
    auto result = empty_set(definition);
    for (const auto &child : spec.children_) {
        if (child) {
            append(*result, *object_set(*child, definition, scope));
        }
    }
    result->extensible_ = result->extensible_ || spec.kind_ == Kind::extensible;
    return result;
}

// An element written as a value: an object reference, an inline object, or the objects of an object's set field.
ObjectSetPtr Checker::object_set_element(const TokenRange range, const ClassPtr &definition, const Scope &scope) {
    auto result = empty_set(definition);
    result->objects_.push_back(object(range, definition, scope));
    return result;
}

ObjectSetPtr Checker::named_object_set(const TypeSyntax &reference, const ClassPtr &definition, const Scope &scope) {
    if (!reference.fields_.empty()) {
        fail(scope, reference.token_, "objects extracted from object sets are not supported");
    }
    const auto found = find(reference.reference_, scope);
    if (found.binding_ != nullptr) {
        if (found.binding_->kind_ != Binding::Kind::object_set) {
            fail(scope, reference.token_, reference.reference_.name_ + " is not an object set");
        }
        return object_set(found.binding_->range_, found.binding_->class_, found.binding_->scope_);
    }
    const auto &assignment = *found.assignment_;
    if (assignment.kind_ != AssignmentSyntax::Kind::set) {
        fail(scope, reference.token_, reference.reference_.name_ + " is not an object set");
    }
    if (assignment.parameterized_) {
        const auto inner = instantiate(found, reference.arguments_, scope, reference.token_);
        return object_set(assignment.value_, class_of(*assignment.governor_, inner), inner);
    }
    const std::pair key{found.scope_.module_->syntax_->name_, assignment.name_};
    if (const auto known = object_sets_.find(key); known != object_sets_.end()) {
        return known->second;
    }
    auto governor = class_of(*assignment.governor_, found.scope_);
    if (!governor) {
        fail(scope, reference.token_, reference.reference_.name_ + " is not an object set");
    }
    // Registered empty first: a set may refer to itself through its objects.
    auto placeholder = empty_set(governor);
    object_sets_.emplace(key, placeholder);
    auto result = object_set(assignment.value_, governor, found.scope_);
    *placeholder = *result;
    (void)definition;
    return placeholder;
}
} // namespace clause::asn1::detail
