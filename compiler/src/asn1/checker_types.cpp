#include "checker_types.hpp"
#include "components.hpp"
#include <array>

// Added for the ASN.1 compiler (plan 21 step LIB84): built-in types, references and parameterized instances
// (X.680 clauses 17-41, X.683 clause 9).
namespace clause::asn1::detail {
namespace {
Node basic(const NodeKind kind, const std::uint64_t universal) {
    Node node;
    node.kind_ = kind;
    node.tags_.push_back({TagClass::universal, universal});
    return node;
}

// The universal tag of each restricted character string type (X.680 clause 41).
std::uint64_t string_tag(const std::string_view name) {
    struct Entry {
        std::string_view name_;
        std::uint64_t tag_;
    };

    static constexpr std::array TAGS = {
        Entry{"UTF8String", 12},      Entry{"NumericString", 18},  Entry{"PrintableString", 19},
        Entry{"TeletexString", 20},   Entry{"VideotexString", 21}, Entry{"IA5String", 22},
        Entry{"GraphicString", 25},   Entry{"VisibleString", 26},  Entry{"GeneralString", 27},
        Entry{"UniversalString", 28}, Entry{"BMPString", 30}};
    for (const auto &entry : TAGS) {
        if (entry.name_ == name) {
            return entry.tag_;
        }
    }
    return 0;
}
} // namespace

Resolved TypeChecker::fresh(Node node) { return {checker_.add(std::move(node)), true, false}; }

Resolved TypeChecker::resolve(const TypeSyntax &syntax) {
    auto resolved = untagged(syntax);
    if (syntax.constraints_.empty() && syntax.tags_.empty()) {
        return resolved;
    }
    resolved = own(resolved);
    // INSTANCE OF takes its table constraint into its associated type.
    if (!syntax.constraints_.empty() && syntax.kind_ != TypeKind::instance_of) {
        checker_.constrain(resolved.id_, syntax, scope_, context_);
    }
    apply_tags(checker_, resolved.id_, syntax.tags_, scope_);
    return resolved;
}

Resolved TypeChecker::untagged(const TypeSyntax &syntax) {
    using Method = Resolved (TypeChecker::*)(const TypeSyntax &);
    // Indexed by TypeKind, in its declaration order.
    static constexpr std::array<Method, 29> METHODS = {&TypeChecker::reference,
                                                       &TypeChecker::boolean,
                                                       &TypeChecker::integer,
                                                       &TypeChecker::bit_string,
                                                       &TypeChecker::octet_string,
                                                       &TypeChecker::null,
                                                       &TypeChecker::object_identifier,
                                                       &TypeChecker::relative_oid,
                                                       &TypeChecker::real,
                                                       &TypeChecker::enumerated,
                                                       &TypeChecker::sequence,
                                                       &TypeChecker::set,
                                                       &TypeChecker::sequence_of,
                                                       &TypeChecker::set_of,
                                                       &TypeChecker::choice,
                                                       &TypeChecker::unsupported,
                                                       &TypeChecker::restricted_string,
                                                       &TypeChecker::utc_time,
                                                       &TypeChecker::generalized_time,
                                                       &TypeChecker::object_descriptor,
                                                       &TypeChecker::unsupported,
                                                       &TypeChecker::unsupported,
                                                       &TypeChecker::any,
                                                       &TypeChecker::class_field,
                                                       &TypeChecker::object_field,
                                                       &TypeChecker::instance_of,
                                                       &TypeChecker::selection,
                                                       &TypeChecker::not_a_type,
                                                       &TypeChecker::not_a_type};
    return (this->*METHODS[static_cast<std::size_t>(syntax.kind_)])(syntax);
}

Resolved TypeChecker::boolean(const TypeSyntax & /*syntax*/) { return fresh(basic(NodeKind::boolean, 1)); }

Resolved TypeChecker::integer(const TypeSyntax &syntax) {
    auto node = basic(NodeKind::integer, 2);
    node.named_numbers_ = named_numbers(syntax);
    return fresh(std::move(node));
}

Resolved TypeChecker::bit_string(const TypeSyntax &syntax) {
    auto node = basic(NodeKind::bit_string, 3);
    node.named_numbers_ = named_numbers(syntax);
    return fresh(std::move(node));
}

Resolved TypeChecker::octet_string(const TypeSyntax & /*syntax*/) { return fresh(basic(NodeKind::octet_string, 4)); }

Resolved TypeChecker::null(const TypeSyntax & /*syntax*/) { return fresh(basic(NodeKind::null, 5)); }

Resolved TypeChecker::object_identifier(const TypeSyntax & /*syntax*/) {
    return fresh(basic(NodeKind::object_identifier, 6));
}

Resolved TypeChecker::relative_oid(const TypeSyntax & /*syntax*/) { return fresh(basic(NodeKind::relative_oid, 13)); }

Resolved TypeChecker::real(const TypeSyntax & /*syntax*/) { return fresh(basic(NodeKind::real, 9)); }

Resolved TypeChecker::utc_time(const TypeSyntax & /*syntax*/) { return fresh(basic(NodeKind::utc_time, 23)); }

Resolved TypeChecker::generalized_time(const TypeSyntax & /*syntax*/) {
    return fresh(basic(NodeKind::generalized_time, 24));
}

Resolved TypeChecker::object_descriptor(const TypeSyntax & /*syntax*/) {
    return fresh(basic(NodeKind::object_descriptor, 7));
}

Resolved TypeChecker::restricted_string(const TypeSyntax &syntax) {
    auto node = basic(NodeKind::restricted_string, string_tag(syntax.string_));
    node.string_ = syntax.string_;
    return fresh(std::move(node));
}

// ANY and ANY DEFINED BY: a complete encoding of some type, kept as its bytes.
Resolved TypeChecker::any(const TypeSyntax & /*syntax*/) {
    Node node;
    node.kind_ = NodeKind::open_type;
    return fresh(std::move(node));
}

Resolved TypeChecker::unsupported(const TypeSyntax &syntax) {
    checker_.fail(scope_, syntax.token_,
                  "unsupported ASN.1 type (EXTERNAL, EMBEDDED PDV and CHARACTER STRING are not implemented)");
}

Resolved TypeChecker::not_a_type(const TypeSyntax &syntax) {
    checker_.fail(scope_, syntax.token_, "an information object class is not a type");
}

std::vector<std::pair<std::string, BigInt>> TypeChecker::named_numbers(const TypeSyntax &syntax) {
    std::vector<std::pair<std::string, BigInt>> result;
    result.reserve(syntax.named_.size());
    for (const auto &named : syntax.named_) {
        result.emplace_back(named.name_, checker_.integer(named.value_, scope_));
    }
    return result;
}

Resolved TypeChecker::enumerated(const TypeSyntax &syntax) {
    auto node = basic(NodeKind::enumerated, 10);
    enumeration_values(syntax, node);
    return fresh(std::move(node));
}

namespace {
// X.680 20: root items without a number take the least unused non-negative ones; an extension item without one
// takes the least number above the previous addition that the root does not use.
struct EnumerationNumbering {
    std::set<BigInt> root_;
    BigInt next_ = 0;
    BigInt last_addition_ = -1;
    bool extension_ = false;

