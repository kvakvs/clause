#include "balanced_tree.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>

// The gb_sets builtins (plan 21 step LIB3, docs/builtins.md#collections): {Size, Tree} with nodes {Key, Smaller,
// Bigger} over the balanced tree engine (balanced_tree.hpp). union, intersection and difference choose between a
// merge of the sorted lists and insertions or lookups as OTP does, keep the representative OTP keeps of equal
// elements, and build merged trees as OTP's balance_revlist does.
namespace clause::runtime::builtins::collections {
namespace {
struct OrderRole;
struct OtherRole;
// An iterator order (ordered or reversed), and the second set of a binary operation.
using Order = Named<OrderRole>;
using Other = Named<OtherRole>;

BalancedTree sets(ProcessContext &context) { return BalancedTree(context, false); }

Term tuple(ProcessContext &context, std::span<const Term> parts) { return need(TermFactory(context).tuple(parts)); }

Term boolean(ProcessContext &context, bool value) { return need(TermFactory(context).boolean(value)); }

// `size - 1` of a size field OTP decrements without a check: badarith for a non-number.
Term one_less(ProcessContext &context, const Term &size) {
    return add(context, size, need(TermFactory(context).integer(-1)));
}

std::vector<Term> elements(ProcessContext &context, const Term &root) {
    std::vector<Term> keys;
    for (const auto &node : sets(context).in_order(root)) {
        keys.push_back(node.key);
    }
    return keys;
}

std::vector<NodeParts> as_entries(std::span<const Term> keys) {
    std::vector<NodeParts> entries;
    entries.reserve(keys.size());
    for (const auto &key : keys) {
        entries.push_back({key, {}, {}, {}});
    }
    return entries;
}

// {Size, Tree} of sorted keys built as balance_revlist does (the right side gets the bigger half).
Term merged(ProcessContext &context, std::span<const Term> keys) {
    return sized_tree(context, static_cast<std::int64_t>(keys.size()), sets(context).build(as_entries(keys), true));
}

bool member(ProcessContext &context, const Term &element, const Term &root) {
    return sets(context).find({element}, root).has_value();
}

// is_member(Element, Set) and is_element/2.
Term is_member(ProcessContext &context, const Key &element, const List &set) {
    return boolean(context, member(context, element.term, tree_parts(set.term, false).root));
}

// insert(Element, Set1): {key_exists, Element} when present.
Term insert(ProcessContext &context, const Key &element, const List &set) {
    const auto parts = tree_parts(set.term, true);
    const auto size = parts.size + 1;
    const auto budget = static_cast<std::uint64_t>(size) * static_cast<std::uint64_t>(size);
    const auto grown = sets(context).insert({element.term, {}, {}, {}}, parts.root, budget);
    if (!grown.measure) {
        return sized_tree(context, size, grown.tree);
    }
    TermFactory factory(context);
    const std::array measured{grown.tree, need(factory.integer(static_cast<std::int64_t>(grown.measure->height))),
                              need(factory.integer(static_cast<std::int64_t>(grown.measure->size)))};
    return sized_tree(context, size, tuple(context, measured));
}

// add(Element, Set1) and add_element/2: Set1 itself when present.
Term add_element(ProcessContext &context, const Key &element, const List &set) {
    return member(context, element.term, tree_parts(set.term, false).root) ? set.term : insert(context, element, set);
}

// delete(Element, Set1): function_clause when absent.
Term delete_element(ProcessContext &context, const Key &element, const List &set) {
    const auto parts = tree_parts(set.term, false);
    const auto root = sets(context).remove(element, parts.root);
    return tuple(context, std::array{one_less(context, parts.size_term), root});
}

// delete_any(Element, Set1) and del_element/2: Set1 itself when absent.
Term delete_any(ProcessContext &context, const Key &element, const List &set) {
    return member(context, element.term, tree_parts(set.term, false).root) ? delete_element(context, element, set)
                                                                           : set.term;
}

// balance(Set1).
Term balance(ProcessContext &context, const List &set) {
    const auto parts = tree_parts(set.term, true);
    const auto engine = sets(context);
    return sized_tree(context, parts.size, engine.build(engine.in_order(parts.root)));
}

// to_list(Set).
Term to_list(ProcessContext &context, const List &set) {
    return need(TermFactory(context).list(elements(context, tree_parts(set.term, false).root)));
}

// from_list(List): ordsets:from_list/1 (the first of equal elements kept, ==), then balanced.
Term from_list(ProcessContext &context, const List &list) {
    auto keys = lists::proper(list.term);
    std::ranges::stable_sort(keys, [](const Term &a, const Term &b) { return compare(a, b) < 0; });
    const auto [first, last] =
        std::ranges::unique(keys, [](const Term &a, const Term &b) { return compare(a, b) == 0; });
    keys.erase(first, last);
    return sized_tree(context, static_cast<std::int64_t>(keys.size()), sets(context).build(as_entries(keys)));
}

// from_ordset(List): badarg unless a proper list; {badarg, not_ordset} unless the elements strictly increase.
Term from_ordset(ProcessContext &context, const List &list) {
    auto rest = list.term;
    std::vector<Term> keys;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        keys.push_back(rest.head_unchecked());
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    for (std::size_t i = 1; i < keys.size(); ++i) {
        if (compare(keys[i - 1], keys[i]) >= 0) {
            raise_error(tuple(context, std::array{atom(context, "badarg"_atom), atom(context, "not_ordset"_atom)}));
        }
    }
    return sized_tree(context, static_cast<std::int64_t>(keys.size()), sets(context).build(as_entries(keys)));
}

// smallest(Set), largest(Set); function_clause when empty.
Term smallest(ProcessContext &context, const List &set) {
    return sets(context).take_smallest(tree_parts(set.term, false).root).key;
}

Term largest(ProcessContext &context, const List &set) {
    return sets(context).take_largest(tree_parts(set.term, false).root).key;
}

// take_smallest(Set1), take_largest(Set1): {Element, Set2}.
Term take_smallest(ProcessContext &context, const List &set) {
    const auto parts = tree_parts(set.term, false);
    const auto taken = sets(context).take_smallest(parts.root);
    return tuple(context,
                 std::array{taken.key, tuple(context, std::array{one_less(context, parts.size_term), taken.left})});
}

Term take_largest(ProcessContext &context, const List &set) {
    const auto parts = tree_parts(set.term, false);
    const auto taken = sets(context).take_largest(parts.root);
    return tuple(context,
                 std::array{taken.key, tuple(context, std::array{one_less(context, parts.size_term), taken.right})});
}

// The nearest element before (smaller) or after (larger) Element: {found, Element2} or none.
Term nearest(ProcessContext &context, const Key &element, const List &set, bool before) {
    const auto best = sets(context).nearest(element, tree_parts(set.term, false).root, before);
    return best ? tuple(context, std::array{atom(context, "found"_atom), best->key}) : atom(context, "none"_atom);
}

Term smaller(ProcessContext &context, const Key &element, const List &set) {
    return nearest(context, element, set, true);
}

Term larger(ProcessContext &context, const Key &element, const List &set) {
    return nearest(context, element, set, false);
}

// Whether a binary operation of a set of `left` elements with one of `right` merges the sorted lists (else it
// inserts or looks up the first set's elements in the second): OTP's N2 < 10 or N2 < N1 * round(1.46 ln N2).
bool merges(std::int64_t left, std::int64_t right) {
    return right < 10 ||
           right < left * static_cast<std::int64_t>(std::round(1.46 * std::log(static_cast<double>(right))));
}

// Two sorted lists walked as OTP's union_2 and intersection_2 do: after taking from the second list the lists
// swap, so of equal elements the current first list's is kept.
struct Walk {
    std::span<const Term> first;
    std::span<const Term> second;

