#include "../collections/collections.hpp"
#include "../front_end/abstract.hpp"
#include "qlc_pt.hpp"
#include <algorithm>
#include <string>

// The list, set and walk helpers of the run-time qlc transform (plan 21 step LIB73): OTP's lists, ordsets and sofs
// calls of qlc_pt.erl, and its walks over terms.
namespace clause::runtime::builtins::qlc_pt {
using front_end::forms::element;
using front_end::forms::is_atom;
using front_end::forms::is_node;

bool less(const Term &left, const Term &right) { return collections::compare(left, right) < 0; }

bool equal(const Term &left, const Term &right) { return collections::compare(left, right) == 0; }

bool same(const Term &left, const Term &right) { return collections::same(left, right); }

TermList sorted(TermList items) {
    std::ranges::stable_sort(items, less);
    return items;
}

TermList usorted(TermList items) {
    std::ranges::stable_sort(items, less);
    const auto [first, last] = std::ranges::unique(items, equal);
    items.erase(first, last);
    return items;
}

TermList minus(TermList left, const Removed &right) {
    for (const auto &item : right.items) {
        const auto found = std::ranges::find_if(left, [&](const Term &candidate) { return same(candidate, item); });
        if (found != left.end()) {
            left.erase(found);
        }
    }
    return left;
}

bool member(const Term &item, std::span<const Term> items) {
    return std::ranges::any_of(items, [&](const Term &candidate) { return same(candidate, item); });
}

std::optional<Term> keyfind(const Term &key, std::size_t position, std::span<const Term> tuples) {
    for (const auto &tuple : tuples) {
        if (tuple.is_tuple() && tuple.tuple_size_unchecked() > position && equal(element(tuple, position), key)) {
            return tuple;
        }
    }
    return std::nullopt;
}

TermList ord_subtract(const TermList &left, const TermList &right) {
    TermList out;
    std::ranges::set_difference(left, right, std::back_inserter(out), less);
    return out;
}

TermList ord_intersection(const TermList &left, const TermList &right) {
    TermList out;
    std::ranges::set_intersection(left, right, std::back_inserter(out), less);
    return out;
}

TermList elements(const Term &list) { return lists::proper(list); }

namespace {
void flatten_into(TermList &out, const Term &deep) {
    auto rest = deep;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto head = rest.head_unchecked();
        if (head.is_cons() || head.is_nil()) {
            flatten_into(out, head);
        } else {
            out.push_back(head);
        }
    }
    if (!rest.is_nil()) {
        lists::no_clause();
    }
}

// The usorted values of a run of pairs with equal keys.
Term family_values(const Builder &build, std::span<const Term> run) {
    TermList values;
    for (const auto &pair : run) {
        values.push_back(element(pair, 1));
    }
    return build.list(usorted(std::move(values)));
}

// The union of two usorted lists.
Term set_union(const Builder &build, const Term &left, const Term &right) {
    TermList out;
    const auto lefts = elements(left);
    const auto rights = elements(right);
    std::ranges::set_union(lefts, rights, std::back_inserter(out), less);
    return build.list(out);
}
} // namespace

TermList flattened(const Term &deep) {
    TermList out;
    flatten_into(out, deep);
    return out;
}

TermList family_of(const Builder &build, TermList pairs) {
    const auto relation = usorted(std::move(pairs));
    TermList family;
    for (std::size_t at = 0; at < relation.size();) {
        auto end = at + 1;
        while (end < relation.size() && equal(element(relation[end], 0), element(relation[at], 0))) {
            ++end;
        }
        const std::array items{element(relation[at], 0),
                               family_values(build, std::span(relation).subspan(at, end - at))};
        family.push_back(build.tuple(items));
        at = end;
    }
    return family;
}

TermList family_union(const Builder &build, const TermList &left, const TermList &right) {
    TermList out;
    std::size_t at_left = 0;
    std::size_t at_right = 0;
    while (at_left < left.size() || at_right < right.size()) {
        const auto take_left = at_right == right.size() ||
                               (at_left < left.size() && less(element(left[at_left], 0), element(right[at_right], 0)));
        const auto take_right =
            at_left == left.size() || (!take_left && less(element(right[at_right], 0), element(left[at_left], 0)));
        if (take_left) {
            out.push_back(left[at_left++]);
        } else if (take_right) {
            out.push_back(right[at_right++]);
        } else {
            const std::array items{element(left[at_left], 0),
                                   set_union(build, element(left[at_left], 1), element(right[at_right], 1))};
            out.push_back(build.tuple(items));
            ++at_left;
            ++at_right;
        }
    }
    return out;
}

TermList family_union_sets(const Builder &build, const TermList &family) {
    TermList out;
    for (const auto &entry : family) {
        auto all = build.nil();
        for (const auto &set : elements(element(entry, 1))) {
            all = set_union(build, all, build.list(usorted(elements(set))));
        }
        const std::array items{element(entry, 0), all};
        out.push_back(build.tuple(items));
    }
    return out;
}

