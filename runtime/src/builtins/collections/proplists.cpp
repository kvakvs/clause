#include "collections.hpp"
#include <algorithm>
#include <array>

// The proplists builtins (plan 21 step LIB1, docs/builtins.md#collections): a property list holds atoms (Key standing
// for {Key, true}) and tuples whose first element is the key; anything else is skipped. Keys match with =:=. Each
// walks its list as OTP's clauses do: an improper tail raises function_clause, a comprehension's non-list input
// {bad_generator, Rest}.
namespace clause::runtime::builtins::collections {
namespace {
// A list of pairs that drives a walk: {Key, Key1} substitutions, {Property, Expansion} pairs, or split's keys.
struct PairsRole;
using Pairs = Named<PairsRole>;

// How a property list element relates to a key.
enum class Match : std::uint8_t {
    none,
    // The element is the key atom itself.
    atom,
    // The element is a tuple whose first element is the key.
    tuple,
};

Match match(const Term &element, const Key &key) {
    if (element.is_atom()) {
        return same(element, key.term) ? Match::atom : Match::none;
    }
    const auto first = first_element(element);
    return first && same(*first, key.term) ? Match::tuple : Match::none;
}

// The key of an element: the atom itself or a tuple's first element; none for anything else.
std::optional<Term> key_of(const Term &element) {
    return element.is_atom() ? std::optional{element} : first_element(element);
}

Term atom(ProcessContext &context, std::string_view name) { return need(TermFactory(context).atom(name)); }

Term boolean(ProcessContext &context, bool value) { return need(TermFactory(context).boolean(value)); }

bool is_true(const Term &term) { return term.is_atom() && need(term.atom_spelling()) == "true"; }

// {Key, true} for a key found as an atom.
Term with_true(ProcessContext &context, const Key &key) {
    const std::array parts{key.term, boolean(context, true)};
    return need(TermFactory(context).tuple(parts));
}

// function_clause unless a walk ended at [].
void ended(const Term &rest) {
    if (!rest.is_nil()) {
        no_clause();
    }
}

// The elements of a proper list; function_clause at an improper tail.
std::vector<Term> elements_of(const Term &list) {
    std::vector<Term> elements;
    auto rest = list;
    for (; rest.is_cons(); rest = need(rest.tail())) {
        elements.push_back(need(rest.head()));
    }
    ended(rest);
    return elements;
}

// The elements of a comprehension's input list; {bad_generator, Rest} at a non-list rest.
std::vector<Term> generated(const Term &list) {
    std::vector<Term> elements;
    auto rest = list;
    for (; rest.is_cons(); rest = need(rest.tail())) {
        elements.push_back(need(rest.head()));
    }
    if (!rest.is_nil()) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::bad_generator, .payload = rest.word()};
    }
    return elements;
}

// property(Key, Value): Key for {Key, true} with an atom Key, else {Key, Value}.
Term property_of(ProcessContext &context, const Key &key, const Value &value) {
    if (is_true(value.term) && key.term.is_atom()) {
        return key.term;
    }
    const std::array parts{key.term, value.term};
    return need(TermFactory(context).tuple(parts));
}

// property(Property): {Key, true} with an atom Key shortened to Key.
Term property_short(ProcessContext &context, const Term &property) {
    const auto parts = pair_of(property);
    return parts ? property_of(context, {parts->first}, {parts->second}) : property;
}

// The first element matching `key` and how it matches; none past the end, function_clause at an improper tail.
std::optional<std::pair<Term, Match>> first_match(const Key &key, const List &list) {
    auto rest = list.term;
    for (; rest.is_cons(); rest = need(rest.tail())) {
        const auto element = need(rest.head());
        if (const auto how = match(element, key); how != Match::none) {
            return std::pair{element, how};
        }
    }
    ended(rest);
    return std::nullopt;
}

// unfold(ListIn): each atom Key as {Key, true}.
Term unfold(ProcessContext &context, const List &list) {
    Prefix unfolded;
    for (const auto &element : elements_of(list.term)) {
        unfolded.push(element.is_atom() ? with_true(context, {element}) : element);
    }
    return unfolded.done(context);
}

