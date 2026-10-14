#include "../terms/structural_order.hpp"
#include "typed.hpp"
#include <array>
#include <terms.hpp>
#include <vector>

// The erts_internal functions stdlib needs (plan step 41N, docs/builtins.md#erts-internal): cmp_term/2, the exact
// term order maps sort keys by, and map_next/3, the maps iterator protocol. A map's entries are kept in exact key
// order, so iteration follows it; an iterator that starts at a position continues over the remaining keys.
namespace clause::runtime::builtins {
namespace {
// The atom `name`.
Term atom(ProcessContext &context, std::string_view name) { return need(TermFactory(context).atom(name)); }

// cmp_term(A, B): -1, 0 or 1 in exact term order, where an integer orders before an equal float.
Term cmp_term(ProcessContext &, const Term &left, const Term &right) {
    const auto order = need(detail::structural_order(left, right, true));
    return need(Term::from_word(need(encode_integer(order < 0 ? -1 : (order > 0 ? 1 : 0)))));
}

// A pair {First, Second}.
Term pair(ProcessContext &context, const Term &first, const Term &second) {
    const std::array parts{first, second};
    return need(TermFactory(context).tuple(parts));
}

// {Key, Value, [Keys | Map]}: one association and the iterator over the keys after it.
TermResult<Term> step(ProcessContext &context, const std::pair<Term, Term> &entry, const Term &keys, const Term &map) {
    const std::array parts{entry.first, entry.second, need(TermFactory(context).cons(keys, map))};
    return TermFactory(context).tuple(parts);
}

// The remaining keys of an iterator over `entries` that starts after position `at`.
Term keys_after(ProcessContext &context, const std::vector<std::pair<Term, Term>> &entries, std::size_t at) {
    std::vector<Term> keys;
    for (std::size_t index = at + 1; index < entries.size(); ++index) {
        keys.push_back(entries[index].first);
    }
    return need(TermFactory(context).list(keys));
}

// The next association of an iterator at position `at`: none past the last entry.
TermResult<Term> next_at(ProcessContext &context, const Term &map, std::int64_t at) {
    const auto entries = need(map.map_entries());
    if (at < 0) {
        bad_argument();
    }
    if (static_cast<std::size_t>(at) >= entries.size()) {
        return atom(context, "none");
    }
    const auto index = static_cast<std::size_t>(at);
    return step(context, entries[index], keys_after(context, entries, index), map);
}

// The next association of an iterator over a key list: none when it is empty, badarg for a key the map lacks.
TermResult<Term> next_key(ProcessContext &context, const Term &map, const Term &keys) {
    if (keys.is_nil()) {
        return atom(context, "none");
    }
    if (!keys.is_cons()) {
        bad_argument();
    }
    const auto key = need(keys.head());
    const auto value = need(map.map_find(key));
    if (!value) {
        bad_argument();
    }
    return step(context, {key, *value}, need(keys.tail()), map);
}

// Every association from position `at`, in key order, followed by `accumulated`.
TermResult<Term> collect(ProcessContext &context, const Term &map, std::int64_t at, const Term &accumulated) {
    const auto entries = need(map.map_entries());
    if (at < 0) {
        bad_argument();
    }
    std::vector<Term> pairs;
    for (std::size_t index = static_cast<std::size_t>(at); index < entries.size(); ++index) {
        pairs.push_back(pair(context, entries[index].first, entries[index].second));
    }
    return TermFactory(context).list(pairs, accumulated);
}

// map_next(Path, Map, iterator | Acc): the next association and iterator, or none; with a list accumulator, every
// association from Path prepended to it.
TermResult<Term> map_next(ProcessContext &context, const Term &path, const Term &map, const Term &mode) {
    if (!map.is_map()) {
        bad_argument();
    }
    const auto position = small(path.word());
    if (mode.is_list()) {
        if (!position) {
            bad_argument();
        }
        return collect(context, map, *position, mode);
    }
    if (!mode.is_atom() || need(mode.atom_spelling()) != "iterator") {
        bad_argument();
    }
    return position ? next_at(context, map, *position) : next_key(context, map, path);
}

constexpr std::array ERTS_INTERNAL_BUILTINS{
    typed_entry<cmp_term>("erts_internal", "cmp_term"),
    typed_entry<map_next>("erts_internal", "map_next"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> erts_internal_builtins() noexcept { return builtins::ERTS_INTERNAL_BUILTINS; }
} // namespace clause::runtime