namespace {
// var_fold/3's walk: the names of the {var, _, V} with V not '_' in visiting order.
void collect_vars(const Term &term, TermList &out) {
    if (term.is_tuple()) {
        if (is_node(term, "var", 3) && !is_atom(element(term, 2), "_")) {
            out.push_back(element(term, 2));
            return;
        }
        for (std::size_t at = 0; at < term.tuple_size_unchecked(); ++at) {
            collect_vars(element(term, at), out);
        }
        return;
    }
    auto rest = term;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        collect_vars(rest.head_unchecked(), out);
    }
    if (rest.is_tuple()) {
        collect_vars(rest, out);
    }
}
} // namespace

TermList vars_of(const Term &term) {
    TermList names;
    collect_vars(term, names);
    return usorted(std::move(names));
}

TermList occurring_vars(const Term &term) {
    TermList names;
    collect_vars(term, names);
    std::ranges::reverse(names);
    return names;
}

Term rebuilt(const Builder &build, const Term &term, const NodeVisit &visit) {
    if (auto replaced = visit(term)) {
        return *replaced;
    }
    if (term.is_tuple()) {
        TermList items;
        for (std::size_t at = 0; at < term.tuple_size_unchecked(); ++at) {
            items.push_back(rebuilt(build, element(term, at), visit));
        }
        return build.tuple(items);
    }
    if (!term.is_cons()) {
        return term;
    }
    TermList heads;
    auto rest = term;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        heads.push_back(rebuilt(build, rest.head_unchecked(), visit));
    }
    return build.terms().list(heads, rebuilt(build, rest, visit));
}

Term var_mapped(const Builder &build, const Term &term, const std::function<Term(const Term &)> &map) {
    NodeVisit visit;
    visit = [&](const Term &node) -> std::optional<Term> {
        if (is_node(node, "var", 3)) {
            return map(node);
        }
        if (!is_node(node, "named_fun", 4)) {
            return std::nullopt;
        }
        const auto name = map(build.node("var"_atom, element(node, 1), element(node, 2)));
        return build.node("named_fun"_atom, element(name, 1), element(name, 2),
                          rebuilt(build, element(node, 3), visit));
    };
    return rebuilt(build, term, visit);
}

Term anonymous_numbered(const Builder &build, const Term &term, std::int64_t &next) {
    return rebuilt(build, term, [&](const Term &node) -> std::optional<Term> {
        if (!is_node(node, "var", 3)) {
            return std::nullopt;
        }
        if (!is_atom(element(node, 2), "_")) {
            return node;
        }
        return build.node("var"_atom, element(node, 1), build.integer(next++));
    });
}

Term list_to_cons(const Builder &build, std::span<const Term> items, const std::optional<Term> &tail) {
    auto out = tail.value_or(build.node("nil"_atom, build.integer(0)));
    for (auto at = items.size(); at > 0; --at) {
        out = build.node("cons"_atom, build.integer(0), items[at - 1], out);
    }
    return out;
}

namespace {
// list2cons/1 of a term: a cons chain for a list (a non-list tail kept as the last tail), the term itself otherwise.
Term listed_to_cons(const Builder &build, const Term &term) {
    if (!term.is_cons() && !term.is_nil()) {
        return term;
    }
    TermList heads;
    auto rest = term;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        heads.push_back(rest.head_unchecked());
    }
    return list_to_cons(build, heads, rest.is_nil() ? std::nullopt : std::optional{rest});
}

TermList cons_to_list(const Builder &build, const Term &cons);
} // namespace

Term tuple_to_cons(const Builder &build, const Term &term) {
    return rebuilt(build, term, [&](const Term &node) -> std::optional<Term> {
        if (!is_node(node, "tuple", 3)) {
            return std::nullopt;
        }
        return build.tuple(
            std::array{build.atom("cons_tuple"_atom), listed_to_cons(build, tuple_to_cons(build, element(node, 2)))});
    });
}

Term cons_to_tuple(const Builder &build, const Term &term) {
    return rebuilt(build, term, [&](const Term &node) -> std::optional<Term> {
        if (!(node.is_tuple() && node.tuple_size_unchecked() == 2 && is_atom(element(node, 0), "cons_tuple"))) {
            return std::nullopt;
        }
        return build.node("tuple"_atom, build.integer(0), build.list(cons_to_list(build, element(node, 1))));
    });
}