// compact(ListIn): each {Key, true} with an atom Key as Key.
Term compact(ProcessContext &context, const List &list) {
    Prefix compacted;
    for (const auto &element : generated(list.term)) {
        compacted.push(property_short(context, element));
    }
    return compacted.done(context);
}

// lookup(Key, List): the first entry for Key ({Key, true} for the atom), or none.
Term lookup(ProcessContext &context, const Key &key, const List &list) {
    const auto found = first_match(key, list);
    if (!found) {
        return atom(context, "none");
    }
    return found->second == Match::atom ? with_true(context, key) : found->first;
}

// lookup_all(Key, List): every entry for Key.
Term lookup_all(ProcessContext &context, const Key &key, const List &list) {
    Prefix entries;
    for (const auto &element : elements_of(list.term)) {
        const auto how = match(element, key);
        if (how != Match::none) {
            entries.push(how == Match::atom ? with_true(context, key) : element);
        }
    }
    return entries.done(context);
}

// is_defined(Key, List).
Term is_defined(ProcessContext &context, const Key &key, const List &list) {
    return boolean(context, first_match(key, list).has_value());
}

// get_value(Key, List, Default): the value of the first entry for Key, true for the atom, Default for another tuple
// or no entry.
Term get_value(ProcessContext &context, const Key &key, const List &list, const Value &fallback) {
    const auto found = first_match(key, list);
    if (!found) {
        return fallback.term;
    }
    if (found->second == Match::atom) {
        return boolean(context, true);
    }
    const auto parts = pair_of(found->first);
    return parts ? parts->second : fallback.term;
}

// get_bool(Key, List): whether the first entry for Key is the atom or {Key, true}.
Term get_bool(ProcessContext &context, const Key &key, const List &list) {
    const auto found = first_match(key, list);
    if (!found || found->second == Match::atom) {
        return boolean(context, found.has_value());
    }
    const auto parts = pair_of(found->first);
    return boolean(context, parts && is_true(parts->second));
}

// The value of an entry for get_all_values/2: true for the atom, a pair's value; none for a tuple of another size.
std::optional<Term> entry_value(ProcessContext &context, const Term &element, Match how) {
    if (how == Match::atom) {
        return boolean(context, true);
    }
    const auto parts = how == Match::tuple ? pair_of(element) : std::nullopt;
    return parts ? std::optional{parts->second} : std::nullopt;
}

// get_all_values(Key, List): the values of every entry for Key.
Term get_all_values(ProcessContext &context, const Key &key, const List &list) {
    Prefix values;
    for (const auto &element : elements_of(list.term)) {
        if (const auto value = entry_value(context, element, match(element, key))) {
            values.push(*value);
        }
    }
    return values.done(context);
}

// append_values(Key, List): get_all_values with list values spliced in. An improper list value is badarg, raised
// after the walk, as OTP's `Value ++ Rest` raises it while unwinding.
Term append_values(ProcessContext &context, const Key &key, const List &list) {
    std::vector<Term> values;
    for (const auto &element : elements_of(list.term)) {
        if (const auto value = entry_value(context, element, match(element, key))) {
            values.push_back(*value);
        }
    }
    auto result = need(TermFactory(context).nil());
    for (auto value = values.rbegin(); value != values.rend(); ++value) {
        result = value->is_list() ? append(context, {*value}, result) : need(TermFactory(context).cons(*value, result));
    }
    return result;
}

// get_keys(List): the keys, each once, in the order of a map's keys (OTP collects them in a set).
TermResult<Term> get_keys(ProcessContext &context, const List &list) {
    std::vector<std::pair<Term, Term>> keys;
    for (const auto &element : elements_of(list.term)) {
        if (const auto key = key_of(element)) {
            keys.emplace_back(*key, *key);
        }
    }
    std::vector<Term> unique;
    for (const auto &entry : need(need(TermFactory(context).map(keys)).map_entries())) {
        unique.push_back(entry.first);
    }
    return TermFactory(context).list(unique);
}

// delete(Key, List): every entry for Key removed.
Term delete_key(ProcessContext &context, const Key &key, const List &list) {
    Prefix kept;
    for (const auto &element : elements_of(list.term)) {
        if (match(element, key) == Match::none) {
            kept.push(element);
        }
    }
    return kept.done(context);
}

