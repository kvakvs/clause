#include "collections.hpp"
#include <array>

// The ordsets builtins (plan 21 step LIB1, docs/builtins.md#collections): ordered sets are sorted lists without
// duplicates, elements comparing by Erlang's < and ==. Each walks its lists as OTP's clauses do, so equal elements
// keep the same representative (1 or 1.0), a rest OTP returns unchecked is returned as it is, and a shape no clause
// accepts raises function_clause.
namespace clause::runtime::builtins::collections {
namespace {
// The rests of two sets walked together; OTP's union, intersection and is_disjoint swap them after taking from the
// second, so the set whose head was greater becomes the first.
struct Rests {
    Term first;
    Term second;

    // Whether both rests still have a head.
    bool both() const { return first.is_cons() && second.is_cons(); }

    // The order of the two heads.
    int order() const { return compare(first.head_unchecked(), second.head_unchecked()); }

    // Drop the head of the second set, which becomes the first: OTP's "switch arguments".
    void swap_after_second() {
        auto rest = second.tail_unchecked();
        second = first;
        first = rest;
    }

    // Drop both heads.
    void advance_both() {
        first = first.tail_unchecked();
        second = second.tail_unchecked();
    }
};

Term boolean(ProcessContext &context, bool value) { return need(TermFactory(context).boolean(value)); }

// is_set(Ordset): a proper list in strictly increasing order; false for a non-list.
Term is_set(ProcessContext &context, const Term &set) {
    if (!set.is_cons()) {
        return boolean(context, set.is_nil());
    }
    auto previous = set.head_unchecked();
    auto rest = set.tail_unchecked();
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto element = rest.head_unchecked();
        if (compare(previous, element) >= 0) {
            return boolean(context, false);
        }
        previous = element;
    }
    if (!rest.is_nil()) {
        no_clause();
    }
    return boolean(context, true);
}

// is_element(Element, Ordset).
Term is_element(ProcessContext &context, const Element &element, const List &set) {
    auto rest = set.term;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto order = compare(element.term, rest.head_unchecked());
        if (order <= 0) {
            return boolean(context, order == 0);
        }
    }
    if (!rest.is_nil()) {
        no_clause();
    }
    return boolean(context, false);
}

// add_element(Element, Ordset1): Element inserted before the first greater element; the set itself (from that
// point) when an equal element is present.
Term add_element(ProcessContext &context, const Element &element, const List &set) {
    Prefix passed;
    auto rest = set.term;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto head = rest.head_unchecked();
        const auto order = compare(element.term, head);
        if (order == 0) {
            return passed.then(context, rest);
        }
        if (order < 0) {
            return passed.then(context, need(TermFactory(context).cons(element.term, rest)));
        }
        passed.push(head);
    }
    if (!rest.is_nil()) {
        no_clause();
    }
    passed.push(element.term);
    return passed.done(context);
}

// del_element(Element, Ordset1).
Term del_element(ProcessContext &context, const Element &element, const List &set) {
    Prefix passed;
    auto rest = set.term;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto head = rest.head_unchecked();
        const auto order = compare(element.term, head);
        if (order <= 0) {
            return passed.then(context, order == 0 ? rest.tail_unchecked() : rest);
        }
        passed.push(head);
    }
    if (!rest.is_nil()) {
        no_clause();
    }
    return passed.done(context);
}

// union(Ordset1, Ordset2): of equal elements the one of the current first set is kept.
Term union2(ProcessContext &context, const Term &set1, const Term &set2) {
    Prefix merged;
    Rests rests{set1, set2};
    while (rests.both()) {
        const auto order = rests.order();
        if (order > 0) {
            merged.push(rests.second.head_unchecked());
            rests.swap_after_second();
            continue;
        }
        merged.push(rests.first.head_unchecked());
        if (order == 0) {
            rests.advance_both();
        } else {
            rests.first = rests.first.tail_unchecked();
        }
    }
    if (rests.first.is_nil() || rests.second.is_nil()) {
        return merged.then(context, rests.first.is_nil() ? rests.second : rests.first);
    }
    no_clause();
}

