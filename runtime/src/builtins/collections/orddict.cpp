#include "collections.hpp"
#include <algorithm>
#include <array>

// The orddict builtins (plan 21 step LIB1, docs/builtins.md#collections): an ordered dictionary is a list of
// {Key, Value} pairs sorted by key, keys comparing by Erlang's < and ==. Each walks the list as OTP's clauses do: an
// element that is not a pair where a clause needs one, or an improper tail, raises function_clause.
namespace clause::runtime::builtins::collections {
namespace {
// One step of a walk for a key: the pair at the head, its value and how the key orders against the pair's key.
struct Step {
    Term pair;
    Term value;
    int order;
};

// The head of a dictionary rest as a step for `key`; function_clause unless it is a pair.
Step step(const Key &key, const Term &rest) {
    const auto pair = need(rest.head());
    const auto parts = pair_of(pair);
    if (!parts) {
        no_clause();
    }
    return {pair, parts->second, compare(key.term, parts->first)};
}

// function_clause unless a walk that found no equal key ended at [].
void ended(const Term &rest) {
    if (!rest.is_nil()) {
        no_clause();
    }
}

Term atom(ProcessContext &context, std::string_view name) { return need(TermFactory(context).atom(name)); }

Term pair(ProcessContext &context, const Key &key, const Term &value) {
    const std::array parts{key.term, value};
    return need(TermFactory(context).tuple(parts));
}

// is_key(Key, Orddict).
Term is_key(ProcessContext &context, const Key &key, const List &dict) {
    auto rest = dict.term;
    for (; rest.is_cons(); rest = need(rest.tail())) {
        const auto found = step(key, rest);
        if (found.order <= 0) {
            return need(TermFactory(context).boolean(found.order == 0));
        }
    }
    ended(rest);
    return need(TermFactory(context).boolean(false));
}

// fetch(Key, Orddict): the value; function_clause when Key is absent.
Term fetch(ProcessContext &, const Key &key, const List &dict) {
    for (auto rest = dict.term; rest.is_cons(); rest = need(rest.tail())) {
        const auto found = step(key, rest);
        if (found.order == 0) {
            return found.value;
        }
        if (found.order < 0) {
            break;
        }
    }
    no_clause();
}

// find(Key, Orddict): {ok, Value} or error.
Term find(ProcessContext &context, const Key &key, const List &dict) {
    auto rest = dict.term;
    for (; rest.is_cons(); rest = need(rest.tail())) {
        const auto found = step(key, rest);
        if (found.order == 0) {
            const std::array parts{atom(context, "ok"), found.value};
            return need(TermFactory(context).tuple(parts));
        }
        if (found.order < 0) {
            return atom(context, "error");
        }
    }
    ended(rest);
    return atom(context, "error");
}

// fetch_keys(Orddict): the keys in order.
Term fetch_keys(ProcessContext &context, const List &dict) {
    Prefix keys;
    auto rest = dict.term;
    for (; rest.is_cons(); rest = need(rest.tail())) {
        const auto parts = pair_of(need(rest.head()));
        if (!parts) {
            no_clause();
        }
        keys.push(parts->first);
    }
    ended(rest);
    return keys.done(context);
}

// from_list(List): the pairs sorted by key, the last of pairs with equal keys kept; a one-pair list as it is.
TermResult<Term> from_list(ProcessContext &context, const List &list) {
    std::vector<Term> pairs;
    auto rest = list.term;
    for (; rest.is_cons(); rest = need(rest.tail())) {
        pairs.push_back(need(rest.head()));
        if (!pair_of(pairs.back())) {
            no_clause();
        }
    }
    ended(rest);
    if (pairs.size() <= 1) {
        return list.term;
    }
    std::ranges::reverse(pairs);
    const auto key = [](const Term &pair) { return need(pair.tuple_element(0)); };
    std::ranges::stable_sort(pairs, [&](const Term &a, const Term &b) { return compare(key(a), key(b)) < 0; });
    const auto [first, last] =
        std::ranges::unique(pairs, [&](const Term &a, const Term &b) { return compare(key(a), key(b)) == 0; });
    pairs.erase(first, last);
    return TermFactory(context).list(pairs);
}

// erase(Key, Orddict1): the dictionary without Key.
Term erase(ProcessContext &context, const Key &key, const List &dict) {
    Prefix passed;
    auto rest = dict.term;
    for (; rest.is_cons(); rest = need(rest.tail())) {
        const auto found = step(key, rest);
        if (found.order <= 0) {
            return passed.then(context, found.order == 0 ? need(rest.tail()) : rest);
        }
        passed.push(found.pair);
    }
    ended(rest);
    return passed.done(context);
}

// take(Key, Orddict): {Value, Orddict without Key}, or error.
Term take(ProcessContext &context, const Key &key, const List &dict) {
    Prefix passed;
    auto rest = dict.term;
    for (; rest.is_cons(); rest = need(rest.tail())) {
        const auto found = step(key, rest);
        if (found.order == 0) {
            const std::array parts{found.value, passed.then(context, need(rest.tail()))};
            return need(TermFactory(context).tuple(parts));
        }
        if (found.order < 0) {
            return atom(context, "error");
        }
        passed.push(found.pair);
    }
    ended(rest);
    return atom(context, "error");
}

// The dictionary with `key` changed: an existing pair replaced by {Key, existing(OldValue)}, else {Key, missing}
// inserted before the first greater key or at the end.
template <typename Existing>
Term changed(ProcessContext &context, const Key &key, const List &dict, const Value &missing, Existing existing) {
    Prefix passed;
    auto rest = dict.term;
    for (; rest.is_cons(); rest = need(rest.tail())) {
        const auto found = step(key, rest);
        if (found.order == 0) {
            passed.push(pair(context, key, existing(found.value)));
            return passed.then(context, need(rest.tail()));
        }
        if (found.order < 0) {
            passed.push(pair(context, key, missing.term));
            return passed.then(context, rest);
        }
        passed.push(found.pair);
    }
    ended(rest);
    passed.push(pair(context, key, missing.term));
    return passed.done(context);
}

// store(Key, Value, Orddict1): {Key, Value}, replacing an existing pair.
Term store(ProcessContext &context, const Key &key, const Value &value, const List &dict) {
    return changed(context, key, dict, value, [&](const Term &) { return value.term; });
}

// append(Key, Value, Orddict1): {Key, Old ++ [Value]}, or {Key, [Value]}.
Term append_value(ProcessContext &context, const Key &key, const Value &value, const List &dict) {
    TermFactory factory(context);
    const Value single{need(factory.cons(value.term, need(factory.nil())))};
    return changed(context, key, dict, single, [&](const Term &old) { return append(context, {old}, single.term); });
}

// append_list(Key, ValList, Orddict1): {Key, Old ++ ValList}, or {Key, ValList}.
Term append_list(ProcessContext &context, const Key &key, const Value &values, const List &dict) {
    return changed(context, key, dict, values, [&](const Term &old) { return append(context, {old}, values.term); });
}

// update_counter(Key, Increment, Orddict1): {Key, Old + Increment}, or {Key, Increment}.
Term update_counter(ProcessContext &context, const Key &key, const Value &increment, const List &dict) {
    return changed(context, key, dict, increment, [&](const Term &old) { return add(context, old, increment.term); });
}

constexpr std::array ORDDICT_BUILTINS{
    typed_entry<is_key>("orddict", "is_key"),
    typed_entry<fetch>("orddict", "fetch"),
    typed_entry<find>("orddict", "find"),
    typed_entry<fetch_keys>("orddict", "fetch_keys"),
    typed_entry<from_list>("orddict", "from_list"),
    typed_entry<erase>("orddict", "erase"),
    typed_entry<take>("orddict", "take"),
    typed_entry<store>("orddict", "store"),
    typed_entry<append_value>("orddict", "append"),
    typed_entry<append_list>("orddict", "append_list"),
    typed_entry<update_counter>("orddict", "update_counter"),
};
} // namespace
} // namespace clause::runtime::builtins::collections

namespace clause::runtime {
std::span<const BuiltinEntry> orddict_builtins() noexcept { return builtins::collections::ORDDICT_BUILTINS; }
} // namespace clause::runtime
