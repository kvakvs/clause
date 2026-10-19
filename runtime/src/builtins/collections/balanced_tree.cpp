#include "balanced_tree.hpp"
#include <algorithm>
#include <limits>

// General balanced trees of gb_trees and gb_sets (plan 21 step LIB3).
namespace clause::runtime::builtins::collections {
namespace {
// a * b, saturating: weights of trees beyond 2^32 entries never occur.
std::uint64_t product(std::uint64_t a, std::uint64_t b) {
    return a != 0 && b > std::numeric_limits<std::uint64_t>::max() / a ? std::numeric_limits<std::uint64_t>::max()
                                                                       : a * b;
}
} // namespace

bool BalancedTree::is_node(const Term &node) const {
    return node.is_tuple() && node.tuple_size_unchecked() == (values_ ? 4U : 3U);
}

NodeParts BalancedTree::parts(const Term &node) const {
    if (!is_node(node)) {
        no_clause();
    }
    const auto elements = need(node.tuple_elements());
    if (values_) {
        return {elements[0], elements[1], elements[2], elements[3]};
    }
    return {elements[0], nil(), elements[1], elements[2]};
}

Term BalancedTree::node(const NodeParts &parts) const {
    TermFactory factory(*context_);
    if (values_) {
        const std::array elements{parts.key, parts.value, parts.left, parts.right};
        return need(factory.tuple(elements));
    }
    const std::array elements{parts.key, parts.left, parts.right};
    return need(factory.tuple(elements));
}

Term BalancedTree::nil() const { return need(TermFactory(*context_).atom("nil"_atom)); }

namespace {
bool is_nil_atom(const Term &term) { return term.is_atom() && term.atom_spelling_unchecked() == "nil"; }
} // namespace

bool BalancedTree::is_end(const Term &node) { return is_nil_atom(node); }

std::optional<NodeParts> BalancedTree::nearest(const Key &key, const Term &node, bool before) const {
    std::optional<NodeParts> best;
    auto current = node;
    while (!is_nil_atom(current)) {
        const auto here = parts(current);
        const auto order = compare(key.term, here.key);
        const auto beyond = before ? order > 0 : order < 0;
        if (beyond) {
            best = here;
        }
        current = beyond == before ? here.right : here.left;
    }
    return best;
}

std::optional<NodeParts> BalancedTree::find(const Key &key, const Term &node) const {
    auto current = node;
    while (!is_nil_atom(current)) {
        auto here = parts(current);
        const auto order = compare(key.term, here.key);
        if (order == 0) {
            return here;
        }
        current = order < 0 ? here.left : here.right;
    }
    return std::nullopt;
}

Measure BalancedTree::count(const Term &node) const {
    if (is_nil_atom(node)) {
        return {1, 0};
    }
    const auto here = parts(node);
    if (is_nil_atom(here.left) && is_nil_atom(here.right)) {
        return {1, 1};
    }
    const auto left = count(here.left);
    const auto right = count(here.right);
    return {product(2, std::max(left.height, right.height)), left.size + right.size + 1};
}

Grown BalancedTree::insert(const NodeParts &entry, const Term &node, std::uint64_t budget) const {
    if (is_nil_atom(node)) {
        const auto leaf = this->node({entry.key, entry.value, nil(), nil()});
        return {leaf, budget == 0 ? std::optional{Measure{1, 1}} : std::nullopt};
    }
    if (!is_node(node)) {
        raise_error(need(
            TermFactory(*context_).tuple(std::array{need(TermFactory(*context_).atom("key_exists"_atom)), entry.key})));
    }
    auto here = parts(node);
    const auto order = compare(entry.key, here.key);
    if (order == 0) {
        raise_error(need(
            TermFactory(*context_).tuple(std::array{need(TermFactory(*context_).atom("key_exists"_atom)), entry.key})));
    }
    const auto grown = insert(entry, order < 0 ? here.left : here.right, budget >> 1U);
    (order < 0 ? here.left : here.right) = grown.tree;
    const auto rebuilt = this->node(here);
    if (!grown.measure) {
        return {rebuilt, std::nullopt};
    }
    const auto other = count(order < 0 ? here.right : here.left);
    const auto height = product(2, std::max(grown.measure->height, other.height));
    const auto size = grown.measure->size + other.size + 1;
    if (height > product(size, size)) {
        return {build(in_order(rebuilt)), std::nullopt};
    }
    return {rebuilt, Measure{height, size}};
}

Term BalancedTree::remove(const Key &key, const Term &node) const {
    auto here = parts(node);
    const auto order = compare(key.term, here.key);
    if (order == 0) {
        return merge(here.left, here.right);
    }
    (order < 0 ? here.left : here.right) = remove(key, order < 0 ? here.left : here.right);
    return this->node(here);
}

Term BalancedTree::replace(const NodeParts &entry, const Term &node) const {
    auto here = parts(node);
    const auto order = compare(entry.key, here.key);
    if (order == 0) {
        return this->node({entry.key, entry.value, here.left, here.right});
    }
    (order < 0 ? here.left : here.right) = replace(entry, order < 0 ? here.left : here.right);
    return this->node(here);
}

NodeParts BalancedTree::take_smallest(const Term &node) const {
    auto here = parts(node);
    if (is_nil_atom(here.left)) {
        return {here.key, here.value, here.right, nil()};
    }
    auto inner = take_smallest(here.left);
    here.left = inner.left;
    inner.left = this->node(here);
    return inner;
}

NodeParts BalancedTree::take_largest(const Term &node) const {
    auto here = parts(node);
    if (is_nil_atom(here.right)) {
        return {here.key, here.value, nil(), here.left};
    }
    auto inner = take_largest(here.right);
    here.right = inner.right;
    inner.right = this->node(here);
    return inner;
}

Term BalancedTree::merge(const Term &smaller, const Term &bigger) const {
    if (is_nil_atom(bigger)) {
        return smaller;
    }
    if (is_nil_atom(smaller)) {
        return bigger;
    }
    const auto taken = take_smallest(bigger);
    return node({taken.key, taken.value, smaller, taken.left});
}

std::vector<NodeParts> BalancedTree::in_order(const Term &node) const {
    std::vector<NodeParts> result;
    std::vector<Term> pending;
    auto current = node;
    while (!pending.empty() || !is_nil_atom(current)) {
        while (!is_nil_atom(current)) {
            pending.push_back(current);
            current = parts(current).left;
        }
        const auto here = parts(pending.back());
        pending.pop_back();
        result.push_back(here);
        current = here.right;
    }
    return result;
}

Term BalancedTree::build(std::span<const NodeParts> entries, bool right_heavy) const {
    if (entries.empty()) {
        return nil();
    }
    const auto rest = entries.size() - 1;
    const auto bigger = rest - rest / 2;
    const auto left = right_heavy ? rest / 2 : bigger;
    const auto &middle = entries[left];
    return node({middle.key, middle.value, build(entries.first(left), right_heavy),
                 build(entries.subspan(left + 1), right_heavy)});
}

void BalancedTree::spine(const Term &node, bool reversed, std::vector<Term> &stack) const {
    auto current = node;
    while (!is_nil_atom(current)) {
        stack.push_back(current);
        const auto here = parts(current);
        current = reversed ? here.right : here.left;
    }
}

void BalancedTree::spine_from(const Key &from, const Term &node, bool reversed, std::vector<Term> &stack) const {
    auto current = node;
    while (!is_nil_atom(current)) {
        const auto here = parts(current);
        const auto order = compare(here.key, from.term);
        if (reversed ? order > 0 : order < 0) {
            current = reversed ? here.left : here.right;
            continue;
        }
        stack.push_back(current);
        current = reversed ? here.right : here.left;
    }
}

TreeParts tree_parts(const Term &tree, bool sized) {
    if (!tree.is_tuple() || tree.tuple_size_unchecked() != 2) {
        no_clause();
    }
    const auto size_term = tree.tuple_element_unchecked(0);
    const auto size = small(size_term.word());
    if (sized && (!size || *size < 0)) {
        no_clause();
    }
    return {size_term, size.value_or(0), tree.tuple_element_unchecked(1)};
}

Term sized_tree(ProcessContext &context, std::int64_t size, const Term &root) {
    TermFactory factory(context);
    const std::array parts{need(factory.integer(size)), root};
    return need(factory.tuple(parts));
}

void raise_error(const Term &reason) {
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error, .payload = reason.word()};
}
} // namespace clause::runtime::builtins::collections
