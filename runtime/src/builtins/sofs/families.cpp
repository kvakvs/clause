#include "sofs.hpp"

namespace clause::runtime::builtins::sofs {
namespace {
// Flatten a family into its binary membership relation.
Term to_relation(ProcessContext &context, const Term &value) { return family_to_relation(Terms(context), value); }

// Fold lists of sets, requiring at least one set for intersection.
Items fold_sets(const Items &sets, Operation operation) {
    if (sets.empty() && operation == Operation::intersect) {
        bad_argument();
    }
    Items result;
    bool first = true;
    for (const auto &set : sets) {
        const auto values = elements(set);
        result = first ? values : combine(values, result, operation);
        first = false;
    }
    return result;
}

// Union or intersection over every value in a family.
Term fold_family(const Terms &terms, const Term &value, Operation operation) {
    const auto set = unordered(value);
    if (atom_is(set.type_, "_") && operation == Operation::unite) {
        return value;
    }
    const auto type = family_type(set);
    Items sets;
    for (const auto &pair : elements(set.data_)) {
        sets.push_back(at(pair, 1));
    }
    std::ranges::reverse(sets);
    if (operation == Operation::unite) {
        std::stable_sort(sets.begin(), sets.end(), [](const Term &a, const Term &b) { return compare(a, b) < 0; });
    }
    return make_set(terms, fold_sets(sets, operation), {type[1]});
}

// Union across the entire family's range.
Term union_of_family(ProcessContext &context, const Term &value) {
    return fold_family(Terms(context), value, Operation::unite);
}

// Intersection across the nonempty family's range.
Term intersection_of_family(ProcessContext &context, const Term &value) {
    return fold_family(Terms(context), value, Operation::intersect);
}

// Fold each value of a family of sets of sets independently.
Term nested_fold(const Terms &terms, const Term &value, Operation operation) {
    const auto set = unordered(value);
    if (atom_is(set.type_, "_")) {
        return value;
    }
    const auto type = family_type(set);
    const auto member = list_type(type[1]);
    if (!member) {
        bad_argument();
    }
    Items result;
    for (const auto &pair : elements(set.data_)) {
        const auto data = fold_sets(elements(at(pair, 1)), operation);
        result.push_back(tuple(terms, {at(pair, 0), terms.list(data)}));
    }
    const auto result_type = tuple(terms, {type[0], terms.list(Items{*member})});
    return make_set(terms, result, {result_type});
}

// Flatten each family member's sets by union.
Term family_union1(ProcessContext &context, const Term &value) {
    return nested_fold(Terms(context), value, Operation::unite);
}

// Flatten each family member's nonempty sets by intersection.
Term family_intersection1(ProcessContext &context, const Term &value) {
    return nested_fold(Terms(context), value, Operation::intersect);
}

// Project the domain or range of every relation in a family.
Term family_component(const Terms &terms, const Term &value, std::size_t index) {
    const auto set = unordered(value);
    if (atom_is(set.type_, "_")) {
        return value;
    }
    const auto type = family_type(set);
    if (atom_is(type[1], "_")) {
        return value;
    }
    const auto components = relation_type(terms, {set.data_, type[1], true});
    Items result;
    for (const auto &pair : elements(set.data_)) {
        const auto relation = wrap(terms, {at(pair, 1), type[1], true});
        const auto data = unordered(component(terms, relation, index)).data_;
        result.push_back(tuple(terms, {at(pair, 0), data}));
    }
    const auto result_type = tuple(terms, {type[0], terms.list(Items{components[index]})});
    return make_set(terms, result, {result_type});
}

// Domain of each binary relation in the family.
Term family_domain(ProcessContext &context, const Term &value) { return family_component(Terms(context), value, 0); }

// Range of each binary relation in the family.
Term family_range(ProcessContext &context, const Term &value) { return family_component(Terms(context), value, 1); }

// Field of each binary relation in the family.
Term family_field(ProcessContext &context, const Term &value) {
    Terms terms(context);
    return family_binary(terms, {family_component(terms, value, 0)}, {family_component(terms, value, 1)},
                         Operation::unite);
}

// Pointwise union on the union of the families' domains.
Term family_union2(ProcessContext &context, const Left &left, const Right &right) {
    return family_binary(Terms(context), left, right, Operation::unite);
}

// Pointwise intersection on the intersection of the families' domains.
Term family_intersection2(ProcessContext &context, const Left &left, const Right &right) {
    return family_binary(Terms(context), left, right, Operation::intersect);
}

// Pointwise subtraction while retaining every first-family key.
Term family_difference(ProcessContext &context, const Left &left, const Right &right) {
    return family_binary(Terms(context), left, right, Operation::subtract);
}

constexpr std::array FAMILIES{
    typed_entry<to_relation>("sofs", "family_to_relation"),
    typed_entry<to_relation>("sofs", "fam2rel"),
    typed_entry<union_of_family>("sofs", "union_of_family"),
    typed_entry<intersection_of_family>("sofs", "intersection_of_family"),
    typed_entry<family_union1>("sofs", "family_union"),
    typed_entry<family_intersection1>("sofs", "family_intersection"),
    typed_entry<family_domain>("sofs", "family_domain"),
    typed_entry<family_range>("sofs", "family_range"),
    typed_entry<family_field>("sofs", "family_field"),
    typed_entry<family_union2>("sofs", "family_union"),
    typed_entry<family_intersection2>("sofs", "family_intersection"),
    typed_entry<family_difference>("sofs", "family_difference"),
};
} // namespace
} // namespace clause::runtime::builtins::sofs

namespace clause::runtime {
// Publish family algebra, relation conversion and component projections.
std::span<const BuiltinEntry> sofs_families_builtins() noexcept { return builtins::sofs::FAMILIES; }
} // namespace clause::runtime
