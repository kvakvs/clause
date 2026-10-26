#include "../string/string.hpp"
#include "public_key.hpp"
#include <algorithm>
#include <functional>
#include <ranges>

// Added for public_key's policy tree (plan 21 step LIB80E4): the walks of OTP's pubkey_policy_tree over the valid
// policy tree of a path validation: {} or {Node, Children}, children being subtrees or leaves and nodes the maps
// #{valid_policy, qualifier_set, expected_policy_set}. Lists keep OTP's orders (folds prepend); the verify fun calls,
// counters and the node limit stay in clause_public_key_path.
namespace clause::runtime::builtins::public_key {
using asn1::erlang_error;
using Reason = abi::v1::ErrorReason;
using string::same;

namespace {
using Terms = std::vector<Term>;
// The siblings a parent gets, none for no_sibling.
using SiblingFun = std::function<std::optional<Terms>(const Term &parent)>;
// The children a leaf gets.
using ChildrenFun = std::function<Terms(const Term &leaf)>;
// One leaf replaced.
using LeafFun = std::function<Term(const Term &leaf)>;
// A tree and its node count.
using Counted = std::pair<Term, std::int64_t>;
// A policy argument, apart from the trees and nodes it is looked up in.
using Policy = Named<KeyRole>;

// The fields of a new node: #{valid_policy, qualifier_set, expected_policy_set}.
struct NodeFields {
    Term valid_;
    Term qualifiers_;
    Term expected_;
};

// A policy mapping: the issuer domain policy and the subject domain policy it maps to.
struct Mapping {
    Term issuer_;
    Term subject_;
};

[[noreturn]] void no_clause() { erlang_error(Reason::function_clause); }

bool atom_is(const Term &term, const std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

Term element(const Term &tuple, const std::size_t index) { return tuple.tuple_element_unchecked(index); }

bool is_pair(const Term &term) { return term.is_tuple() && term.tuple_size_unchecked() == 2; }

bool is_empty_tree(const Term &tree) { return tree.is_tuple() && tree.tuple_size_unchecked() == 0; }

bool is_any(const Term &policy) { return same_oid(policy, {2, 5, 29, 32, 0}); }

// [{_, _} | _]: children that are subtrees.
bool has_subtrees(const Term &children) { return children.is_cons() && is_pair(children.head_unchecked()); }

// The elements of a proper list; function_clause otherwise, as the lists functions over it raise.
Terms items(const Term &list) {
    Terms found;
    auto rest = list;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        found.push_back(rest.head_unchecked());
    }
    if (!rest.is_nil()) {
        no_clause();
    }
    return found;
}

bool member(const Term &value, const Term &list) {
    return std::ranges::any_of(items(list), [&value](const Term &item) { return same(item, value); });
}

// A map's value of an atom key, none for another term or key.
std::optional<Term> field_of(const Term &node, const std::string_view key) {
    if (!node.is_map()) {
        return std::nullopt;
    }
    for (const auto &[name, value] : need(node.map_entries())) {
        if (atom_is(name, key)) {
            return value;
        }
    }
    return std::nullopt;
}

// #{Key := Value} matched; function_clause when the node does not have it.
Term field(const Term &node, const std::string_view key) {
    const auto value = field_of(node, key);
    if (!value) {
        no_clause();
    }
    return *value;
}

// #{valid_policy := ?anyPolicy}.
bool any_node(const Term &node) {
    const auto policy = field_of(node, "valid_policy");
    return policy && is_any(*policy);
}

class PolicyTree {
  public:
    explicit PolicyTree(ProcessContext &context) : context_(context), builder_(context) {}

    asn1::Builder &builder() { return builder_; }

    Term list(const Terms &values) { return builder_.list(values); }

    Term counted(const Counted &tree) { return builder_.tuple({tree.first, builder_.integer(tree.second)}); }

