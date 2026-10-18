#include "sofs.hpp"

namespace clause::runtime::builtins::sofs {
Term component(const Terms &terms, const Term &relation, std::size_t index) {
    const auto set = unordered(relation);
    const auto type = relation_type(terms, set);
    if (atom_is(set.type_, "_")) {
        return relation;
    }
    Items result;
    for (const auto &pair : elements(set.data_)) {
        result.push_back(at(pair, index));
    }
    if (index == 1) {
        std::ranges::reverse(result);
    }
    return make_set(terms, unique(std::move(result)), {type[index]});
}

Term converse(const Terms &terms, const Term &relation) {
    const auto set = unordered(relation);
    const auto type = relation_type(terms, set);
    if (atom_is(set.type_, "_")) {
        return relation;
    }
    Items result;
    for (const auto &pair : elements(set.data_)) {
        result.push_back(tuple(terms, {at(pair, 1), at(pair, 0)}));
    }
    std::ranges::reverse(result);
    std::stable_sort(result.begin(), result.end(), [](const Term &a, const Term &b) { return compare(a, b) < 0; });
    return make_set(terms, result, {tuple(terms, {type[1], type[0]})});
}

Term relation_to_family(const Terms &terms, const Term &relation) {
    const auto set = unordered(relation);
    const auto type = relation_type(terms, set);
    if (atom_is(set.type_, "_")) {
        return relation;
    }
    return make_set(terms, group_adjacent(terms, elements(set.data_), false),
                    {tuple(terms, {type[0], terms.list(Items{type[1]})})});
}

Term canonical(const Terms &terms, const Term &sets) {
    const auto set = unordered(sets);
    if (atom_is(set.type_, "_")) {
        return sets;
    }
    const auto member = list_type(set.type_);
    if (!member) {
        bad_argument();
    }
    if (atom_is(*member, "_")) {
        return empty(terms);
    }
    Items result;
    for (const auto &subset : elements(set.data_)) {
        for (const auto &item : elements(subset)) {
            result.push_back(tuple(terms, {item, subset}));
        }
    }
    return make_set(terms, unique(std::move(result)), {tuple(terms, {*member, set.type_})});
}

namespace {
// Public membership relation for a set of sets.
Term canonical_relation(ProcessContext &context, const Term &value) { return canonical(Terms(context), value); }

// Public relation-to-family conversion, including its historical alias.
Term to_family(ProcessContext &context, const Term &value) { return relation_to_family(Terms(context), value); }

// Public domain projection.
Term domain(ProcessContext &context, const Term &value) { return component(Terms(context), value, 0); }

// Public range projection.
Term range(ProcessContext &context, const Term &value) { return component(Terms(context), value, 1); }

// Union of compatible domain and range types.
Term field(ProcessContext &context, const Term &value) {
    Terms terms(context);
    return binary_set(terms, {component(terms, value, 0)}, {component(terms, value, 1)}, Operation::unite);
}

// Public converse conversion.
Term converse1(ProcessContext &context, const Term &value) { return converse(Terms(context), value); }

// Image or inverse image of a set under a binary relation.
Term image_of(const Terms &terms, const Left &relation, const Right &keys, std::size_t index) {
    const auto set = unordered(relation.term);
    const auto key_set = unordered(keys.term);
    const auto type = relation_type(terms, set);
    if (atom_is(set.type_, "_")) {
        return relation.term;
    }
    match(terms, {type[index]}, {key_set.type_});
    const auto candidates = elements(key_set.data_);
    Items result;
    for (const auto &pair : elements(set.data_)) {
        const auto key = at(pair, index);
        if (std::ranges::any_of(candidates, [&](const Term &item) { return compare(key, item) == 0; })) {
            result.push_back(at(pair, 1 - index));
        }
    }
    std::ranges::reverse(result);
    return make_set(terms, unique(std::move(result)), {type[1 - index]});
}

// Range values reached from the supplied domain set.
Term image(ProcessContext &context, const Left &relation, const Right &keys) {
    return image_of(Terms(context), relation, keys, 0);
}

// Domain values reaching the supplied range set.
Term inverse_image(ProcessContext &context, const Left &relation, const Right &keys) {
    return image_of(Terms(context), relation, keys, 1);
}

// Remove reflexive pairs without requiring identical component types.
Term strict_relation(ProcessContext &context, const Term &value) {
    Terms terms(context);
    const auto set = unordered(value);
    static_cast<void>(relation_type(terms, set));
    if (atom_is(set.type_, "_")) {
        return value;
    }
    Items result;
    for (const auto &pair : elements(set.data_)) {
        if (compare(at(pair, 0), at(pair, 1)) != 0) {
            result.push_back(pair);
        }
    }
    return make_set(terms, result, {set.type_});
}

// Pick the missing reflexive pair's representative at its insertion point within one domain group.
std::optional<Term> diagonal_key(std::span<const Term> group) {
    for (const auto &pair : group) {
        const auto order = compare(at(pair, 0), at(pair, 1));
        if (order == 0) {
            return std::nullopt;
        }
        if (order < 0) {
            return at(pair, 0);
        }
    }
    return at(group.back(), 0);
}

// Add one diagonal for each domain group, keeping its insertion-point representative.
Items domain_diagonals(const Terms &terms, const Items &pairs) {
    Items result;
    std::size_t start = 0;
    while (start < pairs.size()) {
        auto end = start + 1;
        const auto key = at(pairs[start], 0);
        while (end < pairs.size() && compare(key, at(pairs[end], 0)) == 0) {
            ++end;
        }
        if (const auto value = diagonal_key(std::span(pairs).subspan(start, end - start))) {
            result.push_back(tuple(terms, {*value, *value}));
        }
        start = end;
    }
    return result;
}

// Add reflexive pairs for all values in the relation's field.
Term weak_relation(ProcessContext &context, const Term &value) {
    Terms terms(context);
    const auto set = unordered(value);
    const auto types = relation_type(terms, set);
    if (atom_is(set.type_, "_")) {
        return value;
    }
    const auto type = unify(terms, {types[0]}, {types[1]});
    if (!type) {
        bad_argument();
    }
    auto result = elements(set.data_);
    const auto diagonal = domain_diagonals(terms, result);
    result.insert(result.end(), diagonal.begin(), diagonal.end());
    const auto domain_values = unordered(component(terms, value, 0));
    const auto range_values = unordered(component(terms, value, 1));
    const auto missing = combine(elements(range_values.data_), elements(domain_values.data_), Operation::subtract);
    for (const auto &item : missing) {
        result.push_back(tuple(terms, {item, item}));
    }
    return make_set(terms, unique(std::move(result)), {tuple(terms, {*type, *type})});
}

// Test the domain uniqueness of a binary relation, including untyped empty relations.
Term is_a_function(ProcessContext &context, const Term &value) {
    Terms terms(context);
    const auto set = unordered(value);
    static_cast<void>(relation_type(terms, set));
    if (atom_is(set.type_, "_")) {
        return terms.boolean(true);
    }
    std::optional<Term> previous;
    for (const auto &pair : elements(set.data_)) {
        const auto key = at(pair, 0);
        if (previous && compare(*previous, key) == 0) {
            return terms.boolean(false);
        }
        previous = key;
    }
    return terms.boolean(true);
}

// Invert a bijection, rejecting repeated domain or range values.
Term inverse(ProcessContext &context, const Term &value) {
    Terms terms(context);
    const auto set = unordered(value);
    static_cast<void>(relation_type(terms, set));
    if (atom_is(set.type_, "_")) {
        return value;
    }
    check_function(terms, elements(set.data_));
    const auto result = converse(terms, value);
    check_function(terms, elements(unordered(result).data_));
    return result;
}

// Refine the result type of an extension whose original relation is untyped.
Term empty_extension(const Terms &terms, const Set &set, const Left &keys, const Right &value) {
    const auto range = record(value.term);
    if (atom_is(keys.term, "_") || (range.unordered_ && atom_is(range.type_, "_"))) {
        return wrap(terms, set);
    }
    return make_set(terms, {}, {tuple(terms, {keys.term, full_type(terms, range)})});
}

// Add a default value only for keys absent from the original relation's domain.
Term extension(ProcessContext &context, const Left &relation, const Right &keys, const Value &value) {
    Terms terms(context);
    const auto set = unordered(relation.term);
    const auto key_set = unordered(keys.term);
    if (!is_record(value.term)) {
        const auto inputs = tuple(terms, {set.type_, key_set.type_, terms.boolean(false)});
        const auto payload = tuple(terms, {terms.atom("case_clause"), inputs});
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error, .payload = payload.word()};
    }
    if (atom_is(set.type_, "_")) {
        return empty_extension(terms, set, {key_set.type_}, {value.term});
    }
    const auto types = relation_type(terms, set);
    const auto range_set = record(value.term);
    match(terms, {types[0]}, {key_set.type_});
    match(terms, {types[1]}, {full_type(terms, range_set)});
    const auto missing =
        unordered(binary_set(terms, {keys.term}, {component(terms, relation.term, 0)}, Operation::subtract));
    auto result = elements(set.data_);
    for (const auto &key : elements(missing.data_)) {
        result.push_back(tuple(terms, {key, range_set.data_}));
    }
    return make_set(terms, unique(std::move(result)), {set.type_});
}

// Partition elements according to identical set-membership signatures.
Term partition1(ProcessContext &context, const Term &value) {
    Terms terms(context);
    const auto membership = canonical(terms, value);
    const auto signatures = relation_to_family(terms, membership);
    return component(terms, relation_to_family(terms, converse(terms, signatures)), 1);
}

constexpr std::array RELATIONS{
    typed_entry<canonical_relation>("sofs", "canonical_relation"),
    typed_entry<to_family>("sofs", "relation_to_family"),
    typed_entry<to_family>("sofs", "rel2fam"),
    typed_entry<domain>("sofs", "domain"),
    typed_entry<range>("sofs", "range"),
    typed_entry<field>("sofs", "field"),
    typed_entry<converse1>("sofs", "converse"),
    typed_entry<image>("sofs", "image"),
    typed_entry<inverse_image>("sofs", "inverse_image"),
    typed_entry<strict_relation>("sofs", "strict_relation"),
    typed_entry<weak_relation>("sofs", "weak_relation"),
    typed_entry<is_a_function>("sofs", "is_a_function"),
    typed_entry<inverse>("sofs", "inverse"),
    typed_entry<extension>("sofs", "extension"),
    typed_entry<partition1>("sofs", "partition"),
};
} // namespace
} // namespace clause::runtime::builtins::sofs

namespace clause::runtime {
// Publish relation queries, normalization and conversions.
std::span<const BuiltinEntry> sofs_relations_builtins() noexcept { return builtins::sofs::RELATIONS; }
} // namespace clause::runtime
