#include "typed.hpp"
#include <array>
#include <terms.hpp>

// The maps builtins (plan step 41X, docs/builtins.md#maps-module): get/3, is_key/2, remove/2, new/0, size/1, merge/2,
// take/2, update/3, with/2, without/2 and from_keys/2, OTP's BIFs and NIF-backed functions, with their errors: a
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
        for (; rest.is_cons(); rest = need(rest.tail())) {
            result.push_back(need(rest.head()));
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
    return TermFactory(context).integer(static_cast<std::int64_t>(need(map.map().map_size())));
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
        return factory.atom("error");
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
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> maps_builtins() noexcept { return builtins::MAPS_BUILTINS; }
} // namespace clause::runtime