namespace {
TermList cons_to_list(const Builder &build, const Term &cons) {
    TermList out;
    auto rest = cons;
    while (is_node(rest, "cons", 4)) {
        out.push_back(cons_to_tuple(build, element(rest, 2)));
        rest = element(rest, 3);
    }
    if (!is_node(rest, "nil", 2)) {
        out.push_back(cons_to_tuple(build, rest));
    }
    return out;
}

// pre_expand/1's call clauses: the call they turn a call into, none for any other node.
std::optional<Term> expanded_call(const Builder &build, const Term &node) {
    if (!is_node(node, "call", 4)) {
        return std::nullopt;
    }
    const auto anno = element(node, 1);
    const auto callee = element(node, 2);
    if (is_node(callee, "atom", 3) && is_atom(element(callee, 2), "record")) {
        return build.node("call"_atom, anno, build.atom_node(element(callee, 1), "is_record"_atom), element(node, 3));
    }
    if (is_node(callee, "atom", 3)) {
        const auto remote = build.node("remote"_atom, anno, build.atom_node(anno, "erlang"_atom), callee);
        return build.node("call"_atom, anno, remote, element(node, 3));
    }
    if (is_node(callee, "tuple", 3)) {
        const auto parts = elements(element(callee, 2));
        if (parts.size() == 2) {
            return build.node("call"_atom, anno, build.node("remote"_atom, anno, parts[0], parts[1]), element(node, 3));
        }
    }
    return std::nullopt;
}
} // namespace

Term pre_expanded(const Builder &build, const Term &term) {
    NodeVisit visit;
    visit = [&](const Term &node) -> std::optional<Term> {
        if (auto call = expanded_call(build, node)) {
            return rebuilt(build, *call, visit);
        }
        return std::nullopt;
    };
    return rebuilt(build, term, visit);
}

Term with_anno(const Builder &build, const Term &term, const Term &anno) {
    return front_end::abstract::replace_annos(build.context(), term, {anno});
}

std::string concat_part(const Term &part) {
    if (part.is_atom()) {
        return std::string(part.atom_spelling_unchecked());
    }
    if (const auto value = string::small_of(part)) {
        return std::to_string(*value);
    }
    bad_argument();
}

Term name_suffix(const Builder &build, std::string_view prefix, std::int64_t suffix) {
    const auto spelling = std::string(prefix) + std::to_string(suffix);
    return build.atom(AtomName(spelling));
}

Term aux_name(const Builder &build, std::string_view prefix, std::int64_t first, const std::set<Word> &taken) {
    for (auto number = first;; ++number) {
        const auto name = name_suffix(build, prefix, number);
        if (!taken.contains(name.word())) {
            return name;
        }
    }
}

TermList tuple_items(const Term &tuple) {
    TermList items;
    for (std::size_t at = 0; at < tuple.tuple_size_unchecked(); ++at) {
        items.push_back(element(tuple, at));
    }
    return items;
}

bool any_child(const Term &term, const std::function<bool(const Term &)> &test) {
    if (term.is_tuple()) {
        return std::ranges::any_of(tuple_items(term), test);
    }
    auto rest = term;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        if (test(rest.head_unchecked())) {
            return true;
        }
    }
    return rest.word() != term.word() && test(rest);
}

std::pair<Term, Term> map_threaded(const Builder &build, const Term &term, const Term &frame, const ThreadedMap &map) {
    if (!term.is_tuple() && !term.is_cons()) {
        return {term, frame};
    }
    auto current = frame;
    const auto step = [&](const Term &child) {
        auto [mapped, next] = map(child, current);
        current = next;
        return mapped;
    };
    if (term.is_tuple()) {
        TermList items;
        for (const auto &item : tuple_items(term)) {
            items.push_back(step(item));
        }
        return {build.tuple(items), current};
    }
    TermList heads;
    auto rest = term;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        heads.push_back(step(rest.head_unchecked()));
    }
    const auto tail = step(rest);
    return {build.terms().list(heads, tail), current};
}

std::optional<TermList> remote_call_args(const Term &call, std::string_view module, std::string_view function) {
    if (!is_node(call, "call", 4) || !element(call, 3).is_cons()) {
        return std::nullopt;
    }
    const auto callee = element(call, 2);
    const auto named = [](const Term &node, std::string_view name) {
        return is_node(node, "atom", 3) && is_atom(element(node, 2), name);
    };
    if (!is_node(callee, "remote", 4) || !named(element(callee, 2), module) || !named(element(callee, 3), function)) {
        return std::nullopt;
    }
    return elements(element(call, 3));
}

std::optional<std::pair<std::int64_t, Term>> element_call(const Term &call) {
    const auto args = remote_call_args(call, "erlang", "element");
    if (!args || args->size() != 2 || !is_node((*args)[0], "integer", 3)) {
        return std::nullopt;
    }
    const auto index = string::small_of(element((*args)[0], 2));
    if (!index || *index <= 0) {
        return std::nullopt;
    }
    return std::pair{*index, (*args)[1]};
}

TermList all_selections(const Builder &build, const TermList &choices) {
    TermList selections{build.nil()};
    for (auto at = choices.size(); at > 0; --at) {
        const auto &choice = choices[at - 1];
        TermList next;
        for (const auto &column : elements(element(choice, 1))) {
            const auto picked = build.tuple(std::array{element(choice, 0), column});
            for (const auto &rest : selections) {
                next.push_back(build.terms().cons(picked, rest));
            }
        }
        selections = std::move(next);
    }
    return selections;
}
} // namespace clause::runtime::builtins::qlc_pt
