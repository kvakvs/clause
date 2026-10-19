#include "sofs.hpp"

namespace clause::runtime::builtins::sofs {
namespace {
// Expose a record's stored data verbatim.
Term to_external(ProcessContext &, const Term &value) { return record(value).data_; }

// Expose the complete public type of either record kind.
Term type(ProcessContext &context, const Term &value) { return full_type(Terms(context), record(value)); }

// Wrap each external component in the record kind denoted by its declared type.
Term typed_component(const Terms &terms, const Left &data, const Right &type) {
    const auto member = list_type(type.term);
    return wrap(terms, {data.term, member.value_or(type.term), member.has_value()});
}

// Decompose either a set or a tuple-valued ordered set into component records.
Term to_sets(ProcessContext &context, const Term &value) {
    Terms terms(context);
    const auto set = record(value);
    Items result;
    if (set.unordered_) {
        for (const auto &item : lists::proper(set.data_)) {
            result.push_back(typed_component(terms, {item}, {set.type_}));
        }
        return terms.list(result);
    }
    if (!set.type_.is_tuple() || !set.data_.is_tuple()) {
        bad_argument();
    }
    const auto data = need(set.data_.tuple_elements());
    if (data.size() != set.type_.tuple_size_unchecked()) {
        no_clause();
    }
    for (std::size_t i = 0; i < data.size(); ++i) {
        result.push_back(typed_component(terms, {data[i]}, {at(set.type_, i)}));
    }
    return terms.tuple(result);
}

// Count unordered elements or ordered tuple components.
Term no_elements(ProcessContext &context, const Term &value) {
    const auto set = record(value);
    if (set.unordered_) {
        return Terms(context).integer(static_cast<std::int64_t>(elements(set.data_).size()));
    }
    if (!set.type_.is_tuple() || !set.data_.is_tuple()) {
        bad_argument();
    }
    return Terms(context).integer(static_cast<std::int64_t>(set.data_.tuple_size_unchecked()));
}

// Recognize either record shape without validating its contents.
Term is_sofs_set(ProcessContext &context, const Term &value) { return Terms(context).boolean(is_record(value)); }

// Distinguish the two accepted record kinds.
Term is_set(ProcessContext &context, const Term &value) { return Terms(context).boolean(record(value).unordered_); }

// Only unordered sets can be empty sets.
Term is_empty_set(ProcessContext &context, const Term &value) {
    const auto set = record(value);
    return Terms(context).boolean(set.unordered_ && set.data_.is_nil());
}

// Compare compatible sets by value rather than exact numeric identity.
Term is_equal(ProcessContext &context, const Left &left, const Right &right) {
    Terms terms(context);
    const auto first = record(left.term);
    const auto second = record(right.term);
    if (first.unordered_ != second.unordered_) {
        error(terms, "type_mismatch"_atom);
    }
    match(terms, {first.type_}, {second.type_});
    return terms.boolean(compare(first.data_, second.data_) == 0);
}

// Check inclusion through the observable ordered element lists.
Term is_subset(ProcessContext &context, const Left &left, const Right &right) {
    Terms terms(context);
    const auto result = unordered(binary_set(terms, left, right, Operation::subtract));
    return terms.boolean(result.data_.is_nil());
}

// Check whether compatible sets have any common element.
Term is_disjoint(ProcessContext &context, const Left &left, const Right &right) {
    Terms terms(context);
    const auto result = unordered(binary_set(terms, left, right, Operation::intersect));
    return terms.boolean(result.data_.is_nil());
}

// Merge two compatible unordered sets.
Term union2(ProcessContext &context, const Left &left, const Right &right) {
    return binary_set(Terms(context), left, right, Operation::unite);
}

// Retain the shared elements of two compatible unordered sets.
Term intersection2(ProcessContext &context, const Left &left, const Right &right) {
    return binary_set(Terms(context), left, right, Operation::intersect);
}

// Remove the second set's elements from the first.
Term difference(ProcessContext &context, const Left &left, const Right &right) {
    return binary_set(Terms(context), left, right, Operation::subtract);
}

// Retain elements belonging to exactly one input set.
Term symdiff(ProcessContext &context, const Left &left, const Right &right) {
    return binary_set(Terms(context), left, right, Operation::symmetric);
}

// Return left-only, common, and right-only sets with their unified type.
Term symmetric_partition(ProcessContext &context, const Left &left, const Right &right) {
    Terms terms(context);
    return tuple(terms, {binary_set(terms, left, right, Operation::subtract),
                         binary_set(terms, left, right, Operation::intersect),
                         binary_set(terms, {right.term}, {left.term}, Operation::subtract)});
}

// Flatten a set of sets with either union or intersection.
Term flatten_sets(const Terms &terms, const Term &value, Operation operation) {
    const auto set = unordered(value);
    const auto member = list_type(set.type_);
    if (!member) {
        if (operation == Operation::unite && atom_is(set.type_, "_")) {
            return value;
        }
        bad_argument();
    }
    const auto sets = elements(set.data_);
    if (sets.empty() && operation == Operation::intersect) {
        bad_argument();
    }
    Items result;
    bool first = true;
    for (const auto &item : sets) {
        const auto next = elements(item);
        result = first ? next : combine(next, result, operation);
        first = false;
    }
    return make_set(terms, result, {*member});
}

// Union of all members of a set of sets.
Term union1(ProcessContext &context, const Term &value) {
    return flatten_sets(Terms(context), value, Operation::unite);
}

// Intersection of all members of a nonempty set of sets.
Term intersection1(ProcessContext &context, const Term &value) {
    return flatten_sets(Terms(context), value, Operation::intersect);
}

// Constant function whose range value can be either kind of sofs set.
Term constant_function(ProcessContext &context, const Left &domain, const Right &value) {
    Terms terms(context);
    const auto set = record(domain.term);
    if (!set.unordered_ || !is_record(value.term)) {
        bad_argument();
    }
    if (atom_is(set.type_, "_")) {
        return domain.term;
    }
    const auto range = record(value.term);
    Items result;
    for (const auto &item : elements(set.data_)) {
        result.push_back(tuple(terms, {item, range.data_}));
    }
    return make_set(terms, result, {tuple(terms, {set.type_, full_type(terms, range)})});
}

constexpr std::array SETS{
    typed_entry<to_external>("sofs", "to_external"),
    typed_entry<type>("sofs", "type"),
    typed_entry<to_sets>("sofs", "to_sets"),
    typed_entry<no_elements>("sofs", "no_elements"),
    typed_entry<is_sofs_set>("sofs", "is_sofs_set"),
    typed_entry<is_set>("sofs", "is_set"),
    typed_entry<is_empty_set>("sofs", "is_empty_set"),
    typed_entry<is_equal>("sofs", "is_equal"),
    typed_entry<is_subset>("sofs", "is_subset"),
    typed_entry<is_disjoint>("sofs", "is_disjoint"),
    typed_entry<union2>("sofs", "union"),
    typed_entry<intersection2>("sofs", "intersection"),
    typed_entry<difference>("sofs", "difference"),
    typed_entry<symdiff>("sofs", "symdiff"),
    typed_entry<symmetric_partition>("sofs", "symmetric_partition"),
    typed_entry<union1>("sofs", "union"),
    typed_entry<intersection1>("sofs", "intersection"),
    typed_entry<constant_function>("sofs", "constant_function"),
};
} // namespace
} // namespace clause::runtime::builtins::sofs

namespace clause::runtime {
// Publish record queries and set algebra builtins.
std::span<const BuiltinEntry> sofs_sets_builtins() noexcept { return builtins::sofs::SETS; }
} // namespace clause::runtime
