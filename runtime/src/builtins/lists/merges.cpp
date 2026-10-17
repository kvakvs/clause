#include "../../terms/structural_order.hpp"
#include "list_walk.hpp"
#include <algorithm>
#include <array>
#include <terms.hpp>

// The lists builtins that merge sorted lists and sort by key (lists merges, docs/builtins.md#lists-module):
// merge/1,2, merge3/3, umerge3/3, keymerge/3, ukeymerge/3 and ukeysort/2. Elements compare in term order, whole or
// by one tuple element; of elements comparing equal, the one from the earlier list comes first, and the unique forms
// keep only that one.
namespace clause::runtime::builtins::lists {
namespace {
// What a merge or sort compares: whole elements, or element `key` (1-based) of tuple elements.
class Ordering final {
  public:
    // Whole elements when `key` is 0.
    explicit Ordering(std::size_t key = 0) : key_(key) {}

    // -1, 0 or 1 as `left` orders before, equal to or after `right`.
    int compare(const Term &left, const Term &right) const {
        if (key_ == 0) {
            return need(detail::structural_order(left, right));
        }
        return need(detail::structural_order(keyed(left), keyed(right)));
    }

  private:
    // The key of a tuple element; badarg for anything else, as element/2 raises in OTP's clauses.
    Term keyed(const Term &element) const {
        if (!element.is_tuple() || need(element.tuple_size()) < key_) {
            bad_argument();
        }
        return need(element.tuple_element(key_ - 1));
    }

    std::size_t key_;
};

// The elements of the rest a merge appends; badarg for an improper list, as lists:reverse/2 raises in OTP's merges.
std::vector<Term> appended(Term rest) {
    std::vector<Term> elements;
    for (; rest.is_cons(); rest = need(rest.tail())) {
        elements.push_back(need(rest.head()));
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return elements;
}

// The other list of a merge with [], as it is; function_clause unless it is a list.
Term other_list(const Term &other) {
    if (!other.is_list()) {
        no_clause();
    }
    return other;
}

struct Walk {
    // The merged elements so far and the rests of both lists.
    std::vector<Term> result;
    Term left;
    Term right;
};

// Take the smaller head of two non-empty rests: the left one on equal heads, dropping the right one with `unique`.
void step(Walk &walk, const Ordering &ordering, bool unique) {
    const auto a = need(walk.left.head());
    const auto b = need(walk.right.head());
    const auto order = ordering.compare(a, b);
    if (order > 0) {
        walk.result.push_back(b);
        walk.right = need(walk.right.tail());
        return;
    }
    if (unique && order == 0) {
        walk.right = need(walk.right.tail());
    }
    walk.result.push_back(a);
    walk.left = need(walk.left.tail());
}

// `first` and `second` merged, walked as OTP's clauses walk them: an empty list gives the other one as it is, the
// list that ends first must end in [] (else function_clause) and the rest of the other is appended. On equal
// elements the one from `first` comes first, and with `unique` the one from `second` is dropped.
Term merged(ProcessContext &context, const Term &first, const Term &second, const Ordering &ordering, bool unique) {
    if (first.is_nil() || second.is_nil()) {
        return other_list(first.is_nil() ? second : first);
    }
    Walk walk{{}, first, second};
    while (walk.left.is_cons() && walk.right.is_cons()) {
        step(walk, ordering, unique);
    }
    const bool left_ended = !walk.left.is_cons();
    if (!(left_ended ? walk.left : walk.right).is_nil()) {
        no_clause();
    }
    const auto rest = appended(left_ended ? walk.right : walk.left);
    walk.result.insert(walk.result.end(), rest.begin(), rest.end());
    return need(TermFactory(context).list(walk.result));
}

// A key position argument: a positive integer, else function_clause.
std::size_t key_position(const Count &position) {
    const auto key = position.natural();
    if (!key || *key == 0) {
        no_clause();
    }
    return *key;
}

// merge(List1, List2).
Term merge2(ProcessContext &context, const Term &first, const Term &second) {
    return merged(context, first, second, Ordering{}, false);
}

// merge(ListOfLists): earlier lists first among equal elements.
Term merge1(ProcessContext &context, const Term &lists) {
    auto result = need(TermFactory(context).nil());
    for (const auto &list : proper(lists)) {
        result = merged(context, result, list, Ordering{}, false);
    }
    return result;
}

// merge3(List1, List2, List3) and umerge3(List1, List2, List3).
template <bool Unique> Term merge3(ProcessContext &context, const Term &first, const Term &second, const Term &third) {
    return merged(context, merged(context, first, second, Ordering{}, Unique), third, Ordering{}, Unique);
}

// keymerge(N, TupleList1, TupleList2) and ukeymerge(N, TupleList1, TupleList2).
template <bool Unique>
Term keymerge(ProcessContext &context, const Count &position, const Term &first, const Term &second) {
    return merged(context, first, second, Ordering(key_position(position)), Unique);
}

// ukeysort(N, TupleList): sorted by element N, keeping the first of the tuples whose keys compare equal; a list of
// at most one element is returned as it is.
TermResult<Term> ukeysort(ProcessContext &context, const Count &position, const Term &list) {
    const Ordering ordering(key_position(position));
    auto elements = proper(list);
    if (elements.size() < 2) {
        return list;
    }
    std::ranges::stable_sort(elements,
                             [&](const Term &left, const Term &right) { return ordering.compare(left, right) < 0; });
    const auto [first, last] = std::ranges::unique(
        elements, [&](const Term &left, const Term &right) { return ordering.compare(left, right) == 0; });
    elements.erase(first, last);
    return TermFactory(context).list(elements);
}

constexpr std::array MERGE_BUILTINS{
    typed_entry<merge1>("lists", "merge"),
    typed_entry<merge2>("lists", "merge"),
    typed_entry<merge3<false>>("lists", "merge3"),
    typed_entry<merge3<true>>("lists", "umerge3"),
    typed_entry<keymerge<false>>("lists", "keymerge"),
    typed_entry<keymerge<true>>("lists", "ukeymerge"),
    typed_entry<ukeysort>("lists", "ukeysort"),
};
} // namespace
} // namespace clause::runtime::builtins::lists

namespace clause::runtime {
std::span<const BuiltinEntry> lists_merge_builtins() noexcept { return builtins::lists::MERGE_BUILTINS; }
} // namespace clause::runtime
