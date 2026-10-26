#pragma once
// Added for the ASN.1 compiler (plan 21 step LIB84): resolving one type notation into nodes.
#include "checker_internal.hpp"
#include <set>

namespace clause::asn1::detail {
// A resolved type: its node, whether the node was made for this notation (and may be changed in place), and whether
// it came from a type parameter (X.680's DummyReference).
struct Resolved {
    NodeId id_ = NO_NODE;
    bool fresh_ = false;
    bool dummy_ = false;
};

// Resolves the parts of one type notation in one scope and context.
class TypeChecker {
  public:
    TypeChecker(Checker &checker, const Scope &scope, const Context &context)
        : checker_(checker), scope_(scope), context_(context) {}

    // The type of `syntax` with its tags and constraints.
    Resolved resolve(const TypeSyntax &syntax);
    Resolved untagged(const TypeSyntax &syntax);

  private:
    Resolved fresh(Node node);
    Resolved boolean(const TypeSyntax &syntax);
    Resolved integer(const TypeSyntax &syntax);
    Resolved bit_string(const TypeSyntax &syntax);
    Resolved octet_string(const TypeSyntax &syntax);
    Resolved null(const TypeSyntax &syntax);
    Resolved object_identifier(const TypeSyntax &syntax);
    Resolved relative_oid(const TypeSyntax &syntax);
    Resolved real(const TypeSyntax &syntax);
    Resolved enumerated(const TypeSyntax &syntax);
    Resolved sequence(const TypeSyntax &syntax);
    Resolved set(const TypeSyntax &syntax);
    Resolved sequence_of(const TypeSyntax &syntax);
    Resolved set_of(const TypeSyntax &syntax);
    Resolved choice(const TypeSyntax &syntax);
    Resolved restricted_string(const TypeSyntax &syntax);
    Resolved utc_time(const TypeSyntax &syntax);
    Resolved generalized_time(const TypeSyntax &syntax);
    Resolved object_descriptor(const TypeSyntax &syntax);
    Resolved any(const TypeSyntax &syntax);
    Resolved class_field(const TypeSyntax &syntax);
    Resolved object_field(const TypeSyntax &syntax);
    Resolved selection(const TypeSyntax &syntax);
    Resolved instance_of(const TypeSyntax &syntax);
    Checker::Found referenced(const TypeSyntax &syntax);
    Resolved reference(const TypeSyntax &syntax);
    Resolved unsupported(const TypeSyntax &syntax);
    Resolved not_a_type(const TypeSyntax &syntax);

    Resolved constructed(const TypeSyntax &syntax, NodeKind kind, std::uint64_t tag);
    Resolved collection(const TypeSyntax &syntax, NodeKind kind, std::uint64_t tag, const char *suffix);
    PathPart element_part(const TypeSyntax &element, const char *suffix);
    Resolved binding(const Binding &binding, const TypeSyntax &syntax);
    Resolved instance(const Checker::Found &found, const TypeSyntax &syntax);
    std::vector<std::pair<std::string, BigInt>> named_numbers(const TypeSyntax &syntax);
    void enumeration_values(const TypeSyntax &syntax, Node &node);
    std::set<BigInt> root_numbers(const TypeSyntax &syntax);
    // Make `resolved` a node this notation owns: a copy of a shared node, or a reference to one being checked.
    Resolved own(Resolved resolved);

    Checker &checker_;
    const Scope &scope_;
    const Context &context_;
};

// Tags of a notation applied to its type (X.680 31): EXPLICIT adds an outer tag, IMPLICIT replaces the outermost
// one; IMPLICIT becomes EXPLICIT on an untagged CHOICE or open type.
void apply_tags(Checker &checker, NodeId id, const std::vector<TagSyntax> &tags, const Scope &scope);
// A component type with the context tag of automatic tagging (IMPLICIT unless an untagged CHOICE or open type):
// a copy of `id` with the tag, or a reference to it while it is still being checked.
NodeId tag_component(Checker &checker, NodeId id, const TagValue &tag);
} // namespace clause::asn1::detail
