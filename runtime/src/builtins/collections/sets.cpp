#include "../../terms/maps.hpp"
#include "linear_hash.hpp"
#include <algorithm>
#include <array>
#include <functional>

// The sets builtins (plan 21 step LIB2, docs/builtins.md#collections): a set of version 2 is a map whose keys are
// the elements (values []), one of version 1 OTP's set record over a linear hash table (linear_hash.hpp). Elements
// match with =:=. Operations on two maps give OTP's maps, values included; the others fold or filter one set into
// the other in OTP's order, so version 1 records equal OTP's. Anything but a set raises function_clause.
namespace clause::runtime::builtins::collections {
namespace {
using Kind = LinearHash::Kind;
// A second set argument, a role of its own so the two sets of a binary operation cannot be swapped.
struct OtherRole;
using Other = Named<OtherRole>;

Term boolean(ProcessContext &context, bool value) { return need(TermFactory(context).boolean(value)); }

bool is_v1(const Term &set) { return LinearHash::is_record(set, Kind::set); }

// function_clause unless `set` is a set of either version.
void check(const Term &set) {
    if (!set.is_map() && !is_v1(set)) {
        no_clause();
    }
}

std::size_t count(ProcessContext &context, const Term &set) {
    check(set);
    return set.is_map() ? set.map_size_unchecked() : LinearHash(context, set, Kind::set).size();
}

// The elements in the order OTP's fold visits them: a map's keys, a record's fold order.
std::vector<Term> folded(ProcessContext &context, const Term &set) {
    check(set);
    if (!set.is_map()) {
        return LinearHash(context, set, Kind::set).fold_order();
    }
    std::vector<Term> keys;
    for (const auto &entry : need(set.map_entries())) {
        keys.push_back(entry.first);
    }
    return keys;
}

bool contains(ProcessContext &context, const Term &set, const Term &element) {
    check(set);
    if (set.is_map()) {
        return need(set.map_contains(element));
    }
    LinearHash table(context, set, Kind::set);
    const auto bucket = table.bucket(table.slot(element));
    return std::ranges::any_of(bucket, [&](const Term &entry) { return same(entry, element); });
}

// `map` with the associations of `entries` put in (=>), the later winning.
Term put_all(ProcessContext &context, const Term &map, std::span<const std::pair<Term, Term>> entries) {
    std::vector<detail::MapUpdate> updates;
    updates.reserve(entries.size());
    for (const auto &[key, value] : entries) {
        updates.push_back({key, value, false});
    }
    const auto result = detail::MapAccess::update(context.heap(), map, updates);
    if (!result) {
        throw BuiltinFailure{.term = result.error().error};
    }
    return *result;
}

// The map of `kept` once at least `threshold` associations are kept, else of their keys with the values [].
Term kept_map(ProcessContext &context, const std::vector<std::pair<Term, Term>> &kept, std::size_t threshold) {
    if (kept.size() >= threshold) {
        return need(TermFactory(context).map(kept));
    }
    const auto nil = need(TermFactory(context).nil());
    std::vector<std::pair<Term, Term>> keys;
    keys.reserve(kept.size());
    for (const auto &entry : kept) {
        keys.emplace_back(entry.first, nil);
    }
    return need(TermFactory(context).map(keys));
}

// A map of `keys`, each with the value [] (maps:from_keys(Keys, [])).
Term keys_map(ProcessContext &context, std::span<const Term> keys) {
    const auto nil = need(TermFactory(context).nil());
    std::vector<std::pair<Term, Term>> entries;
    entries.reserve(keys.size());
    for (const auto &key : keys) {
        entries.emplace_back(key, nil);
    }
    return need(TermFactory(context).map(entries));
}

// add_element(Element, Set): a record gets the element at the head of its bucket.
Term add(ProcessContext &context, const Term &set, const Element &element) {
    check(set);
    if (set.is_map()) {
        const std::array entry{std::pair{element.term, need(TermFactory(context).nil())}};
        return put_all(context, set, entry);
    }
    LinearHash table(context, set, Kind::set);
    const auto slot = table.slot(element.term);
    auto bucket = table.bucket(slot);
    if (std::ranges::any_of(bucket, [&](const Term &entry) { return same(entry, element.term); })) {
        return set;
    }
    bucket.insert(bucket.begin(), element.term);
    table.put(slot, bucket);
    table.grow(1);
    return table.record();
}

// del_element(Element, Set).
Term remove(ProcessContext &context, const Term &set, const Element &element) {
    check(set);
    if (set.is_map()) {
        auto entries = need(set.map_entries());
        std::erase_if(entries, [&](const auto &entry) { return same(entry.first, element.term); });
        return need(TermFactory(context).map(entries));
    }
    LinearHash table(context, set, Kind::set);
    const auto slot = table.slot(element.term);
    auto bucket = table.bucket(slot);
    const auto at = std::ranges::find_if(bucket, [&](const Term &entry) { return same(entry, element.term); });
    if (at == bucket.end()) {
        return set;
    }
    bucket.erase(at);
    table.put(slot, bucket);
    table.shrink(1);
    return table.record();
}

// OTP's filter(Pred, Set) with Pred a membership test: a map keeps the passing keys (values []); a record is
// rebuilt with reversed buckets and one contraction check.
Term filtered(ProcessContext &context, const Term &set, const std::function<bool(const Term &)> &pass) {
    check(set);
    if (set.is_map()) {
        std::vector<Term> kept;
        for (const auto &entry : need(set.map_entries())) {
            if (pass(entry.first)) {
                kept.push_back(entry.first);
            }
        }
        return keys_map(context, kept);
    }
    LinearHash table(context, set, Kind::set);
    std::size_t removed = 0;
    for (std::size_t slot = 1, seen = 0, total = table.size(); seen < total; ++slot) {
        const auto bucket = table.bucket(slot);
        seen += bucket.size();
        std::vector<Term> kept;
        std::ranges::copy_if(bucket, std::back_inserter(kept), pass);
        removed += bucket.size() - kept.size();
        std::ranges::reverse(kept);
        table.put(slot, kept);
    }
    table.shrink(removed);
    return table.record();
}

// is_set(Set).
Term is_set(ProcessContext &context, const List &set) { return boolean(context, set.term.is_map() || is_v1(set.term)); }

// size(Set) and is_empty(Set).
TermResult<Term> size(ProcessContext &context, const List &set) {
    return TermFactory(context).integer(static_cast<std::int64_t>(count(context, set.term)));
}

Term is_empty(ProcessContext &context, const List &set) { return boolean(context, count(context, set.term) == 0); }

// to_list(Set): a map's keys; a record's elements as its fold prepends them.
Term to_list(ProcessContext &context, const List &set) {
    auto elements = folded(context, set.term);
    if (!set.term.is_map()) {
        std::ranges::reverse(elements);
    }
    return need(TermFactory(context).list(elements));
}

// from_list(List): a version 2 set; badarg unless List is a proper list.
Term from_list(ProcessContext &context, const List &list) {
    std::vector<Term> elements;
    auto rest = list.term;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        elements.push_back(rest.head_unchecked());
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return keys_map(context, elements);
}

// is_element(Element, Set), add_element(Element, Set1), del_element(Element, Set1).
Term is_element(ProcessContext &context, const Element &element, const List &set) {
    return boolean(context, contains(context, set.term, element.term));
}

Term add_element(ProcessContext &context, const Element &element, const List &set) {
    return add(context, set.term, element);
}

Term del_element(ProcessContext &context, const Element &element, const List &set) {
    return remove(context, set.term, element);
}

// union(Set1, Set2): two maps merged, else the smaller set's elements added to the other.
Term union2(ProcessContext &context, const List &set1, const Other &set2) {
    if (set1.term.is_map() && set2.term.is_map()) {
        return put_all(context, set1.term, need(set2.term.map_entries()));
    }
    const auto first_smaller = count(context, set1.term) < count(context, set2.term);
    auto result = first_smaller ? set2.term : set1.term;
    for (const auto &element : folded(context, first_smaller ? set1.term : set2.term)) {
        result = add(context, result, {element});
    }
    return result;
}

// The sets of a list argument of union/1 and intersection/1; function_clause at an improper tail.
std::vector<Term> set_list(const Term &list) {
    std::vector<Term> sets;
    auto rest = list;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        sets.push_back(rest.head_unchecked());
    }
    if (!rest.is_nil()) {
        no_clause();
    }
    return sets;
}

// union(SetList): [] is a new set, one set itself.
Term union1(ProcessContext &context, const List &list) {
    const auto sets = set_list(list.term);
    if (sets.empty()) {
        return need(TermFactory(context).map({}));
    }
    auto result = sets.front();
    for (std::size_t i = 1; i < sets.size(); ++i) {
        result = union2(context, {result}, {sets[i]});
    }
    return result;
}

// The common keys of two maps walked from `walked`, the smaller: with walked's values once at least `threshold` of
// its keys are common (OTP's heuristic switches to removing keys then), else with the values [].
Term common(ProcessContext &context, const Term &walked, const Other &other, std::size_t threshold) {
    auto kept = need(walked.map_entries());
    std::erase_if(kept, [&](const auto &entry) { return !need(other.term.map_contains(entry.first)); });
    return kept_map(context, kept, threshold);
}

// intersection(Set1, Set2).
Term intersection2(ProcessContext &context, const List &set1, const Other &set2) {
    if (set1.term.is_map() && set2.term.is_map()) {
        const auto first_smaller = set1.term.map_size_unchecked() < set2.term.map_size_unchecked();
        const auto &walked = first_smaller ? set1.term : set2.term;
        const auto threshold = walked.map_size_unchecked() * 3 / 4;
        return common(context, walked, {first_smaller ? set2.term : set1.term}, threshold);
    }
    const auto first_smaller = count(context, set1.term) < count(context, set2.term);
    const auto &kept = first_smaller ? set1.term : set2.term;
    const auto &other = first_smaller ? set2.term : set1.term;
    return filtered(context, kept, [&](const Term &element) { return contains(context, other, element); });
}

// intersection(SetList): a non-empty list.
Term intersection1(ProcessContext &context, const List &list) {
    const auto sets = set_list(list.term);
    if (sets.empty()) {
        no_clause();
    }
    auto result = sets.front();
    for (std::size_t i = 1; i < sets.size(); ++i) {
        result = intersection2(context, {result}, {sets[i]});
    }
    return result;
}

// is_disjoint(Set1, Set2): the smaller set's elements tested in the other until one is common.
Term is_disjoint(ProcessContext &context, const List &set1, const Other &set2) {
    const auto both_maps = set1.term.is_map() && set2.term.is_map();
    const auto first_smaller = both_maps ? set1.term.map_size_unchecked() < set2.term.map_size_unchecked()
                                         : count(context, set1.term) < count(context, set2.term);
    const auto &walked = first_smaller ? set1.term : set2.term;
    const auto &other = first_smaller ? set2.term : set1.term;
    const auto elements = folded(context, walked);
    return boolean(context, std::ranges::none_of(elements, [&](const Term &e) { return contains(context, other, e); }));
}

// The keys of a map not in `other`, with their values once at least `threshold` are kept, else with [].
Term difference(ProcessContext &context, const Term &map, const Other &other, std::size_t threshold) {
    auto kept = need(map.map_entries());
    std::erase_if(kept, [&](const auto &entry) { return need(other.term.map_contains(entry.first)); });
    return kept_map(context, kept, threshold);
}

// subtract(Set1, Set2): two maps by OTP's rule (values kept when Set2 is small or most keys stay), else Set1
// filtered.
Term subtract(ProcessContext &context, const List &set1, const Other &set2) {
    if (set1.term.is_map() && set2.term.is_map()) {
        const auto left = set1.term.map_size_unchecked();
        const auto right = set2.term.map_size_unchecked();
        return difference(context, set1.term, set2, right <= left / 4 ? 0 : left * 3 / 4);
    }
    return filtered(context, set1.term, [&](const Term &element) { return !contains(context, set2.term, element); });
}

// is_subset(Set1, Set2): Set1's elements tested in Set2 until one is missing.
Term is_subset(ProcessContext &context, const List &set1, const Other &set2) {
    if (set1.term.is_map() && set2.term.is_map() && set1.term.map_size_unchecked() > set2.term.map_size_unchecked()) {
        return boolean(context, false);
    }
    const auto elements = folded(context, set1.term);
    return boolean(context,
                   std::ranges::all_of(elements, [&](const Term &e) { return contains(context, set2.term, e); }));
}

// is_equal(Set1, Set2): the same elements, whatever the versions.
Term is_equal(ProcessContext &context, const List &set1, const Other &set2) {
    if (count(context, set1.term) != count(context, set2.term)) {
        return boolean(context, false);
    }
    if (same(set1.term, set2.term)) {
        return boolean(context, true);
    }
    const auto elements = folded(context, set1.term);
    return boolean(context,
                   std::ranges::all_of(elements, [&](const Term &e) { return contains(context, set2.term, e); }));
}

constexpr std::array SETS_BUILTINS{
    typed_entry<is_set>("sets", "is_set"),
    typed_entry<size>("sets", "size"),
    typed_entry<is_empty>("sets", "is_empty"),
    typed_entry<to_list>("sets", "to_list"),
    typed_entry<from_list>("sets", "from_list"),
    typed_entry<is_element>("sets", "is_element"),
    typed_entry<add_element>("sets", "add_element"),
    typed_entry<del_element>("sets", "del_element"),
    typed_entry<union2>("sets", "union"),
    typed_entry<union1>("sets", "union"),
    typed_entry<intersection2>("sets", "intersection"),
    typed_entry<intersection1>("sets", "intersection"),
    typed_entry<is_disjoint>("sets", "is_disjoint"),
    typed_entry<subtract>("sets", "subtract"),
    typed_entry<is_subset>("sets", "is_subset"),
    typed_entry<is_equal>("sets", "is_equal"),
};
} // namespace
} // namespace clause::runtime::builtins::collections

namespace clause::runtime {
std::span<const BuiltinEntry> sets_builtins() noexcept { return builtins::collections::SETS_BUILTINS; }
} // namespace clause::runtime