// The first {Key, Key1} of a substitution list for which `element` is an entry, with how it matches; none when the
// list ends; function_clause at an element that is not a pair or an improper tail.
std::optional<std::pair<Term, Match>> substitution(const Pairs &substitutions, const Term &element) {
    auto rest = substitutions.term;
    for (; rest.is_cons(); rest = need(rest.tail())) {
        const auto parts = pair_of(need(rest.head()));
        if (!parts) {
            no_clause();
        }
        if (const auto how = match(element, {parts->first}); how != Match::none) {
            return std::pair{parts->second, how};
        }
    }
    ended(rest);
    return std::nullopt;
}

// An entry renamed by an alias: the atom becomes Key1, a tuple gets Key1 as its first element.
Term renamed(ProcessContext &context, const Term &element, const std::pair<Term, Match> &alias) {
    if (alias.second == Match::atom) {
        return property_of(context, {alias.first}, {boolean(context, true)});
    }
    auto parts = need(element.tuple_elements());
    parts.front() = alias.first;
    return property_short(context, need(TermFactory(context).tuple(parts)));
}

// substitute_aliases(Aliases, ListIn): each entry for an alias Key renamed to its Key1.
Term substitute_aliases(ProcessContext &context, const Pairs &aliases, const List &list) {
    Prefix result;
    for (const auto &element : generated(list.term)) {
        const auto alias = substitution(aliases, element);
        result.push(alias ? renamed(context, element, *alias) : element);
    }
    return result.done(context);
}

// substitute_negations(Negations, ListIn): each entry for a Key renamed to its Key1 with the boolean value negated;
// a value that is not true counts as false.
Term substitute_negations(ProcessContext &context, const Pairs &negations, const List &list) {
    Prefix negated;
    for (const auto &element : generated(list.term)) {
        const auto negation = substitution(negations, element);
        if (!negation) {
            negated.push(element);
            continue;
        }
        const auto parts = negation->second == Match::tuple ? pair_of(element) : std::nullopt;
        const auto value = negation->second == Match::tuple && !(parts && is_true(parts->second));
        negated.push(property_of(context, {negation->first}, {boolean(context, value)}));
    }
    return negated.done(context);
}

// One expansion of expand/2: the property to look for and the list to put in its place.
struct Expansion {
    Term property;
    Term replacement;
};

// The expansions of expand/2: each {Property, Expansion} pair with its property shortened (other elements are
// skipped), consecutive repeats of a property dropped.
std::vector<Expansion> expansions(ProcessContext &context, const Pairs &list) {
    std::vector<Expansion> result;
    for (const auto &element : generated(list.term)) {
        const auto parts = pair_of(element);
        if (!parts) {
            continue;
        }
        const auto property = property_short(context, parts->first);
        if (result.empty() || !same(result.back().property, property)) {
            result.push_back({property, parts->second});
        }
    }
    return result;
}

// One expansion applied: the first entry for the property's key is replaced by the expansion (and later entries for
// the key removed) when it equals the property; elements before it stay.
Term expand_one(ProcessContext &context, const Expansion &expansion, const Term &list) {
    const auto key = key_of(expansion.property);
    if (!key) {
        return list;
    }
    Prefix passed;
    auto rest = list;
    for (; rest.is_cons(); rest = need(rest.tail())) {
        const auto element = need(rest.head());
        const auto how = match(element, {*key});
        if (how == Match::none) {
            passed.push(element);
            continue;
        }
        const auto found = how == Match::atom ? element : property_short(context, element);
        if (!same(found, expansion.property)) {
            passed.push(found);
            return passed.then(context, need(rest.tail()));
        }
        passed.push(expansion.replacement);
        return passed.then(context, delete_key(context, {*key}, {need(rest.tail())}));
    }
    ended(rest);
    return passed.done(context);
}

// The top-level list elements spliced in (one level), as OTP's expand/2 finishes.
Term flatten_once(ProcessContext &context, const Term &list) {
    const auto elements = elements_of(list);
    auto result = need(TermFactory(context).nil());
    for (auto element = elements.rbegin(); element != elements.rend(); ++element) {
        result = element->is_list() ? append(context, {*element}, result)
                                    : need(TermFactory(context).cons(*element, result));
    }
    return result;
}

