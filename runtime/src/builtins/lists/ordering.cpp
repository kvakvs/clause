#include "list_walk.hpp"
#include <algorithm>
#include <array>
#include <terms.hpp>

// The lists builtins that order elements by term order (plan step 41T, docs/builtins.md#lists-module): max/1,
// min/1, usort/1 and umerge/1,2. Of elements comparing equal (==), the first one wins, as in OTP.
namespace clause::runtime::builtins::lists {
namespace {
// max(List): the first greatest element of a non-empty proper list.
Term max(ProcessContext &, const Term &list) {
    const auto elements = proper(list);
    if (elements.empty()) {
        no_clause();
    }
    return std::ranges::max(elements, [](const Term &left, const Term &right) { return before(left, right); });
}

// min(List): the first smallest element.
Term min(ProcessContext &, const Term &list) {
    const auto elements = proper(list);
    if (elements.empty()) {
        no_clause();
    }
    return std::ranges::min(elements, [](const Term &left, const Term &right) { return before(left, right); });
}

// usort(List): sorted, keeping the first of the elements comparing equal.
TermResult<Term> usort(ProcessContext &context, const Term &list) {
    auto elements = proper(list);
    std::ranges::stable_sort(elements, [](const Term &left, const Term &right) { return before(left, right); });
    const auto [first, last] = std::ranges::unique(elements, equal);
    elements.erase(first, last);
    return TermFactory(context).list(elements);
}

// The sorted lists merged so far; of two elements comparing equal, the one merged earlier stays.
class UniqueMerge final {
  public:
    // Merge the sorted elements of one more list.
    void add(const std::vector<Term> &later) {
        std::vector<Term> result;
        std::size_t from_later = 0;
        for (const auto &element : merged_) {
            for (; from_later < later.size() && before(later[from_later], element); ++from_later) {
                result.push_back(later[from_later]);
            }
            if (from_later < later.size() && equal(later[from_later], element)) {
                ++from_later;
            }
            result.push_back(element);
        }
        result.insert(result.end(), later.begin() + static_cast<std::ptrdiff_t>(from_later), later.end());
        merged_ = std::move(result);
    }

    const std::vector<Term> &elements() const noexcept { return merged_; }

  private:
    // The merged elements in order.
    std::vector<Term> merged_;
};

// umerge(List1, List2).
TermResult<Term> umerge2(ProcessContext &context, const Term &left, const Term &right) {
    UniqueMerge merge;
    merge.add(proper(left));
    merge.add(proper(right));
    return TermFactory(context).list(merge.elements());
}

// umerge(ListOfLists): earlier lists win among elements comparing equal.
TermResult<Term> umerge(ProcessContext &context, const Term &lists) {
    UniqueMerge merge;
    for (const auto &list : proper(lists)) {
        merge.add(proper(list));
    }
    return TermFactory(context).list(merge.elements());
}

constexpr std::array ORDERING_BUILTINS{
    typed_entry<max>("lists", "max"),       typed_entry<min>("lists", "min"),
    typed_entry<usort>("lists", "usort"),   typed_entry<umerge2>("lists", "umerge"),
    typed_entry<umerge>("lists", "umerge"),
};
} // namespace
} // namespace clause::runtime::builtins::lists

namespace clause::runtime {
std::span<const BuiltinEntry> lists_ordering_builtins() noexcept { return builtins::lists::ORDERING_BUILTINS; }
} // namespace clause::runtime