    BigInt take() {
        auto candidate = extension_ ? BigInt(last_addition_ + 1) : next_;
        while (root_.contains(candidate)) {
            ++candidate;
        }
        next_ = candidate + 1;
        return candidate;
    }

    void record(const BigInt &value) {
        if (extension_) {
            last_addition_ = value;
        } else {
            root_.insert(value);
        }
    }
};
} // namespace

// The numbers written for the root items, which unnumbered items avoid.
std::set<BigInt> TypeChecker::root_numbers(const TypeSyntax &syntax) {
    std::set<BigInt> root;
    for (const auto &item : syntax.named_) {
        if (item.name_ == "...") {
            break;
        }
        if (!item.value_.empty()) {
            root.insert(checker_.integer(item.value_, scope_));
        }
    }
    return root;
}

void TypeChecker::enumeration_values(const TypeSyntax &syntax, Node &node) {
    EnumerationNumbering numbering{root_numbers(syntax)};
    for (const auto &item : syntax.named_) {
        if (item.name_ == "...") {
            numbering.extension_ = true;
            node.extensible_ = true;
            continue;
        }
        const auto value = item.value_.empty() ? numbering.take() : checker_.integer(item.value_, scope_);
        numbering.record(value);
        node.enumeration_root_ += numbering.extension_ ? 0 : 1;
        node.named_numbers_.emplace_back(item.name_, value);
    }
}

Resolved TypeChecker::sequence(const TypeSyntax &syntax) { return constructed(syntax, NodeKind::sequence, 16); }

Resolved TypeChecker::set(const TypeSyntax &syntax) { return constructed(syntax, NodeKind::set, 17); }

Resolved TypeChecker::choice(const TypeSyntax &syntax) {
    Node node;
    node.kind_ = NodeKind::choice;
    const auto id = checker_.add(std::move(node));
    check_components(checker_, id, syntax, scope_, context_);
    return {id, true, false};
}

Resolved TypeChecker::constructed(const TypeSyntax &syntax, const NodeKind kind, const std::uint64_t tag) {
    auto node = basic(kind, tag);
    node.record_ = record_name(context_.path_);
    const auto id = checker_.add(std::move(node));
    auto inner = context_;
    inner.frames_.push_back({&syntax, scope_});
    check_components(checker_, id, syntax, scope_, inner);
    return {id, true, false};
}

Resolved TypeChecker::sequence_of(const TypeSyntax &syntax) {
    return collection(syntax, NodeKind::sequence_of, 16, "SEQOF");
}

Resolved TypeChecker::set_of(const TypeSyntax &syntax) { return collection(syntax, NodeKind::set_of, 17, "SETOF"); }

Resolved TypeChecker::collection(const TypeSyntax &syntax, const NodeKind kind, const std::uint64_t tag,
                                 const char *suffix) {
    const auto id = checker_.add(basic(kind, tag));
    const auto element = checker_.type(*syntax.element_, scope_, context_.with(element_part(*syntax.element_, suffix)));
    checker_.node(id).element_ = element;
    if (syntax.collection_constraint_) {
        TypeSyntax holder;
        holder.constraints_.push_back(syntax.collection_constraint_);
        checker_.constrain(id, holder, scope_, context_);
    }
    return {id, true, false};
}

// asn1ct's constructed_suffix: an instance of a parameterized SEQUENCE or SET is named after its definition.
PathPart TypeChecker::element_part(const TypeSyntax &element, const char *suffix) {
    if (element.kind_ != TypeKind::reference || !element.parameterized_) {
        return {suffix, false};
    }
    const auto found = checker_.try_find(element.reference_, scope_);
    if (!found || found->assignment_ == nullptr || !found->assignment_->type_) {
        return {suffix, false};
    }
    const auto body = found->assignment_->type_->kind_;
    if (body == TypeKind::sequence || body == TypeKind::set) {
        return {found->assignment_->name_, true};
    }
    return {suffix, false};
}

// `a < T`: the type of alternative `a` of CHOICE T.
Resolved TypeChecker::selection(const TypeSyntax &syntax) {
    const auto choice = checker_.type(*syntax.element_, scope_, context_);
    for (const auto &alternative : checker_.content(choice).components_) {
        if (alternative.name_ == syntax.identifier_) {
            return {alternative.type_, false, false};
        }
    }
    checker_.fail(scope_, syntax.token_, "no alternative " + syntax.identifier_ + " in the selected CHOICE");
}

// The definition a reference names. asn1ct lets a module define Name while importing a parameterized Name{}: the
// instance names the import.
Checker::Found TypeChecker::referenced(const TypeSyntax &syntax) {
    auto found = checker_.find(syntax.reference_, scope_);
    if (syntax.parameterized_ && found.assignment_ != nullptr && !found.assignment_->parameterized_) {
        if (auto imported = checker_.imported(syntax.reference_, scope_)) {
            return *imported;
        }
    }
    return found;
}

Resolved TypeChecker::reference(const TypeSyntax &syntax) {
    const auto found = referenced(syntax);
    if (found.binding_ != nullptr) {
        return binding(*found.binding_, syntax);
    }
    if (found.assignment_ == nullptr) {
        checker_.fail(scope_, syntax.token_, "undefined reference " + syntax.reference_.name_);
    }
    const auto &assignment = *found.assignment_;
    if (assignment.parameterized_) {
        return instance(found, syntax);
    }
    const bool type =
        assignment.kind_ == AssignmentSyntax::Kind::type || assignment.kind_ == AssignmentSyntax::Kind::set;
    if (!type || syntax.parameterized_) {
        checker_.fail(scope_, syntax.token_, syntax.reference_.name_ + " is not a type");
    }
    return {checker_.named_type(found), false, false};
}

// A type parameter stands for its actual type, checked here.
Resolved TypeChecker::binding(const Binding &binding, const TypeSyntax &syntax) {
    if (binding.kind_ != Binding::Kind::type) {
        checker_.fail(scope_, syntax.token_, syntax.reference_.name_ + " is not a type parameter");
    }
    auto resolved = TypeChecker(checker_, binding.type_.scope_, context_).resolve(*binding.type_.syntax_);
    return {resolved.id_, false, true};
}

// An instance of a parameterized type, checked where it is used; a SEQUENCE or SET body records its definition.
Resolved TypeChecker::instance(const Checker::Found &found, const TypeSyntax &syntax) {
    const auto &assignment = *found.assignment_;
    if (assignment.kind_ != AssignmentSyntax::Kind::type || !syntax.parameterized_) {
        checker_.fail(scope_, syntax.token_, assignment.name_ + " is not a parameterized type");
    }
    const auto inner = checker_.instantiate(found, syntax.arguments_, scope_, syntax.token_);
    Context body;
    body.path_ = context_.path_;
    auto resolved = TypeChecker(checker_, inner, body).resolve(*assignment.type_);
    const auto kind = assignment.type_->kind_;
    if (resolved.fresh_ && (kind == TypeKind::sequence || kind == TypeKind::set)) {
        checker_.node(resolved.id_).pname_ = assignment.name_;
    }
    return resolved;
}

Resolved TypeChecker::own(const Resolved resolved) {
    if (resolved.fresh_) {
        return resolved;
    }
    const auto &original = checker_.node(resolved.id_);
    if (original.kind_ == NodeKind::reference) {
        Node reference;
        reference.kind_ = NodeKind::reference;
        reference.target_ = resolved.id_;
        reference.named_ = original.named_;
        return {checker_.add(std::move(reference)), true, resolved.dummy_};
    }
    auto copy = original;
    return {checker_.add(std::move(copy)), true, resolved.dummy_};
}

NodeId Checker::type(const TypeSyntax &syntax, const Scope &scope, const Context &context) {
    return TypeChecker(*this, scope, context).resolve(syntax).id_;
}

NodeId Checker::named_type(const Found &found) {
    const auto &assignment = *found.assignment_;
    const std::pair key{found.scope_.module_->syntax_->name_, assignment.name_};
    if (const auto known = named_types_.find(key); known != named_types_.end()) {
        return known->second;
    }
    if (assignment.kind_ == AssignmentSyntax::Kind::set) {
        // `T Governor ::= {...}`: the governor constrained by the value set.
        auto parser = this->parser(found.scope_, {assignment.value_.begin_ + 1, assignment.value_.end_ - 1});
        TypeSyntax constrained = *assignment.governor_;
        constrained.constraints_.push_back(parser.constraint(false));
        return declare_into(key, assignment.name_, constrained, found.scope_);
    }
    return declare_into(key, assignment.name_, *assignment.type_, found.scope_);
}

NodeId Checker::declare(const std::string &name, const TypeSyntax &syntax, const Scope &scope) {
    return declare_into({scope.module_->syntax_->name_, name}, name, syntax, scope);
}

// A placeholder first (recursive types refer to it), then the checked type copied in.
NodeId Checker::declare_into(const std::pair<std::string, std::string> &key, const std::string &name,
                             const TypeSyntax &syntax, const Scope &scope) {
    Node placeholder;
    placeholder.kind_ = NodeKind::reference;
    placeholder.named_ = name;
    const auto id = add(std::move(placeholder));
    named_types_.emplace(key, id);
    Context context;
    context.path_.push_back({name, false});
    const auto resolved = type(syntax, scope, context);
    auto copy = nodes_[resolved];
    if (copy.kind_ == NodeKind::reference && copy.target_ == NO_NODE) {
        // A type still being checked (mutual recursion): refer to it.
        copy.target_ = resolved;
    }
    copy.alias_ = !copy.named_.empty();
    if (copy.named_.empty()) {
        copy.named_ = name;
    }
    nodes_[id] = std::move(copy);
    return id;
}

void Checker::object_type(const TypeSyntax &syntax, const Scope &scope) {
    const auto name = current_definition_ + "_" + syntax.reference_.name_;
    for (const auto &known : object_types_) {
        if (known.first == name) {
            return;
        }
    }
    object_types_.emplace_back(name, declare(name, syntax, scope));
}
} // namespace clause::asn1::detail