    bool both() const { return !first.empty() && !second.empty(); }

    int order() const { return compare(first.front(), second.front()); }

    void swap_after_second() {
        auto rest = second.subspan(1);
        second = first;
        first = rest;
    }
};

// OTP's union_2: two sorted lists merged, of equal elements the current first list's kept.
std::vector<Term> united_lists(std::span<const Term> first, std::span<const Term> second) {
    std::vector<Term> united;
    Walk walk{first, second};
    while (walk.both()) {
        const auto order = walk.order();
        united.push_back(order > 0 ? walk.second.front() : walk.first.front());
        if (order > 0) {
            walk.swap_after_second();
        } else {
            walk.first = walk.first.subspan(1);
            walk.second = order == 0 ? walk.second.subspan(1) : walk.second;
        }
    }
    united.insert(united.end(), walk.first.begin(), walk.first.end());
    united.insert(united.end(), walk.second.begin(), walk.second.end());
    return united;
}

// The sizes and trees of two sets of a binary operation; function_clause unless both sizes are integers (and
// non-negative when `natural`).
std::pair<TreeParts, TreeParts> both_parts(const List &set1, const Other &set2, bool natural) {
    auto left = tree_parts(set1.term, natural);
    auto right = tree_parts(set2.term, natural);
    if (!small(left.size_term.word()) || !small(right.size_term.word())) {
        no_clause();
    }
    return {left, right};
}

// union(Set1, Set2).
Term union2(ProcessContext &context, const List &set1, const Other &set2) {
    auto [left, right] = both_parts(set1, set2, false);
    if (right.size < left.size) {
        std::swap(left, right);
    }
    const auto list = elements(context, left.root);
    if (!merges(left.size, right.size)) {
        auto result = sized_tree(context, right.size, right.root);
        for (const auto &element : list) {
            result = add_element(context, {element}, {result});
        }
        return result;
    }
    return merged(context, united_lists(list, elements(context, right.root)));
}

// intersection(Set1, Set2).
Term intersection2(ProcessContext &context, const List &set1, const Other &set2) {
    auto [left, right] = both_parts(set1, set2, false);
    if (right.size < left.size) {
        std::swap(left, right);
    }
    const auto list = elements(context, left.root);
    std::vector<Term> common;
    if (!merges(left.size, right.size)) {
        std::ranges::copy_if(list, std::back_inserter(common),
                             [&](const Term &element) { return member(context, element, right.root); });
        return merged(context, common);
    }
    const auto others = elements(context, right.root);
    Walk walk{list, others};
    while (walk.both()) {
        const auto order = walk.order();
        if (order > 0) {
            walk.swap_after_second();
            continue;
        }
        if (order == 0) {
            common.push_back(walk.first.front());
            walk.second = walk.second.subspan(1);
        }
        walk.first = walk.first.subspan(1);
    }
    return merged(context, common);
}

// difference(Set1, Set2) and subtract/2.
Term difference(ProcessContext &context, const List &set1, const Other &set2) {
    const auto [left, right] = both_parts(set1, set2, true);
    const auto list = elements(context, left.root);
    std::vector<Term> kept;
    if (!merges(left.size, right.size)) {
        std::ranges::copy_if(list, std::back_inserter(kept),
                             [&](const Term &element) { return !member(context, element, right.root); });
        return merged(context, kept);
    }
    const auto others = elements(context, right.root);
    std::size_t at = 0;
    for (const auto &element : list) {
        while (at < others.size() && compare(element, others[at]) > 0) {
            ++at;
        }
        if (at < others.size() && compare(element, others[at]) == 0) {
            ++at;
        } else {
            kept.push_back(element);
        }
    }
    return merged(context, kept);
}

// is_subset(Set1, Set2).
Term is_subset(ProcessContext &context, const List &set1, const Other &set2) {
    const auto [left, right] = both_parts(set1, set2, true);
    const auto list = elements(context, left.root);
    return boolean(context, std::ranges::all_of(list, [&](const Term &e) { return member(context, e, right.root); }));
}

// is_disjoint(Set1, Set2).
Term is_disjoint(ProcessContext &context, const List &set1, const Other &set2) {
    const auto left = tree_parts(set1.term, false);
    const auto right = tree_parts(set2.term, false);
    const auto list = elements(context, left.root);
    return boolean(context, std::ranges::none_of(list, [&](const Term &e) { return member(context, e, right.root); }));
}

// is_equal(Set1, Set2): equal sizes (=:=) and pairwise equal elements (==).
Term is_equal(ProcessContext &context, const List &set1, const Other &set2) {
    const auto left = tree_parts(set1.term, false);
    const auto right = tree_parts(set2.term, false);
    if (!same(left.size_term, right.size_term)) {
        return boolean(context, false);
    }
    const auto a = elements(context, left.root);
    const auto b = elements(context, right.root);
    return boolean(context, std::ranges::equal(a, b, [](const Term &x, const Term &y) { return compare(x, y) == 0; }));
}

// The sets of union/1 and intersection/1.
std::vector<Term> set_list(const List &list) { return lists::proper(list.term); }

// union(SetList): [] is the empty set.
Term union1(ProcessContext &context, const List &list) {
    const auto all = set_list(list);
    if (all.empty()) {
        return sized_tree(context, 0, sets(context).nil());
    }
    auto result = all.front();
    for (std::size_t i = 1; i < all.size(); ++i) {
        result = union2(context, {result}, {all[i]});
    }
    return result;
}

// intersection(SetList): a non-empty list.
Term intersection1(ProcessContext &context, const List &list) {
    const auto all = set_list(list);
    if (all.empty()) {
        no_clause();
    }
    auto result = all.front();
    for (std::size_t i = 1; i < all.size(); ++i) {
        result = intersection2(context, {result}, {all[i]});
    }
    return result;
}

// Whether an order argument is reversed; function_clause unless ordered or reversed.
bool is_reversed(const Order &order) {
    const auto spelling = order.term.is_atom() ? order.term.atom_spelling_unchecked() : std::string_view{};
    if (spelling != "ordered" && spelling != "reversed") {
        no_clause();
    }
    return spelling == "reversed";
}

// {Order, Stack}: an iterator from its node stack, the top last.
Term iterator_of(ProcessContext &context, const Order &order, std::vector<Term> stack) {
    std::ranges::reverse(stack);
    return tuple(context, std::array{order.term, need(TermFactory(context).list(stack))});
}

Term iterator2(ProcessContext &context, const List &set, const Order &order) {
    const auto reversed = is_reversed(order);
    std::vector<Term> stack;
    sets(context).spine(tree_parts(set.term, false).root, reversed, stack);
    return iterator_of(context, order, stack);
}

Term iterator1(ProcessContext &context, const List &set) {
    return iterator2(context, set, {atom(context, "ordered"_atom)});
}

Term iterator_from3(ProcessContext &context, const Key &element, const List &set, const Order &order) {
    const auto reversed = is_reversed(order);
    std::vector<Term> stack;
    sets(context).spine_from(element, tree_parts(set.term, false).root, reversed, stack);
    return iterator_of(context, order, stack);
}

Term iterator_from2(ProcessContext &context, const Key &element, const List &set) {
    return iterator_from3(context, element, set, {atom(context, "ordered"_atom)});
}

// next(Iter1): {Element, Iter2} or none.
Term next(ProcessContext &context, const List &iterator) {
    const auto parts = tree_parts(iterator.term, false);
    if (parts.root.is_nil()) {
        return atom(context, "none"_atom);
    }
    const Order order{parts.size_term};
    const auto reversed = is_reversed(order);
    if (!parts.root.is_cons()) {
        no_clause();
    }
    const auto engine = sets(context);
    const auto top = engine.parts(parts.root.head_unchecked());
    std::vector<Term> stack;
    for (const auto &node : lists::proper(parts.root.tail_unchecked()) | std::views::reverse) {
        stack.push_back(node);
    }
    engine.spine(reversed ? top.left : top.right, reversed, stack);
    return tuple(context, std::array{top.key, iterator_of(context, order, stack)});
}

constexpr std::array GB_SETS_BUILTINS{
    typed_entry<is_member>("gb_sets", "is_member"),
    typed_entry<is_member>("gb_sets", "is_element"),
    typed_entry<insert>("gb_sets", "insert"),
    typed_entry<add_element>("gb_sets", "add"),
    typed_entry<add_element>("gb_sets", "add_element"),
    typed_entry<delete_element>("gb_sets", "delete"),
    typed_entry<delete_any>("gb_sets", "delete_any"),
    typed_entry<delete_any>("gb_sets", "del_element"),
    typed_entry<balance>("gb_sets", "balance"),
    typed_entry<to_list>("gb_sets", "to_list"),
    typed_entry<from_list>("gb_sets", "from_list"),
    typed_entry<from_ordset>("gb_sets", "from_ordset"),
    typed_entry<smallest>("gb_sets", "smallest"),
    typed_entry<largest>("gb_sets", "largest"),
    typed_entry<take_smallest>("gb_sets", "take_smallest"),
    typed_entry<take_largest>("gb_sets", "take_largest"),
    typed_entry<smaller>("gb_sets", "smaller"),
    typed_entry<larger>("gb_sets", "larger"),
    typed_entry<union2>("gb_sets", "union"),
    typed_entry<union1>("gb_sets", "union"),
    typed_entry<intersection2>("gb_sets", "intersection"),
    typed_entry<intersection1>("gb_sets", "intersection"),
    typed_entry<difference>("gb_sets", "difference"),
    typed_entry<difference>("gb_sets", "subtract"),
    typed_entry<is_subset>("gb_sets", "is_subset"),
    typed_entry<is_disjoint>("gb_sets", "is_disjoint"),
    typed_entry<is_equal>("gb_sets", "is_equal"),
    typed_entry<iterator1>("gb_sets", "iterator"),
    typed_entry<iterator2>("gb_sets", "iterator"),
    typed_entry<iterator_from2>("gb_sets", "iterator_from"),
    typed_entry<iterator_from3>("gb_sets", "iterator_from"),
    typed_entry<next>("gb_sets", "next"),
};
} // namespace
} // namespace clause::runtime::builtins::collections

namespace clause::runtime {
std::span<const BuiltinEntry> gb_sets_builtins() noexcept { return builtins::collections::GB_SETS_BUILTINS; }
} // namespace clause::runtime