    // policy_node/3: asn1_NOVALUE qualifiers become [].
    Term node(const NodeFields &fields) {
        const auto set = atom_is(fields.qualifiers_, "asn1_NOVALUE") ? builder_.list({}) : fields.qualifiers_;
        const std::array<std::pair<Term, Term>, 3> entries{{
            {builder_.atom("valid_policy"), fields.valid_},
            {builder_.atom("qualifier_set"), set},
            {builder_.atom("expected_policy_set"), fields.expected_},
        }};
        return need(TermFactory(context_).map(entries));
    }

    // Node#{Key => Value}.
    Term with_field(const Term &node, const std::string_view key, const Term &value) {
        auto entries = need(node.map_entries());
        const auto found =
            std::ranges::find_if(entries, [key](const auto &entry) { return atom_is(entry.first, key); });
        if (found == entries.end()) {
            entries.emplace_back(builder_.atom(key), value);
        } else {
            found->second = value;
        }
        return need(TermFactory(context_).map(entries));
    }

    // policy_node(Policy, Qualifiers, [Policy]).
    Term single(const Term &policy, const Term &qualifiers) {
        return node({policy, qualifiers, builder_.list(std::span(&policy, 1))});
    }

    Terms all_leaves(const Term &tree);
    bool in_set(const Term &policy, const Terms &nodes);
    Counted add_leaves(const Term &tree, std::int64_t count, const ChildrenFun &children);
    Counted add_leaf_siblings(const Term &tree, std::int64_t count, const SiblingFun &siblings);
    Term map_leaves(const Term &tree, const LeafFun &each);
    Term prune_leaves(const Term &tree, const Policy &policy);
    Term prune(const Term &tree);
    Terms valid_policy_node_set(const Term &tree);
    Term prune_invalid_nodes(const Term &tree, const Terms &invalid);
    Terms any_leaves(const Term &tree);
    Terms constrain(const Term &tree);
    void collect(const Term &nodes, Terms &out);
    Terms collect_qualifiers(const Term &tree, const Policy &policy);

  private:
    std::pair<Term, std::int64_t> map_leaves_count(const Term &tree, const ChildrenFun &children);
    std::optional<Term> pruned(const Term &child);
    std::optional<Term> valid_child(const Term &child, const Terms &invalid);
    Terms valid_children(const Term &children, const Terms &invalid);
    Terms collect_children(const Term &children, const Policy &policy);
    void qualifiers(const Term &node, Terms &out);

