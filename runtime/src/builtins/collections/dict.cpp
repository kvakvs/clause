#include "linear_hash.hpp"
#include <algorithm>
#include <array>

// The dict builtins (plan 21 step LIB2, docs/builtins.md#collections): OTP's dict record over a linear hash table
// (linear_hash.hpp) with [Key | Value] cells in its buckets, keys matching with =:=. A term that is not a dict raises
// {badrecord, Term}, as OTP's record accesses do; size/1 and is_empty/1 raise function_clause. The private
// clause_hashed builtins give dict's and sets' Erlang functions taking funs the entries in OTP's order.
namespace clause::runtime::builtins::collections {
namespace {
using Kind = LinearHash::Kind;

Term atom(ProcessContext &context, std::string_view name) { return need(TermFactory(context).atom(name)); }

Term tuple(ProcessContext &context, const Term &first, const Term &second) {
    const std::array parts{first, second};
    return need(TermFactory(context).tuple(parts));
}

Term cell(ProcessContext &context, const Key &key, const Term &value) {
    return need(TermFactory(context).cons(key.term, value));
}

// The position of the first cell of `key` in a bucket.
std::optional<std::size_t> position(const std::vector<Term> &bucket, const Key &key) {
    const auto found =
        std::ranges::find_if(bucket, [&](const Term &entry) { return same(need(entry.head()), key.term); });
    return found == bucket.end() ? std::nullopt : std::optional{static_cast<std::size_t>(found - bucket.begin())};
}

// The value of `key`, if present.
std::optional<Term> lookup(ProcessContext &context, const Key &key, const List &dict) {
    LinearHash table(context, dict.term, Kind::dict);
    const auto bucket = table.bucket(table.slot(key.term));
    const auto at = position(bucket, key);
    return at ? std::optional{need(bucket[*at].tail())} : std::nullopt;
}

// is_key(Key, Dict).
Term is_key(ProcessContext &context, const Key &key, const List &dict) {
    return need(TermFactory(context).boolean(lookup(context, key, dict).has_value()));
}

// fetch(Key, Dict): badarg when Key is absent.
Term fetch(ProcessContext &context, const Key &key, const List &dict) {
    const auto value = lookup(context, key, dict);
    if (!value) {
        bad_argument();
    }
    return *value;
}

// find(Key, Dict): {ok, Value} or error.
Term find(ProcessContext &context, const Key &key, const List &dict) {
    const auto value = lookup(context, key, dict);
    return value ? tuple(context, atom(context, "ok"), *value) : atom(context, "error");
}

// The cells as OTP's fold prepends them: the reverse of the fold order.
std::vector<Term> listed(ProcessContext &context, const List &dict) {
    auto cells = LinearHash(context, dict.term, Kind::dict).fold_order();
    std::ranges::reverse(cells);
    return cells;
}

// to_list(Dict): {Key, Value} pairs.
Term to_list(ProcessContext &context, const List &dict) {
    Prefix pairs;
    for (const auto &entry : listed(context, dict)) {
        pairs.push(tuple(context, need(entry.head()), need(entry.tail())));
    }
    return pairs.done(context);
}

// fetch_keys(Dict).
Term fetch_keys(ProcessContext &context, const List &dict) {
    Prefix keys;
    for (const auto &entry : listed(context, dict)) {
        keys.push(need(entry.head()));
    }
    return keys.done(context);
}

// size(Dict) and is_empty(Dict): function_clause for anything but a dict.
std::size_t entry_count(ProcessContext &context, const List &dict) {
    if (!LinearHash::is_record(dict.term, Kind::dict)) {
        no_clause();
    }
    return LinearHash(context, dict.term, Kind::dict).size();
}

TermResult<Term> size(ProcessContext &context, const List &dict) {
    return TermFactory(context).integer(static_cast<std::int64_t>(entry_count(context, dict)));
}

Term is_empty(ProcessContext &context, const List &dict) {
    return need(TermFactory(context).boolean(entry_count(context, dict) == 0));
}

// The dict with `key`'s cell changed: an existing cell replaced by [Key | existing(Old)], else [Key | missing]
// appended to the bucket; then OTP's expansion check.
template <typename Existing>
Term changed(ProcessContext &context, const Key &key, const List &dict, const Value &missing, Existing existing) {
    LinearHash table(context, dict.term, Kind::dict);
    const auto slot = table.slot(key.term);
    auto bucket = table.bucket(slot);
    const auto at = position(bucket, key);
    if (at) {
        bucket[*at] = cell(context, key, existing(need(bucket[*at].tail())));
    } else {
        bucket.push_back(cell(context, key, missing.term));
    }
    table.put(slot, bucket);
    table.grow(at ? 0 : 1);
    return table.record();
}

// store(Key, Value, Dict).
Term store(ProcessContext &context, const Key &key, const Value &value, const List &dict) {
    return changed(context, key, dict, value, [&](const Term &) { return value.term; });
}

// append(Key, Value, Dict): Old ++ [Value], or [Value].
Term append_value(ProcessContext &context, const Key &key, const Value &value, const List &dict) {
    TermFactory factory(context);
    const Value single{need(factory.cons(value.term, need(factory.nil())))};
    return changed(context, key, dict, single, [&](const Term &old) { return append(context, {old}, single.term); });
}

// append_list(Key, ValList, Dict): Old ++ ValList, or ValList.
Term append_list(ProcessContext &context, const Key &key, const Value &values, const List &dict) {
    return changed(context, key, dict, values, [&](const Term &old) { return append(context, {old}, values.term); });
}

// update_counter(Key, Increment, Dict): Old + Increment, or Increment; function_clause for a non-number.
Term update_counter(ProcessContext &context, const Key &key, const Value &increment, const List &dict) {
    if (!increment.term.is_number()) {
        no_clause();
    }
    return changed(context, key, dict, increment, [&](const Term &old) { return add(context, old, increment.term); });
}

// erase(Key, Dict): the first cell of Key removed, then OTP's contraction check (made even when Key is absent).
Term erase(ProcessContext &context, const Key &key, const List &dict) {
    LinearHash table(context, dict.term, Kind::dict);
    const auto slot = table.slot(key.term);
    auto bucket = table.bucket(slot);
    const auto at = position(bucket, key);
    if (at) {
        bucket.erase(bucket.begin() + static_cast<std::ptrdiff_t>(*at));
    }
    table.put(slot, bucket);
    table.shrink(at ? 1 : 0);
    return table.record();
}

// take(Key, Dict): {Value, Dict without Key}, or error.
Term take(ProcessContext &context, const Key &key, const List &dict) {
    LinearHash table(context, dict.term, Kind::dict);
    const auto slot = table.slot(key.term);
    auto bucket = table.bucket(slot);
    const auto at = position(bucket, key);
    if (!at) {
        return atom(context, "error");
    }
    const auto value = need(bucket[*at].tail());
    bucket.erase(bucket.begin() + static_cast<std::ptrdiff_t>(*at));
    table.put(slot, bucket);
    table.shrink(1);
    return tuple(context, value, table.record());
}

// The elements of a proper list; function_clause at an improper tail (lists:foldl's clauses).
std::vector<Term> elements_of(const Term &list) {
    std::vector<Term> elements;
    auto rest = list;
    for (; rest.is_cons(); rest = need(rest.tail())) {
        elements.push_back(need(rest.head()));
    }
    if (!rest.is_nil()) {
        no_clause();
    }
    return elements;
}

// from_list(List): each {Key, Value} stored in order; function_clause for another element.
Term from_list(ProcessContext &context, const List &list) {
    auto dict = LinearHash::fresh(context, Kind::dict);
    for (const auto &element : elements_of(list.term)) {
        const auto parts = pair_of(element);
        if (!parts) {
            no_clause();
        }
        dict = store(context, {parts->first}, {parts->second}, {dict});
    }
    return dict;
}

Term new_dict(ProcessContext &context) { return LinearHash::fresh(context, Kind::dict); }

// The kind of a dict or set record; {badrecord, Term} for anything else.
Kind kind_of(const Term &table) {
    if (LinearHash::is_record(table, Kind::dict)) {
        return Kind::dict;
    }
    if (!LinearHash::is_record(table, Kind::set)) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badrecord, .payload = table.word()};
    }
    return Kind::set;
}

