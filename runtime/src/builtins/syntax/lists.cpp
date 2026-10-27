#include "../collections/collections.hpp"
#include "syntax.hpp"

// erl_syntax's list skeletons (plan 21 LIB77E2): list trees with a prefix and an optional suffix, plain cons chains,
// and the functions that walk, normalize and compact them.
namespace clause::runtime::builtins::syntax {
namespace {
// The suffix none of a list tree.
Term none(ProcessContext &context) { return Trees(context).atom("none"_atom); }

// list(Elements) of a C++ list of elements.
Term list_tree(ProcessContext &context, const Items &items, const Term &tail) {
    return list(context, Trees(context).list(items), tail);
}

// The elements of a list skeleton chain (list_elements/1) or their count; error({case_clause, Type}) at a node
// that is neither a list nor nil.
template <typename Visit> void walk_skeleton(ProcessContext &context, const Term &node, Visit visit) {
    auto at = node;
    for (;;) {
        const auto type = type_of(context, at);
        if (type == "nil") {
            return;
        }
        if (type != "list") {
            case_clause(context, Trees(context).atom_of(type));
        }
        visit(list_prefix(context, at));
        const auto suffix = list_suffix(context, at);
        if (is_none(suffix)) {
            return;
        }
        at = suffix;
    }
}

// normalize_list_1/2: foldr of list([X], Tail), one prefix element per list node.
Term nest(ProcessContext &context, const Term &prefix, const Tail &tail) {
    auto items = elements(prefix);
    auto result = tail.term;
    for (auto at = items.rbegin(); at != items.rend(); ++at) {
        result = list_tree(context, {*at}, result);
    }
    return result;
}

// The plain prefix and suffix of a cons chain.
Term cons_prefix(ProcessContext &context, const Term &cons) {
    Items items;
    auto at = cons;
    for (; is_node(at, "cons", 4); at = element(at, 3)) {
        items.push_back(element(at, 2));
    }
    return Trees(context).list(items);
}

Term cons_suffix(const Term &cons) {
    auto at = cons;
    while (is_node(at, "cons", 4)) {
        at = element(at, 3);
    }
    return at;
}
} // namespace

Term list_prefix(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    return is_node(plain, "cons", 4) ? cons_prefix(context, plain) : record_field(context, plain, {"list", 3}, 0);
}

Term list_suffix(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    if (!is_node(plain, "cons", 4)) {
        return record_field(context, plain, {"list", 3}, 1);
    }
    const auto tail = cons_suffix(plain);
    return is_node(tail, "nil", 2) ? none(context) : tail;
}

Term list_head(ProcessContext &context, const Term &node) { return first(list_prefix(context, node)); }

Term list_tail(ProcessContext &context, const Term &node) {
    const auto tail = list_suffix(context, node);
    const auto prefix = list_prefix(context, node);
    if (!prefix.is_cons()) {
        bad_argument();
    }
    const auto rest = prefix.tail_unchecked();
    if (!rest.is_nil()) {
        return list(context, rest, tail);
    }
    return is_none(tail) ? make(context, "nil", {}) : tail;
}

bool is_list_skeleton(ProcessContext &context, const Term &node) {
    const auto type = type_of(context, node);
    return type == "list" || type == "nil";
}

bool is_proper_list(ProcessContext &context, const Term &node) {
    auto at = node;
    for (;;) {
        const auto type = type_of(context, at);
        if (type != "list") {
            return type == "nil";
        }
        const auto suffix = list_suffix(context, at);
        if (is_none(suffix)) {
            return true;
        }
        at = suffix;
    }
}

Term list_elements(ProcessContext &context, const Term &node) {
    Items items;
    walk_skeleton(context, node, [&](const Term &prefix) {
        const auto part = elements(prefix);
        items.insert(items.end(), part.begin(), part.end());
    });
    return Trees(context).list(items);
}

namespace {
Term list_length(ProcessContext &context, const Term &node) {
    std::size_t count = 0;
    walk_skeleton(context, node, [&](const Term &prefix) { count += length(prefix); });
    return Trees(context).integer(std::int64_t(count));
}

Term normalize_list(ProcessContext &context, const Term &node) {
    if (type_of(context, node) != "list") {
        return node;
    }
    const auto suffix = list_suffix(context, node);
    const auto tail = is_none(suffix) ? make(context, "nil", {}) : normalize_list(context, suffix);
    return copy_attrs(context, node, nest(context, list_prefix(context, node), {tail}));
}

Term compact_list(ProcessContext &context, const Term &node) {
    if (type_of(context, node) != "list") {
        return node;
    }
    const auto suffix = list_suffix(context, node);
    if (is_none(suffix)) {
        return node;
    }
    const auto type = type_of(context, suffix);
    if (type == "list") {
        const auto tail = compact_list(context, suffix);
        const auto prefix = collections::append(context, {list_prefix(context, node)}, list_prefix(context, tail));
        return join_comments(context, tail,
                             copy_attrs(context, node, list(context, prefix, list_suffix(context, tail))));
    }
    if (type == "nil") {
        const auto joined = list(context, list_prefix(context, node), none(context));
        return join_comments(context, suffix, copy_attrs(context, node, joined));
    }
    return node;
}

Term list_prefix1(ProcessContext &context, const Term &node) { return list_prefix(context, node); }

Term list_suffix1(ProcessContext &context, const Term &node) { return list_suffix(context, node); }

Term list_head1(ProcessContext &context, const Term &node) { return list_head(context, node); }

Term list_tail1(ProcessContext &context, const Term &node) { return list_tail(context, node); }

Term is_list_skeleton1(ProcessContext &context, const Term &node) {
    return Trees(context).boolean(is_list_skeleton(context, node));
}

Term is_proper_list1(ProcessContext &context, const Term &node) {
    return Trees(context).boolean(is_proper_list(context, node));
}

Term list_elements1(ProcessContext &context, const Term &node) { return list_elements(context, node); }

constexpr std::array LISTS{
    typed_entry<list_prefix1>("erl_syntax", "list_prefix"),
    typed_entry<list_suffix1>("erl_syntax", "list_suffix"),
    typed_entry<list_head1>("erl_syntax", "list_head"),
    typed_entry<list_tail1>("erl_syntax", "list_tail"),
    typed_entry<is_list_skeleton1>("erl_syntax", "is_list_skeleton"),
    typed_entry<is_proper_list1>("erl_syntax", "is_proper_list"),
    typed_entry<list_elements1>("erl_syntax", "list_elements"),
    typed_entry<list_length>("erl_syntax", "list_length"),
    typed_entry<normalize_list>("erl_syntax", "normalize_list"),
    typed_entry<compact_list>("erl_syntax", "compact_list"),
};
} // namespace
} // namespace clause::runtime::builtins::syntax

namespace clause::runtime {
// Publish erl_syntax's list skeleton functions.
std::span<const BuiltinEntry> erl_syntax_list_builtins() noexcept { return builtins::syntax::LISTS; }
} // namespace clause::runtime