    ProcessContext &context_;
    asn1::Builder builder_;
};

// all_leaves/1.
Terms PolicyTree::all_leaves(const Term &tree) {
    if (is_empty_tree(tree)) {
        return {};
    }
    if (!is_pair(tree)) {
        no_clause();
    }
    if (!has_subtrees(element(tree, 1))) {
        return items(element(tree, 1));
    }
    Terms leaves;
    for (const auto &child : items(element(tree, 1))) {
        const auto found = all_leaves(child);
        leaves.insert(leaves.end(), found.begin(), found.end());
    }
    return leaves;
}

bool PolicyTree::in_set(const Term &policy, const Terms &nodes) {
    return std::ranges::any_of(nodes,
                               [&policy](const Term &node) { return same(field(node, "valid_policy"), policy); });
}

std::pair<Term, std::int64_t> PolicyTree::map_leaves_count(const Term &tree, const ChildrenFun &children) {
    const auto parent = element(tree, 0);
    Terms mapped;
    std::int64_t added = 0;
    const auto subtrees = has_subtrees(element(tree, 1));
    for (const auto &child : items(element(tree, 1))) {
        if (subtrees) {
            const auto [node, count] = map_leaves_count(child, children);
            mapped.push_back(node);
            added += count;
            continue;
        }
        const auto leaves = children(child);
        mapped.push_back(builder_.tuple({child, list(leaves)}));
        added += static_cast<std::int64_t>(leaves.size());
    }
    return {builder_.tuple({parent, list(mapped)}), added};
}

// add_leaves/3: each leaf given the children the fun makes; a root without children gets them itself.
Counted PolicyTree::add_leaves(const Term &tree, const std::int64_t count, const ChildrenFun &children) {
    if (!is_pair(tree)) {
        no_clause();
    }
    if (element(tree, 1).is_nil()) {
        const auto leaves = children(element(tree, 0));
        return {builder_.tuple({element(tree, 0), list(leaves)}), count + static_cast<std::int64_t>(leaves.size())};
    }
    const auto [mapped, added] = map_leaves_count(tree, children);
    return {mapped, count + added};
}

// add_leaf_siblings/3: the deepest parents' leaves joined by the siblings the fun makes for the parent.
Counted PolicyTree::add_leaf_siblings(const Term &tree, const std::int64_t count, const SiblingFun &siblings) {
    if (!is_pair(tree)) {
        no_clause();
    }
    const auto parent = element(tree, 0);
    if (has_subtrees(element(tree, 1))) {
        Terms children;
        std::int64_t added = 0;
        for (const auto &child : items(element(tree, 1))) {
            const auto [node, node_count] = add_leaf_siblings(child, 0, siblings);
            children.push_back(node);
            added += node_count;
        }
        return {builder_.tuple({parent, list(children)}), count + added};
    }
    const auto more = siblings(parent);
    if (!more) {
        return {tree, count};
    }
    auto leaves = items(element(tree, 1));
    leaves.insert(leaves.end(), more->begin(), more->end());
    return {builder_.tuple({parent, list(leaves)}), count + static_cast<std::int64_t>(more->size())};
}

// map_leaves/2.
Term PolicyTree::map_leaves(const Term &tree, const LeafFun &each) {
    if (!is_pair(tree)) {
        no_clause();
    }
    const auto subtrees = has_subtrees(element(tree, 1));
    Terms mapped;
    for (const auto &child : items(element(tree, 1))) {
        mapped.push_back(subtrees ? map_leaves(child, each) : each(child));
    }
    return builder_.tuple({element(tree, 0), list(mapped)});
}

// prune_leaves/2: the leaves of a policy removed.
Term PolicyTree::prune_leaves(const Term &tree, const Policy &policy) {
    if (is_empty_tree(tree)) {
        return tree;
    }
    if (!is_pair(tree)) {
        no_clause();
    }
    const auto subtrees = has_subtrees(element(tree, 1));
    Terms kept;
    for (const auto &child : items(element(tree, 1))) {
        if (subtrees) {
            kept.push_back(prune_leaves(child, policy));
        } else if (!same(field(child, "valid_policy"), policy.term)) {
            kept.push_back(child);
        }
    }
    return builder_.tuple({element(tree, 0), list(kept)});
}

// prune_children/1 of one child: a subtree without leaves left is dropped.
std::optional<Term> PolicyTree::pruned(const Term &child) {
    if (!is_pair(child)) {
        return child;
    }
    Terms children;
    for (const auto &grandchild : items(element(child, 1))) {
        if (const auto kept = pruned(grandchild)) {
            children.push_back(*kept);
        }
    }
    if (children.empty()) {
        return std::nullopt;
    }
    return builder_.tuple({element(child, 0), list(children)});
}

// prune_tree/1: subtrees without leaves removed; a tree left without children is empty.
Term PolicyTree::prune(const Term &tree) {
    if (is_empty_tree(tree) || (is_pair(tree) && element(tree, 1).is_nil())) {
        return builder_.tuple({});
    }
    if (!is_pair(tree)) {
        no_clause();
    }
    const auto kept = pruned(tree);
    return kept ? *kept : builder_.tuple({});
}

// valid_policy_node_set/1: the nodes whose parents are anyPolicy nodes, depth first as OTP folds them.
Terms PolicyTree::valid_policy_node_set(const Term &tree) {
    if (!is_pair(tree) || !any_node(element(tree, 0))) {
        return {};
    }
    const auto children = element(tree, 1);
    if (!has_subtrees(children)) {
        return children.is_list() ? items(children) : Terms{};
    }
    Terms parents;
    Terms nested;
    for (const auto &child : items(children)) {
        if (is_pair(child)) {
            parents.push_back(element(child, 0));
        }
        auto below = valid_policy_node_set(child);
        below.insert(below.end(), nested.begin(), nested.end());
        nested = std::move(below);
    }
    parents.insert(parents.end(), nested.begin(), nested.end());
    return parents;
}

// prune_invalid_children/2 of one child: anyPolicy subtrees walked, invalid parents and leaves dropped.
std::optional<Term> PolicyTree::valid_child(const Term &child, const Terms &invalid) {
    const auto listed = [&invalid](const Term &node) {
        return std::ranges::any_of(invalid, [&node](const Term &item) { return same(item, node); });
    };
    if (is_pair(child) && any_node(element(child, 0))) {
        return builder_.tuple({element(child, 0), list(valid_children(element(child, 1), invalid))});
    }
    if (is_pair(child)) {
        return listed(element(child, 0)) ? std::nullopt : std::optional(child);
    }
    if (any_node(child)) {
        return child;
    }
    if (!child.is_map()) {
        no_clause();
    }
    return listed(child) ? std::nullopt : std::optional(child);
}

// prune_invalid_children/2.
Terms PolicyTree::valid_children(const Term &children, const Terms &invalid) {
    Terms kept;
    for (const auto &child : items(children)) {
        if (const auto valid = valid_child(child, invalid)) {
            kept.push_back(*valid);
        }
    }
    return kept;
}

// prune_invalid_nodes/2: the invalid nodes removed below anyPolicy nodes.
Term PolicyTree::prune_invalid_nodes(const Term &tree, const Terms &invalid) {
    if (invalid.empty()) {
        return tree;
    }
    if (!is_pair(tree)) {
        no_clause();
    }
    const auto kept = valid_children(element(tree, 1), invalid);
    return kept.empty() ? builder_.tuple({}) : builder_.tuple({element(tree, 0), list(kept)});
}

Terms PolicyTree::any_leaves(const Term &tree) {
    Terms found;
    for (const auto &leaf : all_leaves(tree)) {
        if (any_node(leaf)) {
            found.push_back(leaf);
        }
    }
    return found;
}

// #{valid_policy := P} with P other than anyPolicy.
bool policy_node(const Term &node) {
    const auto policy = field_of(node, "valid_policy");
    return policy && !is_any(*policy);
}

// constrain/1: the valid policy nodes other than anyPolicy below anyPolicy nodes.
Terms PolicyTree::constrain(const Term &tree) {
    if (!is_pair(tree) || !any_node(element(tree, 0))) {
        return {};
    }
    const auto children = element(tree, 1);
    Terms found;
    if (!has_subtrees(children)) {
        std::ranges::copy_if(children.is_list() ? items(children) : Terms{}, std::back_inserter(found), policy_node);
        return found;
    }
    Terms nested;
    for (const auto &child : items(children)) {
        if (is_pair(child) && policy_node(element(child, 0))) {
            found.push_back(element(child, 0));
        }
        auto below = constrain(child);
        below.insert(below.end(), nested.begin(), nested.end());
        nested = std::move(below);
    }
    found.insert(found.end(), nested.begin(), nested.end());
    return found;
}

// qualifier/1 of each qualifier of a node: a user notice as it is, a CPS URI as {uri, URI}.
void PolicyTree::qualifiers(const Term &node, Terms &out) {
    for (const auto &info : items(field(node, "qualifier_set"))) {
        if (!info.is_tuple() || info.tuple_size_unchecked() != 3 || !atom_is(element(info, 0), "PolicyQualifierInfo")) {
            no_clause();
        }
        const auto id = element(info, 1);
        const auto qualifier = element(info, 2);
        if (same_oid(id, {1, 3, 6, 1, 5, 5, 7, 2, 2}) && qualifier.is_tuple()) {
            out.push_back(qualifier);
        } else if (same_oid(id, {1, 3, 6, 1, 5, 5, 7, 2, 1})) {
            out.push_back(builder_.tuple({builder_.atom("uri"), qualifier}));
        } else {
            no_clause();
        }
    }
}

// lists:flatten(children_collect(Nodes)): the qualifiers of a subtree's nodes, or of leaves, in order.
void PolicyTree::collect(const Term &nodes, Terms &out) {
    if (is_pair(nodes)) {
        qualifiers(element(nodes, 0), out);
        collect(element(nodes, 1), out);
        return;
    }
    if (has_subtrees(nodes)) {
        collect(nodes.head_unchecked(), out);
        collect(nodes.tail_unchecked(), out);
        return;
    }
    for (const auto &leaf : items(nodes)) {
        qualifiers(leaf, out);
    }
}

// collect_children_qualifiers/2: the qualifiers below the first child expecting the policy.
Terms PolicyTree::collect_children(const Term &children, const Policy &policy) {
    Terms out;
    auto rest = children;
    while (rest.is_cons()) {
        const auto head = rest.head_unchecked();
        const auto expected = is_pair(head) ? field_of(element(head, 0), "expected_policy_set") : std::nullopt;
        if (!expected) {
            collect(rest, out);
            return out;
        }
        if (member(policy.term, *expected)) {
            collect(head, out);
            return out;
        }
        rest = rest.tail_unchecked();
    }
    if (!rest.is_nil()) {
        collect(rest, out);
    }
    return out;
}

// collect_qualifiers/2: those of the policy, else those of anyPolicy.
Terms PolicyTree::collect_qualifiers(const Term &tree, const Policy &policy) {
    if (!is_pair(tree)) {
        no_clause();
    }
    auto found = collect_children(element(tree, 1), policy);
    if (found.empty()) {
        found = collect_children(element(tree, 1), {oid_term(builder_, {2, 5, 29, 32, 0})});
    }
    return found;
}

std::int64_t count_of(const Term &count) {
    const auto value = small(count.word());
    if (!value) {
        bad_argument();
    }
    return *value;
}

// #'PolicyInformation'{policyIdentifier = Policy, policyQualifiers = Qualifiers}.
std::pair<Term, Term> policy_information(const Term &info) {
    if (!info.is_tuple() || info.tuple_size_unchecked() != 3 || !atom_is(element(info, 0), "PolicyInformation")) {
        no_clause();
    }
    return {element(info, 1), element(info, 2)};
}

// lists:keytake(?anyPolicy, 2, Infos): the anyPolicy information, if any, and the others.
std::pair<std::optional<Term>, Terms> take_any(const Term &infos) {
    auto rest = items(infos);
    const auto found = std::ranges::find_if(rest, [](const Term &info) {
        return info.is_tuple() && info.tuple_size_unchecked() >= 2 && is_any(element(info, 1));
    });
    if (found == rest.end()) {
        return {std::nullopt, rest};
    }
    const auto any = *found;
    rest.erase(found);
    return {any, rest};
}

// policy_children/2 and any_policy_children/2: nodes of the policies, prepended as OTP folds them.
Terms children_of(PolicyTree &tree, const Terms &infos, const std::optional<Term> &expected) {
    Terms nodes;
    for (const auto &info : infos) {
        const auto [policy, qualifiers] = policy_information(info);
        if (!expected || member(policy, *expected)) {
            nodes.push_back(tree.single(policy, qualifiers));
        }
    }
    std::ranges::reverse(nodes);
    return nodes;
}

// handle_any_ext/6: anyPolicy in the certificate adds the expected policies no leaf has yet.
Counted any_extension(PolicyTree &tree, const Counted &grown, const Term &qualifiers) {
    const auto leaves = tree.all_leaves(grown.first);
    return tree.add_leaf_siblings(grown.first, grown.second, [&](const Term &parent) {
        Terms siblings;
        for (const auto &expected : items(field(parent, "expected_policy_set"))) {
            if (!tree.in_set(expected, leaves)) {
                siblings.push_back(tree.single(expected, qualifiers));
            }
        }
        return std::optional(siblings);
    });
}

// map_policy/5's MapPolicy fun: the issuer policy's expected set mapped to the subject policy.
Term mapped_node(PolicyTree &tree, const Term &node, const Mapping &mapping) {
    const auto &subject = mapping.subject_;
    const auto valid = field_of(node, "valid_policy");
    const auto set = field_of(node, "expected_policy_set");
    if (!valid || !set || !same(*valid, mapping.issuer_)) {
        return node;
    }
    if (set->is_cons() && set->tail_unchecked().is_nil() && same(set->head_unchecked(), *valid) &&
        !same(*valid, subject)) {
        return tree.with_field(node, "expected_policy_set", tree.list({subject}));
    }
    if (member(subject, *set)) {
        return node;
    }
    auto extended = items(*set);
    extended.push_back(subject);
    return tree.with_field(node, "expected_policy_set", tree.list(extended));
}

// add_policy_nodes/5 for one anyPolicy leaf: the user's policies the tree lacks, as siblings of the anyPolicy
// leaves with the same qualifiers.
Counted add_policy_nodes(PolicyTree &tree, const Counted &current, const Term &leaf, const Terms &valid_nodes,
                         const Term &user_set) {
    const auto qualifiers = field(leaf, "qualifier_set");
    Terms children;
    for (const auto &policy : items(user_set)) {
        if (!tree.in_set(policy, valid_nodes)) {
            children.push_back(tree.single(policy, qualifiers));
        }
    }
    return tree.add_leaf_siblings(current.first, current.second, [&](const Term &parent) {
        const auto set = field_of(parent, "qualifier_set");
        return std::optional(any_node(parent) && set && same(*set, qualifiers) ? children : Terms{});
    });
}

// handle_any_policy_leaves/4: the user's policies the tree lacks added beside anyPolicy leaves, then those leaves
// removed.
Counted any_policy_leaves(PolicyTree &tree, const Counted &start, const Terms &valid_nodes, const Term &user_set) {
    const auto any_leaves = tree.any_leaves(start.first);
    if (any_leaves.empty()) {
        return start;
    }
    auto current = start;
    for (const auto &leaf : any_leaves) {
        current = add_policy_nodes(tree, current, leaf, valid_nodes, user_set);
    }
    return {tree.prune_leaves(current.first, {oid_term(tree.builder(), {2, 5, 29, 32, 0})}), current.second};
}
} // namespace

// policy_add(Tree, Count, Infos, AnyAllowed): add_policy_children/3 (6.1.3 d), unpruned.
Term policy_add(ProcessContext &context, const Named<TreeRole> &tree, const Named<CountRole> &count,
                const Named<ValueRole> &infos, const Named<FlagRole> &any_allowed) {
    PolicyTree walk(context);
    const auto [any, others] = take_any(infos.term);
    const auto first = walk.add_leaves(tree.term, count_of(count.term), [&](const Term &leaf) {
        return children_of(walk, others, field(leaf, "expected_policy_set"));
    });
    const auto leaves = walk.all_leaves(first.first);
    const auto grown = walk.add_leaf_siblings(first.first, first.second, [&](const Term &parent) {
        if (!any_node(parent)) {
            return std::optional(Terms{});
        }
        return leaves.empty() ? std::optional(children_of(walk, others, std::nullopt)) : std::nullopt;
    });
    if (!any) {
        return walk.counted(grown);
    }
    // handle_any_ext/6 matches the record before it looks at the counters.
    const auto qualifiers = policy_information(*any).second;
    return walk.counted(atom_is(any_allowed.term, "true") ? any_extension(walk, grown, qualifiers) : grown);
}

// policy_map(Tree, Count, Mapping, Inhibit, AnyQualifiers): map_policy/5 (6.1.4 b).
Term policy_map(ProcessContext &context, const Named<TreeRole> &tree, const Named<CountRole> &count,
                const Named<ValueRole> &mapping, const Named<FlagRole> &inhibit, const Named<KeyRole> &any_qualifiers) {
    PolicyTree walk(context);
    const auto &map = mapping.term;
    if (!map.is_tuple() || map.tuple_size_unchecked() != 3 || !atom_is(element(map, 0), "PolicyMappings_SEQOF")) {
        no_clause();
    }
    const auto issuer = element(map, 1);
    const auto subject = element(map, 2);
    const auto limit = small(inhibit.term.word());
    if (limit && *limit == 0) {
        return walk.counted({walk.prune(walk.prune_leaves(tree.term, {issuer})), count_of(count.term)});
    }
    if (!limit || *limit < 0) {
        no_clause();
    }
    const auto mapped =
        walk.map_leaves(tree.term, [&](const Term &node) { return mapped_node(walk, node, {issuer, subject}); });
    if (!same(mapped, tree.term)) {
        return walk.counted({mapped, count_of(count.term)});
    }
    const auto sibling = walk.node({issuer, any_qualifiers.term, walk.list({subject})});
    return walk.counted(walk.add_leaf_siblings(tree.term, count_of(count.term), [&](const Term &parent) {
        return any_node(parent) ? std::optional(Terms{sibling}) : std::nullopt;
    }));
}

// policy_intersection(UserPolicySet, Tree, Count): policy_tree_intersection/3 (6.1.5 g).
Term policy_intersection(ProcessContext &context, const Named<ValueRole> &user_set, const Named<TreeRole> &tree,
                         const Named<CountRole> &count) {
    PolicyTree walk(context);
    const auto &set = user_set.term;
    const auto start = Counted{tree.term, count_of(count.term)};
    if ((set.is_cons() && set.tail_unchecked().is_nil() && is_any(set.head_unchecked())) || is_empty_tree(tree.term)) {
        return walk.counted(start);
    }
    const auto valid_nodes = walk.valid_policy_node_set(tree.term);
    Terms invalid;
    for (const auto &node : std::views::reverse(valid_nodes)) {
        const auto policy = field_of(node, "valid_policy");
        if (policy && !is_any(*policy) && !member(*policy, set)) {
            invalid.push_back(node);
        }
    }
    const auto valid = walk.prune_invalid_nodes(tree.term, invalid);
    const auto grown = any_policy_leaves(walk, {valid, start.second}, valid_nodes, set);
    return walk.counted({walk.prune(grown.first), grown.second});
}

Term policy_prune(ProcessContext &context, const Named<TreeRole> &tree) {
    PolicyTree walk(context);
    return walk.prune(tree.term);
}

// policy_constrained(Tree): constrained_policy_node_set/1, the anyPolicy leaves or else the constrained nodes.
Term policy_constrained(ProcessContext &context, const Named<TreeRole> &tree) {
    PolicyTree walk(context);
    if (is_empty_tree(tree.term)) {
        return walk.list({});
    }
    const auto any = walk.any_leaves(tree.term);
    return walk.list(any.empty() ? walk.constrain(tree.term) : any);
}

// policy_result(Tree): the constrained nodes with the qualifiers collected for their expected policies.
Term policy_result(ProcessContext &context, const Named<TreeRole> &tree) {
    PolicyTree walk(context);
    const auto nodes = items(policy_constrained(context, tree));
    Terms result;
    for (const auto &node : nodes) {
        Terms qualifiers;
        for (const auto &policy : items(field(node, "expected_policy_set"))) {
            const auto found = walk.collect_qualifiers(tree.term, {policy});
            qualifiers.insert(qualifiers.end(), found.begin(), found.end());
        }
        result.push_back(walk.with_field(node, "qualifier_set", walk.list(qualifiers)));
    }
    return walk.list(result);
}
} // namespace clause::runtime::builtins::public_key
