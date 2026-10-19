#include "../memory/heap_object.hpp"
#include "../terms/map_cursor.hpp"
#include "../terms/structural_order.hpp"
#include "typed.hpp"
#include <algorithm>
#include <array>
#include <ranges>
#include <terms.hpp>

// The maps builtins (plan step 41X, docs/builtins.md#maps-module): get/3, is_key/2, remove/2, new/0, size/1, merge/2,
// take/2, update/3, with/2, without/2, from_keys/2, intersect/2 and is_iterator_valid/1, with their errors: a
// non-map is {badmap, Map}, a missing key of update/3 {badkey, Key}, a key list that is not a proper list badarg.
namespace clause::runtime::builtins {
namespace {
using Entries = std::vector<std::pair<Term, Term>>;

// The map argument of a maps function, checked by map() ({badmap, Term}, not badarg).
struct MapOperand {
    // The argument as passed.
    Term term;

    // The argument, which must be a map.
    Term map() const {
        if (!term.is_map()) {
            throw BuiltinFailure{.reason = abi::v1::ErrorReason::badmap, .payload = term.word()};
        }
        return term;
    }
};

// A key list argument, checked by keys() (badarg unless a proper list).
struct KeyList {
    // The argument as passed.
    Term term;

    // The keys of the proper list.
    std::vector<Term> keys() const {
        std::vector<Term> result;
        auto rest = term;
        for (; rest.is_cons(); rest = rest.tail_unchecked()) {
            result.push_back(rest.head_unchecked());
        }
        if (!rest.is_nil()) {
            bad_argument();
        }
        return result;
    }
};
} // namespace

// Any term as a maps operand; the builtin checks it.
template <> struct Argument<MapOperand> {
    static TermResult<std::optional<MapOperand>> convert(const Term &term) { return MapOperand{term}; }
};

template <> struct Argument<KeyList> {
    static TermResult<std::optional<KeyList>> convert(const Term &term) { return KeyList{term}; }
};

namespace {
// The associations except the one whose key is exactly `key`.
Entries entries_without(Entries entries, const Term &key) {
    std::erase_if(entries, [&](const auto &entry) { return need(entry.first.exactly_equal(key)); });
    return entries;
}

// get(Key, Map, Default).
Term get3(ProcessContext &, const Term &key, const MapOperand &map, const Term &fallback) {
    const auto found = need(map.map().map_find(key));
    return found ? *found : fallback;
}

// is_key(Key, Map).
TermResult<Term> is_key(ProcessContext &context, const Term &key, const MapOperand &map) {
    return TermFactory(context).boolean(need(map.map().map_contains(key)));
}

// remove(Key, Map): Map without Key; Map itself when Key is absent.
TermResult<Term> remove(ProcessContext &context, const Term &key, const MapOperand &operand) {
    auto map = operand.map();
    if (!need(map.map_contains(key))) {
        return map;
    }
    return TermFactory(context).map(entries_without(need(map.map_entries()), key));
}

// new().
TermResult<Term> new_map(ProcessContext &context) { return TermFactory(context).map({}); }

// size(Map).
TermResult<Term> size(ProcessContext &context, const MapOperand &map) {
    return TermFactory(context).integer(static_cast<std::int64_t>(map.map().map_size_unchecked()));
}

// merge(Map1, Map2): the associations of both, Map2's winning for keys in both.
TermResult<Term> merge(ProcessContext &context, const MapOperand &first, const MapOperand &second) {
    auto entries = need(first.map().map_entries());
    const auto later = need(second.map().map_entries());
    entries.insert(entries.end(), later.begin(), later.end());
    return TermFactory(context).map(entries);
}

// take(Key, Map): {Value, Map without Key}, or error.
TermResult<Term> take(ProcessContext &context, const Term &key, const MapOperand &operand) {
    TermFactory factory(context);
    const auto map = operand.map();
    const auto found = need(map.map_find(key));
    if (!found) {
        return factory.atom("error"_atom);
    }
    const std::array parts{*found, need(factory.map(entries_without(need(map.map_entries()), key)))};
    return factory.tuple(parts);
}

// update(Key, Value, Map): Map with the existing Key associated to Value; {badkey, Key} when absent.
TermResult<Term> update(ProcessContext &context, const Term &key, const Term &value, const MapOperand &operand) {
    const auto map = operand.map();
    if (!need(map.map_contains(key))) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badkey, .payload = key.word()};
    }
    auto entries = entries_without(need(map.map_entries()), key);
    entries.emplace_back(key, value);
    return TermFactory(context).map(entries);
}

// with(Keys, Map): the associations of Map whose keys are in Keys.
TermResult<Term> with(ProcessContext &context, const KeyList &keys, const MapOperand &operand) {
    const auto map = operand.map();
    Entries kept;
    for (const auto &key : keys.keys()) {
        if (const auto found = need(map.map_find(key))) {
            kept.emplace_back(key, *found);
        }
    }
    return TermFactory(context).map(kept);
}

// without(Keys, Map): Map without the keys in Keys.
TermResult<Term> without(ProcessContext &context, const KeyList &keys, const MapOperand &operand) {
    TermFactory factory(context);
    auto entries = need(operand.map().map_entries());
    Entries removed;
    for (const auto &key : keys.keys()) {
        removed.emplace_back(key, key);
    }
    const auto removed_map = need(factory.map(removed));
    std::erase_if(entries, [&](const auto &entry) { return need(removed_map.map_contains(entry.first)); });
    return factory.map(entries);
}