// clause_hashed:entries(Table): a dict's {Key, Value} pairs or a set's elements in storage order, the order OTP's
// map, filter and the sets' filter visit them.
Term entries(ProcessContext &context, const List &table) {
    const auto kind = kind_of(table.term);
    Prefix result;
    for (const auto &entry : LinearHash(context, table.term, kind).entries()) {
        result.push(kind == Kind::dict ? tuple(context, need(entry.head()), need(entry.tail())) : entry);
    }
    return result.done(context);
}

// clause_hashed:with_values(Dict, Values): the dict with its values, in storage order, replaced (dict:map/2).
Term with_values(ProcessContext &context, const List &dict, const Value &values) {
    LinearHash table(context, dict.term, Kind::dict);
    if (table.size() == 0) {
        return dict.term;
    }
    auto next = values.term;
    for (std::size_t slot = 1; next.is_cons(); ++slot) {
        auto bucket = table.bucket(slot);
        for (auto &entry : bucket) {
            entry = need(TermFactory(context).cons(need(entry.head()), need(next.head())));
            next = need(next.tail());
        }
        table.put(slot, bucket);
    }
    return table.record();
}

// The entries of a bucket whose flags, read from `flags` in order, are true; counts the others in `removed`.
std::vector<Term> kept_of(const std::vector<Term> &bucket, Term &flags, std::size_t &removed) {
    std::vector<Term> kept;
    for (const auto &entry : bucket) {
        const auto flag = need(flags.head());
        flags = need(flags.tail());
        if (flag.is_atom() && need(flag.atom_spelling()) == "true") {
            kept.push_back(entry);
        } else {
            ++removed;
        }
    }
    return kept;
}