// intersection(Ordset1, Ordset2).
Term intersection2(ProcessContext &context, const Term &set1, const Term &set2) {
    Prefix common;
    Rests rests{set1, set2};
    while (rests.both()) {
        const auto order = rests.order();
        if (order > 0) {
            rests.swap_after_second();
        } else if (order < 0) {
            rests.first = rests.first.tail_unchecked();
        } else {
            common.push(rests.first.head_unchecked());
            rests.advance_both();
        }
    }
    if (!rests.first.is_nil() && !rests.second.is_nil()) {
        no_clause();
    }
    return common.done(context);
}

// intersection(OrdsetList): a non-empty list of sets.
Term intersection1(ProcessContext &context, const Term &sets) {
    if (!sets.is_cons()) {
        no_clause();
    }
    auto result = sets.head_unchecked();
    auto rest = sets.tail_unchecked();
    if (!rest.is_cons() && !rest.is_nil()) {
        no_clause();
    }
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        result = intersection2(context, result, rest.head_unchecked());
    }
    if (!rest.is_nil()) {
        no_clause();
    }
    return result;
}

// is_disjoint(Ordset1, Ordset2).
Term is_disjoint(ProcessContext &context, const Term &set1, const Term &set2) {
    Rests rests{set1, set2};
    while (rests.both()) {
        const auto order = rests.order();
        if (order == 0) {
            return boolean(context, false);
        }
        if (order > 0) {
            rests.swap_after_second();
        } else {
            rests.first = rests.first.tail_unchecked();
        }
    }
    if (!rests.first.is_nil() && !rests.second.is_nil()) {
        no_clause();
    }
    return boolean(context, true);
}

// subtract(Ordset1, Ordset2): the rest of Ordset1 as it is once Ordset2 ends.
Term subtract(ProcessContext &context, const Term &set1, const Term &set2) {
    Prefix kept;
    Rests rests{set1, set2};
    while (rests.both()) {
        const auto order = rests.order();
        if (order < 0) {
            kept.push(rests.first.head_unchecked());
            rests.first = rests.first.tail_unchecked();
        } else if (order > 0) {
            rests.second = rests.second.tail_unchecked();
        } else {
            rests.advance_both();
        }
    }
    if (rests.first.is_nil()) {
        return kept.done(context);
    }
    if (rests.second.is_nil()) {
        return kept.then(context, rests.first);
    }
    no_clause();
}

// is_subset(Ordset1, Ordset2).
Term is_subset(ProcessContext &context, const Term &set1, const Term &set2) {
    Rests rests{set1, set2};
    while (rests.both()) {
        const auto order = rests.order();
        if (order < 0) {
            return boolean(context, false);
        }
        if (order > 0) {
            rests.second = rests.second.tail_unchecked();
        } else {
            rests.advance_both();
        }
    }
    if (rests.first.is_nil() || rests.second.is_nil()) {
        return boolean(context, rests.first.is_nil());
    }
    no_clause();
}

constexpr std::array ORDSETS_BUILTINS{
    typed_entry<is_set>("ordsets", "is_set"),
    typed_entry<is_element>("ordsets", "is_element"),
    typed_entry<add_element>("ordsets", "add_element"),
    typed_entry<del_element>("ordsets", "del_element"),
    typed_entry<union2>("ordsets", "union"),
    typed_entry<intersection2>("ordsets", "intersection"),
    typed_entry<intersection1>("ordsets", "intersection"),
    typed_entry<is_disjoint>("ordsets", "is_disjoint"),
    typed_entry<subtract>("ordsets", "subtract"),
    typed_entry<is_subset>("ordsets", "is_subset"),
};
} // namespace
} // namespace clause::runtime::builtins::collections

namespace clause::runtime {
std::span<const BuiltinEntry> ordsets_builtins() noexcept { return builtins::collections::ORDSETS_BUILTINS; }
} // namespace clause::runtime
