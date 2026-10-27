#pragma once
#include "../collections/collections.hpp"
#include "../syntax/syntax.hpp"

// merl's templates in C++ (plan 21 LIB77E5, docs/builtins.md#merl): syntax trees with metavariables ('@Name',
// _@Name, 909N, "'@Name") as templates {template, Type, Attrs, Groups}, {Id} and {'*', Id}, substitution, renaming,
// matching and the template's meta form. merl views nodes in its own way: [] is a list, {} keeps its element group,
// and lists, binary fields, clauses, receives and record nodes have fixed groups (clause_merl:subtrees/1).
namespace clause::runtime::builtins::merl {
using front_end::forms::element;
using front_end::forms::is_atom;
using syntax::Items;
using syntax::Trees;

// merl's type/1: erl_syntax's type, with nil as list.
Term node_type(ProcessContext &context, const Term &tree);
// merl's subtrees/1 and make_tree/2: groups by merl's view of a node, and a node of a type from them.
std::vector<Items> node_groups(ProcessContext &context, const Term &tree);
Term make_node(ProcessContext &context, const Term &type, const std::vector<Items> &groups);

// A template {template, Type, Attrs, Groups}.
inline bool is_template(const Term &term) {
    return term.is_tuple() && term.tuple_size_unchecked() == 4 && is_atom(element(term, 0), "template");
}

// A metavariable {Id} or a glob {'*', Id}.
inline bool is_variable(const Term &term) { return term.is_tuple() && term.tuple_size_unchecked() == 1; }

inline bool is_glob(const Term &term) {
    return term.is_tuple() && term.tuple_size_unchecked() == 2 && is_atom(element(term, 0), "*");
}

// template/1 of one pattern: a template, or the tree itself when it has no metavariables.
Term template_of(ProcessContext &context, const Term &pattern);

// throw({error, Message}), merl's fail/2.
[[noreturn]] void fail(ProcessContext &context, std::u32string_view message);

// The elements of a list term (lists:flatten/1 of a group: nested lists spliced in).
void flatten_into(const Term &term, Items &into);
} // namespace clause::runtime::builtins::merl
