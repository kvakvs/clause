#include "merl.hpp"

// merl's view of syntax tree shapes as builtins (plan 21 LIB77E5): its type/1, subtrees/1 and make_tree/2, which
// give lists, tuples, binary fields, clauses, receives and record nodes fixed groups, and clause_merl:subtrees/1.
namespace clause::runtime::builtins::merl {
namespace {
// [] for an absent part (none), else [Part].
Items optional_group(const Term &part) { return syntax::is_none(part) ? Items{} : Items{part}; }

// The groups of the nodes merl shapes itself; none for the others (erl_syntax's subtrees/1).
std::optional<std::vector<Items>> own_groups(ProcessContext &context, const Term &tree, std::string_view type) {
    using syntax::elements;
    using syntax::get;
    if (type == "tuple") {
        return std::vector<Items>{elements(get(context, tree, "tuple_elements"))};
    }
    if (type == "nil") {
        return std::vector<Items>{{}, {}};
    }
    if (type == "list") {
        return std::vector<Items>{elements(syntax::list_prefix(context, tree)),
                                  optional_group(syntax::list_suffix(context, tree))};
    }
    if (type == "binary_field") {
        return std::vector<Items>{Items{syntax::binary_field_body(context, tree)},
                                  elements(syntax::binary_field_types(context, tree))};
    }
    if (type == "clause") {
        return std::vector<Items>{elements(get(context, tree, "clause_patterns")),
                                  optional_group(syntax::clause_guard(context, tree)),
                                  elements(get(context, tree, "clause_body"))};
    }
    return std::nullopt;
}

// The groups of receives and record nodes, the rest of own_groups.
std::optional<std::vector<Items>> record_groups(ProcessContext &context, const Term &tree, std::string_view type) {
    using syntax::elements;
    if (type == "receive_expr") {
        const auto timeout = syntax::receive_expr_timeout(context, tree);
        const auto clauses = elements(syntax::receive_expr_clauses(context, tree));
        if (syntax::is_none(timeout)) {
            return std::vector<Items>{clauses, {}, {}};
        }
        return std::vector<Items>{clauses, Items{timeout}, elements(syntax::receive_expr_action(context, tree))};
    }
    if (type == "record_expr") {
        return std::vector<Items>{optional_group(syntax::record_expr_argument(context, tree)),
                                  Items{syntax::record_expr_type(context, tree)},
                                  elements(syntax::record_expr_fields(context, tree))};
    }
    if (type == "record_field") {
        return std::vector<Items>{Items{syntax::get(context, tree, "record_field_name")},
                                  optional_group(syntax::get(context, tree, "record_field_value"))};
    }
    return std::nullopt;
}

// A part of a fixed group of at most one: the part, or none.
Term part_or_none(const Trees &trees, const Items &group) {
    return group.empty() ? trees.atom("none"_atom) : group.front();
}

// What a group of a fixed shape holds: any number of trees, exactly one, or at most one.
enum class Size : std::uint8_t { any, one, optional };

using Build = Term (*)(ProcessContext &context, const Trees &trees, const std::vector<Items> &groups);

// One of merl's make_tree/2 clauses: the node type, its groups' sizes and how the node is built.
struct Shape {
    std::string_view type;
    std::vector<Size> sizes;
    Build build;
};

Term build_list(ProcessContext &context, const Trees &trees, const std::vector<Items> &groups) {
    return syntax::list(context, trees.list(groups[0]), part_or_none(trees, groups[1]));
}

Term build_tuple(ProcessContext &context, const Trees &trees, const std::vector<Items> &groups) {
    return syntax::make(context, "tuple", std::array{trees.list(groups[0])});
}

Term build_binary_field(ProcessContext &context, const Trees &trees, const std::vector<Items> &groups) {
    return syntax::make(context, "binary_field", std::array{groups[0][0], trees.list(groups[1])});
}

Term build_clause(ProcessContext &context, const Trees &trees, const std::vector<Items> &groups) {
    return syntax::clause(context,
                          std::array{trees.list(groups[0]), part_or_none(trees, groups[1]), trees.list(groups[2])});
}

// receive_expr(Clauses) without a timeout (its action group ignored), else receive_expr/3.
Term build_receive(ProcessContext &context, const Trees &trees, const std::vector<Items> &groups) {
    const auto action = groups[1].empty() ? trees.nil() : trees.list(groups[2]);
    return syntax::receive_expr(context, std::array{trees.list(groups[0]), part_or_none(trees, groups[1]), action});
}

Term build_record_expr(ProcessContext &context, const Trees &trees, const std::vector<Items> &groups) {
    return syntax::record_expr(context,
                               std::array{part_or_none(trees, groups[0]), groups[1][0], trees.list(groups[2])});
}

Term build_record_field(ProcessContext &context, const Trees &trees, const std::vector<Items> &groups) {
    return syntax::make(context, "record_field", std::array{groups[0][0], part_or_none(trees, groups[1])});
}

// Whether groups have a shape's sizes.
bool fits(const Shape &shape, const std::vector<Items> &groups) {
    if (groups.size() != shape.sizes.size()) {
        return false;
    }
    for (std::size_t at = 0; at < groups.size(); ++at) {
        const auto count = groups[at].size();
        const auto size = shape.sizes[at];
        if ((size == Size::one && count != 1) || (size == Size::optional && count > 1)) {
            return false;
        }
    }
    return true;
}

// merl's make_tree/2 clauses for a type and its groups; none when erl_syntax's make_tree/2 builds the node.
std::optional<Term> own_node(ProcessContext &context, std::string_view type, const std::vector<Items> &groups) {
    using enum Size;
    const std::array<Shape, 7> shapes{{
        {"list", {any, optional}, build_list},
        {"tuple", {any}, build_tuple},
        {"binary_field", {one, any}, build_binary_field},
        {"clause", {any, optional, any}, build_clause},
        {"receive_expr", {any, optional, any}, build_receive},
        {"record_expr", {optional, one, any}, build_record_expr},
        {"record_field", {one, optional}, build_record_field},
    }};
    for (const auto &shape : shapes) {
        if (shape.type == type && fits(shape, groups)) {
            return shape.build(context, Trees(context), groups);
        }
    }
    return std::nullopt;
}

// clause_merl:subtrees(Tree): merl's groups of a node.
Term subtrees1(ProcessContext &context, const Term &tree) {
    const Trees trees(context);
    Items groups;
    for (const auto &group : node_groups(context, tree)) {
        groups.push_back(trees.list(group));
    }
    return trees.list(groups);
}

constexpr std::array SHAPES{
    typed_entry<subtrees1>("clause_merl", "subtrees"),
};
} // namespace

Term node_type(ProcessContext &context, const Term &tree) {
    const auto type = syntax::type_of(context, tree);
    return Trees(context).atom_of(type == "nil" ? "list" : type);
}

std::vector<Items> node_groups(ProcessContext &context, const Term &tree) {
    const auto type = syntax::type_of(context, tree);
    if (auto groups = own_groups(context, tree, type)) {
        return std::move(*groups);
    }
    if (auto groups = record_groups(context, tree, type)) {
        return std::move(*groups);
    }
    std::vector<Items> groups;
    for (const auto &group : syntax::elements(syntax::subtrees(context, tree))) {
        groups.push_back(syntax::elements(group));
    }
    return groups;
}

Term make_node(ProcessContext &context, const Term &type, const std::vector<Items> &groups) {
    const auto spelling = type.is_atom() ? type.atom_spelling_unchecked() : std::string_view{};
    if (auto made = own_node(context, spelling, groups)) {
        return *made;
    }
    const Trees trees(context);
    Items lists;
    for (const auto &group : groups) {
        lists.push_back(trees.list(group));
    }
    return syntax::make_tree(context, type, {trees.list(lists)});
}

void fail(ProcessContext &context, std::u32string_view message) {
    const Trees trees(context);
    Items codes;
    for (const auto code : message) {
        codes.push_back(trees.integer(code));
    }
    const auto reason = trees.tuple(std::array{trees.atom("error"_atom), trees.list(codes)});
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_throw, .payload = reason.word()};
}

void flatten_into(const Term &term, Items &into) {
    if (!term.is_list()) {
        into.push_back(term);
        return;
    }
    for (const auto &item : syntax::elements(term)) {
        flatten_into(item, into);
    }
}
} // namespace clause::runtime::builtins::merl

namespace clause::runtime {
// Publish clause_merl's shapes.
std::span<const BuiltinEntry> merl_shape_builtins() noexcept { return builtins::merl::SHAPES; }
} // namespace clause::runtime
