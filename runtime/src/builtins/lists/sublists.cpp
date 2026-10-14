#include "list_walk.hpp"
#include <array>
#include <terms.hpp>

// The lists builtins that take or build parts of one list (plan step 41T, docs/builtins.md#lists-module):
// sublist/2,3, droplast/1, nthtail/2, last/1, duplicate/2, join/2 and flatten/1,2. They run to completion.
namespace clause::runtime::builtins::lists {
namespace {
// The first `limit` elements of `rest`, or all of a shorter proper list; function_clause at an improper tail reached
// before the limit.
std::vector<Term> prefix(Term rest, std::size_t limit) {
    std::vector<Term> elements;
    for (; elements.size() < limit && rest.is_cons(); rest = need(rest.tail())) {
        elements.push_back(need(rest.head()));
    }
    if (elements.size() < limit && !rest.is_nil()) {
        no_clause();
    }
    return elements;
}

// sublist(List, Len): the first Len elements.
TermResult<Term> sublist(ProcessContext &context, const Term &list, const Count &length) {
    const auto limit = length.natural();
    if (!limit || !list.is_list()) {
        no_clause();
    }
    return TermFactory(context).list(prefix(list, *limit));
}

// sublist(List, Start, Len): Len elements from position Start (1-based); [] when Start is past the end.
TermResult<Term> sublist_from(ProcessContext &context, const Term &list, const Count &start, const Term &length) {
    const auto first = start.natural();
    if (!first || *first == 0) {
        no_clause();
    }
    auto rest = list;
    for (auto position = *first; position > 1; --position) {
        if (rest.is_nil()) {
            return rest;
        }
        if (!rest.is_cons()) {
            no_clause();
        }
        rest = need(rest.tail());
    }
    return sublist(context, rest, Count{length});
}

// droplast(List): a non-empty proper list without its last element.
TermResult<Term> droplast(ProcessContext &context, const Term &list) {
    auto elements = proper(list);
    if (elements.empty()) {
        no_clause();
    }
    elements.pop_back();
    return TermFactory(context).list(elements);
}

// nthtail(N, List): List after its first N cells, whatever its tail is.
Term nthtail(ProcessContext &, const Count &count, const Term &list) {
    const auto cells = count.natural();
    if (!cells || !list.is_list()) {
        no_clause();
    }
    auto rest = list;
    for (std::size_t walked = 0; walked < *cells; ++walked) {
        if (!rest.is_cons()) {
            no_clause();
        }
        rest = need(rest.tail());
    }
    return rest;
}

// last(List): the last element of a non-empty proper list.
Term last(ProcessContext &, const Term &list) {
    const auto elements = proper(list);
    if (elements.empty()) {
        no_clause();
    }
    return elements.back();
}

// duplicate(N, Elem): N copies of Elem.
TermResult<Term> duplicate(ProcessContext &context, const Count &count, const Term &element) {
    const auto copies = count.natural();
    if (!copies) {
        no_clause();
    }
    if (!small(count.term.word())) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::system_limit};
    }
    const std::vector<Word> words(*copies, element.word());
    TermFactory factory(context);
    return factory.list_words(words, need(factory.nil()));
}

// The elements with `separator` between each two.
std::vector<Term> interleave(const std::vector<Term> &elements, const Term &separator) {
    std::vector<Term> joined;
    for (const auto &element : elements) {
        if (!joined.empty()) {
            joined.push_back(separator);
        }
        joined.push_back(element);
    }
    return joined;
}

// join(Sep, List): the elements of List with Sep between each two.
TermResult<Term> join(ProcessContext &context, const Term &separator, const Term &list) {
    return TermFactory(context).list(interleave(proper(list), separator));
}

// The non-list elements of the deep list `list`, depth first; function_clause for an improper list at any depth.
std::vector<Term> flat(const Term &list) {
    std::vector<Term> elements;
    std::vector<Term> pending{list};
    while (!pending.empty()) {
        auto &rest = pending.back();
        if (rest.is_nil()) {
            pending.pop_back();
            continue;
        }
        if (!rest.is_cons()) {
            no_clause();
        }
        const auto head = need(rest.head());
        rest = need(rest.tail());
        if (head.is_list()) {
            pending.push_back(head);
        } else {
            elements.push_back(head);
        }
    }
    return elements;
}

// flatten(DeepList).
TermResult<Term> flatten(ProcessContext &context, const Term &list) {
    if (!list.is_list()) {
        no_clause();
    }
    return TermFactory(context).list(flat(list));
}

// flatten(DeepList, Tail): the flattened list followed by Tail.
TermResult<Term> flatten_onto(ProcessContext &context, const Term &list, const Term &tail) {
    if (!list.is_list() || !tail.is_list()) {
        no_clause();
    }
    return TermFactory(context).list(flat(list), tail);
}

constexpr std::array SUBLIST_BUILTINS{
    typed_entry<sublist>("lists", "sublist"),
    typed_entry<sublist_from>("lists", "sublist"),
    typed_entry<droplast>("lists", "droplast"),
    typed_entry<nthtail>("lists", "nthtail"),
    typed_entry<last>("lists", "last"),
    typed_entry<duplicate>("lists", "duplicate"),
    typed_entry<join>("lists", "join"),
    typed_entry<flatten>("lists", "flatten"),
    typed_entry<flatten_onto>("lists", "flatten"),
};
} // namespace
} // namespace clause::runtime::builtins::lists

namespace clause::runtime {
std::span<const BuiltinEntry> lists_sublist_builtins() noexcept { return builtins::lists::SUBLIST_BUILTINS; }
} // namespace clause::runtime
