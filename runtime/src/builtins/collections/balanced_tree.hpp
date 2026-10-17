#pragma once
#include "collections.hpp"
#include <cstdint>
#include <optional>
#include <vector>

// General balanced trees (plan 21 step LIB3, docs/builtins.md#collections): gb_trees' {Size, Tree} with nodes
// {Key, Value, Smaller, Bigger} and gb_sets' with nodes {Key, Smaller, Bigger}, nil for an empty tree. Insertion
// rebalances by Andersson's weight rule and rebalancing rebuilds from the sorted entries, as OTP does, so the trees
// equal OTP's. Keys compare by Erlang's < and ==.
namespace clause::runtime::builtins::collections {
// One node's parts; `value` is unused by sets.
struct NodeParts {
    Term key;
    Term value;
    Term left;
    Term right;
};

// The height and size measured on the way back up from an insertion that went too deep.
struct Measure {
    std::uint64_t height;
    std::uint64_t size;
};

// A subtree after an insertion: the tree, and its measure while the path is still checked.
struct Grown {
    Term tree;
    std::optional<Measure> measure;
};

class BalancedTree final {
  public:
    // Trees of gb_trees (`values`) or gb_sets.
    BalancedTree(ProcessContext &context, bool values) : context_(&context), values_(values) {}

    // Node access: function_clause unless `node` is a node (a tuple of the node arity).
    bool is_node(const Term &node) const;
    NodeParts parts(const Term &node) const;
    Term node(const NodeParts &parts) const;
    Term nil() const;
    // Whether `node` is nil, the end of a branch.
    static bool is_end(const Term &node);

    // The nearest node with a key before (`before`) or after `key`, if any (smaller/2 and larger/2).
    std::optional<NodeParts> nearest(const Key &key, const Term &node, bool before) const;

    // The node of `key` below `node`, if any; function_clause at a term that is neither a node nor nil.
    std::optional<NodeParts> find(const Key &key, const Term &node) const;

    // OTP's insert_1: `key` (with `value`) put below `node` with the weight budget `budget`; {key_exists, Key} when
    // present.
    Grown insert(const NodeParts &entry, const Term &node, std::uint64_t budget) const;

    // The tree without `key` (function_clause when absent), and with Key's node replaced by `entry`.
    Term remove(const Key &key, const Term &node) const;
    Term replace(const NodeParts &entry, const Term &node) const;

    // The smallest node taken out: its parts with `left` holding the rest of the tree (OTP's take_smallest1), and
    // the largest one with `right` holding the rest.
    NodeParts take_smallest(const Term &node) const;
    NodeParts take_largest(const Term &node) const;

    // Two subtrees joined after their root went: the bigger side's smallest node on top.
    Term merge(const Term &smaller, const Term &bigger) const;

    // The nodes in key order.
    std::vector<NodeParts> in_order(const Term &node) const;

    // A balanced tree of sorted entries: the left side gets the bigger half (OTP's balance_list), or the right
    // side (balance_revlist, for gb_sets' merges).
    Term build(std::span<const NodeParts> entries, bool right_heavy = false) const;

    // OTP's count/1: the height as a power of two and the size of a subtree.
    Measure count(const Term &node) const;

    // An iterator's node stack along one spine from `node` onto `stack` (iterator_1 and iterator_r), and the one
    // starting at the first key not before (ordered) or not after (reversed) `from`.
    void spine(const Term &node, bool reversed, std::vector<Term> &stack) const;
    void spine_from(const Key &from, const Term &node, bool reversed, std::vector<Term> &stack) const;

  private:
    ProcessContext *context_;
    bool values_;
};

// {Size, Tree}: its parts; function_clause unless a 2-tuple, or unless its size is a non-negative integer when
// `sized`.
struct TreeParts {
    Term size_term;
    std::int64_t size;
    Term root;
};

TreeParts tree_parts(const Term &tree, bool sized);

// The {Size, Tree} term.
Term sized_tree(ProcessContext &context, std::int64_t size, const Term &root);

// Raise error(Reason).
[[noreturn]] void raise_error(const Term &reason);
} // namespace clause::runtime::builtins::collections