// clause_hashed:keep(Table, Flags): the entries whose flag, in storage order, is true. A dict keeps its bucket order
// (and an empty dict stays as it is); a set's buckets come out reversed, as OTP's sets filter builds them. Then one
// contraction check for the removed entries.
Term keep(ProcessContext &context, const List &table, const Value &flags) {
    const auto kind = kind_of(table.term);
    LinearHash hash(context, table.term, kind);
    if (kind == Kind::dict && hash.size() == 0) {
        return table.term;
    }
    auto next = flags.term;
    std::size_t removed = 0;
    for (std::size_t slot = 1; next.is_cons(); ++slot) {
        auto kept = kept_of(hash.bucket(slot), next, removed);
        if (kind == Kind::set) {
            std::ranges::reverse(kept);
        }
        hash.put(slot, kept);
    }
    hash.shrink(removed);
    return hash.record();
}

constexpr std::array DICT_BUILTINS{
    typed_entry<new_dict>("dict", "new"),
    typed_entry<is_key>("dict", "is_key"),
    typed_entry<fetch>("dict", "fetch"),
    typed_entry<find>("dict", "find"),
    typed_entry<to_list>("dict", "to_list"),
    typed_entry<fetch_keys>("dict", "fetch_keys"),
    typed_entry<size>("dict", "size"),
    typed_entry<is_empty>("dict", "is_empty"),
    typed_entry<store>("dict", "store"),
    typed_entry<append_value>("dict", "append"),
    typed_entry<append_list>("dict", "append_list"),
    typed_entry<update_counter>("dict", "update_counter"),
    typed_entry<erase>("dict", "erase"),
    typed_entry<take>("dict", "take"),
    typed_entry<from_list>("dict", "from_list"),
    typed_entry<entries>("clause_hashed", "entries"),
    typed_entry<with_values>("clause_hashed", "with_values"),
    typed_entry<keep>("clause_hashed", "keep"),
};
} // namespace
} // namespace clause::runtime::builtins::collections

namespace clause::runtime {
std::span<const BuiltinEntry> dict_builtins() noexcept { return builtins::collections::DICT_BUILTINS; }
} // namespace clause::runtime
