#pragma once
#include "../collections/collections.hpp"
#include "../syntax/syntax.hpp"
#include <algorithm>
#include <vector>

// erl_syntax_lib in C++ (plan 21 LIB77E4a, docs/builtins.md#erl-syntax-lib): the analyses, binding annotations,
// variables, limit/2,3 and strip_comments/1 over erl_syntax's nodes; the folds and maps, which take funs, stay Erlang.
namespace clause::runtime::builtins::syntax_lib {
using namespace syntax;

// throw(syntax_error), which the analyses raise for a node they cannot analyse.
[[noreturn]] inline void syntax_error(ProcessContext &context) {
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_throw,
                         .payload = builtins::atom(context, "syntax_error"_atom).word()};
}

// Whether a failure is throw(syntax_error).
inline bool is_syntax_error(const BuiltinFailure &failure, ProcessContext &context) {
    return failure.reason == abi::v1::ErrorReason::raised_throw &&
           failure.payload == builtins::atom(context, "syntax_error"_atom).word();
}

// An ordset of terms (variable names), merged as ordsets merges them, in Erlang's term order.
class Names final {
  public:
    Names() = default;

    // The elements of an ordset term, taken as sorted.
    explicit Names(const Term &ordset) : items_(syntax::elements(ordset)) {}

    static Names of(const Term &name) {
        Names names;
        names.items_.push_back(name);
        return names;
    }

    bool contains(const Term &name) const {
        return std::ranges::any_of(items_, [&](const Term &item) { return collections::compare(item, name) == 0; });
    }

    // ordsets:union/2, subtract/2 and intersection/2.
    Names unite(const Names &other) const;
    Names minus(const Names &other) const;
    Names common(const Names &other) const;

    Term list(const Trees &trees) const { return trees.list(items_); }

  private:
    // The elements in ascending order.
    Items items_;
};

// The subtree groups of a node, each a vector; empty for a leaf.
std::vector<Items> groups_of(ProcessContext &context, const Term &node);
// make_tree(type(Node), Groups) with Node's attributes (erl_syntax_lib's rewrite/2 of a rebuilt node).
Term rebuilt(ProcessContext &context, const Term &node, const std::vector<Items> &groups);

// map_subtrees/2 with a C++ visitor: each subtree replaced by visit(Subtree); a leaf is returned as it is.
template <typename Visit> Term map_subtrees(ProcessContext &context, const Term &node, Visit &&visit) {
    auto groups = groups_of(context, node);
    if (groups.empty()) {
        return node;
    }
    for (auto &group : groups) {
        for (auto &item : group) {
            item = visit(item);
        }
    }
    return rebuilt(context, node, groups);
}

// analyze_function_name/1: Name, {Name, Arity} or {Module, ...}; throws syntax_error for anything else.
Term function_name_of(ProcessContext &context, const Term &node);
} // namespace clause::runtime::builtins::syntax_lib
