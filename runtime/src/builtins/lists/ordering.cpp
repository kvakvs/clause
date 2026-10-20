#include "../../terms/atom_table.hpp"
#include "../../terms/word_order.hpp"
#include "list_walk.hpp"
#include <algorithm>
#include <array>
#include <clause/abi/equality.hpp>
#include <functional>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <terms.hpp>
#include <type_traits>
#include <utility>

// The lists builtins that order elements by term order (plan step 41T, docs/builtins.md#lists-module): max/1,
// min/1, sort/1, usort/1 and umerge/1,2. Of elements comparing equal (==), the first one wins, as in OTP.
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

// Sort the element words when every one is a small integer; false, leaving them as they were, otherwise. Equal
// small integers are the same word, so their order needs no stable sort. Added for the lists:sort builtin.
bool sort_small(std::vector<Word> &elements) {
    constexpr Word tag = abi::v1::small_integer_tag;
    if (!std::ranges::all_of(elements, [](Word element) { return (element & tag) == tag; })) {
        return false;
    }
    std::ranges::sort(elements, std::ranges::less{},
                      [](Word element) { return static_cast<std::make_signed_t<Word>>(element); });
    return true;
}

// Sort the element words by their spellings, each read once, when every one is an atom; false, leaving them as they
// were, otherwise. Equal spellings are the same atom. Added for the lists:sort builtin.
bool sort_atoms(std::vector<Word> &elements) {
    std::vector<std::pair<std::string_view, Word>> keyed;
    keyed.reserve(elements.size());
    for (const auto element : elements) {
        const auto spelling = detail::is_atom_word(element) ? detail::atom_table::spelling(element) : std::nullopt;
        if (!spelling) {
            return false;
        }
        keyed.emplace_back(*spelling, element);
    }
    std::ranges::sort(keyed, std::ranges::less{}, &std::pair<std::string_view, Word>::first);
    std::ranges::copy(keyed | std::views::values, elements.begin());
    return true;
}

// Sort the element words in term order; elements comparing equal (1 and 1.0) keep their order, as in OTP. Added for
// the lists:sort builtin.
void sort_elements(ProcessContext &context, std::vector<Word> &elements) {
    if (!sort_small(elements) && !sort_atoms(elements)) {
        auto &heap = context.heap();
        std::ranges::stable_sort(elements, [&](Word left, Word right) { return before(heap, left, right); });
    }
}

// The list of the sorted element words.
TermResult<Term> sorted_list(ProcessContext &context, std::span<const Word> elements) {
    return TermFactory(context).list_of_words(elements, need(Term::from_word(abi::v1::empty_list)));
}

// sort(List): sorted in term order, stable. Added for the lists:sort builtin.
TermResult<Term> sort(ProcessContext &context, const Term &list) {
    auto elements = proper_words(list);
    sort_elements(context, elements);
    return sorted_list(context, elements);
}

// usort(List): sorted, keeping the first of the elements comparing equal.
TermResult<Term> usort(ProcessContext &context, const Term &list) {
    auto elements = proper_words(list);
    sort_elements(context, elements);
    // Sorted neighbours compare equal unless the first sorts before the second.
    auto &heap = context.heap();
    const auto [first, last] =
        std::ranges::unique(elements, [&](Word left, Word right) { return !before(heap, left, right); });
    elements.erase(first, last);
    return sorted_list(context, elements);
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
    typed_entry<max>("lists", "max"),        typed_entry<min>("lists", "min"),
    typed_entry<sort>("lists", "sort"),      typed_entry<usort>("lists", "usort"),
    typed_entry<umerge2>("lists", "umerge"), typed_entry<umerge>("lists", "umerge"),
};
} // namespace
} // namespace clause::runtime::builtins::lists

namespace clause::runtime {
std::span<const BuiltinEntry> lists_ordering_builtins() noexcept { return builtins::lists::ORDERING_BUILTINS; }
} // namespace clause::runtime