// expand(Expansions, ListIn).
Term expand(ProcessContext &context, const Pairs &expansion_list, const List &list) {
    if (!list.term.is_list()) {
        no_clause();
    }
    auto result = list.term;
    for (const auto &expansion : expansions(context, expansion_list)) {
        result = expand_one(context, expansion, result);
    }
    return flatten_once(context, result);
}

// The positions in `keys` of the keys exactly equal to an element's key (split/2 lists a key once per occurrence).
std::vector<std::size_t> positions(const std::vector<Term> &keys, const Term &element) {
    std::vector<std::size_t> found;
    if (const auto key = key_of(element)) {
        for (std::size_t i = 0; i < keys.size(); ++i) {
            if (same(keys[i], *key)) {
                found.push_back(i);
            }
        }
    }
    return found;
}

// split(List, Keys): {Lists, Rest}, one list of the entries for each key and the other elements, in order.
Term split(ProcessContext &context, const List &list, const Pairs &keys) {
    const auto wanted = generated(keys.term);
    std::vector<std::vector<Term>> groups(wanted.size());
    std::vector<Term> others;
    for (const auto &element : elements_of(list.term)) {
        const auto at = positions(wanted, element);
        if (at.empty()) {
            others.push_back(element);
        }
        for (const auto i : at) {
            groups[i].push_back(element);
        }
    }
    TermFactory factory(context);
    std::vector<Term> lists;
    lists.reserve(groups.size());
    for (const auto &group : groups) {
        lists.push_back(need(factory.list(group)));
    }
    const std::array parts{need(factory.list(lists)), need(factory.list(others))};
    return need(factory.tuple(parts));
}

// What a to_map/1 element says about its key: a value, or removal (none); nothing for a skipped element.
std::optional<std::pair<Term, std::optional<Term>>> map_entry(ProcessContext &context, const Term &element) {
    if (const auto parts = pair_of(element)) {
        return std::pair{parts->first, std::optional{parts->second}};
    }
    if (const auto first = first_element(element)) {
        return std::pair{*first, std::optional<Term>{}};
    }
    if (element.is_atom()) {
        return std::pair{element, std::optional{boolean(context, true)}};
    }
    return std::nullopt;
}

// to_map(List): {Key, Value} pairs and atoms (true) as associations. OTP folds from the right, so the leftmost entry
// for a key decides, and a tuple of another size there leaves the key out.
TermResult<Term> to_map(ProcessContext &context, const List &list) {
    std::vector<std::pair<Term, std::optional<Term>>> decided;
    for (const auto &element : elements_of(list.term)) {
        const auto entry = map_entry(context, element);
        if (entry &&
            std::ranges::none_of(decided, [&](const auto &known) { return same(known.first, entry->first); })) {
            decided.push_back(*entry);
        }
    }
    std::vector<std::pair<Term, Term>> associations;
    for (const auto &[key, value] : decided) {
        if (value) {
            associations.emplace_back(key, *value);
        }
    }
    return TermFactory(context).map(associations);
}

constexpr std::array PROPLISTS_BUILTINS{
    typed_entry<unfold>("proplists", "unfold"),
    typed_entry<compact>("proplists", "compact"),
    typed_entry<lookup>("proplists", "lookup"),
    typed_entry<lookup_all>("proplists", "lookup_all"),
    typed_entry<is_defined>("proplists", "is_defined"),
    typed_entry<get_value>("proplists", "get_value"),
    typed_entry<get_bool>("proplists", "get_bool"),
    typed_entry<get_all_values>("proplists", "get_all_values"),
    typed_entry<append_values>("proplists", "append_values"),
    typed_entry<get_keys>("proplists", "get_keys"),
    typed_entry<delete_key>("proplists", "delete"),
    typed_entry<substitute_aliases>("proplists", "substitute_aliases"),
    typed_entry<substitute_negations>("proplists", "substitute_negations"),
    typed_entry<expand>("proplists", "expand"),
    typed_entry<split>("proplists", "split"),
    typed_entry<to_map>("proplists", "to_map"),
};
} // namespace
} // namespace clause::runtime::builtins::collections

namespace clause::runtime {
std::span<const BuiltinEntry> proplists_builtins() noexcept { return builtins::collections::PROPLISTS_BUILTINS; }
} // namespace clause::runtime
