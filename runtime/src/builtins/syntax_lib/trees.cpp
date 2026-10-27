#include "syntax_lib.hpp"

// erl_syntax_lib's walks without funs as builtins (plan 21 LIB77E4a): variables/1, limit/2,3 and strip_comments/1,
// with the ordsets and subtree rebuilding the other files share.
namespace clause::runtime::builtins::syntax_lib {
Names Names::unite(const Names &other) const {
    Names joined;
    auto left = items_.begin();
    auto right = other.items_.begin();
    while (left != items_.end() && right != other.items_.end()) {
        const auto order = collections::compare(*left, *right);
        joined.items_.push_back(order < 0 ? *left : *right);
        left += order <= 0 ? 1 : 0;
        right += order >= 0 ? 1 : 0;
    }
    joined.items_.insert(joined.items_.end(), left, items_.end());
    joined.items_.insert(joined.items_.end(), right, other.items_.end());
    return joined;
}

Names Names::minus(const Names &other) const {
    Names rest;
    std::ranges::copy_if(items_, std::back_inserter(rest.items_),
                         [&](const Term &item) { return !other.contains(item); });
    return rest;
}

Names Names::common(const Names &other) const {
    Names both;
    std::ranges::copy_if(items_, std::back_inserter(both.items_),
                         [&](const Term &item) { return other.contains(item); });
    return both;
}

std::vector<Items> groups_of(ProcessContext &context, const Term &node) {
    std::vector<Items> groups;
    for (const auto &group : syntax::elements(subtrees(context, node))) {
        groups.push_back(syntax::elements(group));
    }
    return groups;
}

Term rebuilt(ProcessContext &context, const Term &node, const std::vector<Items> &groups) {
    const Trees trees(context);
    Items lists;
    for (const auto &group : groups) {
        lists.push_back(trees.list(group));
    }
    return copy_attrs(context, node, make_tree(context, node_type(context, node), {trees.list(lists)}));
}

namespace {
// variables/2: the names of the variables in a node, but a macro's name.
void collect_variables(ProcessContext &context, const Term &node, std::vector<std::pair<Term, Term>> &into) {
    const auto type = type_of(context, node);
    if (type == "variable") {
        into.emplace_back(get(context, node, "variable_name"), Trees(context).nil());
        return;
    }
    if (type == "macro") {
        const auto arguments = get(context, node, "macro_arguments");
        for (const auto &argument : is_none(arguments) ? Items{} : syntax::elements(arguments)) {
            collect_variables(context, argument, into);
        }
        return;
    }
    for (const auto &group : groups_of(context, node)) {
        for (const auto &item : group) {
            collect_variables(context, item, into);
        }
    }
}

// variables/1: a set (sets:new/0's map) of the variable names.
Term variables(ProcessContext &context, const Term &node) {
    std::vector<std::pair<Term, Term>> names;
    collect_variables(context, node, names);
    return need(TermFactory(context).map(names));
}

// The leaf types limit/3 keeps at depth zero.
constexpr auto SIMPLE_LEAVES = std::to_array<std::string_view>(
    {"atom", "char", "float", "integer", "nil", "operator", "tuple", "underscore", "variable"});

bool is_simple_leaf(ProcessContext &context, const Term &node) {
    return std::ranges::find(SIMPLE_LEAVES, type_of(context, node)) != SIMPLE_LEAVES.end();
}

// limit/3's state: the replacement node.
struct Limit {
    ProcessContext &context;
    Term node;
};

Term limit_node(const Limit &limit, const Term &tree, std::int64_t depth);

// limit_list/3: a group longer than the depth keeps depth - 1 subtrees and the replacement.
Items limit_list(const Limit &limit, const Items &group, std::int64_t depth) {
    if (std::int64_t(group.size()) <= depth) {
        return group;
    }
    Items kept(group.begin(), group.begin() + std::max<std::int64_t>(depth - 1, 0));
    kept.push_back(limit.node);
    return kept;
}

// cut_group/2 at depth one: a group of one subtree may keep a simple leaf, a longer one becomes the replacement.
Items cut_group(const Limit &limit, const Items &group) {
    if (group.size() > 1) {
        return {limit.node};
    }
    Items cut;
    for (const auto &item : group) {
        cut.push_back(limit_node(limit, item, 0));
    }
    return cut;
}

// limit_1/3 with a nonnegative depth.
Term limit_node(const Limit &limit, const Term &tree, std::int64_t depth) {
    auto groups = groups_of(limit.context, tree);
    if (groups.empty()) {
        return depth > 0 || is_simple_leaf(limit.context, tree) ? tree : limit.node;
    }
    if (depth == 0) {
        return limit.node;
    }
    for (auto &group : groups) {
        if (depth == 1) {
            group = cut_group(limit, group);
            continue;
        }
        group = limit_list(limit, group, depth);
        for (auto &item : group) {
            item = limit_node(limit, item, depth - 1);
        }
    }
    return rebuilt(limit.context, tree, groups);
}

// The roles of limit/3's depth and replacement node (typed.hpp's Named).
struct DepthRole;
struct StubRole;

Term limit3(ProcessContext &context, const Term &tree, const Named<DepthRole> &depth, const Named<StubRole> &stub) {
    const auto value = small(depth.term.word());
    if (!value) {
        bad_argument();
    }
    return *value < 0 ? stub.term : limit_node({context, stub.term}, tree, *value);
}

Term limit2(ProcessContext &context, const Term &tree, const Named<DepthRole> &depth) {
    const Trees trees(context);
    return limit3(context, tree, depth, {make(context, "text", std::array{trees.chars("...")})});
}

// strip_comments_1/1: comments out of a form list, a comment node emptied, comments off any other node.
Term strip_node(ProcessContext &context, const Term &node) {
    const Trees trees(context);
    const auto type = type_of(context, node);
    if (type == "comment") {
        return make(context, "comment", std::array{trees.atom("none"_atom), trees.nil()});
    }
    if (type != "form_list") {
        return remove_comments(context, node);
    }
    Items forms;
    for (const auto &form : syntax::elements(get(context, node, "form_list_elements"))) {
        if (type_of(context, form) != "comment") {
            forms.push_back(form);
        }
    }
    const auto stripped = copy_attrs(context, node, make(context, "form_list", std::array{trees.list(forms)}));
    return remove_comments(context, stripped);
}

// strip_comments/1: strip_node over every node, bottom-up (map/2).
Term strip_comments(ProcessContext &context, const Term &node) {
    auto groups = groups_of(context, node);
    if (groups.empty()) {
        return strip_node(context, node);
    }
    for (auto &group : groups) {
        for (auto &item : group) {
            item = strip_comments(context, item);
        }
    }
    return strip_node(context, rebuilt(context, node, groups));
}

constexpr std::array TREES{
    typed_entry<variables>("erl_syntax_lib", "variables"),
    typed_entry<limit2>("erl_syntax_lib", "limit"),
    typed_entry<limit3>("erl_syntax_lib", "limit"),
    typed_entry<strip_comments>("erl_syntax_lib", "strip_comments"),
};
} // namespace
} // namespace clause::runtime::builtins::syntax_lib

namespace clause::runtime {
// Publish erl_syntax_lib's walks without funs.
std::span<const BuiltinEntry> erl_syntax_lib_tree_builtins() noexcept { return builtins::syntax_lib::TREES; }
} // namespace clause::runtime
