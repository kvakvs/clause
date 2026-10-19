#include "../memory/heap_object.hpp"
#include "../terms/map_cursor.hpp"
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
// cmp_term(A, B): -1, 0 or 1 in exact term order, where an integer orders before an equal float.
Term cmp_term(ProcessContext &, const Term &left, const Term &right) {
    const auto order = need(detail::structural_order(left, right, true));
    return need(Term::from_word(need(encode_integer(order < 0 ? -1 : (order > 0 ? 1 : 0)))));
}

// {Key, Value, [Keys | Map]}: one association and the iterator over the keys after it.
TermResult<Term> step(ProcessContext &context, const std::pair<Term, Term> &entry, const Term &keys, const Term &map) {
    const std::array parts{entry.first, entry.second, need(TermFactory(context).cons(keys, map))};
    return TermFactory(context).tuple(parts);
}

// A cursor over a map's entries from position `at` (map cursors); none for the empty map, an immediate.
std::optional<detail::MapCursor> cursor(ProcessContext &context, const Term &map, std::int64_t at) {
    if (at < 0) {
        bad_argument();
    }
    const auto cell = detail::TermAccess::boxed_words(context.heap(), map.word());
    if (cell.empty()) {
        return std::nullopt;
    }
    return detail::MapCursor(cell, static_cast<std::size_t>(at));
}

// The next association of an iterator at position `at`, then the keys after it, read by a map cursor: none past the
// last entry.
TermResult<Term> next_at(ProcessContext &context, const Term &map, std::int64_t at) {
    auto entries = cursor(context, map, at);
    const auto entry = entries ? entries->next() : std::nullopt;
    if (!entry) {
        return atom(context, "none"_atom);
    }
    std::vector<Word> keys;
    while (const auto later = entries->next()) {
        keys.push_back(later->first);
    }
    TermFactory factory(context);
    const auto rest = need(factory.list_of_words(keys, need(factory.nil())));
    return factory.tuple_of_words(std::array{entry->first, entry->second, need(factory.cons(rest, map)).word()});
}

// The next association of an iterator over a key list: none when it is empty, badarg for a key the map lacks.
TermResult<Term> next_key(ProcessContext &context, const Term &map, const Term &keys) {
    if (keys.is_nil()) {
        return atom(context, "none"_atom);
    }
    if (!keys.is_cons()) {
        bad_argument();
    }
    const auto key = keys.head_unchecked();
    const auto value = need(map.map_find(key));
    if (!value) {
        bad_argument();
    }
    return step(context, {key, *value}, keys.tail_unchecked(), map);
}

// Every association from position `at`, in key order, followed by `accumulated`, read by a map cursor.
TermResult<Term> collect(ProcessContext &context, const Term &map, std::int64_t at, const Term &accumulated) {
    auto entries = cursor(context, map, at);
    TermFactory factory(context);
    std::vector<Word> pairs;
    while (const auto entry = entries ? entries->next() : std::nullopt) {
        pairs.push_back(need(factory.tuple_of_words(std::array{entry->first, entry->second})).word());
    }
    return factory.list_of_words(pairs, accumulated);
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
    if (!mode.is_atom() || mode.atom_spelling_unchecked() != "iterator") {
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
