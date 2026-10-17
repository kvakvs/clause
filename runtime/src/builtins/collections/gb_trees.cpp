#include "balanced_tree.hpp"
#include <algorithm>
#include <array>
#include <ranges>

// The gb_trees builtins (plan 21 step LIB3, docs/builtins.md#collections): {Size, Tree} over the balanced tree
// engine (balanced_tree.hpp). A missing key of get/2, update/3, delete/2 or take/2, or an empty tree's
// smallest/largest, raises function_clause; insert/3 of a present key {key_exists, Key}; from_orddict/1 of an
// unsorted list {badarg, not_orddict}.
namespace clause::runtime::builtins::collections {
namespace {
struct OrderRole;
// An iterator order: ordered or reversed.
using Order = Named<OrderRole>;

BalancedTree trees(ProcessContext &context) { return BalancedTree(context, true); }

Term atom(ProcessContext &context, std::string_view name) { return need(TermFactory(context).atom(name)); }

Term tuple(ProcessContext &context, std::span<const Term> parts) { return need(TermFactory(context).tuple(parts)); }

Term pair(ProcessContext &context, const NodeParts &node) { return tuple(context, std::array{node.key, node.value}); }

// lookup(Key, Tree): {value, Value} or none.
Term lookup(ProcessContext &context, const Key &key, const List &tree) {
    const auto found = trees(context).find(key, tree_parts(tree.term, false).root);
    return found ? tuple(context, std::array{atom(context, "value"), found->value}) : atom(context, "none");
}

// is_defined(Key, Tree).
Term is_defined(ProcessContext &context, const Key &key, const List &tree) {
    return need(TermFactory(context).boolean(trees(context).find(key, tree_parts(tree.term, false).root).has_value()));
}

// get(Key, Tree): function_clause when absent.
Term get(ProcessContext &context, const Key &key, const List &tree) {
    const auto found = trees(context).find(key, tree_parts(tree.term, false).root);
    if (!found) {
        no_clause();
    }
    return found->value;
}

// insert(Key, Value, Tree1): with OTP's weight check; an integer size is all OTP checks.
Term insert(ProcessContext &context, const Key &key, const Value &value, const List &tree) {
    const auto parts = tree_parts(tree.term, false);
    if (!parts.size_term.is_integer() || !small(parts.size_term.word())) {
        no_clause();
    }
    const auto size = parts.size + 1;
    const auto budget = static_cast<std::uint64_t>(size) * static_cast<std::uint64_t>(size);
    const auto grown = trees(context).insert({key.term, value.term, {}, {}}, parts.root, budget);
    if (!grown.measure) {
        return sized_tree(context, size, grown.tree);
    }
    TermFactory factory(context);
    const std::array measured{grown.tree, need(factory.integer(static_cast<std::int64_t>(grown.measure->height))),
                              need(factory.integer(static_cast<std::int64_t>(grown.measure->size)))};
    return sized_tree(context, size, tuple(context, measured));
}

// update(Key, Value, Tree1): function_clause when absent.
Term update(ProcessContext &context, const Key &key, const Value &value, const List &tree) {
    const auto parts = tree_parts(tree.term, false);
    const auto root = trees(context).replace({key.term, value.term, {}, {}}, parts.root);
    return tuple(context, std::array{parts.size_term, root});
}

// enter(Key, Value, Tree1): update/3 or insert/3.
Term enter(ProcessContext &context, const Key &key, const Value &value, const List &tree) {
    const auto present = trees(context).find(key, tree_parts(tree.term, false).root).has_value();
    return present ? update(context, key, value, tree) : insert(context, key, value, tree);
}

// delete(Key, Tree1): function_clause when absent.
Term delete_key(ProcessContext &context, const Key &key, const List &tree) {
    const auto parts = tree_parts(tree.term, true);
    return sized_tree(context, parts.size - 1, trees(context).remove(key, parts.root));
}

// delete_any(Key, Tree1): Tree1 itself when Key is absent.
Term delete_any(ProcessContext &context, const Key &key, const List &tree) {
    const auto present = trees(context).find(key, tree_parts(tree.term, false).root).has_value();
    return present ? delete_key(context, key, tree) : tree.term;
}

// take(Key, Tree1): {Value, Tree2}; function_clause when absent.
Term take(ProcessContext &context, const Key &key, const List &tree) {
    const auto parts = tree_parts(tree.term, true);
    const auto engine = trees(context);
    const auto found = engine.find(key, parts.root);
    if (!found) {
        no_clause();
    }
    return tuple(context,
                 std::array{found->value, sized_tree(context, parts.size - 1, engine.remove(key, parts.root))});
}

// take_any(Key, Tree1): take/2, or error.
Term take_any(ProcessContext &context, const Key &key, const List &tree) {
    const auto present = trees(context).find(key, tree_parts(tree.term, false).root).has_value();
    return present ? take(context, key, tree) : atom(context, "error");
}

// balance(Tree1): rebuilt from its entries.
Term balance(ProcessContext &context, const List &tree) {
    const auto parts = tree_parts(tree.term, true);
    const auto engine = trees(context);
    return sized_tree(context, parts.size, engine.build(engine.in_order(parts.root)));
}

// to_list(Tree), keys(Tree), values(Tree).
template <typename Part> Term listed(ProcessContext &context, const List &tree, Part part) {
    Prefix result;
    for (const auto &node : trees(context).in_order(tree_parts(tree.term, false).root)) {
        result.push(part(node));
    }
    return result.done(context);
}

Term to_list(ProcessContext &context, const List &tree) {
    return listed(context, tree, [&](const NodeParts &node) { return pair(context, node); });
}

Term keys(ProcessContext &context, const List &tree) {
    return listed(context, tree, [](const NodeParts &node) { return node.key; });
}

Term values(ProcessContext &context, const List &tree) {
    return listed(context, tree, [](const NodeParts &node) { return node.value; });
}

// The {Key, Value} pairs of a list; function_clause for an element that is not a pair or an improper tail.
std::vector<NodeParts> pairs_of(const Term &list) {
    std::vector<NodeParts> entries;
    for (const auto &element : lists::proper(list)) {
        const auto parts = pair_of(element);
        if (!parts) {
            no_clause();
        }
        entries.push_back({parts->first, parts->second, {}, {}});
    }
    return entries;
}

// from_list(List): orddict:from_list/1 (the last of equal keys kept, ==), then balanced.
Term from_list(ProcessContext &context, const List &list) {
    auto entries = pairs_of(list.term);
    std::ranges::reverse(entries);
    std::ranges::stable_sort(entries, [](const NodeParts &a, const NodeParts &b) { return compare(a.key, b.key) < 0; });
    const auto [first, last] =
        std::ranges::unique(entries, [](const NodeParts &a, const NodeParts &b) { return compare(a.key, b.key) == 0; });
    entries.erase(first, last);
    return sized_tree(context, static_cast<std::int64_t>(entries.size()), trees(context).build(entries));
}

// from_orddict(List): badarg unless a proper list; {badarg, not_orddict} unless the keys strictly increase.
Term from_orddict(ProcessContext &context, const List &list) {
    auto rest = list.term;
    while (rest.is_cons()) {
        rest = rest.tail_unchecked();
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    const auto entries = pairs_of(list.term);
    for (std::size_t i = 1; i < entries.size(); ++i) {
        if (compare(entries[i - 1].key, entries[i].key) >= 0) {
            raise_error(tuple(context, std::array{atom(context, "badarg"), atom(context, "not_orddict")}));
        }
    }
    return sized_tree(context, static_cast<std::int64_t>(entries.size()), trees(context).build(entries));
}

// smallest(Tree), largest(Tree): {Key, Value}; function_clause when empty.
Term smallest(ProcessContext &context, const List &tree) {
    const auto engine = trees(context);
    return pair(context, engine.take_smallest(tree_parts(tree.term, false).root));
}

Term largest(ProcessContext &context, const List &tree) {
    const auto engine = trees(context);
    return pair(context, engine.take_largest(tree_parts(tree.term, false).root));
}

// take_smallest(Tree1), take_largest(Tree1): {Key, Value, Tree2}.
Term take_smallest(ProcessContext &context, const List &tree) {
    const auto parts = tree_parts(tree.term, true);
    const auto taken = trees(context).take_smallest(parts.root);
    return tuple(context, std::array{taken.key, taken.value, sized_tree(context, parts.size - 1, taken.left)});
}

Term take_largest(ProcessContext &context, const List &tree) {
    const auto parts = tree_parts(tree.term, true);
    const auto taken = trees(context).take_largest(parts.root);
    return tuple(context, std::array{taken.key, taken.value, sized_tree(context, parts.size - 1, taken.right)});
}

// The nearest entry before (smaller) or after (larger) Key: {Key2, Value} or none.
Term nearest(ProcessContext &context, const Key &key, const List &tree, bool before) {
    const auto best = trees(context).nearest(key, tree_parts(tree.term, false).root, before);
    return best ? pair(context, *best) : atom(context, "none");
}

Term smaller(ProcessContext &context, const Key &key, const List &tree) { return nearest(context, key, tree, true); }

Term larger(ProcessContext &context, const Key &key, const List &tree) { return nearest(context, key, tree, false); }

// Whether an order argument is reversed; function_clause unless ordered or reversed.
bool is_reversed(const Order &order) {
    const auto spelling = order.term.is_atom() ? order.term.atom_spelling_unchecked() : std::string_view{};
    if (spelling != "ordered" && spelling != "reversed") {
        no_clause();
    }
    return spelling == "reversed";
}

// {Order, Stack}: an iterator from its node stack, the top first.
Term iterator_of(ProcessContext &context, const Order &order, std::vector<Term> stack) {
    std::ranges::reverse(stack);
    return tuple(context, std::array{order.term, need(TermFactory(context).list(stack))});
}

// iterator(Tree, Order) and iterator(Tree).
Term iterator2(ProcessContext &context, const List &tree, const Order &order) {
    const auto reversed = is_reversed(order);
    std::vector<Term> stack;
    trees(context).spine(tree_parts(tree.term, false).root, reversed, stack);
    return iterator_of(context, order, stack);
}

Term iterator1(ProcessContext &context, const List &tree) {
    return iterator2(context, tree, {atom(context, "ordered")});
}

// iterator_from(Key, Tree, Order) and iterator_from(Key, Tree).
Term iterator_from3(ProcessContext &context, const Key &key, const List &tree, const Order &order) {
    const auto reversed = is_reversed(order);
    std::vector<Term> stack;
    trees(context).spine_from(key, tree_parts(tree.term, false).root, reversed, stack);
    return iterator_of(context, order, stack);
}

Term iterator_from2(ProcessContext &context, const Key &key, const List &tree) {
    return iterator_from3(context, key, tree, {atom(context, "ordered")});
}

// next(Iter1): {Key, Value, Iter2} or none.
Term next(ProcessContext &context, const List &iterator) {
    const auto parts = tree_parts(iterator.term, false);
    if (parts.root.is_nil()) {
        return atom(context, "none");
    }
    const Order order{parts.size_term};
    const auto reversed = is_reversed(order);
    if (!parts.root.is_cons()) {
        no_clause();
    }
    const auto engine = trees(context);
    const auto top = engine.parts(parts.root.head_unchecked());
    std::vector<Term> stack;
    for (const auto &node : lists::proper(parts.root.tail_unchecked()) | std::views::reverse) {
        stack.push_back(node);
    }
    engine.spine(reversed ? top.left : top.right, reversed, stack);
    return tuple(context, std::array{top.key, top.value, iterator_of(context, order, stack)});
}

constexpr std::array GB_TREES_BUILTINS{
    typed_entry<lookup>("gb_trees", "lookup"),
    typed_entry<is_defined>("gb_trees", "is_defined"),
    typed_entry<get>("gb_trees", "get"),
    typed_entry<insert>("gb_trees", "insert"),
    typed_entry<update>("gb_trees", "update"),
    typed_entry<enter>("gb_trees", "enter"),
    typed_entry<delete_key>("gb_trees", "delete"),
    typed_entry<delete_any>("gb_trees", "delete_any"),
    typed_entry<take>("gb_trees", "take"),
    typed_entry<take_any>("gb_trees", "take_any"),
    typed_entry<balance>("gb_trees", "balance"),
    typed_entry<to_list>("gb_trees", "to_list"),
    typed_entry<keys>("gb_trees", "keys"),
    typed_entry<values>("gb_trees", "values"),
    typed_entry<from_list>("gb_trees", "from_list"),
    typed_entry<from_orddict>("gb_trees", "from_orddict"),
    typed_entry<smallest>("gb_trees", "smallest"),
    typed_entry<largest>("gb_trees", "largest"),
    typed_entry<take_smallest>("gb_trees", "take_smallest"),
    typed_entry<take_largest>("gb_trees", "take_largest"),
    typed_entry<smaller>("gb_trees", "smaller"),
    typed_entry<larger>("gb_trees", "larger"),
    typed_entry<iterator1>("gb_trees", "iterator"),
    typed_entry<iterator2>("gb_trees", "iterator"),
    typed_entry<iterator_from2>("gb_trees", "iterator_from"),
    typed_entry<iterator_from3>("gb_trees", "iterator_from"),
    typed_entry<next>("gb_trees", "next"),
};
} // namespace
} // namespace clause::runtime::builtins::collections

namespace clause::runtime {
std::span<const BuiltinEntry> gb_trees_builtins() noexcept { return builtins::collections::GB_TREES_BUILTINS; }
} // namespace clause::runtime
