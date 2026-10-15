#include "structural_order.hpp"
#include "../memory/heap_object.hpp"
#include "bitstrings.hpp"
#include "floats.hpp"
#include "funs.hpp"
#include "identities.hpp"
#include "maps.hpp"
#include "records.hpp"
#include <algorithm>
#include <array>
#include <new>
#include <stdexcept>

namespace clause::runtime::detail {
namespace {
struct Pair {
    // Each queued comparison owns its children; map keys always use exact ordering independently of values.
    Term left;
    Term right;
    bool exact;
};

// Preserve Erlang category order independently of representation tags or pointer addresses.
TermResult<unsigned> rank(const Term &term) {
    if (term.is_number()) {
        return 0;
    }
    static constexpr std::array<std::pair<TermKind, unsigned>, 12> ranks{{{TermKind::atom, 1},
                                                                          {TermKind::local_reference, 2},
                                                                          {TermKind::function, 3},
                                                                          {TermKind::local_port, 4},
                                                                          {TermKind::local_pid, 5},
                                                                          {TermKind::tuple, 6},
                                                                          {TermKind::empty_tuple, 6},
                                                                          {TermKind::native_record, 7},
                                                                          {TermKind::map, 8},
                                                                          {TermKind::empty_list, 9},
                                                                          {TermKind::list, 10},
                                                                          {TermKind::bitstring, 11}}};
    const auto kind = term.kind();
    const auto found = std::ranges::find(ranks, kind, &std::pair<TermKind, unsigned>::first);
    if (found == ranks.end()) {
        return std::unexpected(TermError::invalid_encoding);
    }
    return found->second;
}

// Exact host atom equality preserves runtime identity; Erlang ordering uses Unicode spelling.
int atoms(const Term &left, const Term &right, bool exact) {
    const auto lhs = left.atom_spelling().value();
    const auto rhs = right.atom_spelling().value();
    if (lhs != rhs) {
        return lhs < rhs ? -1 : 1;
    }
    if (exact && left.word() != right.word()) {
        return left.word() < right.word() ? -1 : 1;
    }
    return 0;
}

// Scalar comparisons allocate only when arbitrary integer intermediates need storage.
TermResult<int> scalar(const Pair &values) {
    if (values.left.is_atom()) {
        return atoms(values.left, values.right, values.exact);
    }
    if (values.left.is_number()) {
        return numeric_order(values.left, values.right, values.exact);
    }
    return 0;
}

// Tuple arity precedes lexicographic fields; reverse scheduling visits the first field first.
TermResult<int> tuples(const Pair &values, std::vector<Pair> &pending) {
    const auto lhs = values.left.tuple_size().value();
    const auto rhs = values.right.tuple_size().value();
    if (lhs != rhs) {
        return lhs < rhs ? -1 : 1;
    }
    for (std::size_t i = lhs; i > 0; --i) {
        pending.push_back(
            {values.left.tuple_element(i - 1).value(), values.right.tuple_element(i - 1).value(), values.exact});
    }
    return 0;
}

// Cons comparison checks heads before arbitrary tails, preserving proper and improper list semantics.
TermResult<int> lists(const Pair &values, std::vector<Pair> &pending) {
    pending.push_back({values.left.tail().value(), values.right.tail().value(), values.exact});
    pending.push_back({values.left.head().value(), values.right.head().value(), values.exact});
    return 0;
}

// Canonical storage orders exact keys; compare all keys before any values, after comparing map sizes.
TermResult<int> maps(const Pair &values, std::vector<Pair> &pending) {
    const auto lhs = values.left.map_size().value();
    const auto rhs = values.right.map_size().value();
    if (lhs != rhs) {
        return lhs < rhs ? -1 : 1;
    }
    for (std::size_t i = lhs; i > 0; --i) {
        pending.push_back(
            {map_entry(values.left, i - 1)->second, map_entry(values.right, i - 1)->second, values.exact});
    }
    for (std::size_t i = lhs; i > 0; --i) {
        pending.push_back({map_entry(values.left, i - 1)->first, map_entry(values.right, i - 1)->first, true});
    }
    return 0;
}

// Order two atom words of records by spelling.
int atom_words(const Pair &values, Word left, Word right) {
    return atoms(TermAccess::child(values.left, left).value(), TermAccess::child(values.right, right).value(), false);
}

// Definitions order by module, name, export flag (false first), then field count.
int identities(const Pair &values, const RecordDefinition &lhs, const RecordDefinition &rhs) {
    if (const auto order = atom_words(values, lhs.module, rhs.module); order != 0) {
        return order;
    }
    if (const auto order = atom_words(values, lhs.name, rhs.name); order != 0) {
        return order;
    }
    if (lhs.exported != rhs.exported) {
        return lhs.exported ? 1 : -1;
    }
    if (lhs.fields.size() != rhs.fields.size()) {
        return lhs.fields.size() < rhs.fields.size() ? -1 : 1;
    }
    return 0;
}

// After their identities, definitions order by field names in definition order.
int definitions(const Pair &values, const RecordDefinition &lhs, const RecordDefinition &rhs) {
    if (const auto order = identities(values, lhs, rhs); order != 0) {
        return order;
    }
    for (std::size_t i = 0; i < lhs.fields.size(); ++i) {
        if (const auto order = atom_words(values, lhs.fields[i], rhs.fields[i]); order != 0) {
            return order;
        }
    }
    return 0;
}

// Records compare their captured definitions, then their values in definition order, first field first.
TermResult<int> records(const Pair &values, std::vector<Pair> &pending) {
    const auto lhs = record_view(values.left).value();
    const auto rhs = record_view(values.right).value();
    if (lhs.definition != rhs.definition) {
        if (const auto order = definitions(values, *lhs.definition, *rhs.definition); order != 0) {
            return order;
        }
    }
    for (std::size_t i = lhs.values.size(); i > 0; --i) {
        pending.push_back({TermAccess::child(values.left, lhs.values[i - 1]).value(),
                           TermAccess::child(values.right, rhs.values[i - 1]).value(), values.exact});
    }
    return 0;
}

// Order two counts or indices.
int sizes(std::size_t left, std::size_t right) {
    if (left == right) {
        return 0;
    }
    return left < right ? -1 : 1;
}

// External funs order by module, function and arity (OTP erts_cmp).
int external_funs(const Pair &values, const FunDefinition &lhs, const FunDefinition &rhs) {
    if (const auto order = atom_words(values, lhs.module, rhs.module); order != 0) {
        return order;
    }
    if (const auto order = atom_words(values, lhs.function, rhs.function); order != 0) {
        return order;
    }
    return sizes(lhs.arity, rhs.arity);
}

// Local funs order before external ones; external funs by module, function and arity.
int fun_kinds(const Pair &values, const FunDefinition &left, const FunDefinition &right) {
    if (left.external && right.external) {
        return external_funs(values, left, right);
    }
    return left.external ? 1 : -1;
}

// Local funs order by module and index, then by their captured values in order.
TermResult<int> funs(const Pair &values, std::vector<Pair> &pending) {
    const auto lhs = fun_view(values.left).value();
    const auto rhs = fun_view(values.right).value();
    const auto &left = *lhs.definition;
    const auto &right = *rhs.definition;
    if (left.external || right.external) {
        return fun_kinds(values, left, right);
    }
    if (&left != &right) {
        const auto order = atom_words(values, left.module, right.module);
        return order != 0 ? order : sizes(left.index, right.index);
    }
    for (std::size_t i = lhs.captures.size(); i > 0; --i) {
        pending.push_back({TermAccess::child(values.left, lhs.captures[i - 1]).value(),
                           TermAccess::child(values.right, rhs.captures[i - 1]).value(), values.exact});
    }
    return 0;
}

TermResult<int> same_rank(const Pair &values, std::vector<Pair> &pending);

// Dispatch only validated parents; extracted children inherit their live owning storage. One word names one term,
// so identical words are equal without a walk, as in ERTS.
TermResult<int> step(const Pair &values, std::vector<Pair> &pending) {
    if (values.left.word() == values.right.word()) {
        return 0;
    }
    const auto lhs = rank(values.left);
    const auto rhs = rank(values.right);
    if (!lhs || !rhs) {
        return std::unexpected(TermError::invalid_encoding);
    }
    if (*lhs != *rhs) {
        return *lhs < *rhs ? -1 : 1;
    }
    return same_rank(values, pending);
}

// Same-category containers queue their children; bitstrings and scalars compare at once.
TermResult<int> same_rank(const Pair &values, std::vector<Pair> &pending) {
    if (values.left.is_tuple()) {
        return tuples(values, pending);
    }
    if (values.left.is_cons()) {
        return lists(values, pending);
    }
    if (values.left.is_map()) {
        return maps(values, pending);
    }
    if (values.left.is_native_record()) {
        return records(values, pending);
    }
    if (values.left.is_function()) {
        return funs(values, pending);
    }
    if (values.left.is_bitstring()) {
        return bit_order(values.left, values.right);
    }
    if (values.left.is_pid() || values.left.is_port() || values.left.is_reference()) {
        return identity_order(values.left, values.right);
    }
    return scalar(values);
}

// Walk pending pairs depth first, with no source-depth recursion.
TermResult<int> compare(const Term &left, const Term &right, bool exact) {
    std::vector<Pair> pending;
    const auto initial = step({left, right, exact}, pending);
    if (!initial || *initial != 0) {
        return initial;
    }
    while (!pending.empty()) {
        auto values = std::move(pending.back());
        pending.pop_back();
        const auto result = step(values, pending);
        if (!result || *result != 0) {
            return result;
        }
    }
    return 0;
}
} // namespace

TermResult<int> structural_order(const Term &left, const Term &right, bool exact) noexcept {
    if (const auto order = small_order(left.word(), right.word())) {
        return *order;
    }
    for (const auto *value : {&left, &right}) {
        if (const auto checked = TermAccess::validate(*value); !checked) {
            return std::unexpected(checked.error());
        }
    }
    try {
        return compare(left, right, exact);
    } catch (const std::bad_alloc &) {
        return std::unexpected(TermError::out_of_memory);
    } catch (const std::length_error &) {
        return std::unexpected(TermError::resource_limit);
    } catch (...) {
        return std::unexpected(TermError::invalid_encoding);
    }
}

} // namespace clause::runtime::detail