// from_keys(Keys, Value): every key of Keys associated to Value.
TermResult<Term> from_keys(ProcessContext &context, const KeyList &keys, const Term &value) {
    Entries entries;
    for (const auto &key : keys.keys()) {
        entries.emplace_back(key, value);
    }
    return TermFactory(context).map(entries);
}

// intersect(Map1, Map2): the associations of Map2 whose keys Map1 has, walking the smaller map (maps completion).
TermResult<Term> intersect(ProcessContext &context, const MapOperand &first, const MapOperand &second) {
    const auto left = first.map();
    const auto right = second.map();
    const bool walk_left = left.map_size_unchecked() <= right.map_size_unchecked();
    const auto &probed = walk_left ? right : left;
    Entries kept;
    for (const auto &[key, value] : need((walk_left ? left : right).map_entries())) {
        if (const auto found = need(probed.map_find(key))) {
            kept.emplace_back(key, walk_left ? *found : value);
        }
    }
    return TermFactory(context).map(kept);
}

// Whether `iterator` is a [Path | Map] that map_next/3 walks to its end: a position from 0, or a proper list of
// Map's keys (maps completion).
bool valid_path(const Term &iterator) {
    if (!iterator.is_cons() || !iterator.tail_unchecked().is_map()) {
        return false;
    }
    const auto map = iterator.tail_unchecked();
    auto keys = iterator.head_unchecked();
    if (const auto position = small(keys.word())) {
        return *position >= 0;
    }
    for (; keys.is_cons(); keys = keys.tail_unchecked()) {
        if (!need(map.map_contains(keys.head_unchecked()))) {
            return false;
        }
    }
    return keys.is_nil();
}

// is_iterator_valid(MaybeIter): whether maps:next/1 walks MaybeIter to none; a {Key, Value, Next} step is valid when
// Next is (maps completion).
TermResult<Term> is_iterator_valid(ProcessContext &context, const Term &maybe) {
    auto iterator = maybe;
    while (iterator.is_tuple() && iterator.tuple_size_unchecked() == 3) {
        iterator = iterator.tuple_element_unchecked(2);
    }
    const bool none = iterator.is_atom() && iterator.atom_spelling_unchecked() == "none";
    return TermFactory(context).boolean(none || valid_path(iterator));
}

// The values of a run of {Key, Value} pairs listed last element first, in source order (maps completion).
std::vector<Term> run_values(std::span<const Term> run) {
    std::vector<Term> values;
    values.reserve(run.size());
    for (const auto &pair : run | std::views::reverse) {
        values.push_back(need(pair.tuple_element(1)));
    }
    return values;
}

// clause_maps:groups(Pairs): the map of maps:groups_from_list/2,3 from the {Key, Value} pairs of the list's elements,
// last element first, so the funs run in OTP's order: each key with its values in source order (maps completion).
TermResult<Term> groups(ProcessContext &context, const KeyList &list) {
    auto pairs = list.keys();
    const auto key_order = [](const Term &left, const Term &right) {
        return need(detail::structural_order(need(left.tuple_element(0)), need(right.tuple_element(0)), true));
    };
    std::ranges::stable_sort(pairs, [&](const Term &left, const Term &right) { return key_order(left, right) < 0; });
    TermFactory factory(context);
    Entries entries;
    for (auto run = pairs.begin(); run != pairs.end();) {
        const auto end = std::find_if(run, pairs.end(), [&](const Term &pair) { return key_order(pair, *run) != 0; });
        entries.emplace_back(need(run->tuple_element(0)), need(factory.list(run_values({run, end}))));
        run = end;
    }
    return factory.map(entries);
}

// clause_maps:entries(Map): {K1, V1, K2, V2, ...} in key order, read by a map cursor, so maps:fold walks one tuple
// instead of a list of pairs (map cursors).
TermResult<Term> entries(ProcessContext &context, const MapOperand &map) {
    const auto cell = detail::TermAccess::boxed_words(context.heap(), map.map().word());
    // The empty map is an immediate: it has no cell to walk.
    if (cell.empty()) {
        return TermFactory(context).tuple_of_words({});
    }
    detail::MapCursor cursor(cell);
    std::vector<Word> words;
    words.reserve(2 * cursor.size());
    while (const auto entry = cursor.next()) {
        words.push_back(entry->first);
        words.push_back(entry->second);
    }
    return TermFactory(context).tuple_of_words(words);
}

constexpr std::array MAPS_BUILTINS{
    typed_entry<get3>("maps", "get"),
    typed_entry<is_key>("maps", "is_key"),
    typed_entry<remove>("maps", "remove"),
    typed_entry<new_map>("maps", "new"),
    typed_entry<size>("maps", "size"),
    typed_entry<merge>("maps", "merge"),
    typed_entry<take>("maps", "take"),
    typed_entry<update>("maps", "update"),
    typed_entry<with>("maps", "with"),
    typed_entry<without>("maps", "without"),
    typed_entry<from_keys>("maps", "from_keys"),
    typed_entry<intersect>("maps", "intersect"),
    typed_entry<is_iterator_valid>("maps", "is_iterator_valid"),
    typed_entry<entries>("clause_maps", "entries"),
    typed_entry<groups>("clause_maps", "groups"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> maps_builtins() noexcept { return builtins::MAPS_BUILTINS; }
} // namespace clause::runtime
